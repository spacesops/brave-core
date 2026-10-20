/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_DECENTRALIZED_DNS_BROWSER_SPACES_SPACES_RESOLVER_H_
#define BRAVE_COMPONENTS_DECENTRALIZED_DNS_BROWSER_SPACES_SPACES_RESOLVER_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/time/time.h"
#include "brave/components/decentralized_dns/browser/spaces/rs/src/lib.rs.h"
#include "components/prefs/pref_change_registrar.h"
#include "net/base/ip_address.h"

class PrefService;

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace decentralized_dns {

// Resolves Spaces handles (e.g. "retro@space") to the IPv4 address in their
// `ipv4` addr record, verified with Fabric at Observed trust. UI thread only.
class SpacesResolver {
 public:
  using ResolveCallback =
      base::OnceCallback<void(std::optional<net::IPAddress>)>;

  static SpacesResolver* GetInstance();

  SpacesResolver(const SpacesResolver&) = delete;
  SpacesResolver& operator=(const SpacesResolver&) = delete;

  // Keeps the omnibox handle-navigation switch in sync with the Spaces
  // resolve method.
  void Init(PrefService* local_state);
  void Shutdown();

  // `callback` always runs asynchronously; std::nullopt means not resolved.
  void Resolve(const std::string& handle,
               scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
               ResolveCallback callback);

 private:
  friend base::NoDestructor<SpacesResolver>;

  struct CacheEntry {
    net::IPAddress address;
    base::TimeTicks expiry;
  };

  SpacesResolver();
  ~SpacesResolver();

  void OnResolveMethodChanged();
  void OnResolved(const std::string& handle,
                  spaces::ResolveStatus status,
                  const std::string& ipv4);

  raw_ptr<PrefService> local_state_ = nullptr;
  PrefChangeRegistrar pref_change_registrar_;
  std::optional<rust::Box<spaces::SpacesClient>> client_;
  std::map<std::string, CacheEntry> cache_;
  std::map<std::string, std::vector<ResolveCallback>> pending_;
  base::WeakPtrFactory<SpacesResolver> weak_ptr_factory_{this};
};

}  // namespace decentralized_dns

#endif  // BRAVE_COMPONENTS_DECENTRALIZED_DNS_BROWSER_SPACES_SPACES_RESOLVER_H_
