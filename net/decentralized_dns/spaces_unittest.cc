/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/net/decentralized_dns/spaces.h"

#include "net/base/ip_address.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace decentralized_dns {

TEST(SpacesTest, GetSpacesHandle) {
  EXPECT_EQ(GetSpacesHandle(GURL("http://retro@space/")), "retro@space");
  EXPECT_EQ(GetSpacesHandle(GURL("http://Retro@Space/a/b?c")), "retro@space");
  EXPECT_EQ(GetSpacesHandle(GURL("https://retro@space/")), "retro@space");
  EXPECT_EQ(GetSpacesHandle(GURL("http://retro@space:8080/")), "retro@space");

  EXPECT_EQ(GetSpacesHandle(GURL("http://space/")), "");
  EXPECT_EQ(GetSpacesHandle(GURL("http://retro@example.com/")), "");
  EXPECT_EQ(GetSpacesHandle(GURL("http://user:pass@space/")), "");
  EXPECT_EQ(GetSpacesHandle(GURL("http://retro@1.2.3.4/")), "");
  EXPECT_EQ(GetSpacesHandle(GURL("http://re%20tro@space/")), "");
  EXPECT_EQ(GetSpacesHandle(GURL("ftp://retro@space/")), "");
  EXPECT_EQ(GetSpacesHandle(GURL("not a url")), "");
}

TEST(SpacesTest, InternalHost) {
  EXPECT_EQ(GetSpacesInternalHost("retro@space"), "retro--space.alt");
  EXPECT_EQ(GetSpacesInternalHost("a-b@c-d"), "a-hb--c-hd.alt");
  EXPECT_EQ(GetSpacesInternalHost("a-@-b"), "a-h---hb.alt");
  EXPECT_EQ(GetSpacesInternalHost("retro"), "");
  EXPECT_EQ(GetSpacesInternalHost("Retro@space"), "");
  EXPECT_EQ(GetSpacesInternalHost("re.tro@space"), "");
  EXPECT_EQ(GetSpacesInternalHost(std::string(30, 'a') + "@" +
                                  std::string(31, 'b')),
            std::string(30, 'a') + "--" + std::string(31, 'b') + ".alt");
  EXPECT_EQ(GetSpacesInternalHost(std::string(31, 'a') + "@" +
                                  std::string(31, 'b')),
            "");

  for (const char* handle : {"retro@space", "a-b@c-d", "a-@-b", "a--b@c"}) {
    EXPECT_EQ(GetSpacesHandleFromInternalHost(GetSpacesInternalHost(handle)),
              handle);
  }

  EXPECT_EQ(GetSpacesHandleFromInternalHost("retro.alt"), "");
  EXPECT_EQ(GetSpacesHandleFromInternalHost("retro--space.com"), "");
  EXPECT_EQ(GetSpacesHandleFromInternalHost("a--b--c.alt"), "");
  EXPECT_EQ(GetSpacesHandleFromInternalHost("a-x--b.alt"), "");
  EXPECT_EQ(GetSpacesHandleFromInternalHost("a--b-.alt"), "");
  EXPECT_EQ(GetSpacesHandleFromInternalHost("--b.alt"), "");
  EXPECT_EQ(GetSpacesHandleFromInternalHost("x.a--b.alt"), "");
}

TEST(SpacesTest, UrlForms) {
  EXPECT_EQ(GetSpacesInternalUrl(GURL("http://Retro@space:8080/a?b#c")),
            GURL("http://retro--space.alt:8080/a?b#c"));
  EXPECT_FALSE(GetSpacesInternalUrl(GURL("http://space/")).is_valid());

  EXPECT_EQ(GetSpacesHandleUrl(GURL("http://retro--space.alt:8080/a?b#c")),
            GURL("http://retro@space:8080/a?b#c"));
  EXPECT_FALSE(GetSpacesHandleUrl(GURL("http://example.alt/")).is_valid());
  EXPECT_FALSE(
      GetSpacesHandleUrl(GURL("http://u@retro--space.alt/")).is_valid());
}

TEST(SpacesTest, HostAddressMap) {
  EXPECT_FALSE(GetSpacesHostAddress("unit-test-space"));

  SetSpacesHostAddress("unit-test-space", net::IPAddress(68, 183, 128, 132));
  EXPECT_EQ(GetSpacesHostAddress("unit-test-space"),
            net::IPAddress(68, 183, 128, 132));

  SetSpacesHostAddress("unit-test-space", net::IPAddress(10, 0, 0, 1));
  EXPECT_EQ(GetSpacesHostAddress("unit-test-space"),
            net::IPAddress(10, 0, 0, 1));
}

}  // namespace decentralized_dns
