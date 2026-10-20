//! Minimal HTTP client used by `client` in place of `reqwest`.
//!
//! The embedder performs the actual network I/O by implementing [`Transport`]
//! and installing it with [`crate::client::Fabric::with_transport`]. Only the
//! subset of the `reqwest` API used by `client` is provided.

use serde::de::DeserializeOwned;
use std::fmt;
use std::future::Future;
use std::pin::Pin;
use std::rc::Rc;
use std::sync::atomic::{AtomicU64, Ordering};

/// An HTTP request handed to the embedder.
pub struct Request {
    pub method: &'static str,
    pub url: String,
    pub headers: Vec<(String, String)>,
    pub body: Vec<u8>,
}

pub type TransportFuture = Pin<Box<dyn Future<Output = Result<Response, Error>>>>;

/// Network transport supplied by the embedder.
pub trait Transport {
    fn send(&self, request: Request) -> TransportFuture;
    /// Random seed used to shuffle the relay pool.
    fn random_seed(&self) -> u64;
}

#[derive(Debug, Clone)]
pub struct Error(String);

impl Error {
    pub fn new(msg: impl Into<String>) -> Self {
        Self(msg.into())
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for Error {}

#[derive(Debug, Clone, Copy)]
pub struct StatusCode(u16);

impl StatusCode {
    pub fn is_success(&self) -> bool {
        (200..300).contains(&self.0)
    }

    pub fn as_u16(&self) -> u16 {
        self.0
    }
}

pub struct HeaderValue(String);

impl HeaderValue {
    pub fn to_str(&self) -> Result<&str, Error> {
        Ok(&self.0)
    }
}

pub struct HeaderMap(Vec<(String, HeaderValue)>);

impl HeaderMap {
    pub fn get(&self, name: &str) -> Option<&HeaderValue> {
        self.0
            .iter()
            .find(|(k, _)| k.eq_ignore_ascii_case(name))
            .map(|(_, v)| v)
    }
}

pub struct Response {
    status: StatusCode,
    headers: HeaderMap,
    body: Vec<u8>,
}

impl Response {
    pub fn new(status: u16, headers: Vec<(String, String)>, body: Vec<u8>) -> Self {
        Self {
            status: StatusCode(status),
            headers: HeaderMap(
                headers
                    .into_iter()
                    .map(|(k, v)| (k, HeaderValue(v)))
                    .collect(),
            ),
            body,
        }
    }

    pub fn status(&self) -> StatusCode {
        self.status
    }

    pub fn headers(&self) -> &HeaderMap {
        &self.headers
    }

    pub async fn bytes(self) -> Result<Vec<u8>, Error> {
        Ok(self.body)
    }

    pub async fn text(self) -> Result<String, Error> {
        String::from_utf8(self.body).map_err(|e| Error::new(e.to_string()))
    }

    pub async fn json<T: DeserializeOwned>(self) -> Result<T, Error> {
        serde_json::from_slice(&self.body).map_err(|e| Error::new(e.to_string()))
    }
}

#[derive(Clone, Default)]
pub struct Client {
    transport: Option<Rc<dyn Transport>>,
}

impl Client {
    pub fn new(transport: Rc<dyn Transport>) -> Self {
        seed_shuffle(transport.random_seed());
        Self {
            transport: Some(transport),
        }
    }

    pub fn get(&self, url: impl Into<String>) -> RequestBuilder {
        self.request("GET", url)
    }

    pub fn head(&self, url: impl Into<String>) -> RequestBuilder {
        self.request("HEAD", url)
    }

    pub fn post(&self, url: impl Into<String>) -> RequestBuilder {
        self.request("POST", url)
    }

    fn request(&self, method: &'static str, url: impl Into<String>) -> RequestBuilder {
        RequestBuilder {
            transport: self.transport.clone(),
            request: Request {
                method,
                url: url.into(),
                headers: Vec::new(),
                body: Vec::new(),
            },
        }
    }
}

pub struct RequestBuilder {
    transport: Option<Rc<dyn Transport>>,
    request: Request,
}

impl RequestBuilder {
    pub fn query<K: AsRef<str>, V: AsRef<str>>(mut self, pairs: &[(K, V)]) -> Self {
        for (k, v) in pairs {
            let sep = if self.request.url.contains('?') { '&' } else { '?' };
            self.request.url.push(sep);
            self.request.url.push_str(&encode_component(k.as_ref()));
            self.request.url.push('=');
            self.request.url.push_str(&encode_component(v.as_ref()));
        }
        self
    }

    pub fn header(mut self, name: &str, value: &str) -> Self {
        self.request
            .headers
            .push((name.to_string(), value.to_string()));
        self
    }

    pub fn body(mut self, body: impl Into<Vec<u8>>) -> Self {
        self.request.body = body.into();
        self
    }

    pub async fn send(self) -> Result<Response, Error> {
        match self.transport {
            Some(transport) => transport.send(self.request).await,
            None => Err(Error::new("no HTTP transport installed")),
        }
    }
}

fn encode_component(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for b in s.bytes() {
        if b.is_ascii_alphanumeric() || matches!(b, b'-' | b'_' | b'.' | b'~') {
            out.push(b as char);
        } else {
            out.push_str(&format!("%{b:02X}"));
        }
    }
    out
}

static SHUFFLE_STATE: AtomicU64 = AtomicU64::new(0x9E37_79B9_7F4A_7C15);

fn seed_shuffle(seed: u64) {
    SHUFFLE_STATE.store(seed | 1, Ordering::Relaxed);
}

fn next_random() -> u64 {
    // xorshift64*; only used to spread load across relays.
    let mut x = SHUFFLE_STATE.load(Ordering::Relaxed);
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    SHUFFLE_STATE.store(x, Ordering::Relaxed);
    x.wrapping_mul(0x2545_F491_4F6C_DD1D)
}

/// Fisher-Yates shuffle, replacing `rand::seq::SliceRandom::shuffle`.
pub fn shuffle<T>(items: &mut [T]) {
    for i in (1..items.len()).rev() {
        let j = (next_random() % (i as u64 + 1)) as usize;
        items.swap(i, j);
    }
}
