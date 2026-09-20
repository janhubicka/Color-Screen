# Rolling `current` release (Color-Screen 2.0 development)

The **Publish current release** workflow updates the GitHub release whose tag
and title are both `current`. It is a **prerelease for the upcoming 2.0**, not
an announcement of the final 2.0 release. It does not change the application
version, publish a `v2.0` tag, or replace the existing stable release as Latest.

## When it publishes

The workflow responds to completion of **MacOS build** or **Windows build** on
`main`. It publishes only when **both complete workflows have succeeded for the
same current `main` commit**, including the existing checks and smoke tests.
It does not publish a macOS package merely because the app artifact was uploaded
before the later checking build and relocated-bundle smoke test.

Only same-repository **push** builds qualify. Pull requests (including forks),
other branches, manual platform builds, failed/cancelled builds and superseded
main commits cannot publish. All Windows GCC matrix variants must finish
successfully. The Windows Clang, Linux and dedicated sanitizer workflows remain
independent CI checks; they are not additional gates for this packaging release.

Publication is serialized without cancelling an active publisher. Main HEAD and
both run IDs/attempts are checked again after downloading every package, so a
new commit or rerun during staging cannot silently mix revisions. Missing,
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
| `SHA256SUMS` | SHA-256 checksums of all seven packages and the build manifest. |
| `BUILD-INFO.json` | Source SHA, target release series, source build URLs/IDs/attempts and package checksums. |

Prefer the unsuffixed `ucrt64` Windows packages unless the CPU supports the
instruction set used by a specialized build. Portable packages use the
`windows-binary-*` Actions archives, **not** the older bin-only `portable-binary-*`
archives: the complete tree retains shared resources, DLLs, LICENSE and README.
The raw Actions ZIP is already a portable archive and is not wrapped in another
ZIP. The macOS app's internal executable permissions, framework symlinks and
ad-hoc signature are preserved by copying its original `ditto` ZIP byte-for-byte.
This does not introduce Developer ID signing or notarization, a new macOS
architecture, or a change to the existing packaging process.

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

## Testing and maintenance

The following offline suite runs on publisher-related PRs and before publishing:

```sh
python3 -m unittest discover -s .github/scripts -p 'test_current_release.py' -v
```

It covers provenance and latest-run selection, failed/incomplete builds, artifact
validation, checksum manifests, intact macOS archives and Windows installed trees,
first/repeated publication, stale HEADs, reruns, immutability and upload errors.
If platform workflow names, artifact names or Windows variants change, update
`release-current.yml`, `publish-current-release.py` and its tests together.
