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

// A Spaces handle has two URL forms:
// - The handle URL, typed and displayed: http://retro@space/.
// - The internal URL, loaded: http://retro--space.alt/. Each handle is its own
//   host under `.alt` (RFC 9476), so handles get separate origins and sites.

// Returns the lowercase handle (e.g. "retro@space") for an http(s) handle URL:
// a username, no password, and a host with no '.'. Returns "" otherwise.
NET_EXPORT std::string GetSpacesHandle(const GURL& url);

// Encodes a handle as its internal host, e.g. "retro@space" ->
// "retro--space.alt". '-' within a label is escaped as "-h". Returns "" if
// the handle is invalid or does not fit in one DNS label.
NET_EXPORT std::string GetSpacesInternalHost(std::string_view handle);

// Decodes an internal host back to its handle, or returns "".
NET_EXPORT std::string GetSpacesHandleFromInternalHost(std::string_view host);

// Converts between the two URL forms, keeping scheme, port, path, query and
// ref. Return an invalid GURL if `url` is not in the source form.
NET_EXPORT GURL GetSpacesInternalUrl(const GURL& handle_url);
NET_EXPORT GURL GetSpacesHandleUrl(const GURL& internal_url);

// Browser-process switch that mirrors the Spaces resolve method being
// Enabled. Lets the omnibox treat a bare `retro@space` as a URL.
NET_EXPORT bool IsSpacesHandleNavigationEnabled();
NET_EXPORT void SetSpacesHandleNavigationEnabled(bool enabled);

// Network-process map from an internal host to the address resolved by the
// browser. Consulted by the host resolver instead of DNS. Thread-safe.
NET_EXPORT void SetSpacesHostAddress(std::string_view host,
                                     const net::IPAddress& address);
NET_EXPORT std::optional<net::IPAddress> GetSpacesHostAddress(
    std::string_view host);

}  // namespace decentralized_dns

#endif  // BRAVE_NET_DECENTRALIZED_DNS_SPACES_H_
