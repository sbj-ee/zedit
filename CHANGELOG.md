# Changelog

zedit had no changelog before 1.8.0; earlier history is in the git log and the GitHub releases.

## 1.8.2

### Fixed

- **Release workflow.** The v1.8.1 tag built and tested but its release run failed, so nothing
  was published. The macOS "Verify .dmg" step died with `hdiutil: attach canceled` because the
  .dmg shows its licence agreement and CI has no stdin; it now accepts the agreement
  non-interactively and still checks that `ze.app` and its executable are in the image. The Publish
  step also looked for the public key on a single line of `core/src/update_signing_key.cpp`, but
  the key literal sits on its own line, so it would have refused to publish ("has no public key");
  it now matches the key literal itself. No change to the app. 1.8.1 was tagged but never
  published; 1.8.2 is the first release signed with `DBC46B23C3AF0EF3`.

## 1.8.1 — tagged, not published (see 1.8.2)

### Changed

- **zedit's own release key.** The updater now trusts minisign key `DBC46B23C3AF0EF3` (zedit's
  own, replacing the zterminal key `32960BBA77B36F1F` that 1.8.0 embedded), the key that
  `release.yml` signs with. 1.8.0 was never tagged or released. The real-signature test (a
  published zterminal `SHA256SUMS`) now pins zterminal's key as a constant in the test instead of
  reading the embedded key, so it still proves real-minisign interoperability; the embedded
  key's ID and bytes are pinned by their own test. Docs name the new key. No other behaviour
  change.

## 1.8.0 — unreleased

### Added

- **In-app updates.** Help → Check for Updates now offers to install a newer release instead of
  just linking to it: it downloads the package, verifies it, and installs it.
  - The release's `SHA256SUMS` must carry a valid minisign signature
    (`SHA256SUMS.minisig`) by the release key built into zedit, checked before the package is
    downloaded; the package must then match the signed SHA-256. A release without a signature
    is never installed: zedit says so and points at the release page.
  - Linux: the verified package is copied to a private folder, re-hashed, and installed with
    `pkexec apt install` (a system password prompt; no terminal `sudo`). Only for the `.deb`
    install at `/usr/bin/ze`. If it fails, the verified package is kept and the manual
    `sudo apt install` command is shown.
  - macOS: the verified `.dmg` opens for drag-to-Applications.
  - After a Linux install, zedit offers to restart (disabled while a buffer has unsaved
    changes) and reopens the current file.
  - Download progress with Cancel; "Skip This Version"; **Help → Check for Updates at Startup**
    (on by default, at most once a day).
- Release workflow (`.github/workflows/release.yml`) that builds, tests, packages, signs
  `SHA256SUMS` with minisign and publishes on a `v*` tag; `docs/RELEASING.md`.

### Changed

- The startup update check now runs at most once a day (it ran on every launch), and can be
  switched off in the Help menu. Its state is stored in `~/.config/zedit/update_state`.
- Update checks parse versions as full semantic versions (prereleases sort before releases).

### Dependencies

- Vendored [Monocypher](https://monocypher.com/) 4.0.2 (public domain / BSD-2), statically linked,
  used only to verify the minisign signature. No new runtime libraries; the `.deb`'s
  dependencies are unchanged. Network access still goes through the system `curl`.
