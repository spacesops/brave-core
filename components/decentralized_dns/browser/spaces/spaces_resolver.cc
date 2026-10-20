/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/decentralized_dns/browser/spaces/spaces_resolver.h"

#include <list>
#include <memory>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/rand_util.h"
#include "base/strings/string_util.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "brave/components/decentralized_dns/core/pref_names.h"
#include "brave/components/decentralized_dns/core/utils.h"
#include "brave/net/decentralized_dns/spaces.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace decentralized_dns {

namespace {

constexpr size_t kMaxResponseSize = 4 * 1024 * 1024;
constexpr base::TimeDelta kCacheTtl = base::Minutes(5);

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("spaces_resolver", R"(
      semantics {
        sender: "Spaces Protocol resolver"
        description:
          "Queries Spaces certificate relays to resolve a Spaces handle such "
          "as retro@space to an IP address."
        trigger:
          "Navigating to a Spaces URL such as http://retro@space/ while "
          "Spaces Protocol resolution is enabled."
        data: "The Spaces handle being resolved."
        destination: OTHER
        destination_other: "Spaces certificate relays."
      }
      policy {
        cookies_allowed: NO
        setting:
          "brave://settings/web3, Resolve Spaces Protocol domain names."
        policy_exception_justification: "Not implemented."
      }
    )");

class UrlLoaderFetcher : public spaces::SpacesFetcher {
 public:
  explicit UrlLoaderFetcher(
      scoped_refptr<network::SharedURLLoaderFactory> loader_factory)
      : loader_factory_(std::move(loader_factory)) {}
  ~UrlLoaderFetcher() override = default;

  void Fetch(spaces::HttpRequest request,
             rust::Box<spaces::HttpRoundtripContext> ctx) const override {
    const std::string method(request.method);
    auto resource_request = std::make_unique<network::ResourceRequest>();
    resource_request->url = GURL(std::string(request.url));
    resource_request->method = method;
    resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    for (const auto& header : request.headers) {
      const std::string line(header);
      const size_t colon = line.find(':');
      if (colon == std::string::npos || colon == 0) {
        continue;
      }
      resource_request->headers.SetHeader(
          std::string_view(line).substr(0, colon),
          base::TrimWhitespaceASCII(std::string_view(line).substr(colon + 1),
                                    base::TRIM_ALL));
    }

    auto loader = network::SimpleURLLoader::Create(std::move(resource_request),
                                                   kTrafficAnnotation);
    loader->SetAllowHttpErrorResults(true);
    if (!request.body.empty()) {
      loader->AttachStringForUpload(
          std::string(request.body.begin(), request.body.end()));
    }

    loaders_.push_front(std::move(loader));
    auto it = loaders_.begin();
    auto on_complete =
        base::BindOnce(&UrlLoaderFetcher::OnComplete,
                       weak_ptr_factory_.GetWeakPtr(), it, std::move(ctx));
    if (method == "HEAD") {
      (*it)->DownloadHeadersOnly(
          loader_factory_.get(),
          base::BindOnce(
              [](base::OnceCallback<void(std::optional<std::string>)> done,
                 scoped_refptr<net::HttpResponseHeaders>) {
                std::move(done).Run(std::string());
              },
              std::move(on_complete)));
    } else {
      (*it)->DownloadToString(loader_factory_.get(), std::move(on_complete),
                              kMaxResponseSize);
    }
  }

 private:
  using LoaderList = std::list<std::unique_ptr<network::SimpleURLLoader>>;

  void OnComplete(LoaderList::iterator it,
                  rust::Box<spaces::HttpRoundtripContext> ctx,
                  std::optional<std::string> body) const {
    std::unique_ptr<network::SimpleURLLoader> loader = std::move(*it);
    loaders_.erase(it);

    spaces::HttpResponse response{};
    const auto* info = loader->ResponseInfo();
    response.ok = body.has_value() && info && info->headers;
    if (response.ok) {
      response.status = info->headers->response_code();
      size_t iter = 0;
      std::string name, value;
      while (info->headers->EnumerateHeaderLines(&iter, &name, &value)) {
        response.headers.push_back(name + ": " + value);
      }
      response.body.reserve(body->size());
      for (char c : *body) {
        response.body.push_back(static_cast<uint8_t>(c));
      }
    }
    spaces::on_http_response(std::move(ctx), std::move(response));
  }

  scoped_refptr<network::SharedURLLoaderFactory> loader_factory_;
  mutable LoaderList loaders_;
  mutable base::WeakPtrFactory<UrlLoaderFetcher> weak_ptr_factory_{this};
};

class ResolveCallbackImpl : public spaces::ResolveCallback {
 public:
  using Callback =
      base::OnceCallback<void(spaces::ResolveStatus, const std::string&)>;

  explicit ResolveCallbackImpl(Callback callback)
      : callback_(std::move(callback)) {}
  ~ResolveCallbackImpl() override = default;

  void Run(spaces::ResolveResult result) override {
    std::move(callback_).Run(result.status, std::string(result.ipv4));
  }

 private:
  Callback callback_;
};

}  // namespace

// static
SpacesResolver* SpacesResolver::GetInstance() {
  static base::NoDestructor<SpacesResolver> instance;
  return instance.get();
}

SpacesResolver::SpacesResolver() = default;
SpacesResolver::~SpacesResolver() = default;

void SpacesResolver::Init(PrefService* local_state) {
  local_state_ = local_state;
  pref_change_registrar_.Init(local_state);
  pref_change_registrar_.Add(
      kSpacesResolveMethod,
      base::BindRepeating(&SpacesResolver::OnResolveMethodChanged,
                          base::Unretained(this)));
  OnResolveMethodChanged();
}

void SpacesResolver::Shutdown() {
  pref_change_registrar_.RemoveAll();
  local_state_ = nullptr;
  SetSpacesHandleNavigationEnabled(false);
  weak_ptr_factory_.InvalidateWeakPtrs();
  pending_.clear();
  cache_.clear();
  client_.reset();
}

void SpacesResolver::Resolve(
    const std::string& handle,
    scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
    ResolveCallback callback) {
  callback = base::BindPostTaskToCurrentDefault(std::move(callback));

  auto cached = cache_.find(handle);
  if (cached != cache_.end()) {
    if (cached->second.expiry > base::TimeTicks::Now()) {
      std::move(callback).Run(cached->second.address);
      return;
    }
    cache_.erase(cached);
  }

  auto& callbacks = pending_[handle];
  callbacks.push_back(std::move(callback));
  if (callbacks.size() > 1) {
    return;
  }

  if (!client_) {
    client_.emplace(spaces::new_spaces_client(
        std::make_unique<UrlLoaderFetcher>(std::move(loader_factory)),
        base::RandUint64()));
  }
  (*client_)->resolve_ipv4(
      handle, std::make_unique<ResolveCallbackImpl>(base::BindPostTaskToCurrentDefault(
                  base::BindOnce(&SpacesResolver::OnResolved,
                                 weak_ptr_factory_.GetWeakPtr(), handle))));
}

void SpacesResolver::OnResolveMethodChanged() {
  SetSpacesHandleNavigationEnabled(IsSpacesResolveMethodEnabled(local_state_));
}

void SpacesResolver::OnResolved(const std::string& handle,
                                spaces::ResolveStatus status,
                                const std::string& ipv4) {
  std::optional<net::IPAddress> address;
  net::IPAddress parsed;
  if (status == spaces::ResolveStatus::Ok && parsed.AssignFromIPLiteral(ipv4) &&
      parsed.IsIPv4()) {
    address = parsed;
    cache_[handle] = {parsed, base::TimeTicks::Now() + kCacheTtl};
  }

  auto node = pending_.extract(handle);
  if (node.empty()) {
    return;
  }
  for (auto& callback : node.mapped()) {
    std::move(callback).Run(address);
  }
}

}  // namespace decentralized_dns
