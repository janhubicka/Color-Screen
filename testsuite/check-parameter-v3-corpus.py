#!/usr/bin/env python3
"""Round-trip every checked-in .par fixture through the native JSON v3 format.

Only temporary files are written. Each fixture follows the exact path:

    existing .par -> current canonical .par -> JSON v3 -> reloaded .par

The two canonical .par results must agree byte for byte; this detects
serialization omissions even when the JSON encoder and decoder agree with
each other. A second JSON rewrite must also preserve canonical JSON bytes.
"""

from __future__ import annotations

import copy
import difflib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from parameter_schema_validate import (
    SchemaValidationError, check_schema, validate_document,
)


def say(message: str) -> None:
    """Emit diagnostics without introducing another Automake TAP test."""
    for line in message.splitlines():
        print(f"# {line}", flush=True)


def corpus_files(root: Path) -> list[Path]:
    """Find source .par fixtures, excluding generated testsuite outputs.

    The testsuite writes out-* and out2-* files in its build directory.
    In an in-tree build this is also the source fixture directory, so a prior
    or concurrent test must not change the set of inputs being validated.
    Examples do not use the testsuite output naming convention.
    """
    return sorted(
        (
            path
            for directory in (root / "testsuite", root / "examples")
            for path in directory.rglob("*")
            if path.is_file()
            and path.suffix.lower() == ".par"
            and not (
                directory.name == "testsuite"
                and path.name.startswith(("out-", "out2-"))
            )
        ),
        key=lambda path: path.relative_to(root).as_posix(),
    )


def adjust(program: Path, source: Path, target: Path, *, json_v3=False) -> str | None:
    """Run the real CLI and return an actionable error, if any."""
    argv = [str(program), "adjust-par", str(source), "--out", str(target)]
    if json_v3:
        argv.insert(2, "--json-v3")
    try:
        process = subprocess.run(
            argv,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding="utf-8",
            errors="replace",
            check=False,
            timeout=90,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"{argv!r}: {exc}"
    if process.returncode:
        output = (process.stdout + process.stderr).strip()
        return (
            f"{' '.join(argv)} returned {process.returncode}\n"
            f"{output[-3000:]}"
        )
    if not target.is_file():
        return f"{' '.join(argv)} succeeded but did not create {target}"
    return None


def compare_files(reference: Path, replay: Path) -> str | None:
    """Compare normalized .par files exactly; include a short unified diff."""
    before, after = reference.read_bytes(), replay.read_bytes()
    if before == after:
        return None
    before_lines = before.decode("utf-8", "replace").splitlines(keepends=True)
    after_lines = after.decode("utf-8", "replace").splitlines(keepends=True)
    diff = list(
        difflib.unified_diff(
            before_lines, after_lines,
            fromfile="canonical-legacy.par",
            tofile="json-reloaded-legacy.par",
        )
    )
    explanation = "".join(diff[:65]).rstrip()
    if len(diff) > 65:
        explanation += f"\n... ({len(diff) - 65} more diff lines)"
    return (
        f"canonical legacy mismatch ({len(before)} vs {len(after)} bytes)\n"
        f"{explanation or 'nontext binary difference'}"
    )


def main() -> int:
    if len(sys.argv) != 3:
        say("usage: check-parameter-v3-corpus.py SOURCE-ROOT COLORSCREEN-EXECUTABLE")
        return 2

    root = Path(sys.argv[1]).resolve()
    program = Path(sys.argv[2]).resolve()
    try:
        with (root / "doc" / "parameters-schema-v3.json").open(
                encoding="utf-8") as stream:
            schema = json.load(stream)
        check_schema(schema)
    except (OSError, ValueError, SchemaValidationError) as exc:
        say(f"Invalid or missing checked-in schema-3 contract: {exc}")
        return 1
    sources = corpus_files(root)
    found_test = any((root / "testsuite") in p.parents for p in sources)
    found_example = any((root / "examples") in p.parents for p in sources)
    if not sources or not found_test or not found_example:
        say(
            "Missing .par fixtures: expected at least one file in both "
            "testsuite/ and examples/; check source/distribution paths"
        )
        return 1

    say(
        f"Native JSON v3 corpus: {len(sources)} checked-in .par inputs from "
        "testsuite/ and examples/"
    )
    failures: list[tuple[str, str]] = []
    checked = 0

    # Put all outputs outside the checked-in source tree, so a failing test
    # can never rewrite fixture data (including when run by distcheck).
    with tempfile.TemporaryDirectory(prefix="colorscreen-json-v3-corpus-") as temporary:
        folder = Path(temporary)
        for i, source in enumerate(sources):
            title = source.relative_to(root).as_posix()
            # Reuse four filenames within each isolated iteration; this
            # prevents large meshes from filling temp storage unnecessarily.
            canonical = folder / "canonical.par"
            json_path = folder / "state.cspar"
            restored = folder / "restored.par"
            json_rewritten = folder / "rewritten.cspar"
            for output in (canonical, json_path, restored, json_rewritten):
                output.unlink(missing_ok=True)

            error = adjust(program, source, canonical)
            if not error:
                error = adjust(program, canonical, json_path, json_v3=True)
            if not error:
                try:
                    with json_path.open(encoding="utf-8") as stream:
                        parsed = json.load(stream)
                    validate_document(parsed, schema)
                    if (
                        parsed.get("format") != "org.colorscreen.parameters"
                        or parsed.get("schema_version") != 3
                        or "legacy_csp" in parsed
                        or "state" in parsed
                    ):
                        error = "native JSON v3 identity is wrong or includes a legacy mirror"
                    # Check the independent validator actually enforces the
                    # two most important schema promises: no unknown keys and
                    # no unannounced extra or missing required fields.
                    if i == 0:
                        extra = copy.deepcopy(parsed)
                        extra["capture"]["future_setting"] = True
                        missing = copy.deepcopy(parsed)
                        del missing["process"]["contact_copy"]
                        for corrupt in (extra, missing):
                            try:
                                validate_document(corrupt, schema)
                            except SchemaValidationError:
                                pass
                            else:
                                error = "schema validator accepted an illegal document"
                                break
                except (OSError, UnicodeError, ValueError, AttributeError,
                        SchemaValidationError) as exc:
                    error = f"native JSON v3 is not schema-conformant: {exc}"
            if not error:
                error = adjust(program, json_path, restored)
            if not error:
                error = compare_files(canonical, restored)
            if not error:
                error = adjust(program, json_path, json_rewritten)
            if not error and json_path.read_bytes() != json_rewritten.read_bytes():
                error = "native JSON v3 is not byte-stable after reloading"

            if error:
                failures.append((title, error))
                say(f"FAIL {title}\n{error}")
            else:
                checked += 1

            if (i + 1) % 25 == 0:
                say(f"Checked {i + 1}/{len(sources)} parameter fixtures")

    say(
        f"Corpus result: {checked} identical legacy round trips out of "
        f"{len(sources)} files; {len(failures)} mismatches/errors"
    )
    if failures:
        say("Failed fixture paths: " + ", ".join(path for path, _ in failures))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
