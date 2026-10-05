# Color-Screen 2.0 release checklist

This checklist is for promoting the tested 2.0 development line to the final
`v2.0` release. The mutable `current` release remains a prerelease and should
continue to be used for beta/development packages until the final release is
approved.

## 1. Choose the release commit

Choose one commit already on `main`. Do not create `v2.0` first and expect the
normal platform workflows to build the tag: the maintained macOS, Windows and
Ubuntu workflows are triggered by pushes to `main` (and selected maintenance
branches), not tag pushes.

Prefer a commit for which the rolling `current` release already points to the
same SHA. That gives the final release a tested, provenance-recorded macOS app,
Windows packages and distchecked source archive from one commit.

## 2. Require the complete automated gate

For the chosen SHA, require successful main runs of:

- Ubuntu build, including `make check`, checking/sanitizer lanes, `distcheck`
  and source-tarball reconstruction;
- MacOS build, including packaged-app relocation/smoke checks;
- Windows build, including all maintained GCC package variants;
- Windows build (clang);
- Sanitizer builds;
- Doxygen Documentation;
- Publish current release.

The rolling publisher itself gates on matching successful macOS, Windows and
Ubuntu builds, including the Ubuntu `distcheck` source artifact. The
final-release decision should additionally inspect the independent clang-Windows,
dedicated sanitizer and documentation workflows above.

## 3. Complete the manual GUI beta gate

Before changing the version, exercise at least:

- one regular-screen capture through screen detection, geometry/registration,
  reconstruction/detail, colour/tone and render-to-file;
- one line-screen capture;
- one RGB+IR or monochrome capture, including the intended native/simulated
  image-layer workflow;
- parameter save/load and failed-save recovery;
- multiple documents, New View, detach/reattach and one slanted-edge reference;
- cancellation/supersession of at least one long-running analysis;
- installer/portable startup on Windows and the packaged app on macOS.

Record any operator-confusion issue that appears during these passes in
`doc/qtgui-workflow-roadmap.md` rather than redesigning the nine-panel
navigation before beta feedback.

## 4. Change release-version metadata together

The development version is currently `2.0alpha`. For final 2.0, update the
tracked release-version sources in the same patch:

- `configure.ac`: `AC_INIT([colorscreen], [2.0], ...)`;
- `os/linux/control`: `Version: 2.0`;
- `NEWS`: first line exactly `Changes in version 2.0`.

Ubuntu CI derives its Debian package version from the configured top-level
`PACKAGE_VERSION`, so it must not grow another literal release version.

Run `autoreconf -fi` when required by the normal generated-build-metadata
workflow and verify the generated-file audit before merging.

Do not change the rolling `current` asset names or mark it as a stable release
as part of this step.

## 5. Freeze and verify release notes

`NEWS` should describe the 2.0 release, with the exact final heading required
by the promotion workflow. Review it once for user-visible additions/removals and
high-impact fixes, then stop accumulating unrelated development notes on the
release commit.

## 6. Publish the final release

After the version patch itself passes the complete automated and manual gates:

1. Confirm `current` points to the exact chosen SHA and its
   `BUILD-INFO.json`/checksums match that SHA.
2. From branch **main**, manually run **Publish Color-Screen 2.0**.
3. The guarded promoter rechecks final source metadata, `main`/`current`
   identity, rolling-release provenance/checksums and every package before any
   stable-release write.
4. It creates/resumes a draft `v2.0` release, uploads byte-identical tested
   package payloads under stable names, writes final provenance/checksums, and
   only then publishes it as a non-prerelease Latest release.
5. Smoke-test downloads from the final release page, not only Actions artifacts.
6. Leave `current` available for subsequent post-2.0 development rather than
   moving the stable `v2.0` tag.

The workflow refuses to modify an already-published `v2.0` release or a
`v2.0` tag pointing at another commit. A failed upload leaves at most a draft
that can be resumed by rerunning the same manual workflow.
