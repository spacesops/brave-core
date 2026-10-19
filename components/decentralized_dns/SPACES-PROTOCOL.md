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
- **What a resolved name points to** (IP, content hash, URL record, etc.) and
  how that becomes a redirect target.
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
