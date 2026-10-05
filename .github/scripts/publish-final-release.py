#!/usr/bin/env python3
"""Promote one fully tested rolling current build to the final v2.0 release.

This script is intentionally manual-only through release-2.0.yml. It never
rebuilds Color-Screen: it validates and copies the exact packages already
published by the rolling current release for the selected main commit.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile


REPOSITORY_PATTERN = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
SHA_PATTERN = re.compile(r"[0-9a-f]{40}")
TARGET_VERSION = "2.0"
CURRENT_TAG = "current"
FINAL_TAG = "v2.0"
CURRENT_PREFIX = "Color-Screen-2.0-current"
FINAL_PREFIX = "Color-Screen-2.0"
VARIANTS = ("ucrt64", "ucrt64-znver2", "ucrt64-znver4")


def package_names(prefix: str) -> tuple[str, ...]:
    """Return all package asset names for PREFIX in stable display order."""
    names = [f"{prefix}-macOS.zip"]
    for variant in VARIANTS:
        names.extend((
            f"{prefix}-windows-{variant}-installer.exe",
            f"{prefix}-windows-{variant}-portable.zip",
        ))
    names.append(f"{prefix}-sources.tar.gz")
    return tuple(names)


CURRENT_PACKAGES = package_names(CURRENT_PREFIX)
FINAL_PACKAGES = package_names(FINAL_PREFIX)


class GitHub:
    """Use gh for authenticated GitHub API reads and release mutations."""

    def __init__(self, repository: str):
        self.repository = repository
        self.base = f"repos/{repository}/"

    def command(self, *args: str, output=None) -> str:
        """Run gh without a shell, optionally streaming stdout to OUTPUT."""
        result = subprocess.run(
            ["gh", *args], check=True, stdout=output or subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        return result.stdout.decode() if output is None else ""

    def api(self, path: str, *, data: dict | None = None,
            method: str = "GET", optional: bool = False):
        """Read/write JSON; OPTIONAL permits only a genuine HTTP 404 response."""
        args = ["api", self.base + path, "--method", method]
        if data is not None:
            result = subprocess.run(
                ["gh", *args, "--input", "-"], input=json.dumps(data).encode(),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
            )
            return json.loads(result.stdout)
        try:
            return json.loads(self.command(*args))
        except subprocess.CalledProcessError as error:
            if optional and b"(HTTP 404)" in (error.stderr or b""):
                return None
            raise

    def download_release_asset(self, asset: dict, destination: Path) -> None:
        """Download one release asset and verify GitHub's digest when present."""
        with destination.open("wb") as output:
            self.command(
                "api", self.base + f"releases/assets/{asset['id']}",
                "-H", "Accept: application/octet-stream", output=output,
            )
        digest = asset.get("digest")
        if digest and digest != "sha256:" + sha256(destination):
            raise RuntimeError(f"Release asset digest mismatch: {asset['name']}")


def sha256(path: Path) -> str:
    """Hash PATH without loading a package into memory."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def release_metadata_is_final(root: Path) -> bool:
    """Return whether ROOT explicitly declares the final 2.0 source version."""
    configure = (root / "configure.ac").read_text(encoding="utf-8")
    control = (root / "os/linux/control").read_text(encoding="utf-8")
    news = (root / "NEWS").read_text(encoding="utf-8")
    return (
        "AC_INIT([colorscreen], [2.0], [hubicka@ucw.cz])" in configure
        and re.search(r"(?m)^Version:[ ]+2[.]0[ ]*$", control) is not None
        and news.startswith("Changes in version 2.0\n")
    )


def require_asset(assets: list[dict], name: str) -> dict:
    """Return exactly one non-empty CURRENT release asset named NAME."""
    matches = [asset for asset in assets if asset.get("name") == name]
    if len(matches) != 1:
        raise RuntimeError(f"Expected exactly one current release asset named {name}")
    asset = matches[0]
    if asset.get("state") not in (None, "uploaded") or asset.get("size", 0) <= 0:
        raise RuntimeError(f"Empty or incomplete current release asset: {name}")
    return asset


def release_asset_identity(release: dict) -> dict[str, tuple]:
    """Return immutable-enough identity used to detect CURRENT changing mid-copy."""
    return {
        asset["name"]: (
            asset.get("id"), asset.get("size"), asset.get("digest"),
            asset.get("updated_at"),
        )
        for asset in release.get("assets", [])
    }


def parse_checksums(text: str) -> dict[str, str]:
    """Parse the strict two-space SHA256SUMS format used by current."""
    checksums = {}
    for line in text.splitlines():
        if not line:
            continue
        parts = line.split("  ", 1)
        if len(parts) != 2 or not re.fullmatch(r"[0-9a-f]{64}", parts[0]):
            raise RuntimeError(f"Malformed checksum line: {line}")
        if parts[1] in checksums:
            raise RuntimeError(f"Duplicate checksum entry: {parts[1]}")
        checksums[parts[1]] = parts[0]
    return checksums


def validate_zip(archive: Path, required: tuple[str, ...]) -> None:
    """Check a ZIP's integrity and required application files."""
    with zipfile.ZipFile(archive) as source:
        names = source.namelist()
        for name in required:
            if names.count(name) != 1 or source.getinfo(name).file_size == 0:
                raise RuntimeError(f"Missing or duplicate {name} in {archive.name}")
        if source.testzip() is not None:
            raise RuntimeError(f"Corrupt ZIP: {archive.name}")


def validate_source_tarball(archive: Path) -> None:
    """Inspect the source archive without extracting or executing it."""
    try:
        with tarfile.open(archive, "r:gz") as source:
            members = source.getmembers()
    except (tarfile.TarError, OSError) as error:
        raise RuntimeError(f"Invalid source tarball: {archive.name}") from error
    if not members:
        raise RuntimeError(f"Empty source tarball: {archive.name}")
    for member in members:
        path = PurePosixPath(member.name)
        if path.is_absolute() or ".." in path.parts:
            raise RuntimeError(f"Unsafe source member: {member.name}")
    roots = {member.name.split("/", 1)[0] for member in members if member.name}
    if len(roots) != 1 or not next(iter(roots)).startswith("colorscreen-2.0"):
        raise RuntimeError(f"Unexpected final source root: {sorted(roots)}")
    root = next(iter(roots))
    by_name = {member.name: member for member in members}
    for relative in ("configure", "NEWS", "README.md", "src/qtgui/Makefile.in"):
        name = f"{root}/{relative}"
        if name not in by_name or by_name[name].size == 0:
            raise RuntimeError(f"Missing or empty {name}")


def validate_package(path: Path) -> None:
    """Validate one package according to its final/current filename suffix."""
    name = path.name
    if name.endswith("-macOS.zip"):
        validate_zip(path, (
            "Color-Screen.app/Contents/Info.plist",
            "Color-Screen.app/Contents/MacOS/Color-Screen",
            "Color-Screen.app/Contents/MacOS/colorscreen",
        ))
    elif name.endswith("-portable.zip"):
        validate_zip(path, (
            "bin/colorscreen-qt.exe", "bin/colorscreen.exe",
            "LICENSE", "README.md",
        ))
    elif name.endswith("-installer.exe"):
        with path.open("rb") as stream:
            if stream.read(2) != b"MZ":
                raise RuntimeError(f"Not a Windows executable: {name}")
    elif name.endswith("-sources.tar.gz"):
        validate_source_tarball(path)
    else:
        raise RuntimeError(f"Unexpected package asset: {name}")


def stage_current_release(github: GitHub, sha: str, directory: Path) -> tuple[list[Path], dict]:
    """Validate CURRENT and stage byte-identical packages under final names."""
    main_sha = github.api("git/ref/heads/main")["object"]["sha"]
    current_ref = github.api(f"git/ref/tags/{CURRENT_TAG}")["object"]["sha"]
    if main_sha != sha or current_ref != sha:
        raise RuntimeError("main/current do not both point at RELEASE_SHA")

    release = github.api(f"releases/tags/{CURRENT_TAG}")
    if release.get("draft") or not release.get("prerelease"):
        raise RuntimeError("current must be a published prerelease")
    assets = release.get("assets", [])

    build_info_asset = require_asset(assets, "BUILD-INFO.json")
    sums_asset = require_asset(assets, "SHA256SUMS")
    build_info_path = directory / "current-BUILD-INFO.json"
    sums_path = directory / "current-SHA256SUMS"
    github.download_release_asset(build_info_asset, build_info_path)
    github.download_release_asset(sums_asset, sums_path)

    try:
        build_info = json.loads(build_info_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise RuntimeError("Invalid current BUILD-INFO.json") from error
    if (build_info.get("release") != CURRENT_TAG
            or build_info.get("target_version") != TARGET_VERSION
            or build_info.get("repository") != github.repository
            or build_info.get("commit") != sha):
        raise RuntimeError("current BUILD-INFO provenance does not match release")
    recorded = build_info.get("sha256")
    if not isinstance(recorded, dict) or set(recorded) != set(CURRENT_PACKAGES):
        raise RuntimeError("current BUILD-INFO package set is incomplete")

    sums = parse_checksums(sums_path.read_text(encoding="utf-8"))
    if set(sums) != set(CURRENT_PACKAGES) | {"BUILD-INFO.json"}:
        raise RuntimeError("current SHA256SUMS package set is incomplete")
    if sums["BUILD-INFO.json"] != sha256(build_info_path):
        raise RuntimeError("current BUILD-INFO checksum mismatch")

    staged = []
    for current_name, final_name in zip(CURRENT_PACKAGES, FINAL_PACKAGES):
        asset = require_asset(assets, current_name)
        destination = directory / final_name
        github.download_release_asset(asset, destination)
        digest = sha256(destination)
        if digest != recorded[current_name] or digest != sums[current_name]:
            raise RuntimeError(f"current package checksum mismatch: {current_name}")
        validate_package(destination)
        staged.append(destination)

    # A rolling publication can race this manual promotion. Recheck tag, main,
    # release metadata and all asset identities before any stable-release write.
    if github.api("git/ref/heads/main")["object"]["sha"] != sha:
        raise RuntimeError("main changed while staging final release")
    if github.api(f"git/ref/tags/{CURRENT_TAG}")["object"]["sha"] != sha:
        raise RuntimeError("current tag changed while staging final release")
    latest = github.api(f"releases/tags/{CURRENT_TAG}")
    if release_asset_identity(latest) != release_asset_identity(release):
        raise RuntimeError("current release assets changed while staging final release")

    final_info = dict(build_info)
    final_info["release"] = FINAL_TAG
    final_info["promoted_from"] = CURRENT_TAG
    final_info["sha256"] = {path.name: sha256(path) for path in staged}
    final_manifest = directory / "BUILD-INFO.json"
    final_manifest.write_text(json.dumps(final_info, indent=2) + "\n", encoding="utf-8")
    staged.append(final_manifest)

    final_sums = directory / "SHA256SUMS"
    final_sums.write_text(
        "".join(f"{sha256(path)}  {path.name}\n" for path in sorted(staged)),
        encoding="utf-8",
    )
    return staged + [final_sums], build_info


def publish_final(github: GitHub, sha: str, root: Path, directory: Path) -> bool:
    """Promote CURRENT to final v2.0 after all release gates are satisfied."""
    if not release_metadata_is_final(root):
        raise RuntimeError(
            "Source metadata is not final 2.0 (configure.ac/control/NEWS)")
    assets, _ = stage_current_release(github, sha, directory)

    existing_release = github.api(f"releases/tags/{FINAL_TAG}", optional=True)
    existing_ref = github.api(f"git/ref/tags/{FINAL_TAG}", optional=True)
    if existing_ref and existing_ref["object"]["sha"] != sha:
        raise RuntimeError(f"{FINAL_TAG} already points at another commit")
    if existing_release and not existing_release.get("draft"):
        raise RuntimeError(f"{FINAL_TAG} is already published; refusing to modify it")
    if existing_release and existing_release.get("immutable"):
        raise RuntimeError(f"{FINAL_TAG} draft is immutable")

    notes = directory / "release-notes.md"
    notes.write_text(
        "# Color-Screen 2.0\n\n"
        f"Final release built and tested from commit "
        f"[\`{sha}\`](https://github.com/{github.repository}/commit/{sha}).\n\n"
        "The packages below are byte-identical payloads promoted from the tested "
        "rolling \`current\` prerelease for this commit. See "
        f"[NEWS](https://github.com/{github.repository}/blob/{FINAL_TAG}/NEWS) "
        "for the complete change list.\n\n"
        "Use the general **ucrt64** Windows package unless your CPU supports one "
        "of the specialized znver builds. The macOS bundle is ad-hoc signed and "
        "is not Developer ID notarized. \`SHA256SUMS\` and "
        "\`BUILD-INFO.json\` record package integrity and build provenance.\n",
        encoding="utf-8",
    )

    if existing_release is None:
        github.command(
            "release", "create", FINAL_TAG, "--repo", github.repository,
            "--target", sha, "--title", "Color-Screen 2.0", "--draft",
            "--latest=false", "--notes-file", str(notes),
        )
    github.command(
        "release", "upload", FINAL_TAG, "--repo", github.repository,
        "--clobber", *(str(path) for path in assets),
    )
    github.command(
        "release", "edit", FINAL_TAG, "--repo", github.repository,
        "--target", sha, "--title", "Color-Screen 2.0",
        "--draft=false", "--prerelease=false", "--latest",
        "--notes-file", str(notes),
    )
    print(f"Published https://github.com/{github.repository}/releases/tag/{FINAL_TAG}")
    return True


def main() -> None:
    """Validate trusted workflow inputs and promote the current tested payload."""
    repository = os.environ["GITHUB_REPOSITORY"]
    sha = os.environ["RELEASE_SHA"]
    if not REPOSITORY_PATTERN.fullmatch(repository):
        raise ValueError("Invalid repository")
    if not SHA_PATTERN.fullmatch(sha):
        raise ValueError("RELEASE_SHA must be a full commit SHA")
    with tempfile.TemporaryDirectory(
            prefix="colorscreen-final-release-",
            dir=os.environ.get("RUNNER_TEMP")) as directory:
        publish_final(GitHub(repository), sha, Path.cwd(), Path(directory))


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        print((error.stderr or b"").decode(errors="replace"), file=sys.stderr)
        raise SystemExit(error.returncode) from error
