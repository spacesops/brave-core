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
