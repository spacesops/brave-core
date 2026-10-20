// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_COMPONENTS_DECENTRALIZED_DNS_BROWSER_SPACES_RS_SRC_SHIM_H_
#define BRAVE_COMPONENTS_DECENTRALIZED_DNS_BROWSER_SPACES_RS_SRC_SHIM_H_

#include "third_party/rust/cxx/v1/cxx.h"

namespace decentralized_dns::spaces {

struct HttpRequest;
struct HttpRoundtripContext;
struct ResolveResult;

// Performs relay HTTP requests for the Rust client. Implementations must
// eventually call `on_http_response(ctx, ...)` exactly once per request, on
// the client's thread and never synchronously from `Fetch`, unless they are
// destroyed first.
class SpacesFetcher {
 public:
  virtual ~SpacesFetcher() = default;
  virtual void Fetch(HttpRequest request,
                     rust::Box<HttpRoundtripContext> ctx) const = 0;
};

class ResolveCallback {
 public:
  virtual ~ResolveCallback() = default;
  virtual void Run(ResolveResult result) = 0;
};

}  // namespace decentralized_dns::spaces

#endif  // BRAVE_COMPONENTS_DECENTRALIZED_DNS_BROWSER_SPACES_RS_SRC_SHIM_H_
