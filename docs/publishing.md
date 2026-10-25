# Publishing and maintainer handoff

This document is for maintainers. It describes the implemented machinery and
the external setup that is still required. It is not evidence that a release,
PyPI project, apt repository, or Debian upload exists.

## Current status

- Source version: `2.0.0`.
- `v2.0.0` tag/release: **not published**.
- PyPI/TestPyPI `neotrickle`: the JSON API returned 404 on 2026-10-24; the name
  appears unclaimed but ownership is not established until the PyPI account is
  configured.
- Debian: materials are prepared for `src:neotrickle`, `bin:trickle`; nothing
  has been uploaded or accepted.
- apt hosting: not configured; no production URL is invented here.

## Artifact flow

1. `Build release artifacts` runs on pull requests, `main`, and `v*` tags. It
   builds source, manylinux wheels, portable archives, and Debian packages from
   one source tree. It never publishes.
2. The `assemble` job generates `manifest.json` and `SHA256SUMS` and runs
   `scripts/validate-artifacts.py`.
3. `Publish tested release` is manually dispatched with the successful build
   run ID. It downloads that run's `release-artifacts`, verifies checksums, and
   publishes only those files. It does not rebuild.

## Maintainer launch checklist

### PyPI / TestPyPI

- [ ] Create or claim the `neotrickle` project on TestPyPI and PyPI.
- [ ] Configure a Trusted Publisher for this repository and the workflow file
      `.github/workflows/publish.yml`.
- [ ] Create GitHub environments named `testpypi` and `pypi`.
- [ ] Restrict the `pypi` environment to maintainers and require approval.
- [ ] Run a TestPyPI publication first (`testpypi: true`, `pypi: false`), install
      from TestPyPI in a clean environment, and verify `trickle -V`,
      `trickle doctor`, and a throttled transfer.
- [ ] Then publish to PyPI. PyPI versions are immutable; if a partial upload
      occurs, do not rebuild with the same version. Re-run the publish job with
      `skip-existing: true` for the already-uploaded artifact, or issue a new
      version for any source change.

### Architecture runners

The release build enables x86_64 and aarch64 wheel/portable jobs. aarch64 uses
GitHub's arm64 hosted runner plus the manylinux_2_28_aarch64 container. If arm64
runners are unavailable for the repository, remove the aarch64 matrix entries
until they are enabled; do not advertise aarch64 artifacts that were not built
and tested.

### GitHub release

- [ ] Create the `v2.0.0` tag and GitHub release outside the publish workflow.
- [ ] Create the `release` environment, restrict it, and require approval.
- [ ] Confirm the `github-release` job has `contents: write` and attaches the
      tested artifacts to the existing release.
- [ ] Do not attach locally rebuilt artifacts; use the manifest-matching set.

### apt repository

The implementation builds a signed repository; hosting is external.

- [ ] Create a dedicated signing key for the repository; do not reuse a
      personal key.
- [ ] Export the public key as a keyring at a stable path (for example
      `/usr/share/keyrings/neotrickle-archive-keyring.gpg`).
- [ ] Configure GitHub environment `apt` with:
      - `APT_GPG_PRIVATE_KEY` — ASCII-armored private key, secret.
      - `APT_GPG_KEY_ID` — signing key fingerprint, secret or variable.
      - `APT_REPO_TARGET` — rsync destination supported by the host, secret.
- [ ] Configure the hosting/web server to serve the generated `repo/` tree over
      HTTPS. The workflow does not invent or verify a public URL.
- [ ] Decide the supported suite names (`bookworm`, `trixie`, `jammy`,
      `noble`) and run the publish workflow once per supported suite and
      architecture. The workflow currently builds the amd64 apt index; arm64
      repository generation must be run with `--arch arm64` and the matching
      `.deb` set.
- [ ] Test the deployed repository with a scoped `Signed-By` entry, install and
      upgrade through apt, and verify signature failures are not suppressed.
- [ ] Document key rotation and revocation. Re-sign `Release`/`InRelease` when
      the key or repository contents change.

Example client configuration after hosting exists:

```bash
sudo install -d -m 0755 /usr/share/keyrings
sudo cp neotrickle-archive-keyring.gpg /usr/share/keyrings/
echo 'deb [signed-by=/usr/share/keyrings/neotrickle-archive-keyring.gpg] https://<apt-host>/neotrickle/debian bookworm main' |
  sudo tee /etc/apt/sources.list.d/neotrickle.list
sudo apt update
```

Do not use `[trusted=yes]` or disable signature verification in production.

### Debian maintainer handoff

The Debian source package name is `neotrickle`, the binary package is
`trickle`, and the installed command names are `trickle`/`trickled`. The
existing Debian maintainer relationship and upload decision belong to the
Debian maintainer; this repository does not assign responsibility or claim
acceptance.

Handoff materials:

- `debian/` packaging tree (`control`, `rules`, `copyright`, `changelog`,
  `NEWS`, service integration, docs/examples).
- `neotrickle_2.0.0.orig.tar.gz`, `.dsc`, and `.debian.tar.xz` from the
  Debian build job.
- Per-release `.deb` artifacts for the tested matrix.
- `docs/migration-classic.md` and `debian/README.Debian` for upgrade behavior.
- `docs/distribution.md` for the intended identity and dependency rules.

Validate the source package with `dpkg-buildpackage -S` and the binary package
with `dpkg-buildpackage -b`; then install, upgrade from the classic package,
remove, and purge in clean target environments. A locally built `.deb` does not
mean Debian archive acceptance.

## Partial-failure recovery

- PyPI: publication is append-only per version. Uploads are checked against the
  tested artifact checksums; use `skip-existing` only to finish an interrupted
  upload of the same files, never to replace a version.
- GitHub release: re-running the upload with `--clobber` replaces attachments
  but does not change the tested artifact contents.
- apt: regenerate indexes from the complete tested `.deb` set, re-sign, then
  deploy. Never publish a repository containing a package that failed
  validation.
- If any job fails validation, stop publication, retain the workflow artifact,
  and fix the source or packaging issue in a new build. Do not rebuild during
  publication.
