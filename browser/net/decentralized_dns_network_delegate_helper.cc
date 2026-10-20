/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/browser/net/decentralized_dns_network_delegate_helper.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/check.h"
#include "brave/browser/brave_wallet/brave_wallet_service_factory.h"
#include "brave/browser/net/url_context.h"
#include "brave/components/brave_wallet/browser/brave_wallet_service.h"
#include "brave/components/brave_wallet/browser/brave_wallet_utils.h"
#include "brave/components/brave_wallet/browser/json_rpc_service.h"
#include "brave/components/brave_wallet/common/brave_wallet.mojom.h"
#include "brave/components/brave_wallet/common/buildflags/buildflags.h"
#include "brave/components/decentralized_dns/browser/spaces/spaces_resolver.h"
#include "brave/components/decentralized_dns/core/constants.h"
#include "brave/components/decentralized_dns/core/utils.h"
#include "brave/components/ipfs/ipfs_utils.h"
#include "brave/net/decentralized_dns/spaces.h"
#include "chrome/browser/browser_process.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/network_service_instance.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "services/network/public/mojom/network_service.mojom.h"
#include "url/url_constants.h"

static_assert(BUILDFLAG(ENABLE_BRAVE_WALLET));

namespace {

bool IsValidDecentralizedDnsRedirectUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS();
}

}  // namespace

namespace decentralized_dns {

template <template <typename> class T>
int OnBeforeURLRequest_DecentralizedDnsPreRedirectWork(
    const brave::ResponseCallback& next_callback,
    T<brave::BraveRequestInfo> ctx) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(ctx);
  DCHECK(!next_callback.is_null());

  if (!ctx->browser_context() || !g_browser_process) {
    return net::OK;
  }

  // Spaces names never fall back to DNS: resolve them here or fail.
  const std::string spaces_handle = GetSpacesHandle(ctx->request_url());
  if (!spaces_handle.empty() &&
      IsSpacesResolveMethodEnabled(g_browser_process->local_state())) {
    if (ctx->browser_context()->IsOffTheRecord() ||
        !ctx->request_url().SchemeIs(url::kHttpScheme) ||
        !brave_wallet::IsAllowed(
            user_prefs::UserPrefs::Get(ctx->browser_context()))) {
      return net::ERR_NAME_NOT_RESOLVED;
    }
    SpacesResolver::GetInstance()->Resolve(
        spaces_handle, g_browser_process->shared_url_loader_factory(),
        base::BindOnce(&OnBeforeURLRequest_SpacesResolveWork<T>, next_callback,
                       ctx));
    return net::ERR_IO_PENDING;
  }

  if (ctx->browser_context()->IsOffTheRecord()) {
    return net::OK;
  }

  // Check if Brave Wallet is disabled by policy - if so, disable decentralized
  // DNS
  auto* prefs = user_prefs::UserPrefs::Get(ctx->browser_context());
  if (!brave_wallet::IsAllowed(prefs)) {
    return net::OK;
  }

  auto* brave_wallet_service =
      brave_wallet::BraveWalletServiceFactory::GetServiceForContext(
          ctx->browser_context());
  if (!brave_wallet_service) {
    return net::OK;
  }

  auto* json_rpc_service = brave_wallet_service->json_rpc_service();
  CHECK(json_rpc_service);

  if (IsUnstoppableDomainsTLD(ctx->request_url().host()) &&
      IsUnstoppableDomainsResolveMethodEnabled(
          g_browser_process->local_state())) {
    json_rpc_service->UnstoppableDomainsResolveDns(
        std::string(ctx->request_url().host()),
        base::BindOnce(&OnBeforeURLRequest_UnstoppableDomainsRedirectWork<T>,
                       next_callback, ctx));

    return net::ERR_IO_PENDING;
  }

  if (IsENSTLD(ctx->request_url().host()) &&
      IsENSResolveMethodEnabled(g_browser_process->local_state())) {
    json_rpc_service->EnsGetContentHash(
        std::string(ctx->request_url().host()),
        base::BindOnce(&OnBeforeURLRequest_EnsRedirectWork<T>, next_callback,
                       ctx));

    return net::ERR_IO_PENDING;
  }

  if (IsSnsTLD(ctx->request_url().host()) &&
      IsSnsResolveMethodEnabled(g_browser_process->local_state())) {
    json_rpc_service->SnsResolveHost(
        std::string(ctx->request_url().host()),
        base::BindOnce(&OnBeforeURLRequest_SnsRedirectWork<T>, next_callback,
                       ctx));

    return net::ERR_IO_PENDING;
  }

  return net::OK;
}

template <template <typename> class T>
void OnBeforeURLRequest_EnsRedirectWork(
    const brave::ResponseCallback& next_callback,
    T<brave::BraveRequestInfo> ctx,
    const std::vector<uint8_t>& content_hash,
    bool require_offchain_consent,
    brave_wallet::mojom::ProviderError error,
    const std::string& error_message) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(!next_callback.is_null());

  if (!ctx || error != brave_wallet::mojom::ProviderError::kSuccess) {
    next_callback.Run();
    return;
  }

  if (require_offchain_consent) {
    ctx->set_pending_error(net::ERR_ENS_OFFCHAIN_LOOKUP_NOT_SELECTED);
    next_callback.Run();
    return;
  }

  GURL resolved_ipfs_uri;
  GURL ipfs_uri = ipfs::ContentHashToCIDv1URL(content_hash);
  if (ipfs_uri.is_valid() &&
      ipfs::TranslateIPFSURI(ipfs_uri, &resolved_ipfs_uri, true)) {
    if (IsValidDecentralizedDnsRedirectUrl(resolved_ipfs_uri)) {
      ctx->set_new_url_spec(resolved_ipfs_uri.spec());
    }
  }

  next_callback.Run();
}

template <template <typename> class T>
void OnBeforeURLRequest_SnsRedirectWork(
    const brave::ResponseCallback& next_callback,
    T<brave::BraveRequestInfo> ctx,
    const std::optional<GURL>& url) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (ctx && url.has_value() && IsValidDecentralizedDnsRedirectUrl(*url)) {
    ctx->set_new_url_spec(url->spec());
  }

  if (!next_callback.is_null()) {
    next_callback.Run();
  }
}

template <template <typename> class T>
void OnBeforeURLRequest_UnstoppableDomainsRedirectWork(
    const brave::ResponseCallback& next_callback,
    T<brave::BraveRequestInfo> ctx,
    const std::optional<GURL>& url,
    brave_wallet::mojom::ProviderError error,
    const std::string& error_message) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (ctx && error == brave_wallet::mojom::ProviderError::kSuccess && url &&
      IsValidDecentralizedDnsRedirectUrl(*url)) {
    ctx->set_new_url_spec(url->spec());
  }

  if (!next_callback.is_null()) {
    next_callback.Run();
  }
}

template <template <typename> class T>
void OnBeforeURLRequest_SpacesResolveWork(
    const brave::ResponseCallback& next_callback,
    T<brave::BraveRequestInfo> ctx,
    std::optional<net::IPAddress> address) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!ctx || next_callback.is_null()) {
    return;
  }
  if (!address) {
    ctx->set_pending_error(net::ERR_NAME_NOT_RESOLVED);
    next_callback.Run();
    return;
  }
  content::GetNetworkService()->SetSpacesHostAddress(
      std::string(ctx->request_url().host()), *address,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(base::BindOnce(
          [](const brave::ResponseCallback& next_callback) {
            next_callback.Run();
          },
          next_callback)));
}

template int OnBeforeURLRequest_DecentralizedDnsPreRedirectWork<
    std::shared_ptr>(const brave::ResponseCallback& next_callback,
                     std::shared_ptr<brave::BraveRequestInfo> ctx);

template int OnBeforeURLRequest_DecentralizedDnsPreRedirectWork<base::WeakPtr>(
    const brave::ResponseCallback& next_callback,
    base::WeakPtr<brave::BraveRequestInfo> ctx);

}  // namespace decentralized_dns
