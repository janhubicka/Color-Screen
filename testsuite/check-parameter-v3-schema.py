#!/usr/bin/env python3
"""Check the *reference-only* schema-3 structural contract without PyPI.

The production codec still emits schema 2.  This file independently checks
schema-3 draft-2020-12's deliberately small vocabulary subset (types, oneOf,
local references, required keys, closed objects, arrays, constants and numeric
bounds). It is not a replacement for an actual JSON Schema implementation:
full draft compliance is additionally tested by jsonschema when available.
"""

from __future__ import annotations

from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import sys


def check_definition(node: object, root: dict, path: str = "$") -> None:
    """Reject dangling local schema references or a permissive object schema."""
    if isinstance(node, list):
        for index, child in enumerate(node):
            check_definition(child, root, f"{path}[{index}]")
        return
    if not isinstance(node, dict):
        return
    if "$ref" in node:
        reference = node["$ref"]
        if not isinstance(reference, str) or not reference.startswith("#/$defs/"):
            raise AssertionError(f"{path}: non-local schema reference {reference!r}")
        if reference.removeprefix("#/$defs/") not in root["$defs"]:
            raise AssertionError(f"{path}: dangling schema reference {reference!r}")
    if node.get("type") == "object":
        if node.get("additionalProperties") is not False:
            raise AssertionError(f"{path}: object vocabulary must be closed")
        properties = node.get("properties")
        if not isinstance(properties, dict) or not properties:
            raise AssertionError(f"{path}: missing schema properties")
        if set(node.get("required", [])) - properties.keys():
            raise AssertionError(f"{path}: required schema field is undefined")
    for key, child in node.items():
        if key == "$defs" and node is not root:
            raise AssertionError(f"{path}: nested schema definitions are unsupported")
        check_definition(child, root, path + "." + key)


def validate(schema: dict, data: object, root: dict, path: str = "$", depth: int = 0) -> None:
    """Evaluate only the schema vocabulary used by the checked-in schema."""
    if depth > 50:
        raise AssertionError(f"{path}: excessive schema recursion")
    if "$ref" in schema:
        validate(root["$defs"][schema["$ref"].split("/")[-1]], data,
                 root, path, depth + 1)
    if "oneOf" in schema:
        accepted = 0
        for variant in schema["oneOf"]:
            try:
                validate(variant, data, root, path, depth + 1)
                accepted += 1
            except AssertionError:
                pass
        if accepted != 1:
            raise AssertionError(f"{path}: exactly one shape required (matched {accepted})")
    if "const" in schema and data != schema["const"]:
        raise AssertionError(f"{path}: incorrect constant {data!r}")
    if "enum" in schema and data not in schema["enum"]:
        raise AssertionError(f"{path}: unrecognized choice {data!r}")
    kind = schema.get("type")
    if kind == "null":
        if data is not None:
            raise AssertionError(f"{path}: expected null")
    elif kind == "string":
        if not isinstance(data, str):
            raise AssertionError(f"{path}: expected text")
        if len(data) < schema.get("minLength", 0):
            raise AssertionError(f"{path}: string is empty")
    elif kind == "boolean":
        if type(data) is not bool:
            raise AssertionError(f"{path}: expected boolean")
    elif kind in ("number", "integer"):
        if type(data) not in ((int, float) if kind == "number" else (int,)):
            raise AssertionError(f"{path}: expected {kind}")
        if "minimum" in schema and data < schema["minimum"]:
            raise AssertionError(f"{path}: number is below minimum")
        if "maximum" in schema and data > schema["maximum"]:
            raise AssertionError(f"{path}: number is above maximum")
        if "exclusiveMinimum" in schema and data <= schema["exclusiveMinimum"]:
            raise AssertionError(f"{path}: number is not above exclusive minimum")
    elif kind == "array":
        if not isinstance(data, list):
            raise AssertionError(f"{path}: expected array")
        if len(data) < schema.get("minItems", 0) or len(data) > schema.get("maxItems", float("inf")):
            raise AssertionError(f"{path}: wrong array length")
        for index, item in enumerate(data):
            validate(schema["items"], item, root, f"{path}[{index}]", depth + 1)
    elif kind == "object":
        if not isinstance(data, dict):
            raise AssertionError(f"{path}: expected object")
        props = schema["properties"]
        missing = set(schema.get("required", [])) - data.keys()
        extra = data.keys() - props.keys()
        if missing or extra:
            raise AssertionError(f"{path}: missing {sorted(missing)}, unexpected {sorted(extra)}")
        for key, item in data.items():
            validate(props[key], item, root, f"{path}.{key}", depth + 1)


def should_fail(schema: dict, value: object, root: dict, hint: str) -> None:
    """Assert that an intentionally broken reference document is rejected."""
    try:
        validate(schema, value, root)
    except AssertionError:
        return
    raise AssertionError(f"accepted invalid v3 document: {hint}")


def main() -> None:
    if len(sys.argv) != 2:
        raise AssertionError("usage: check-parameter-v3-schema.py SOURCE-ROOT")
    root = Path(sys.argv[1]).resolve()
    schema = json.loads((root / "doc/parameter-json-v3-reference.schema.json").read_text(encoding="utf-8"))
    assert schema["$schema"] == "https://json-schema.org/draft/2020-12/schema"
    assert schema["properties"]["schema_version"]["const"] == 3
    assert schema["properties"]["format"]["const"] == "org.colorscreen.parameters"
    check_definition(schema, schema)
    mod_file = root / "testsuite/parameter-json-v3-reference.py"
    spec = importlib.util.spec_from_file_location("v3ref", mod_file)
    if spec is None or spec.loader is None:
        raise AssertionError("cannot import v3 reference translator")
    reference = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reference)
    examples = []
    for name in ("default", "rich"):
        sample = root / f"doc/parameter-json-v3-{name}-example.json"
        doc = json.loads(sample.read_text(encoding="utf-8"))
        validate(schema, doc, schema)
        if reference.upgrade_v2(reference.downgrade_v3(doc)) != doc:
            raise AssertionError(f"{name} example is not exactly reversible")
        examples.append(doc)
    default, rich = examples
    # Wrong shape, future nested fields and bad tagged decisions must fail.
    broken = deepcopy(default)
    broken["capture"]["orientation"]["quarter_turns"] = 4
    should_fail(schema, broken, schema, "too many quarter turns")
    broken = deepcopy(default)
    broken["capture"]["linearization"] = {"mode": "srgb", "exponent": 2.2}
    should_fail(schema, broken, schema, "srgb plus exponent")
    broken = deepcopy(default)
    broken["process"]["screen"]["strip_widths"]["red"] = {"mode": "fraction", "value": 0}
    should_fail(schema, broken, schema, "automatic width must be explicit")
    broken = deepcopy(default)
    broken["color"]["viewing"]["output_profile"] = "srgb"
    should_fail(schema, broken, schema, "view-only rendering setting")
    broken = deepcopy(rich)
    broken["calibration"]["capture"]["mtf"]["measurements"][0]["edge_quality"]["future_metric"] = 0.5
    should_fail(schema, broken, schema, "unknown measurement field")
    broken = deepcopy(rich)
    broken["calibration"]["capture"]["mtf"]["measurements"][0]["id"] = ""
    should_fail(schema, broken, schema, "empty measurement identifier")
    broken = deepcopy(rich)
    broken["calibration"]["capture"]["mtf"]["measurements"][0]["source"]["roi"] = {"status": "available", "x_px": 1, "y_px": 1, "width_px": 0, "height_px": 9}
    should_fail(schema, broken, schema, "nonpositive spatial ROI")
    # A missing reference filename is a genuinely nullable *value*.
    copy = deepcopy(rich)
    copy["calibration"]["capture"]["mtf"]["measurements"][0]["source"]["path"] = None
    validate(schema, copy, schema)
    # Native jsonschema is optional in the build environment; if installed,
    # it additionally checks draft compliance and agrees on the test fixtures.
    try:
        import jsonschema
    except ModuleNotFoundError:
        print("# jsonschema package absent: core structural checks still passed")
    else:
        jsonschema.Draft202012Validator.check_schema(schema)
        external = jsonschema.Draft202012Validator(schema)
        for example in examples:
            if not external.is_valid(example):
                raise AssertionError("official JSON Schema rejected a reference example")
        if not external.is_valid(copy):
            raise AssertionError("official JSON Schema rejected nullable source")
        print("# draft 2020-12 validated with jsonschema")
    print("# Structural vocabulary, default/rich samples and malformed cases passed")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, ValueError, OSError, TypeError, KeyError) as exc:
        print(f"# schema-3 reference validation failure: {exc}", file=sys.stderr)
        raise SystemExit(1)
