# Releasing zedit

1. Make sure `project(zedit VERSION x.y.z ...)` in the top-level `CMakeLists.txt` has the version to
   release, and the `CHANGELOG.md` heading says the release date instead of "unreleased".
   (Every pull request bumps the version and adds its changelog entry; change nothing else.)
2. Merge to `main`, then tag and push: `git tag vX.Y.Z && git push origin vX.Y.Z`.
3. `.github/workflows/release.yml` builds and tests on Linux and macOS, packages
   `zedit-X.Y.Z-Linux-amd64.deb` and `zedit-X.Y.Z-Darwin.dmg`, writes `SHA256SUMS`,
   **signs it** with minisign (`SHA256SUMS.minisig`), checks that signature against the public
   key in `core/src/update_signing_key.cpp` at the tagged commit, and creates the GitHub Release.
   Without the signing secrets, or with no public key in the source, the publish job stops
   and nothing is released.
   Do not also create the release by hand: the workflow does it, and a release without
   `SHA256SUMS.minisig` can never be installed by the in-app updater.

(Releases before 1.8.1 were made by hand and have no `SHA256SUMS`; see "Transition".)

## Signing key

The in-app updater (Help → Check for Updates) installs only releases whose `SHA256SUMS`
verifies against the minisign public key compiled into the app, so a compromised GitHub
release alone cannot push malware to users.

zedit has its **own release key**: key ID `DBC46B23C3AF0EF3` (public key below), generated
with `minisign -G` in October 2026. Its public half is in `core/src/update_signing_key.cpp`.
The secret key lives only on the maintainer's machine (`~/.minisign/minisign.key`) and, with its
password, in the two repository secrets `MINISIGN_SECRET_KEY` and `MINISIGN_PASSWORD` that
`release.yml` uses; it is never committed or pasted into a chat. To set or replace the secrets
(the owner only):

```sh
gh secret set MINISIGN_SECRET_KEY -R sbj-ee/zedit < ~/.minisign/minisign.key
gh secret set MINISIGN_PASSWORD   -R sbj-ee/zedit    # paste the password when asked
```

To change the key: generate one on your own machine (not in CI, not on a shared box),
`minisign -G -p zedit-release.pub -s zedit-release.key`, set the two secrets from that key,
paste the second line of the `.pub` file (it starts with `RW`) into `kUpdateSigningPublicKey` in
`core/src/update_signing_key.cpp`, update the key ID in this file, the README and
`tests/core/test_minisign.cpp` (the "release key compiled into zedit" test), and release.
`tests/data/minisign/release-key/` is a real signature by a *different* key (zterminal's, whose
public key is a constant in the test); it only proves the verifier reads real minisign output
and does not need to change.

Keep the secret key and its password in a password manager plus an offline backup. If it is
lost, installed copies cannot verify later releases (install the next one by hand); if it
leaks, rotate it: new key pair, new secrets, new public key in the source, release, and tell
users to install that release by hand.

## Checking a release by hand

```sh
minisign -Vm SHA256SUMS -P RWTzDq/DI2vE2xMeF01QWTxP2lwwlxGL9M1UxQE4EFV5kVmTF84wwyi2
sha256sum -c --ignore-missing SHA256SUMS          # Linux
shasum -a 256 -c --ignore-missing SHA256SUMS      # macOS
```

The first command proves `SHA256SUMS` was signed by the release key; the second proves your
package matches it.

## Transition

Releases up to 1.7.4 have no `SHA256SUMS` or signature and the app before 1.8.1 cannot
install updates itself. Install the first signed release (1.8.1 or later) by hand, verifying it
with `minisign -Vm` as above; updates after that are checked in the app. A release that
lacks `SHA256SUMS.minisig` is never installed by the app: it says so and points at the
release page.

## What the updater does

`core/src/update_service.cpp` (the logic; the dialogs are `frontend_imgui/src/update_ui.cpp`):

1. `GET api.github.com/repos/sbj-ee/zedit/releases/latest` (through `curl`, https only, also
   across redirects). A newer version is offered; "Skip This Version" silences it for the
   automatic check only.
2. Download `SHA256SUMS` and `SHA256SUMS.minisig`; verify the signature **before** the package
   is downloaded. No signature asset, or an invalid one: stop, install nothing, say so.
3. Download the package (`.deb` on Linux, `.dmg` on macOS) and check its SHA-256 against the
   signed `SHA256SUMS`.
4. Linux: copy the package into a private (`0700`) folder, hash it again, and run
   `/usr/bin/pkexec /usr/bin/apt install -y <absolute path>`. Only when the running binary is
   `/usr/bin/ze` (the .deb install). If the password prompt is dismissed (pkexec exit 126), or
   anything else fails, the verified package is kept and the dialog shows
   `sudo apt install <path>`.
   macOS: open the verified `.dmg` for the usual drag-to-Applications install.
5. After an install on Linux, offer "Restart Now" (disabled while a buffer has unsaved changes);
   it re-executes `/usr/bin/ze` with the current file.

## Local checks

`ctest` runs the updater tests (`[sha256]`, `[minisign]`, `[update]`, `[update-service]`) against
fixtures and a fake release server on a local socket, with the real `curl` and a stand-in
installer; nothing is installed. The fixtures in `tests/data/minisign/` were made with the real
`minisign` tool and a throwaway test key.
