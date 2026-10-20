/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "third_party/blink/renderer/core/dom/icon_url.h"

#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include "third_party/blink/renderer/platform/wtf/text/ascii_ctype.h"

namespace blink {
namespace {

// Mirrors decentralized_dns::GetSpacesHandle() (brave/net), which Blink can't
// depend on.
bool IsSpacesLabelChar(UChar c) {
  return IsAsciiAlphanumeric(c) || c == '-';
}

bool IsSpacesLabel(StringView label) {
  return !label.empty() && label.IsAllSpecialCharacters<IsSpacesLabelChar>();
}

// `retro@space` names a site, so its favicon must be fetched from
// `http://retro@space/` rather than `http://space/`.
void MaybeKeepSpacesName(KURL& icon_url, const KURL& document_url) {
  if (document_url.Pass().empty() && IsSpacesLabel(document_url.User()) &&
      IsSpacesLabel(document_url.Host())) {
    icon_url.SetUser(document_url.User().ToString());
  }
}

}  // namespace
}  // namespace blink

#define SetPath(path) \
  SetPath(path);      \
  MaybeKeepSpacesName(url, document_url)

#include <third_party/blink/renderer/core/dom/icon_url.cc>

#undef SetPath
