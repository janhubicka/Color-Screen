# Rolling `current` release (Color-Screen 2.0 development)

The **Publish current release** workflow updates the GitHub release whose tag
and title are both `current`. It is a **prerelease for the upcoming 2.0**, not
an announcement of the final 2.0 release. It does not change the application
version, publish a `v2.0` tag, or replace the existing stable release as Latest.

## When it publishes

The workflow responds to completion of **MacOS build**, **Windows build**, or
**Ubuntu build** on `main`. It publishes only when all three complete workflows
have succeeded for the same current `main` commit, including the existing
checks and smoke tests. The Ubuntu workflow supplies the source tarball only
after `make distcheck` has validated it. The publisher therefore does not
publish a macOS package merely because the app artifact was uploaded before the
later checking build and relocated-bundle smoke test, nor publish a source
archive before distcheck succeeds.

Only same-repository **push** builds qualify. Pull requests (including forks),
other branches, manual platform builds, failed/cancelled builds and superseded
main commits cannot publish. All Windows GCC matrix variants and the complete
Ubuntu workflow must finish successfully. Windows Clang and the dedicated
sanitizer workflow remain independent CI checks; they are not additional gates
for this packaging release.

Publication is serialized without cancelling an active publisher. Main HEAD and
all three run IDs/attempts are checked again after downloading every package, so
a new commit or rerun during staging cannot silently mix revisions. Missing,
expired, corrupt or wrongly attributed artifacts fail before release writes.
The workflow reuses existing build artifacts; it does not rebuild the program.

## Release assets

| Asset | Contents |
| --- | --- |
| `Color-Screen-2.0-current-macOS.zip` | Existing self-contained `Color-Screen.app` bundle, unchanged inside its original ZIP. |
| `Color-Screen-2.0-current-windows-ucrt64-installer.exe` | General x86-64 NSIS installer. |
| `Color-Screen-2.0-current-windows-ucrt64-portable.zip` | Complete installed tree; extract everything and launch `bin/colorscreen-qt.exe`. |
| `Color-Screen-2.0-current-windows-ucrt64-znver2-installer.exe` | Installer for the existing `-march=znver2` build. |
| `Color-Screen-2.0-current-windows-ucrt64-znver2-portable.zip` | Portable `-march=znver2` build. |
| `Color-Screen-2.0-current-windows-ucrt64-znver4-installer.exe` | Installer for the existing `-march=znver4` build. |
| `Color-Screen-2.0-current-windows-ucrt64-znver4-portable.zip` | Portable `-march=znver4` build. |
| `Color-Screen-2.0-current-sources.tar.gz` | Exact source archive produced and validated by Ubuntu `make distcheck`. |
| `SHA256SUMS` | SHA-256 checksums of all eight packages and the build manifest. |
| `BUILD-INFO.json` | Source SHA, target release series, all three source build URLs/IDs/attempts and package checksums. |

Prefer the unsuffixed `ucrt64` Windows packages unless the CPU supports the
instruction set used by a specialized build. Portable packages use the
`windows-binary-*` Actions archives, **not** the older bin-only `portable-binary-*`
archives: the complete tree retains shared resources, DLLs, LICENSE and README.
The raw Actions ZIP is already a portable archive and is not wrapped in another
ZIP. The macOS app's internal executable permissions, framework symlinks and
ad-hoc signature are preserved by copying its original `ditto` ZIP byte-for-byte.
The source Actions artifact is opened only to copy its single
`colorscreen-*.tar.gz` member; the publisher never extracts it. It rejects
unexpected roots, unsafe absolute/`..` members and missing core release files
before touching the GitHub release. This does not introduce Developer ID signing
or notarization, a new macOS architecture, or a change to the application build
process.

Asset names and the release URL remain stable between successful main builds:

<https://github.com/janhubicka/Color-Screen/releases/tag/current>

## Permissions and recovery

Only the serialized publishing job receives `contents: write` and `actions:
read`, using the normal `GITHUB_TOKEN`. No personal access token is needed.
Publisher code comes from trusted `main`; artifact contents are never executed
or extracted into the checkout. PR tests have read-only repository permissions.
The `current` release must remain mutable, and repository tag rules must permit
the Actions token to move `current`. Existing stable tags are never moved.

The first publication starts as a draft. After uploads succeed, the publisher
creates/moves the `current` Git tag to the tested SHA and publishes the release
notes as a prerelease. Only matching asset names under `current` are replaced;
other releases and unrelated assets are not deleted.

GitHub does not provide an atomic multi-asset replacement. `gh release upload
--clobber` replaces matching files individually; a network failure during upload
can leave a partial update. The source tag and release notes are not advanced
until the upload command succeeds. To recover, rerun the failed publishing job
or manually run **Publish current release** with branch **main**. The manual
entry point still requires successful push builds of the current main SHA; it
cannot bypass the tests. If artifacts have expired, rerun their original push
runs (or push a new main revision) before publishing again.

## Diagnosing transient Windows installer failures

Windows packages require **official NSIS 3.11** for installation and
uninstallation smoke testing. The MSYS2-provided NSIS stub is intentionally
not used for release installers because it has previously stalled before
extracting any files on GitHub-hosted Windows runners. The Windows workflow
first checks whether the official compiler is installed, then installs the
pinned Chocolatey package with **three bounded attempts** to tolerate transient
Chocolatey community feed HTTP 5xx/504 errors. After provisioning, the
workflow still requires an actual `makensis.exe` and runs the usual installer
and uninstaller smoke tests. This retry does not weaken any release checks.

If just one Windows matrix job fails during dependency download while its
build and `make check` have already passed, use **Re-run failed jobs** on
that *original main-branch Windows workflow*. This preserves successful
matrix jobs and their build artifacts while retrying the failing package step
on a fresh runner. The rolling publisher requires the whole matching Windows
workflow to conclude successfully; a publisher workflow reporting success
while saying `Not publishing: build-windows.yml has not succeeded` is
an intentional no-op, not evidence that the release is current.

After a successful Windows rerun, the `workflow_run` publisher checks all
three platform builds again, compares the selected attempts, validates every
package/provenance and updates `current` only for the still-current main
commit. If the automatic publisher does not run, the documented manual
**Publish current release** dispatcher can apply the same checks.

## Testing and maintenance

The following offline suite runs on publisher-related PRs and before publishing:

```sh
python3 -m unittest discover -s .github/scripts -p 'test_current_release.py' -v
```

It covers provenance and latest-run selection, failed/incomplete builds, artifact
validation, checksum manifests, intact macOS archives, Windows installed trees,
distchecked source archives (including unsafe-member rejection), first/repeated
publication, stale HEADs, reruns, immutability and upload errors. If platform
workflow names, artifact names or Windows variants change, update
`release-current.yml`, `publish-current-release.py` and its tests together.
