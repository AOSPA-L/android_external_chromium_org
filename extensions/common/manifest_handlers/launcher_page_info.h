// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_MANIFEST_HANDLERS_LAUNCHER_PAGE_INFO_H_
#define EXTENSIONS_COMMON_MANIFEST_HANDLERS_LAUNCHER_PAGE_INFO_H_

#include <string>
#include <vector>

#include "base/strings/string16.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handler.h"

namespace extensions {

struct LauncherPageInfo : public Extension::ManifestData {
  // The page URL.
  std::string page;
};

// Parses the "launcher_page" manifest key.
class LauncherPageHandler : public ManifestHandler {
 public:
  LauncherPageHandler();
  virtual ~LauncherPageHandler();

  // Gets the LauncherPageInfo for a given |extension|.
  static LauncherPageInfo* GetInfo(const Extension* extension);

  // ManifestHandler overrides:
  virtual bool Parse(Extension* extension, base::string16* error) OVERRIDE;
  virtual bool Validate(const Extension* extension,
                        std::string* error,
                        std::vector<InstallWarning>* warnings) const OVERRIDE;

 private:
  virtual const std::vector<std::string> Keys() const OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(LauncherPageHandler);
};

}  // namespace extensions

#endif  // EXTENSIONS_COMMON_MANIFEST_HANDLERS_LAUNCHER_PAGE_INFO_H_
