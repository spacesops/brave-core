/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "net/http/http_network_transaction.h"

#include <string>

#include "base/strings/strcat.h"
#include "brave/net/decentralized_dns/spaces.h"
#include "net/base/url_util.h"
#include "url/gurl.h"

namespace net {
namespace {

// Spaces web servers route on the full handle, so http://retro@space/ is sent
// with `Host: retro@space` rather than `Host: space`.
std::string GetHostAndOptionalPortForSpaces(const GURL& url) {
  std::string handle = decentralized_dns::GetSpacesHandle(url);
  if (handle.empty() || !decentralized_dns::GetSpacesHostAddress(url.host())) {
    return GetHostAndOptionalPort(url);
  }
  return url.has_port() ? base::StrCat({handle, ":", url.port()}) : handle;
}

}  // namespace
}  // namespace net

#define GetHostAndOptionalPort GetHostAndOptionalPortForSpaces
#include <net/http/http_network_transaction.cc>
#undef GetHostAndOptionalPort
