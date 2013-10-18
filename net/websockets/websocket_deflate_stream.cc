// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/websockets/websocket_deflate_stream.h"

#include <algorithm>
#include <string>

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "net/base/completion_callback.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/websockets/websocket_deflater.h"
#include "net/websockets/websocket_errors.h"
#include "net/websockets/websocket_frame.h"
#include "net/websockets/websocket_inflater.h"
#include "net/websockets/websocket_stream.h"

class GURL;

namespace net {

namespace {

const int kWindowBits = 15;
const size_t kChunkSize = 4 * 1024;

}  // namespace

WebSocketDeflateStream::WebSocketDeflateStream(
    scoped_ptr<WebSocketStream> stream)
    : stream_(stream.Pass()),
      deflater_(WebSocketDeflater::TAKE_OVER_CONTEXT),
      inflater_(kChunkSize, kChunkSize),
      reading_state_(NOT_READING),
      writing_state_(NOT_WRITING),
      current_reading_opcode_(WebSocketFrameHeader::kOpCodeText),
      current_writing_opcode_(WebSocketFrameHeader::kOpCodeText) {
  DCHECK(stream_);
  deflater_.Initialize(kWindowBits);
  inflater_.Initialize(kWindowBits);
}

WebSocketDeflateStream::~WebSocketDeflateStream() {}

int WebSocketDeflateStream::ReadFrames(ScopedVector<WebSocketFrame>* frames,
                                       const CompletionCallback& callback) {
  CompletionCallback callback_to_pass =
      base::Bind(&WebSocketDeflateStream::OnReadComplete,
                 base::Unretained(this),
                 base::Unretained(frames),
                 callback);
  int result = stream_->ReadFrames(frames, callback_to_pass);
  if (result < 0)
    return result;
  DCHECK_EQ(OK, result);
  return InflateAndReadIfNecessary(frames, callback_to_pass);
}

int WebSocketDeflateStream::WriteFrames(ScopedVector<WebSocketFrame>* frames,
                                        const CompletionCallback& callback) {
  int result = Deflate(frames);
  if (result != OK)
    return result;
  if (frames->empty())
    return OK;
  return stream_->WriteFrames(frames, callback);
}

void WebSocketDeflateStream::Close() {
  stream_->Close();
}

std::string WebSocketDeflateStream::GetSubProtocol() const {
  return stream_->GetSubProtocol();
}

std::string WebSocketDeflateStream::GetExtensions() const {
  return stream_->GetExtensions();
}

int WebSocketDeflateStream::SendHandshakeRequest(
    const GURL& url,
    const HttpRequestHeaders& headers,
    HttpResponseInfo* response_info,
    const CompletionCallback& callback) {
  // TODO(yhirano) handshake related functions will be moved to somewhere.
  NOTIMPLEMENTED();
  return OK;
}

int WebSocketDeflateStream::ReadHandshakeResponse(
    const CompletionCallback& callback) {
  // TODO(yhirano) handshake related functions will be moved to somewhere.
  NOTIMPLEMENTED();
  return OK;
}

void WebSocketDeflateStream::OnReadComplete(
    ScopedVector<WebSocketFrame>* frames,
    const CompletionCallback& callback,
    int result) {
  if (result != OK) {
    frames->clear();
    callback.Run(result);
    return;
  }

  int r = InflateAndReadIfNecessary(frames, callback);
  if (r != ERR_IO_PENDING)
    callback.Run(r);
}

int WebSocketDeflateStream::Deflate(ScopedVector<WebSocketFrame>* frames) {
  ScopedVector<WebSocketFrame> frames_to_write;
  for (size_t i = 0; i < frames->size(); ++i) {
    scoped_ptr<WebSocketFrame> frame((*frames)[i]);
    (*frames)[i] = NULL;
    DCHECK(!frame->header.reserved1);
    if (!WebSocketFrameHeader::IsKnownDataOpCode(frame->header.opcode)) {
      frames_to_write.push_back(frame.release());
      continue;
    }

    if (writing_state_ == NOT_WRITING) {
      current_writing_opcode_ = frame->header.opcode;
      DCHECK(current_writing_opcode_ == WebSocketFrameHeader::kOpCodeText ||
             current_writing_opcode_ == WebSocketFrameHeader::kOpCodeBinary);
      // TODO(yhirano): For now, we unconditionally compress data messages.
      // Further optimization is needed.
      // http://crbug.com/163882
      writing_state_ = WRITING_COMPRESSED_MESSAGE;
    }
    if (writing_state_ == WRITING_UNCOMPRESSED_MESSAGE) {
      if (frame->header.final)
        writing_state_ = NOT_WRITING;
      frames_to_write.push_back(frame.release());
      current_writing_opcode_ = WebSocketFrameHeader::kOpCodeContinuation;
    } else {
      DCHECK_EQ(WRITING_COMPRESSED_MESSAGE, writing_state_);
      if (frame->data &&
          !deflater_.AddBytes(frame->data->data(),
                              frame->header.payload_length)) {
        DVLOG(1) << "WebSocket protocol error. "
                 << "deflater_.AddBytes() returns an error.";
        return ERR_WS_PROTOCOL_ERROR;
      }
      if (frame->header.final && !deflater_.Finish()) {
        DVLOG(1) << "WebSocket protocol error. "
                 << "deflater_.Finish() returns an error.";
        return ERR_WS_PROTOCOL_ERROR;
      }
      if (deflater_.CurrentOutputSize() >= kChunkSize || frame->header.final) {
        const WebSocketFrameHeader::OpCode opcode = current_writing_opcode_;
        scoped_ptr<WebSocketFrame> compressed(new WebSocketFrame(opcode));
        scoped_refptr<IOBufferWithSize> data =
            deflater_.GetOutput(deflater_.CurrentOutputSize());
        if (!data) {
          DVLOG(1) << "WebSocket protocol error. "
                   << "deflater_.GetOutput() returns an error.";
          return ERR_WS_PROTOCOL_ERROR;
        }
        compressed->header.CopyFrom(frame->header);
        compressed->header.opcode = opcode;
        compressed->header.final = frame->header.final;
        compressed->header.reserved1 =
            (opcode != WebSocketFrameHeader::kOpCodeContinuation);
        compressed->data = data;
        compressed->header.payload_length = data->size();

        current_writing_opcode_ = WebSocketFrameHeader::kOpCodeContinuation;
        frames_to_write.push_back(compressed.release());
      }
      if (frame->header.final)
        writing_state_ = NOT_WRITING;
    }
  }
  frames->swap(frames_to_write);
  return OK;
}

int WebSocketDeflateStream::Inflate(ScopedVector<WebSocketFrame>* frames) {
  ScopedVector<WebSocketFrame> frames_to_output;
  ScopedVector<WebSocketFrame> frames_passed;
  frames->swap(frames_passed);
  for (size_t i = 0; i < frames_passed.size(); ++i) {
    scoped_ptr<WebSocketFrame> frame(frames_passed[i]);
    frames_passed[i] = NULL;
    if (!WebSocketFrameHeader::IsKnownDataOpCode(frame->header.opcode)) {
      frames_to_output.push_back(frame.release());
      continue;
    }

    if (reading_state_ == NOT_READING) {
      if (frame->header.reserved1)
        reading_state_ = READING_COMPRESSED_MESSAGE;
      else
        reading_state_ = READING_UNCOMPRESSED_MESSAGE;
      current_reading_opcode_ = frame->header.opcode;
    } else {
      if (frame->header.reserved1) {
        DVLOG(1) << "WebSocket protocol error. "
                 << "Receiving a non-first frame with RSV1 flag set.";
        return ERR_WS_PROTOCOL_ERROR;
      }
    }

    if (reading_state_ == READING_UNCOMPRESSED_MESSAGE) {
      if (frame->header.final)
        reading_state_ = NOT_READING;
      current_reading_opcode_ = WebSocketFrameHeader::kOpCodeContinuation;
      frames_to_output.push_back(frame.release());
    } else {
      DCHECK_EQ(reading_state_, READING_COMPRESSED_MESSAGE);
      if (frame->data &&
          !inflater_.AddBytes(frame->data->data(),
                              frame->header.payload_length)) {
        DVLOG(1) << "WebSocket protocol error. "
                 << "inflater_.AddBytes() returns an error.";
        return ERR_WS_PROTOCOL_ERROR;
      }
      if (frame->header.final) {
        if (!inflater_.Finish()) {
          DVLOG(1) << "WebSocket protocol error. "
                   << "inflater_.Finish() returns an error.";
          return ERR_WS_PROTOCOL_ERROR;
        }
      }
      // TODO(yhirano): Many frames can be generated by the inflater and
      // memory consumption can grow.
      // We could avoid it, but avoiding it makes this class much more
      // complicated.
      while (inflater_.CurrentOutputSize() >= kChunkSize ||
             frame->header.final) {
        size_t size = std::min(kChunkSize, inflater_.CurrentOutputSize());
        scoped_ptr<WebSocketFrame> inflated(
            new WebSocketFrame(WebSocketFrameHeader::kOpCodeText));
        scoped_refptr<IOBufferWithSize> data = inflater_.GetOutput(size);
        bool is_final = !inflater_.CurrentOutputSize();
        // |is_final| can't be true if |frame->header.final| is false.
        DCHECK(!(is_final && !frame->header.final));
        if (!data) {
          DVLOG(1) << "WebSocket protocol error. "
                   << "inflater_.GetOutput() returns an error.";
          return ERR_WS_PROTOCOL_ERROR;
        }
        inflated->header.CopyFrom(frame->header);
        inflated->header.opcode = current_reading_opcode_;
        inflated->header.final = is_final;
        inflated->header.reserved1 = false;
        inflated->data = data;
        inflated->header.payload_length = data->size();

        frames_to_output.push_back(inflated.release());
        current_reading_opcode_ = WebSocketFrameHeader::kOpCodeContinuation;
        if (is_final)
          break;
      }
      if (frame->header.final)
        reading_state_ = NOT_READING;
    }
  }
  frames->swap(frames_to_output);
  return frames->empty() ? ERR_IO_PENDING : OK;
}

int WebSocketDeflateStream::InflateAndReadIfNecessary(
    ScopedVector<WebSocketFrame>* frames,
    const CompletionCallback& callback) {
  int result = Inflate(frames);
  while (result == ERR_IO_PENDING) {
    DCHECK(frames->empty());
    result = stream_->ReadFrames(frames, callback);
    if (result < 0)
      break;
    DCHECK_EQ(OK, result);
    DCHECK(!frames->empty());
    result = Inflate(frames);
  }
  if (result < 0)
    frames->clear();
  return result;
}

}  // namespace net