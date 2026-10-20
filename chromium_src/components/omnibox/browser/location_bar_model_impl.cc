/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "components/omnibox/browser/location_bar_model_impl.h"

#include "brave/net/decentralized_dns/spaces.h"
#include "components/url_formatter/url_formatter.h"
#include "url/gurl.h"

namespace {

// Spaces pages load from http://retro--space.alt/ but are shown as
// http://retro@space/.
GURL GetSpacesDisplayUrl(const GURL& url) {
  GURL handle_url = decentralized_dns::GetSpacesHandleUrl(url);
  return handle_url.is_valid() ? handle_url : url;
}

// The username of a Spaces URL is part of the name (`retro@space`), so it
// must stay visible.
url_formatter::FormatUrlTypes AdjustFormatTypesForSpaces(
    const GURL& url,
    url_formatter::FormatUrlTypes format_types) {
  if (decentralized_dns::GetSpacesHandleUrl(url).is_valid() ||
      (decentralized_dns::IsSpacesHandleNavigationEnabled() &&
       !decentralized_dns::GetSpacesHandle(url).empty())) {
    format_types &= ~url_formatter::kFormatUrlOmitUsernamePassword;
  }
  return format_types;
}

}  // namespace

#define FormatUrl(url, format_types, ...)                   \
  FormatUrl(GetSpacesDisplayUrl(url),                        \
            AdjustFormatTypesForSpaces(url, format_types), \
            __VA_ARGS__)

#include <components/omnibox/browser/location_bar_model_impl.cc>

#undef FormatUrl
