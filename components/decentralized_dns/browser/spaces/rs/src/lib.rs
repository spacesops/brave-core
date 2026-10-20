// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

//! Resolves Spaces handles (e.g. `retro@space`) to IPv4 addresses with the
//! Fabric relay client. All HTTP goes through the embedder's `SpacesFetcher`;
//! everything runs on the thread that created the client.

use std::cell::RefCell;
use std::rc::{Rc, Weak};

use cxx::UniquePtr;
use fabric_resolver::client::Fabric;
use fabric_resolver::http::{Error, Request, Response, Transport, TransportFuture};
use futures::channel::oneshot;
use futures::executor::{LocalPool, LocalSpawner};
use futures::task::LocalSpawnExt;
use libveritas::sip7::{Record, RecordSet};

#[cxx::bridge(namespace = decentralized_dns::spaces)]
mod ffi {
    struct HttpRequest {
        method: String,
        url: String,
        /// `name: value` pairs.
        headers: Vec<String>,
        body: Vec<u8>,
    }

    struct HttpResponse {
        /// False on network error.
        ok: bool,
        status: u16,
        /// `name: value` pairs.
        headers: Vec<String>,
        body: Vec<u8>,
    }

    enum ResolveStatus {
        Ok,
        NotFound,
        NoIpv4Record,
        Failed,
    }

    struct ResolveResult {
        status: ResolveStatus,
        /// Raw value of the first `ipv4` addr record; set when status is Ok.
        ipv4: String,
    }

    extern "Rust" {
        type SpacesClient;
        type HttpRoundtripContext;

        fn new_spaces_client(fetcher: UniquePtr<SpacesFetcher>, seed: u64) -> Box<SpacesClient>;
        fn resolve_ipv4(self: &SpacesClient, handle: &str, callback: UniquePtr<ResolveCallback>);
        fn on_http_response(ctx: Box<HttpRoundtripContext>, response: HttpResponse);
    }

    unsafe extern "C++" {
        include!("brave/components/decentralized_dns/browser/spaces/rs/src/shim.h");

        type SpacesFetcher;
        type ResolveCallback;

        fn Fetch(self: &SpacesFetcher, request: HttpRequest, ctx: Box<HttpRoundtripContext>);
        fn Run(self: Pin<&mut ResolveCallback>, result: ResolveResult);
    }
}

use ffi::{ResolveResult, ResolveStatus};

struct Executor {
    pool: RefCell<LocalPool>,
    spawner: LocalSpawner,
}

impl Executor {
    fn run_until_stalled(&self) {
        // Re-entrant calls (e.g. a callback starting a new resolve) are picked
        // up by the outer run.
        if let Ok(mut pool) = self.pool.try_borrow_mut() {
            pool.run_until_stalled();
        }
    }
}

pub struct HttpRoundtripContext {
    tx: oneshot::Sender<ffi::HttpResponse>,
    executor: Weak<Executor>,
}

fn on_http_response(ctx: Box<HttpRoundtripContext>, response: ffi::HttpResponse) {
    let HttpRoundtripContext { tx, executor } = *ctx;
    let _ = tx.send(response);
    if let Some(executor) = executor.upgrade() {
        executor.run_until_stalled();
    }
}

struct CxxTransport {
    fetcher: UniquePtr<ffi::SpacesFetcher>,
    executor: Weak<Executor>,
    seed: u64,
}

impl Transport for CxxTransport {
    fn send(&self, request: Request) -> TransportFuture {
        let (tx, rx) = oneshot::channel();
        let request = ffi::HttpRequest {
            method: request.method.to_string(),
            url: request.url,
            headers: request.headers.into_iter().map(|(k, v)| format!("{k}: {v}")).collect(),
            body: request.body,
        };
        let ctx = Box::new(HttpRoundtripContext { tx, executor: self.executor.clone() });
        self.fetcher.Fetch(request, ctx);
        Box::pin(async move {
            let response = rx.await.map_err(|_| Error::new("request cancelled"))?;
            if !response.ok {
                return Err(Error::new("network error"));
            }
            let headers = response
                .headers
                .iter()
                .filter_map(|h| h.split_once(':'))
                .map(|(k, v)| (k.trim().to_string(), v.trim().to_string()))
                .collect();
            Ok(Response::new(response.status, headers, response.body))
        })
    }

    fn random_seed(&self) -> u64 {
        self.seed
    }
}

pub struct SpacesClient {
    fabric: Rc<Fabric>,
    executor: Rc<Executor>,
}

fn new_spaces_client(fetcher: UniquePtr<ffi::SpacesFetcher>, seed: u64) -> Box<SpacesClient> {
    let pool = LocalPool::new();
    let executor = Rc::new(Executor { spawner: pool.spawner(), pool: RefCell::new(pool) });
    let transport = CxxTransport { fetcher, executor: Rc::downgrade(&executor), seed };
    let fabric = Fabric::new().with_transport(Rc::new(transport));
    fabric.set_prefer_latest(false);
    Box::new(SpacesClient { fabric: Rc::new(fabric), executor })
}

impl SpacesClient {
    fn resolve_ipv4(&self, handle: &str, mut callback: UniquePtr<ffi::ResolveCallback>) {
        let fabric = self.fabric.clone();
        let handle = handle.to_string();
        let spawned = self.executor.spawner.spawn_local(async move {
            let result = match fabric.resolve(&handle).await {
                Ok(Some(zone)) => match first_ipv4(&zone.records)
                    .or_else(|| first_ipv4(&zone.fallback_records))
                {
                    Some(ipv4) => ResolveResult { status: ResolveStatus::Ok, ipv4 },
                    None => failure(ResolveStatus::NoIpv4Record),
                },
                Ok(None) => failure(ResolveStatus::NotFound),
                Err(_) => failure(ResolveStatus::Failed),
            };
            if let Some(callback) = callback.as_mut() {
                callback.Run(result);
            }
        });
        if spawned.is_ok() {
            self.executor.run_until_stalled();
        }
    }
}

fn failure(status: ResolveStatus) -> ResolveResult {
    ResolveResult { status, ipv4: String::new() }
}

fn first_ipv4(records: &RecordSet) -> Option<String> {
    records.unpack_owned().ok()?.into_iter().find_map(|record| match record {
        Record::Addr { key, value } if key == "ipv4" => value.into_iter().next(),
        _ => None,
    })
}
