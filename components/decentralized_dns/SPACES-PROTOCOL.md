# Spaces Protocol (.spaces) resolution — implementation notes

Working log for adding Spaces Protocol hostname resolution to Brave, modeled on
the existing SNS (`.sol`) support. Name-format details live in
[spaces-names.md](spaces-names.md). Protocol site: <https://spacesprotocol.org>.

## Status

| Phase | State |
|---|---|
| Settings scaffolding (pref, helpers, settings UI row, unit tests) | Done — commit `1e1828c0284` on `spaces-protocol-support` |
| Ask-mode interstitial | Not started |
| Actual resolution (Enabled mode) | Not started |
| Mojo / iOS / Android exposure | Not started |

## Primary example: `retro@space`

A live handle, resolved with the Fabric CLI (`fabric resolve retro@space`),
which uses libveritas to verify the relay's proof:

```json
{
  "anchor": 971172,
  "anchor_hash": "9641f29cca4fc8948a8126ce9e5003d7ea473966f3477af4ea1bb4672ed7550c",
  "sovereignty": "sovereign",
  "handle": "retro@space",
  "canonical": "retro@space",
  "alias": null,
  "script_pubkey": "5120b2c6efcd4597637755465065f46182a46a1c20f2759b3e32e756a19bf98e1e6d",
  "records": [
    { "type": "seq", "version": 5 },
    { "type": "txt", "key": "type", "value": ["spaces-webproxy"] },
    { "type": "addr", "key": "ipv4", "value": ["68.183.128.132"] },
    {
      "type": "sig",
      "canonical": "retro@space",
      "handle": "retro@space",
      "sig": "5ae127c65c044d821cd4bf151c206636504243afe4b2c9377125ddcb5028ea4c309d1ac173b83630432f2be32209eef26c0c8b26c100c3ad7730a734565c2c8a",
      "flags": 1
    }
  ],
  "fallback_records": [],
  "delegate": { "status": "unknown" },
  "commitment": { "status": "unknown" },
  "num_id": "num1nuwls5fcl6mkqesfv0a8a27gpelvyqvqvpgqjaqwj0ccdd3kzggs569r97"
}
```

Fields that matter for browser resolution:

- `records` is the [SIP-7](https://github.com/spacesprotocol/sips/blob/main/sip-0007.mediawiki)
  record set. Here, `ADDR ipv4` gives a host address and `TXT type =
  spaces-webproxy` labels what the handle serves. Neither key is in SIP-7's
  recommended-key list, which only has `website` (TXT) for web content.
- `seq` is the record-set version; a higher value is newer.
- `sig` is the owner's signature over the record set (`flags: 1`).
- `sovereignty`, `anchor` and `anchor_hash` describe the verification result:
  the Bitcoin block height and hash of the trust anchor the proof was checked
  against.
- `num_id` is the handle's permanent numeric ID (bech32m, `num1…`).

The server at `68.183.128.132` is a custom nginx web server on the default
HTTP port 80 (plain HTTP, no TLS). It is configured to route Spaces Protocol
hostnames to matching content folders, so a single IP serves many handles and
picks the site from the requested hostname. This matches the `TXT type =
spaces-webproxy` marker: the IP is a shared web proxy, not a dedicated host.

`retro@space` is a Spaces handle, not a hostname. How a `.spaces` URL maps to
it is still an open question (see below).

## How decentralized DNS works in Brave today

Each name service (Unstoppable Domains, ENS, SNS) has an integer local-state
pref holding a `ResolveMethodTypes` value (`components/decentralized_dns/core/constants.h`):
`ASK = 0`, `DISABLED = 1`, `DEPRECATED_DNS_OVER_HTTPS = 2`, `ENABLED = 3`.
Note `ENABLED` is 3, not 2 — the settings TS compares against the raw number.

The pref is read in two places:

1. **Ask** — `content/decentralized_dns_navigation_throttle.cc` defers
   navigations to a matching TLD and shows an opt-in interstitial
   (`decentralized_dns_opt_in_page.cc`,
   `decentralized_dns_interstitial_controller_client.cc`). The controller client
   maps the TLD to the pref it writes when the user opts in or out.
2. **Enabled** — `browser/net/decentralized_dns_network_delegate_helper.cc`
   intercepts the request before it is sent, resolves the name (for SNS via
   `brave_wallet::JsonRpcService::SnsResolveHost`) and redirects to the result.

**Disabled** means neither path acts, so the hostname goes to normal DNS and
fails to resolve.

Settings UI: `browser/resources/settings/brave_web3_domains_page/` binds a
`settings-dropdown-menu` to each pref via `settingsPrivate`. Dropdown options
come from `BraveDefaultExtensionsHandler::GetDecentralizedDnsResolveMethodList`
(`browser/ui/webui/settings/brave_default_extensions_handler.cc`). For the page
to read and write a pref, it has to be on the allowlist in
`browser/extensions/api/settings_private/brave_prefs_util.cc`.

## What the scaffolding added

- Pref `brave.spaces.resolve_method` (`kSpacesResolveMethod`, `core/pref_names.h`),
  registered with a default of `ASK` (same as SNS) in `core/utils.cc`.
- `kSpacesDomain = ".spaces"` in `net/decentralized_dns/constants.h`.
- Helpers in `core/utils.{h,cc}`: `IsSpacesTLD`, `Set/GetSpacesResolveMethod`,
  `IsSpacesResolveMethodAsk`, `IsSpacesResolveMethodEnabled`. Nothing calls them
  yet.
- Settings allowlist entry, string `IDS_SETTINGS_RESOLVE_SPACES_DESC` /
  `resolveSpacesDesc`, and a dropdown row below SNS on brave://settings/web3.
- Tests in `browser/decentralized_dns/test/utils_unittest.cc`.

Because nothing reads the pref yet, the default of `ASK` currently has no
visible effect.

## Hostname resolution (HTTP only)

`http://retro@space/` loads when the pref is `ENABLED`. Verified manually: the
page loads from 68.183.128.132. Hooks:

- `net/decentralized_dns/spaces.{h,cc}`: `GetSpacesHandle(GURL)` returns
  `name@space` when the URL has a username, no password, and a dot-free host.
  The file also holds the host-to-IP map used by the network service.
- `browser/spaces/spaces_resolver.{h,cc}` plus the `rs/` cxx bridge resolve
  the handle with the patched Fabric client (Observed trust) and read the
  `ipv4` addr record.
- `browser/net/decentralized_dns_network_delegate_helper.cc` resolves the
  handle, then pushes the mapping to the network service through
  `NetworkService::SetSpacesHostAddress` (a Brave mojom addition). Failures
  return `ERR_NAME_NOT_RESOLVED`.
- `chromium_src/net/dns/host_resolver_manager.cc` treats a mapped host as an
  IP literal, so no DNS lookup happens.
- `chromium_src/net/http/http_network_transaction.cc` sends
  `Host: retro@space`.
- `chromium_src/chrome/browser/ssl/https_upgrades_interceptor.cc` skips HTTPS
  upgrades for Spaces URLs.
- `chromium_src/components/omnibox/browser/autocomplete_input.cc` treats a
  bare `retro@space` as a URL rather than a search.
- `chromium_src/components/omnibox/browser/location_bar_model_impl.cc` keeps
  the username when formatting, so the location bar shows `retro@space`
  instead of `space`. Other surfaces that use `url_formatter`, such as tab
  hover cards and history, still strip it.
- `chromium_src/third_party/blink/renderer/core/dom/icon_url.cc` keeps the
  username on the default `/favicon.ico` URL. Blink builds that URL from scheme,
  host and port only, so without this it went out with `Host: space`. Relative
  subresources already keep the username, and Blink allows them because they
  match the page's credentials.

Limitation: the origin ignores the username, so every handle shares the origin
`http://space`. Requests built from the origin, such as
`location.origin + "/x"`, still go out with `Host: space`. All handles also
share cookies and storage.

## Remaining work (SNS touchpoints to mirror)

- [ ] Throttle: add an `IsSpacesTLD && IsSpacesResolveMethodAsk` branch.
- [ ] Opt-in page and controller client: Spaces strings and the
      `kSpacesResolveMethod` mapping.
- [ ] Network delegate: an `IsSpacesTLD && IsSpacesResolveMethodEnabled`
      branch that calls a Spaces resolver.
- [ ] Spaces resolver. Spaces is Bitcoin-based, so `JsonRpcService` (EVM and
      Solana) is probably not the right home. Needs a design decision.
- [ ] `browser/net/decentralized_dns_network_delegate_helper_unittest.cc` and
      `browser/decentralized_dns/test/decentralized_dns_navigation_throttle_browsertest.cc`
      coverage.
- [ ] Optional: Mojo `Get/SetSpacesResolveMethod` (`brave_wallet.mojom`), plus
      the iOS (`Web3SettingsView.swift`) and Android (`WalletNativeUtils`) UIs.

## Open questions

- **URL to Spaces name mapping.** Spaces names are `handle@space` or
  `#block-txpos-vout`, but a browser hostname can't contain `@` or `#`. We need
  to decide how a `.spaces` hostname maps to a name, for example whether
  `alice.bitcoin.spaces` means `alice@bitcoin`, and how numeric labels are
  addressed (if at all).
- **What a resolved name points to** and how that becomes a redirect target.
  The `retro@space` example has an `ADDR ipv4` record and a `TXT type =
  spaces-webproxy` marker rather than a SIP-7 `website` URL, so Brave may need
  to support an IP address (DNS-like) as well as a URL redirect. The
  `spaces-webproxy` server is name-based virtual hosting on port 80, so Brave
  has to connect to the resolved IP while still sending the Spaces hostname in
  the `Host` header. A plain redirect to `http://68.183.128.132/` would lose
  the hostname and serve the wrong site (or none). Still to confirm: the exact
  `Host` value nginx matches on (for example `retro.space.spaces` or
  `retro@space`).
- **Trust model.** Is resolution done through a remote resolver service or by
  verifying proofs locally (libveritas)? This affects privacy wording in the
  settings and interstitial strings.

## Build and test notes (macOS, this checkout)

- Use `pnpm`, not `npm`. `npm run ...` fails with `EBADDEVENGINES`.
- Build through the wrapper:
  `pnpm run build Component --target_arch arm64 [--target brave_unit_tests]`.
  Running `autoninja` directly fails because the wrapper sets up `PYTHONPATH`
  (`brave_chromium_utils`).
- `depot_tools` is in `src/brave/vendor/depot_tools`, not on `PATH`.
- `pnpm run format` / `presubmit` need to run outside the Cursor sandbox
  (vpython cache lock under `~/Library/Caches`).
- Unit tests:
  `out/Component_arm64/brave_unit_tests --gtest_filter='UtilsUnitTest.*'`.
- Baseline timing: the first `brave_unit_tests` build took about 36 minutes;
  incremental `brave` builds take seconds.
- Manual check: `pnpm start Component --target_arch arm64`, then open
  brave://settings/web3.
