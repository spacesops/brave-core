/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/net/decentralized_dns/spaces.h"

#include <algorithm>
#include <atomic>
#include <map>

#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"
#include "url/gurl.h"

namespace decentralized_dns {

namespace {

bool IsSpacesLabel(std::string_view label) {
  return !label.empty() && std::ranges::all_of(label, [](char c) {
    return base::IsAsciiLower(c) || base::IsAsciiDigit(c) || c == '-';
  });
}

std::atomic<bool> g_handle_navigation_enabled{false};

struct HostMap {
  base::Lock lock;
  std::map<std::string, net::IPAddress, std::less<>> addresses GUARDED_BY(lock);
};

HostMap& GetHostMap() {
  static base::NoDestructor<HostMap> map;
  return *map;
}

}  // namespace

std::string GetSpacesHandle(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || !url.has_username() ||
      url.has_password() || url.HostIsIPAddress()) {
    return std::string();
  }
  const std::string name = base::ToLowerASCII(url.username());
  const std::string_view space = url.host();
  if (!IsSpacesLabel(name) || !IsSpacesLabel(space)) {
    return std::string();
  }
  return base::StrCat({name, "@", space});
}

bool IsSpacesHandleNavigationEnabled() {
  return g_handle_navigation_enabled.load(std::memory_order_relaxed);
}

void SetSpacesHandleNavigationEnabled(bool enabled) {
  g_handle_navigation_enabled.store(enabled, std::memory_order_relaxed);
}

void SetSpacesHostAddress(std::string_view host,
                          const net::IPAddress& address) {
  HostMap& map = GetHostMap();
  base::AutoLock auto_lock(map.lock);
  map.addresses.insert_or_assign(std::string(host), address);
}

std::optional<net::IPAddress> GetSpacesHostAddress(std::string_view host) {
  HostMap& map = GetHostMap();
  base::AutoLock auto_lock(map.lock);
  auto it = map.addresses.find(host);
  if (it == map.addresses.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace decentralized_dns
