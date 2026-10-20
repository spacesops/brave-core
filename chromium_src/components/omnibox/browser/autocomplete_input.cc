/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "components/omnibox/browser/autocomplete_input.h"

#include "brave/net/decentralized_dns/spaces.h"
#include "components/omnibox/browser/autocomplete_scheme_classifier.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/search_engines/template_url_starter_pack_data.h"
#include "components/url_formatter/url_fixer.h"
#include "components/url_formatter/url_formatter.h"
#include "net/base/url_util.h"
#include "url/gurl.h"

namespace {

bool IsSpacesHandleInput(const GURL& url) {
  return decentralized_dns::IsSpacesHandleNavigationEnabled() &&
         !decentralized_dns::GetSpacesHandle(url).empty();
}

}  // namespace

// A bare `retro@space` is a Spaces handle, not an email address: skip the
// user@host check and treat it like a host with a port.
#define has_username() \
  has_username() && !IsSpacesHandleInput(*canonicalized_url)
#define has_port() has_port() || IsSpacesHandleInput(*canonicalized_url)

#include <components/omnibox/browser/autocomplete_input.cc>

#undef has_port
#undef has_username
