/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_NET_DECENTRALIZED_DNS_SPACES_H_
#define BRAVE_NET_DECENTRALIZED_DNS_SPACES_H_

#include <optional>
#include <string>
#include <string_view>

#include "net/base/ip_address.h"
#include "net/base/net_export.h"

class GURL;

namespace decentralized_dns {

// Returns the lowercase Spaces handle (e.g. "retro@space") for an http(s) URL
// whose authority is `handle@space` with no '.' in the space and no password,
// e.g. http://retro@space/. Returns an empty string otherwise.
NET_EXPORT std::string GetSpacesHandle(const GURL& url);

// Browser-process switch that lets the omnibox treat a bare `retro@space` as
// a URL. Mirrors the Spaces resolve method being Enabled.
NET_EXPORT bool IsSpacesHandleNavigationEnabled();
NET_EXPORT void SetSpacesHandleNavigationEnabled(bool enabled);

// Network-process map from a Spaces host (the part after '@', e.g. "space")
// to the address resolved by the browser. Consulted by the host resolver
// instead of DNS. Thread-safe.
NET_EXPORT void SetSpacesHostAddress(std::string_view host,
                                     const net::IPAddress& address);
NET_EXPORT std::optional<net::IPAddress> GetSpacesHostAddress(
    std::string_view host);

}  // namespace decentralized_dns

#endif  // BRAVE_NET_DECENTRALIZED_DNS_SPACES_H_
