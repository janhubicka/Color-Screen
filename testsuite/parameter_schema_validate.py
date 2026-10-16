"""Dependency-free structural checker for our checked-in JSON Schema subset.

The authoritative semantic/physical validator remains libcolorscreen. This
independent test consumes doc/parameters-schema-v3.json and verifies that real
CLI-generated documents conform to its object vocabulary, required fields,
types, fixed tuples and mode-tagged unions. This implements exactly the
2020-12 keywords used by our schema, rather than silently skipping validation
when an optional third-party jsonschema package is missing.
"""

from __future__ import annotations

import math
from typing import Any


class SchemaValidationError(ValueError):
    """An input document or schema fails the supported structural contract."""


def _type_matches(value: Any, expected: str) -> bool:
    if expected == "null":
        return value is None
    if expected == "boolean":
        return type(value) is bool
    if expected == "integer":
        return type(value) is int
    if expected == "number":
        return type(value) in (int, float) and math.isfinite(value)
    if expected == "string":
        return type(value) is str
    if expected == "array":
        return type(value) is list
    if expected == "object":
        return type(value) is dict
    raise SchemaValidationError(f"unsupported JSON Schema type: {expected!r}")


def _matches(value: Any, schema: dict[str, Any], defs: dict[str, Any],
             path: str) -> None:
    """Raise with a JSON path if VALUE does not match SCHEMA."""
    if "$ref" in schema:
        ref = schema["$ref"]
        if not isinstance(ref, str) or not ref.startswith("#/$defs/"):
            raise SchemaValidationError(f"{path}: unsupported reference {ref!r}")
        key = ref[len("#/$defs/"):]
        if key not in defs:
            raise SchemaValidationError(f"{path}: undefined schema {key!r}")
        _matches(value, defs[key], defs, path)
        return

    for union in ("oneOf", "anyOf"):
        if union in schema:
            passing = 0
            reasons: list[str] = []
            for candidate in schema[union]:
                try:
                    _matches(value, candidate, defs, path)
                except SchemaValidationError as exc:
                    reasons.append(str(exc))
                else:
                    passing += 1
            if (union == "oneOf" and passing != 1) or (
                    union == "anyOf" and passing < 1):
                raise SchemaValidationError(
                    f"{path}: {union} matched {passing} alternatives"
                    + (f"; first failure: {reasons[0]}" if reasons else ""))
            return

    if "not" in schema:
        try:
            _matches(value, schema["not"], defs, path)
        except SchemaValidationError:
            pass
        else:
            raise SchemaValidationError(f"{path}: forbidden value")

    if "type" in schema and not _type_matches(value, schema["type"]):
        raise SchemaValidationError(
            f"{path}: expected {schema['type']}, got {type(value).__name__}")
    if "const" in schema and value != schema["const"]:
        raise SchemaValidationError(f"{path}: expected constant {schema['const']!r}")
    if "enum" in schema and value not in schema["enum"]:
        raise SchemaValidationError(f"{path}: unexpected enum value {value!r}")

    if isinstance(value, str):
        if len(value) < schema.get("minLength", 0):
            raise SchemaValidationError(f"{path}: string is too short")

    if _type_matches(value, "number"):
        if "minimum" in schema and value < schema["minimum"]:
            raise SchemaValidationError(f"{path}: below minimum")
        if "exclusiveMinimum" in schema and value <= schema["exclusiveMinimum"]:
            raise SchemaValidationError(f"{path}: below exclusive minimum")
        if "maximum" in schema and value > schema["maximum"]:
            raise SchemaValidationError(f"{path}: above maximum")

    if isinstance(value, list):
        if len(value) < schema.get("minItems", 0):
            raise SchemaValidationError(f"{path}: too few array items")
        if "maxItems" in schema and len(value) > schema["maxItems"]:
            raise SchemaValidationError(f"{path}: too many array items")
        prefix = schema.get("prefixItems", [])
        for i, item_schema in enumerate(prefix):
            if i >= len(value):
                raise SchemaValidationError(f"{path}[{i}]: missing tuple item")
            _matches(value[i], item_schema, defs, f"{path}[{i}]")
        tail = schema.get("items")
        if tail is False and len(value) > len(prefix):
            raise SchemaValidationError(f"{path}: unexpected tuple item")
        if isinstance(tail, dict):
            for i in range(len(prefix), len(value)):
                _matches(value[i], tail, defs, f"{path}[{i}]")

    if isinstance(value, dict):
        properties = schema.get("properties", {})
        for name in schema.get("required", []):
            if name not in value:
                raise SchemaValidationError(f"{path}.{name}: missing required field")
        for name, child in value.items():
            if name not in properties:
                if schema.get("additionalProperties") is False:
                    raise SchemaValidationError(f"{path}.{name}: unknown field")
                continue
            _matches(child, properties[name], defs, f"{path}.{name}")


def check_schema(schema: Any) -> None:
    """Reject unsupported or dangling keywords before checking source data."""
    if not isinstance(schema, dict) or schema.get("$schema") != (
            "https://json-schema.org/draft/2020-12/schema"):
        raise SchemaValidationError("unexpected JSON Schema dialect")
    defs = schema.get("$defs")
    if not isinstance(defs, dict):
        raise SchemaValidationError("missing $defs in canonical format schema")

    supported = {
        "$schema", "$id", "$defs", "title", "description", "$ref",
        "type", "const", "enum", "oneOf", "anyOf", "not", "properties",
        "required", "additionalProperties", "items", "prefixItems",
        "minItems", "maxItems", "minLength", "minimum", "exclusiveMinimum",
        "maximum",
    }

    def inspect(node: Any, path: str) -> None:
        if not isinstance(node, dict):
            raise SchemaValidationError(f"{path}: expected schema object")
        unknown = node.keys() - supported
        if unknown:
            raise SchemaValidationError(f"{path}: unsupported keywords {sorted(unknown)}")
        if "$ref" in node:
            ref = node["$ref"]
            if not isinstance(ref, str) or not ref.startswith("#/$defs/") or (
                    ref[len("#/$defs/"):] not in defs):
                raise SchemaValidationError(f"{path}: unresolved reference {ref!r}")
        for k in ("properties", "$defs"):
            for key, sub in node.get(k, {}).items():
                inspect(sub, f"{path}.{k}.{key}")
        for k in ("oneOf", "anyOf", "prefixItems"):
            for i, sub in enumerate(node.get(k, [])):
                inspect(sub, f"{path}.{k}[{i}]")
        for k in ("not", "items"):
            if isinstance(node.get(k), dict):
                inspect(node[k], f"{path}.{k}")

    inspect(schema, "$")


def validate_document(document: Any, schema: dict[str, Any]) -> None:
    """Validate a real serialized v3 document against the checked-in schema."""
    _matches(document, schema, schema["$defs"], "$")
