// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBRARIES_NACL_IO_MOUNT_NODE_SOCKET_H_
#define LIBRARIES_NACL_IO_MOUNT_NODE_SOCKET_H_

#include "nacl_io/ossocket.h"
#ifdef PROVIDES_SOCKET_API

#include <sys/fcntl.h>
#include <ppapi/c/pp_errors.h>
#include <ppapi/c/pp_resource.h>
#include <ppapi/c/ppb_net_address.h>

#include "nacl_io/mount.h"
#include "nacl_io/mount_node.h"
#include "nacl_io/mount_node_stream.h"
#include "nacl_io/pepper_interface.h"

namespace nacl_io {

/* Only allow single maximum transfers of 64K or less. Socket users
 * should be looping on Send/Recv size. */
static const size_t MAX_SOCK_TRANSFER = 65536;

class MountStream;

class MountNodeSocket : public MountNodeStream {
 public:
  explicit MountNodeSocket(Mount* mount);
  MountNodeSocket(Mount* mount, PP_Resource socket);

 protected:
  virtual void Destroy();

 public:
  // Normal read/write operations on a file (recv/send).
  virtual Error Read(const HandleAttr& attr,
                     void* buf,
                     size_t count,
                     int* out_bytes);
  virtual Error Write(const HandleAttr& attr,
                      const void* buf,
                      size_t count,
                      int* out_bytes);

  virtual Error GetSockOpt(int lvl, int optname, void* optval, socklen_t* len);
  virtual Error SetSockOpt(int lvl,
                           int optname,
                           const void* optval,
                           socklen_t len);

  // Unsupported Functions
  virtual Error Shutdown(int how);
  virtual Error MMap(void* addr,
                     size_t length,
                     int prot,
                     int flags,
                     size_t offset,
                     void** out_addr);

  // Socket Functions.
  virtual Error Accept(const HandleAttr& attr,
                       PP_Resource* new_sock,
                       struct sockaddr* addr,
                       socklen_t* len);
  virtual Error Bind(const struct sockaddr* addr, socklen_t len);
  virtual Error Connect(const HandleAttr& attr,
                        const struct sockaddr* addr,
                        socklen_t len);
  virtual Error Listen(int backlog);
  virtual Error Recv(const HandleAttr& attr,
                     void* buf,
                     size_t len,
                     int flags,
                     int* out_len);
  virtual Error RecvFrom(const HandleAttr& attr,
                         void* buf,
                         size_t len,
                         int flags,
                         struct sockaddr* src_addr,
                         socklen_t* addrlen,
                         int* out_len);

  virtual Error Send(const HandleAttr& attr,
                     const void* buf,
                     size_t len,
                     int flags,
                     int* out_len);
  virtual Error SendTo(const HandleAttr& attr,
                       const void* buf,
                       size_t len,
                       int flags,
                       const struct sockaddr* dest_addr,
                       socklen_t addrlen,
                       int* out_len);

  virtual Error GetPeerName(struct sockaddr* addr, socklen_t* len);
  virtual Error GetSockName(struct sockaddr* addr, socklen_t* len);

  PP_Resource socket_resource() { return socket_resource_; }
  PP_Resource remote_addr() { return remote_addr_; }

  // Updates socket's state, recording last error.
  void SetError_Locked(int pp_error_num);


 protected:

  // Wraps common error checks, timeouts, work pump for send.
  Error SendHelper(const HandleAttr& attr,
                   const void* buf,
                   size_t len,
                   int flags,
                   PP_Resource addr,
                   int* out_len);

  // Wraps common error checks, timeouts, work pump for recv.
  Error RecvHelper(const HandleAttr& attr,
                   void* buf,
                   size_t len,
                   int flags,
                   PP_Resource* addr,
                   int* out_len);


  // Per socket type send and recv
  virtual Error Recv_Locked(void* buffer,
                            size_t len,
                            PP_Resource* out_addr,
                            int* out_len) = 0;

  virtual Error Send_Locked(const void* buffer,
                            size_t len,
                            PP_Resource addr,
                            int* out_len) = 0;


  NetAddressInterface* NetInterface();
  TCPSocketInterface* TCPInterface();
  UDPSocketInterface* UDPInterface();

  PP_Resource SockAddrToResource(const struct sockaddr* addr, socklen_t len);
  socklen_t ResourceToSockAddr(PP_Resource addr,
                               socklen_t len,
                               struct sockaddr* out_addr);

  bool IsEquivalentAddress(PP_Resource addr1, PP_Resource addr2);

 protected:
  PP_Resource socket_resource_;
  PP_Resource local_addr_;
  PP_Resource remote_addr_;
  uint32_t socket_flags_;
  int last_errno_;

  friend class KernelProxy;
  friend class MountStream;
};


}  // namespace nacl_io


#endif  // PROVIDES_SOCKET_API
#endif  // LIBRARIES_NACL_IO_MOUNT_NODE_SOCKET_H_