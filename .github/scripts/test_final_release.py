"""Offline regression tests for the manual final 2.0 promoter."""

import copy
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
import zipfile


spec = importlib.util.spec_from_file_location(
    "promoter", Path(__file__).with_name("publish-final-release.py"))
promoter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(promoter)

REPOSITORY = "janhubicka/Color-Screen"
SHA = "a" * 40
OTHER_SHA = "b" * 40


def zip_bytes(files):
    """Return a ZIP fixture containing FILES."""
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    return stream.getvalue()


def tar_gz_bytes(root="colorscreen-2.0", extra=None):
    """Return a minimal final source archive fixture."""
    files = {
        f"{root}/configure": b"#!/bin/sh\n",
        f"{root}/NEWS": b"Changes in version 2.0\n",
        f"{root}/README.md": b"readme\n",
        f"{root}/src/qtgui/Makefile.in": b"makefile\n",
    }
    files.update(extra or {})
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode="w:gz") as archive:
        for name, data in files.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o755 if name.endswith("/configure") else 0o644
            archive.addfile(info, io.BytesIO(data))
    return stream.getvalue()


def write_release_metadata(root, final=True):
    """Write the source metadata consumed by the manual release gate."""
    version = "2.0" if final else "2.0alpha"
    (root / "os/linux").mkdir(parents=True, exist_ok=True)
    (root / "configure.ac").write_text(
        f"AC_INIT([colorscreen], [{version}], [hubicka@ucw.cz])\n",
        encoding="utf-8",
    )
    (root / "os/linux/control").write_text(
        f"Package: colorscreen\nVersion: {version}\n", encoding="utf-8")
    heading = "Changes in version 2.0\n" if final else (
        "Changes planned for version 2.0 (development version 2.0alpha)\n")
    (root / "NEWS").write_text(heading, encoding="utf-8")


class FakeGitHub:
    """Model current/final releases without network access."""

    repository = REPOSITORY

    def __init__(self):
        self.head = SHA
        self.current_ref = SHA
        self.final_ref = None
        self.final_release = None
        self.writes = []
        self.archives = {}
        self.change_on_download = None
        self.fail_upload = False

        mac = zip_bytes({
            "Color-Screen.app/Contents/Info.plist": b"plist",
            "Color-Screen.app/Contents/MacOS/Color-Screen": b"GUI",
            "Color-Screen.app/Contents/MacOS/colorscreen": b"CLI",
        })
        packages = {promoter.CURRENT_PACKAGES[0]: mac}
        index = 1
        for variant in promoter.VARIANTS:
            packages[promoter.CURRENT_PACKAGES[index]] = b"MZinstaller"
            index += 1
            packages[promoter.CURRENT_PACKAGES[index]] = zip_bytes({
                "bin/colorscreen-qt.exe": b"MZGUI",
                "bin/colorscreen.exe": b"MZCLI",
                "bin/Qt6Core.dll": b"MZruntime",
                "LICENSE": b"license",
                "README.md": b"documentation",
            })
            index += 1
        packages[promoter.CURRENT_PACKAGES[-1]] = tar_gz_bytes()

        build_info = {
            "release": promoter.CURRENT_TAG,
            "target_version": promoter.TARGET_VERSION,
            "repository": REPOSITORY,
            "commit": SHA,
            "runs": {
                "build-macos.yml": {"id": 1, "attempt": 1, "url": "mac"},
                "build-windows.yml": {"id": 2, "attempt": 1, "url": "win"},
                "build-ubuntu.yml": {"id": 3, "attempt": 1, "url": "linux"},
            },
            "sha256": {
                name: self.hash_bytes(data) for name, data in packages.items()
            },
        }
        build_info_bytes = (
            json.dumps(build_info, indent=2) + "\n").encode()
        sums = dict(build_info["sha256"])
        sums["BUILD-INFO.json"] = self.hash_bytes(build_info_bytes)
        sums_bytes = "".join(
            f"{digest}  {name}\n" for name, digest in sorted(sums.items())
        ).encode()

        self.current_release = {
            "draft": False,
            "prerelease": True,
            "immutable": False,
            "assets": [],
        }
        for name, data in packages.items():
            self.add_asset(name, data)
        self.add_asset("BUILD-INFO.json", build_info_bytes)
        self.add_asset("SHA256SUMS", sums_bytes)

    @staticmethod
    def hash_bytes(data):
        import hashlib
        return hashlib.sha256(data).hexdigest()

    def add_asset(self, name, data):
        """Register one uploaded current release asset."""
        asset_id = len(self.archives) + 1
        self.archives[asset_id] = data
        self.current_release["assets"].append({
            "id": asset_id,
            "name": name,
            "size": len(data),
            "state": "uploaded",
            "digest": "sha256:" + self.hash_bytes(data),
            "updated_at": f"2026-10-05T00:00:{asset_id:02d}Z",
        })

    def asset(self, name):
        """Return the live current asset metadata named NAME."""
        return next(item for item in self.current_release["assets"]
                    if item["name"] == name)

    def api(self, path, *, data=None, method="GET", optional=False):
        """Implement the API subset used by the promoter."""
        if method != "GET":
            self.writes.append((method, path, data))
            return {}
        if path == "git/ref/heads/main":
            return {"object": {"sha": self.head}}
        if path == "git/ref/tags/current":
            return {"object": {"sha": self.current_ref}}
        if path == "git/ref/tags/v2.0":
            if self.final_ref is None and optional:
                return None
            return {"object": {"sha": self.final_ref}}
        if path == "releases/tags/current":
            return copy.deepcopy(self.current_release)
        if path == "releases/tags/v2.0":
            if self.final_release is None and optional:
                return None
            return copy.deepcopy(self.final_release)
        raise AssertionError(path)

    def download_release_asset(self, asset, destination):
        """Write one current asset and optionally race the rolling release."""
        destination.write_bytes(self.archives[asset["id"]])
        if self.change_on_download:
            self.change_on_download()
            self.change_on_download = None

    def command(self, *args, output=None):
        """Record final release mutations and optionally fail upload."""
        self.writes.append(args)
        if args[:2] == ("release", "upload") and self.fail_upload:
            raise subprocess.CalledProcessError(1, "gh", stderr=b"upload failed")
        return ""


def workflow_text():
    """Return the release workflow text tracked beside this test suite."""
    return (Path(__file__).parents[1] / "workflows" / "release-2.0.yml").read_text(
        encoding="utf-8")


class FinalReleaseWorkflowTests(unittest.TestCase):
    """Keep the YAML wrapper as restrictive as the promoter itself."""

    def test_publication_is_manual_only_and_main_only(self):
        workflow = workflow_text()
        self.assertIn("workflow_dispatch:", workflow)
        self.assertNotIn("\n  push:", workflow)
        self.assertIn("confirm_release:", workflow)
        self.assertIn("Type v2.0 to publish the final stable release", workflow)
        self.assertIn("github.event_name == 'workflow_dispatch'", workflow)
        self.assertIn("github.ref == 'refs/heads/main'", workflow)
        self.assertIn("inputs.confirm_release == 'v2.0'", workflow)

    def test_final_and_current_publishers_share_one_lock(self):
        workflow = workflow_text()
        self.assertIn("group: colorscreen-current-release", workflow)
        self.assertIn("cancel-in-progress: false", workflow)


class FinalReleaseTests(unittest.TestCase):
    """Exercise release gating, exact payload promotion and failure recovery."""

    def setUp(self):
        self.root_tmp = tempfile.TemporaryDirectory()
        self.stage_tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.root_tmp.cleanup)
        self.addCleanup(self.stage_tmp.cleanup)
        self.root = Path(self.root_tmp.name)
        self.stage = Path(self.stage_tmp.name)
        write_release_metadata(self.root)
        self.github = FakeGitHub()

    def publish(self):
        return promoter.publish_final(
            self.github, SHA, self.root, self.stage)

    def test_success_promotes_exact_packages_and_latest_release(self):
        self.assertTrue(self.publish())
        self.assertEqual(self.github.writes[0][:3],
                         ("release", "create", promoter.FINAL_TAG))
        self.assertIn("--draft", self.github.writes[0])
        self.assertEqual(self.github.writes[1][:3],
                         ("release", "upload", promoter.FINAL_TAG))
        self.assertEqual(self.github.writes[2][:3],
                         ("release", "edit", promoter.FINAL_TAG))
        self.assertIn("--latest", self.github.writes[2])
        self.assertIn("--draft=false", self.github.writes[2])
        self.assertIn("--prerelease=false", self.github.writes[2])

        for current_name, final_name in zip(
                promoter.CURRENT_PACKAGES, promoter.FINAL_PACKAGES):
            current = self.github.asset(current_name)
            self.assertEqual(
                (self.stage / final_name).read_bytes(),
                self.github.archives[current["id"]],
            )

        manifest = json.loads(
            (self.stage / "BUILD-INFO.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["release"], promoter.FINAL_TAG)
        self.assertEqual(manifest["promoted_from"], promoter.CURRENT_TAG)
        self.assertEqual(manifest["commit"], SHA)
        self.assertEqual(set(manifest["sha256"]), set(promoter.FINAL_PACKAGES))
        sums = promoter.parse_checksums(
            (self.stage / "SHA256SUMS").read_text(encoding="utf-8"))
        self.assertEqual(set(sums),
                         set(promoter.FINAL_PACKAGES) | {"BUILD-INFO.json"})
        for name, digest in sums.items():
            self.assertEqual(digest, promoter.sha256(self.stage / name))

    def test_development_metadata_cannot_publish(self):
        write_release_metadata(self.root, final=False)
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_main_or_current_must_match_release_sha(self):
        for field in ("head", "current_ref"):
            with self.subTest(field=field):
                github = FakeGitHub()
                setattr(github, field, OTHER_SHA)
                with self.assertRaises(RuntimeError):
                    promoter.publish_final(github, SHA, self.root, self.stage)
                self.assertEqual(github.writes, [])

    def test_current_build_info_provenance_is_required(self):
        asset = self.github.asset("BUILD-INFO.json")
        info = json.loads(self.github.archives[asset["id"]])
        info["commit"] = OTHER_SHA
        data = (json.dumps(info, indent=2) + "\n").encode()
        self.github.archives[asset["id"]] = data
        asset["size"] = len(data)
        asset["digest"] = "sha256:" + self.github.hash_bytes(data)
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_current_build_workflow_provenance_is_complete(self):
        asset = self.github.asset("BUILD-INFO.json")
        info = json.loads(self.github.archives[asset["id"]])
        del info["runs"]["build-ubuntu.yml"]
        data = (json.dumps(info, indent=2) + "\n").encode()
        self.github.archives[asset["id"]] = data
        asset["size"] = len(data)
        asset["digest"] = "sha256:" + self.github.hash_bytes(data)
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_package_checksum_mismatch_is_rejected(self):
        asset = self.github.asset(promoter.CURRENT_PACKAGES[0])
        self.github.archives[asset["id"]] += b"changed"
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_alpha_source_archive_cannot_be_promoted(self):
        asset = self.github.asset(promoter.CURRENT_PACKAGES[-1])
        data = tar_gz_bytes(root="colorscreen-2.0alpha")
        self.github.archives[asset["id"]] = data
        asset["size"] = len(data)
        asset["digest"] = "sha256:" + self.github.hash_bytes(data)
        # Keep BUILD-INFO/checksums internally consistent so root validation is
        # the gate being tested.
        build_asset = self.github.asset("BUILD-INFO.json")
        info = json.loads(self.github.archives[build_asset["id"]])
        info["sha256"][promoter.CURRENT_PACKAGES[-1]] = self.github.hash_bytes(data)
        build_data = (json.dumps(info, indent=2) + "\n").encode()
        self.github.archives[build_asset["id"]] = build_data
        build_asset["size"] = len(build_data)
        build_asset["digest"] = "sha256:" + self.github.hash_bytes(build_data)
        sums_asset = self.github.asset("SHA256SUMS")
        sums = promoter.parse_checksums(
            self.github.archives[sums_asset["id"]].decode())
        sums[promoter.CURRENT_PACKAGES[-1]] = self.github.hash_bytes(data)
        sums["BUILD-INFO.json"] = self.github.hash_bytes(build_data)
        sums_data = "".join(
            f"{digest}  {name}\n" for name, digest in sorted(sums.items())
        ).encode()
        self.github.archives[sums_asset["id"]] = sums_data
        sums_asset["size"] = len(sums_data)
        sums_asset["digest"] = "sha256:" + self.github.hash_bytes(sums_data)
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_current_change_during_download_is_rejected(self):
        def change_current():
            self.github.current_release["assets"][0]["updated_at"] = (
                "2026-10-05T01:00:00Z")
        self.github.change_on_download = change_current
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_published_final_release_is_never_modified(self):
        self.github.final_ref = SHA
        self.github.final_release = {
            "draft": False, "prerelease": False, "immutable": False,
            "target_commitish": SHA,
        }
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_wrong_existing_final_tag_is_rejected(self):
        self.github.final_ref = OTHER_SHA
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_draft_for_another_commit_is_rejected(self):
        self.github.final_release = {
            "draft": True, "prerelease": False, "immutable": False,
            "target_commitish": OTHER_SHA,
        }
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_current_must_remain_prerelease_during_staging(self):
        def change_release_state():
            self.github.current_release["prerelease"] = False
        self.github.change_on_download = change_release_state
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_current_change_after_staging_is_rejected_before_writes(self):
        original_api = self.github.api
        current_reads = 0

        def racing_api(path, **kwargs):
            nonlocal current_reads
            if path == "releases/tags/current" and kwargs.get("method", "GET") == "GET":
                current_reads += 1
                if current_reads == 3:
                    self.github.current_release["assets"][0]["updated_at"] = (
                        "2026-10-05T02:00:00Z")
            return original_api(path, **kwargs)

        self.github.api = racing_api
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_matching_draft_can_resume(self):
        self.github.final_ref = SHA
        self.github.final_release = {
            "draft": True, "prerelease": False, "immutable": False,
            "target_commitish": SHA,
            "assets": [{"name": promoter.FINAL_PACKAGES[0]}],
        }
        self.assertTrue(self.publish())
        self.assertEqual(len(self.github.writes), 2)
        self.assertEqual(self.github.writes[0][:2], ("release", "upload"))
        self.assertEqual(self.github.writes[1][:2], ("release", "edit"))

    def test_matching_draft_with_unexpected_asset_is_rejected(self):
        self.github.final_ref = SHA
        self.github.final_release = {
            "draft": True, "prerelease": False, "immutable": False,
            "target_commitish": SHA,
            "assets": [{"name": "unrelated-debug-build.zip"}],
        }
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_upload_failure_never_publishes_draft(self):
        self.github.fail_upload = True
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish()
        self.assertEqual(len(self.github.writes), 2)
        self.assertEqual(self.github.writes[0][:2], ("release", "create"))
        self.assertEqual(self.github.writes[1][:2], ("release", "upload"))
        self.assertFalse(any(
            write[:2] == ("release", "edit") for write in self.github.writes))


if __name__ == "__main__":
    unittest.main()
