#!/usr/bin/env python3
"""Publish the existing, tested main artifacts as the rolling 2.0 prerelease.

Requires Python 3.10+ and authenticated GitHub CLI. Run only in the serialized
publish job; downloaded artifacts are data and are never executed or unpacked
into the checkout. No application rebuild is performed here.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import urlencode
import zipfile


WORKFLOWS = ("build-macos.yml", "build-windows.yml")
VARIANTS = ("ucrt64", "ucrt64-znver2", "ucrt64-znver4")
PREFIX = "Color-Screen-2.0-current"
TAG = "current"


class GitHub:
    """Use gh for authentication, API pagination and safe download redirects."""

    def __init__(self, repository: str):
        self.repository = repository
        self.base = f"repos/{repository}/"

    def command(self, *args: str, output=None) -> str:
        """Run gh without a shell, optionally streaming binary stdout to OUTPUT."""
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

    def items(self, path: str, key: str) -> list[dict]:
        """Collect KEY from every API page, including large artifact sets."""
        pages = json.loads(self.command("api", self.base + path,
                                       "--paginate", "--slurp"))
        return [item for page in pages for item in page[key]]

    def download(self, artifact: dict, destination: Path) -> None:
        """Download one exact artifact ZIP, checking its digest when provided."""
        with destination.open("wb") as output:
            self.command("api", self.base +
                         f"actions/artifacts/{artifact['id']}/zip", output=output)
        digest = artifact.get("digest")
        if digest and digest != "sha256:" + sha256(destination):
            raise RuntimeError(f"Artifact digest mismatch: {artifact['name']}")


def sha256(path: Path) -> str:
    """Hash PATH without loading a large application archive into memory."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def select_run(runs: list[dict], repository: str, sha: str) -> dict | None:
    """Require the latest matching push run, never an older successful fallback."""
    matches = [run for run in runs
               if run.get("head_sha") == sha
               and run.get("head_branch") == "main"
               and run.get("event") == "push"
               and (run.get("head_repository") or {}).get("full_name") == repository]
    if not matches:
        return None
    run = max(matches, key=lambda item: item["id"])
    if run.get("status") != "completed" or run.get("conclusion") != "success":
        return None
    return run


def ready_runs(github: GitHub, sha: str) -> dict[str, dict] | None:
    """Require both complete platform workflows for the current main commit."""
    if github.api("git/ref/heads/main")["object"]["sha"] != sha:
        print("Skipping superseded main commit.")
        return None
    selected = {}
    query = urlencode({"branch": "main", "event": "push", "head_sha": sha,
                       "per_page": 100})
    for workflow in WORKFLOWS:
        run = select_run(github.items(f"actions/workflows/{workflow}/runs?{query}",
                                     "workflow_runs"), github.repository, sha)
        if run is None:
            print(f"Not publishing: {workflow} has not succeeded for {sha}.")
            return None
        selected[workflow] = run
    return selected


def run_identity(runs: dict[str, dict]) -> dict[str, tuple[int, int]]:
    """Record run IDs and attempts to detect reruns while downloading."""
    return {name: (run["id"], run["run_attempt"]) for name, run in runs.items()}


def require_artifact(artifacts: list[dict], name: str, run: dict) -> dict:
    """Require exactly one unexpired artifact produced by the selected run."""
    matches = [artifact for artifact in artifacts if artifact["name"] == name]
    if len(matches) != 1:
        raise RuntimeError(f"Expected exactly one artifact named {name}")
    artifact = matches[0]
    source = artifact.get("workflow_run") or {}
    if (artifact.get("expired") or artifact["size_in_bytes"] <= 0
            or source.get("id") != run["id"]
            or source.get("head_sha") != run["head_sha"]):
        raise RuntimeError(f"Expired, empty or mismatched artifact: {name}")
    return artifact


def unpack_single(archive: Path, member: str, destination: Path) -> None:
    """Copy a known payload out of an Actions ZIP, without extracting paths."""
    with zipfile.ZipFile(archive) as source:
        if source.namelist() != [member] or source.getinfo(member).file_size == 0:
            raise RuntimeError(f"Unexpected contents in {archive.name}: {source.namelist()}")
        with source.open(member) as stream, destination.open("wb") as output:
            shutil.copyfileobj(stream, output)


def validate_zip(archive: Path, required: tuple[str, ...]) -> None:
    """Check ZIP integrity and expected application files without executing them."""
    with zipfile.ZipFile(archive) as source:
        names = source.namelist()
        for name in required:
            if names.count(name) != 1 or source.getinfo(name).file_size == 0:
                raise RuntimeError(f"Missing or duplicate {name} in {archive.name}")
        if source.testzip() is not None:
            raise RuntimeError(f"Corrupt ZIP: {archive.name}")


def prepare_assets(github: GitHub, runs: dict[str, dict], directory: Path) -> list[Path]:
    """Stage the tested app bundle and every Windows installer/portable variant."""
    assets = []
    for workflow, run in runs.items():
        artifacts = github.items(f"actions/runs/{run['id']}/artifacts?per_page=100",
                                 "artifacts")
        specs = [("macOS-Color-Screen-App", "Color-Screen-macOS.zip",
                  f"{PREFIX}-macOS.zip")] if workflow == WORKFLOWS[0] else [
            spec for variant in VARIANTS for spec in (
                (f"windows-installer-{variant}",
                 f"Color-Screen-Installer-{variant}.exe",
                 f"{PREFIX}-windows-{variant}-installer.exe"),
                # Use the COMPLETE installed tree: bin, shared resources, LICENSE
                # and README. The older bin-only artifact omits installed data.
                (f"windows-binary-{variant}", None,
                 f"{PREFIX}-windows-{variant}-portable.zip"),
            )
        ]
        for artifact_name, member, filename in specs:
            artifact = require_artifact(artifacts, artifact_name, run)
            destination = directory / filename
            archive = directory / "artifact.zip" if member else destination
            github.download(artifact, archive)
            if member:
                unpack_single(archive, member, destination)
                archive.unlink()
            if filename.endswith("-macOS.zip"):
                validate_zip(destination, ("Color-Screen.app/Contents/Info.plist",
                                          "Color-Screen.app/Contents/MacOS/Color-Screen",
                                          "Color-Screen.app/Contents/MacOS/colorscreen"))
            elif filename.endswith("-portable.zip"):
                validate_zip(destination, ("bin/colorscreen-qt.exe", "bin/colorscreen.exe",
                                          "LICENSE", "README.md"))
            else:
                with destination.open("rb") as stream:
                    if stream.read(2) != b"MZ":
                        raise RuntimeError(f"Not a Windows executable: {filename}")
            assets.append(destination)
    manifest = directory / "BUILD-INFO.json"
    manifest.write_text(json.dumps({
        "release": TAG, "target_version": "2.0", "repository": github.repository,
        "commit": runs[WORKFLOWS[0]]["head_sha"],
        "runs": {name: {"id": run["id"], "attempt": run["run_attempt"],
                        "url": run["html_url"]} for name, run in runs.items()},
        "sha256": {path.name: sha256(path) for path in assets},
    }, indent=2) + "\n", encoding="utf-8")
    assets.append(manifest)
    checksums = directory / "SHA256SUMS"
    checksums.write_text("".join(f"{sha256(path)}  {path.name}\n"
                                 for path in sorted(assets)), encoding="utf-8")
    return assets + [checksums]


def publish(github: GitHub, sha: str, directory: Path) -> bool:
    """Publish only after staging all packages and rechecking build eligibility."""
    runs = ready_runs(github, sha)
    if runs is None:
        return False
    assets = prepare_assets(github, runs, directory)
    final_runs = ready_runs(github, sha)
    if final_runs is None or run_identity(final_runs) != run_identity(runs):
        print("Build state changed during download; leaving the release untouched.")
        return False
    release = github.api(f"releases/tags/{TAG}", optional=True)
    if release and release.get("immutable"):
        raise RuntimeError("The rolling current release must be mutable.")
    notes = directory / "release-notes.md"
    notes.write_text(
        f"Development builds for the upcoming **Color-Screen 2.0** release. "
        "This is a rolling prerelease, **not the final 2.0 release**.\n\n"
        f"All packages were built and tested from main commit "
        f"[`{sha}`](https://github.com/{github.repository}/commit/{sha}).\n\n"
        + "\n".join(f"- [{name}]({run['html_url']}) (attempt {run['run_attempt']})"
                    for name, run in runs.items())
        + "\n\nDownload the macOS ZIP for the self-contained `Color-Screen.app` "
        "bundle. It retains the build workflow's ad-hoc signature; it is not "
        "Developer ID signed or notarized.\n\n"
        "On Windows, use the **ucrt64** installer or portable ZIP for the general "
        "x86-64 build. The **znver2** and **znver4** alternatives require CPUs "
        "supporting those instruction sets. Extract the entire portable ZIP "
        "and launch `bin/colorscreen-qt.exe`; keep the bundled DLLs and resources.\n\n"
        "`SHA256SUMS` contains checksums; `BUILD-INFO.json` records the source "
        "commit and build runs. These assets are replaced after both platform "
        "workflows pass for a new main commit.\n", encoding="utf-8")
    # Stage everything before touching the release. Only current is modified;
    # stable release tags and unrelated assets are deliberately left alone.
    if release is None:
        github.command("release", "create", TAG, "--repo", github.repository,
                       "--target", sha, "--title", TAG, "--prerelease", "--draft",
                       "--latest=false", "--notes-file", str(notes))
    github.command("release", "upload", TAG, "--repo", github.repository,
                   "--clobber", *(str(path) for path in assets))
    # --target does not move an existing tag: update the ref explicitly only
    # after successful uploads. A first draft may not have created its tag yet.
    tag = github.api(f"git/ref/tags/{TAG}", optional=True)
    if tag is None:
        github.api("git/refs", method="POST", data={"ref": f"refs/tags/{TAG}", "sha": sha})
    else:
        github.api(f"git/refs/tags/{TAG}", method="PATCH", data={"sha": sha, "force": True})
    github.command("release", "edit", TAG, "--repo", github.repository,
                   "--target", sha, "--title", TAG, "--prerelease", "--draft=false",
                   "--latest=false", "--notes-file", str(notes))
    print(f"Published https://github.com/{github.repository}/releases/tag/{TAG}")
    return True


def main() -> None:
    """Read the trusted workflow environment and stage outside the checkout."""
    repository = os.environ["GITHUB_REPOSITORY"]
    sha = os.environ["BUILD_SHA"]
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Invalid repository")
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise ValueError("BUILD_SHA must be a full commit SHA")
    with tempfile.TemporaryDirectory(prefix="colorscreen-release-",
                                     dir=os.environ.get("RUNNER_TEMP")) as directory:
        publish(GitHub(repository), sha, Path(directory))


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        print((error.stderr or b"").decode(errors="replace"), file=sys.stderr)
        raise SystemExit(error.returncode) from error
