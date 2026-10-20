/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "net/dns/host_resolver_manager.h"

#include <optional>
#include <string_view>

#include "brave/net/decentralized_dns/spaces.h"
#include "net/base/ip_address.h"

namespace net {
namespace {

bool AssignSpacesHostAddress(IPAddress& address, std::string_view host) {
  std::optional<IPAddress> spaces_address =
      decentralized_dns::GetSpacesHostAddress(host);
  if (!spaces_address) {
    return false;
  }
  address = *spaces_address;
  return true;
}

}  // namespace
}  // namespace net

// Spaces hosts resolved by the browser are served like IP literals, so they
// never reach the cache or DNS.
#define AssignFromIPLiteral(host) \
  AssignFromIPLiteral(host) || AssignSpacesHostAddress(out_ip_address, host)

#include <net/dns/host_resolver_manager.cc>

#undef AssignFromIPLiteral
