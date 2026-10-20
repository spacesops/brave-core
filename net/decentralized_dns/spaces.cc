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

constexpr std::string_view kInternalTld = ".alt";
constexpr std::string_view kSeparator = "--";
constexpr size_t kMaxDnsLabelLength = 63;

bool IsSpacesLabel(std::string_view label) {
  return !label.empty() && std::ranges::all_of(label, [](char c) {
    return base::IsAsciiLower(c) || base::IsAsciiDigit(c) || c == '-';
  });
}

void AppendEscapedLabel(std::string_view label, std::string& out) {
  for (char c : label) {
    if (c == '-') {
      out.append("-h");
    } else {
      out.push_back(c);
    }
  }
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

std::string GetSpacesInternalHost(std::string_view handle) {
  const size_t at = handle.find('@');
  if (at == std::string_view::npos) {
    return std::string();
  }
  const std::string_view name = handle.substr(0, at);
  const std::string_view space = handle.substr(at + 1);
  if (!IsSpacesLabel(name) || !IsSpacesLabel(space)) {
    return std::string();
  }
  std::string label;
  AppendEscapedLabel(name, label);
  label.append(kSeparator);
  AppendEscapedLabel(space, label);
  if (label.size() > kMaxDnsLabelLength) {
    return std::string();
  }
  return base::StrCat({label, kInternalTld});
}

std::string GetSpacesHandleFromInternalHost(std::string_view host) {
  if (!host.ends_with(kInternalTld)) {
    return std::string();
  }
  const std::string_view label =
      host.substr(0, host.size() - kInternalTld.size());
  if (label.size() > kMaxDnsLabelLength) {
    return std::string();
  }
  std::string name;
  std::string space;
  std::string* current = &name;
  for (size_t i = 0; i < label.size(); ++i) {
    const char c = label[i];
    if (c != '-') {
      current->push_back(c);
      continue;
    }
    if (i + 1 == label.size()) {
      return std::string();
    }
    const char next = label[++i];
    if (next == 'h') {
      current->push_back('-');
    } else if (next == '-' && current == &name) {
      current = &space;
    } else {
      return std::string();
    }
  }
  if (current != &space || !IsSpacesLabel(name) || !IsSpacesLabel(space)) {
    return std::string();
  }
  return base::StrCat({name, "@", space});
}

GURL GetSpacesInternalUrl(const GURL& handle_url) {
  const std::string host =
      GetSpacesInternalHost(GetSpacesHandle(handle_url));
  if (host.empty()) {
    return GURL();
  }
  GURL::Replacements replacements;
  replacements.ClearUsername();
  replacements.SetHostStr(host);
  return handle_url.ReplaceComponents(replacements);
}

GURL GetSpacesHandleUrl(const GURL& internal_url) {
  if (!internal_url.is_valid() || !internal_url.SchemeIsHTTPOrHTTPS() ||
      internal_url.has_username() || internal_url.has_password()) {
    return GURL();
  }
  const std::string handle =
      GetSpacesHandleFromInternalHost(internal_url.host());
  if (handle.empty()) {
    return GURL();
  }
  const size_t at = handle.find('@');
  const std::string_view name = std::string_view(handle).substr(0, at);
  const std::string_view space = std::string_view(handle).substr(at + 1);
  GURL::Replacements replacements;
  replacements.SetUsernameStr(name);
  replacements.SetHostStr(space);
  return internal_url.ReplaceComponents(replacements);
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
