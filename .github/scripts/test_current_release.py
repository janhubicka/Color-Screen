"""Offline regression tests for the current release publisher (no credentials)."""

import copy
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile


spec = importlib.util.spec_from_file_location(
    "publisher", Path(__file__).with_name("publish-current-release.py"))
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)
REPOSITORY = "janhubicka/Color-Screen"
SHA = "a" * 40
OTHER_SHA = "b" * 40


def run_record(run_id=1, **changes):
    """Return a successful trusted push run, with optional field overrides."""
    run = dict(id=run_id, run_attempt=1, head_sha=SHA, head_branch="main",
               event="push", head_repository={"full_name": REPOSITORY},
               status="completed", conclusion="success",
               html_url=f"https://github.com/{REPOSITORY}/actions/runs/{run_id}")
    return dict(run, **changes)


def zip_bytes(files):
    """Create an in-memory archive without running any application payload."""
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    return stream.getvalue()


class FakeGitHub:
    """Model read APIs, downloads and release writes independently of gh."""

    repository = REPOSITORY

    def __init__(self):
        self.head = SHA
        self.runs = {workflow: [run_record(index + 1)]
                     for index, workflow in enumerate(publisher.WORKFLOWS)}
        self.artifacts = {1: [], 2: []}
        self.archives = {}
        self.writes = []
        self.release = None
        self.tag = None
        self.fail_upload = False
        self.change_on_download = None
        app = zip_bytes({
            "Color-Screen.app/Contents/Info.plist": b"plist",
            "Color-Screen.app/Contents/MacOS/Color-Screen": b"GUI",
            "Color-Screen.app/Contents/MacOS/colorscreen": b"CLI",
        })
        self.add_artifact(1, "macOS-Color-Screen-App",
                          zip_bytes({"Color-Screen-macOS.zip": app}))
        for variant in publisher.VARIANTS:
            self.add_artifact(2, f"windows-installer-{variant}", zip_bytes({
                f"Color-Screen-Installer-{variant}.exe": b"MZinstaller"}))
            self.add_artifact(2, f"windows-binary-{variant}", zip_bytes({
                "bin/colorscreen-qt.exe": b"MZGUI", "bin/colorscreen.exe": b"MZCLI",
                "bin/Qt6Core.dll": b"MZruntime", "share/resources/example": b"data",
                "LICENSE": b"license", "README.md": b"documentation"}))

    def add_artifact(self, run_id, name, data):
        """Register an archive and the associated Actions metadata."""
        artifact_id = len(self.archives) + 1
        self.archives[artifact_id] = data
        self.artifacts[run_id].append(dict(
            id=artifact_id, name=name, size_in_bytes=len(data), expired=False,
            workflow_run={"id": run_id, "head_sha": SHA}))

    def api(self, path, *, data=None, method="GET", optional=False):
        """Handle the small set of ref/release API calls used by the publisher."""
        if method != "GET":
            self.writes.append((method, path, data))
            return {}
        if path == "git/ref/heads/main":
            return {"object": {"sha": self.head}}
        if path == "git/ref/tags/current":
            return self.tag
        if path == "releases/tags/current":
            return self.release
        raise AssertionError(path)

    def items(self, path, key):
        """Return independent copies so mutating a rerun cannot alter snapshots."""
        if key == "workflow_runs":
            workflow = path.split("/")[2]
            return copy.deepcopy(self.runs[workflow])
        if key == "artifacts":
            return copy.deepcopy(self.artifacts[int(path.split("/")[2])])
        raise AssertionError(path)

    def download(self, artifact, destination):
        """Write a fixture ZIP and optionally change state during staging."""
        destination.write_bytes(self.archives[artifact["id"]])
        if self.change_on_download:
            self.change_on_download()
            self.change_on_download = None

    def command(self, *args):
        """Record publication commands and optionally simulate an upload outage."""
        self.writes.append(args)
        if args[:2] == ("release", "upload") and self.fail_upload:
            raise subprocess.CalledProcessError(1, "gh", stderr=b"Upload failed")
        return ""


class SelectionTests(unittest.TestCase):
    """Prove that build success cannot be borrowed from the wrong provenance."""

    def test_success(self):
        self.assertEqual(publisher.select_run([run_record()], REPOSITORY, SHA)["id"], 1)

    def test_all_failure_states(self):
        for status, conclusion in (("in_progress", None), ("queued", None),
                                   ("completed", "failure"), ("completed", "cancelled"),
                                   ("completed", "skipped"), ("completed", "timed_out")):
            with self.subTest(status=status, conclusion=conclusion):
                self.assertIsNone(publisher.select_run(
                    [run_record(status=status, conclusion=conclusion)], REPOSITORY, SHA))

    def test_wrong_provenance(self):
        for changes in ({"head_sha": OTHER_SHA}, {"head_branch": "feature"},
                        {"event": "pull_request"}, {"event": "workflow_dispatch"},
                        {"head_repository": {"full_name": "someone/fork"}},
                        {"head_repository": None}):
            with self.subTest(changes=changes):
                self.assertIsNone(publisher.select_run([run_record(**changes)], REPOSITORY, SHA))

    def test_no_fallback_to_older_success(self):
        self.assertIsNone(publisher.select_run(
            [run_record(), run_record(2, conclusion="failure")], REPOSITORY, SHA))

    def test_missing_runs(self):
        self.assertIsNone(publisher.select_run([], REPOSITORY, SHA))

    def test_missing_or_duplicate_artifacts(self):
        github = FakeGitHub()
        artifact = github.artifacts[1][0]
        for items in ([], [artifact, artifact]):
            with self.subTest(items=items), self.assertRaises(RuntimeError):
                publisher.require_artifact(items, artifact["name"], run_record())

    def test_invalid_artifacts(self):
        artifact = FakeGitHub().artifacts[1][0]
        for changes in ({"expired": True}, {"size_in_bytes": 0},
                        {"workflow_run": {"id": 2, "head_sha": SHA}},
                        {"workflow_run": {"id": 1, "head_sha": OTHER_SHA}}):
            with self.subTest(changes=changes), self.assertRaises(RuntimeError):
                publisher.require_artifact([dict(artifact, **changes)], artifact["name"], run_record())


class PublicationTests(unittest.TestCase):
    """Exercise complete publication, staging failures and concurrency guards."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.github = FakeGitHub()

    def publish(self):
        """Run the real publisher using fixture GitHub data."""
        return publisher.publish(self.github, SHA, self.directory)

    def test_create_release_and_checksums(self):
        self.assertTrue(self.publish())
        commands = self.github.writes
        self.assertEqual(commands[0][:3], ("release", "create", "current"))
        self.assertIn("--draft", commands[0])
        self.assertEqual(commands[1][:3], ("release", "upload", "current"))
        self.assertEqual(commands[2], ("POST", "git/refs", {"ref": "refs/tags/current", "sha": SHA}))
        self.assertEqual(commands[3][:3], ("release", "edit", "current"))
        self.assertIn("--prerelease", commands[3])
        self.assertIn("--latest=false", commands[3])
        self.assertIn("--draft=false", commands[3])
        manifest = json.loads((self.directory / "BUILD-INFO.json").read_text())
        self.assertEqual(manifest["commit"], SHA)
        self.assertEqual(manifest["target_version"], "2.0")
        self.assertEqual(len(manifest["sha256"]), 7)
        for line in (self.directory / "SHA256SUMS").read_text().splitlines():
            digest, name = line.split("  ")
            self.assertEqual(digest, publisher.sha256(self.directory / name))
        self.assertIn("not the final 2.0 release", (self.directory / "release-notes.md").read_text())

    def test_update_only_current(self):
        self.github.release = {"immutable": False}
        self.github.tag = {"object": {"sha": OTHER_SHA}}
        self.assertTrue(self.publish())
        self.assertEqual(len(self.github.writes), 3)
        self.assertEqual(self.github.writes[1],
                         ("PATCH", "git/refs/tags/current", {"sha": SHA, "force": True}))

    def test_portable_preserves_complete_tree(self):
        self.publish()
        path = self.directory / f"{publisher.PREFIX}-windows-ucrt64-portable.zip"
        self.assertEqual(path.read_bytes(), self.github.archives[3])
        with zipfile.ZipFile(path) as archive:
            self.assertIn("bin/Qt6Core.dll", archive.namelist())
            self.assertIn("share/resources/example", archive.namelist())

    def test_mac_preserves_inner_archive_byte_for_byte(self):
        self.publish()
        with zipfile.ZipFile(io.BytesIO(self.github.archives[1])) as archive:
            self.assertEqual((self.directory / f"{publisher.PREFIX}-macOS.zip").read_bytes(),
                             archive.read("Color-Screen-macOS.zip"))

    def test_stale_main_cannot_publish(self):
        self.github.head = OTHER_SHA
        self.assertFalse(self.publish())
        self.assertEqual(self.github.writes, [])

    def test_one_workflow_not_ready(self):
        self.github.runs[publisher.WORKFLOWS[1]][0]["conclusion"] = "failure"
        self.assertFalse(self.publish())
        self.assertEqual(self.github.writes, [])

    def test_main_changes_during_download(self):
        self.github.change_on_download = lambda: setattr(self.github, "head", OTHER_SHA)
        self.assertFalse(self.publish())
        self.assertEqual(self.github.writes, [])

    def test_rerun_changes_during_download(self):
        def rerun():
            self.github.runs[publisher.WORKFLOWS[0]][0]["run_attempt"] += 1
        self.github.change_on_download = rerun
        self.assertFalse(self.publish())
        self.assertEqual(self.github.writes, [])

    def test_missing_last_asset_leaves_release_untouched(self):
        self.github.artifacts[2].pop()
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_corrupt_archive_leaves_release_untouched(self):
        self.github.archives[1] = b"not a ZIP"
        with self.assertRaises(zipfile.BadZipFile):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_path_traversal_cannot_be_extracted(self):
        self.github.archives[1] = zip_bytes({"../../evil": b"x"})
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_missing_application_in_portable_archive(self):
        self.github.archives[3] = zip_bytes({"README.md": b"not an app"})
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_invalid_installer(self):
        self.github.archives[2] = zip_bytes({"Color-Screen-Installer-ucrt64.exe": b"not PE"})
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])

    def test_upload_failure_does_not_advance_tag(self):
        self.github.release = {"immutable": False}
        self.github.fail_upload = True
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish()
        self.assertEqual(len(self.github.writes), 1)
        self.assertEqual(self.github.writes[0][:2], ("release", "upload"))

    def test_immutable_release_is_not_modified(self):
        self.github.release = {"immutable": True}
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.github.writes, [])


class ClientTests(unittest.TestCase):
    """Test CLI response handling, independent of publication fixtures."""

    def test_only_404_is_optional(self):
        github = publisher.GitHub(REPOSITORY)
        for status in (403, 404, 500):
            with self.subTest(status=status), patch.object(github, "command", side_effect=
                    subprocess.CalledProcessError(1, "gh", stderr=f"gh: error (HTTP {status})".encode())):
                if status == 404:
                    self.assertIsNone(github.api("releases/tags/current", optional=True))
                else:
                    with self.assertRaises(subprocess.CalledProcessError):
                        github.api("releases/tags/current", optional=True)

    def test_pagination(self):
        github = publisher.GitHub(REPOSITORY)
        with patch.object(github, "command", return_value='[{"artifacts": [1]}, {"artifacts": [2]}]') as cmd:
            self.assertEqual(github.items("path", "artifacts"), [1, 2])
            self.assertIn("--paginate", cmd.call_args.args)
            self.assertIn("--slurp", cmd.call_args.args)

    def test_digest_validation(self):
        github = publisher.GitHub(REPOSITORY)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "archive.zip"
            with patch.object(github, "command", side_effect=lambda *args, output: output.write(b"ZIP")):
                github.download({"id": 1, "name": "a"}, path)
                github.download({"id": 1, "name": "a", "digest": "sha256:" + publisher.sha256(path)}, path)
                with self.assertRaises(RuntimeError):
                    github.download({"id": 1, "name": "a", "digest": "sha256:wrong"}, path)


if __name__ == "__main__":
    unittest.main()
