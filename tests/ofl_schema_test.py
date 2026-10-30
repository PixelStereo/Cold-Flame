"""Validate actual C++ exports offline against the pinned, unmodified OFL schema."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

try:
    from jsonschema import Draft202012Validator, FormatChecker
    from referencing import Registry, Resource
except ImportError:
    sys.exit("OFL schema tests require: python -m pip install jsonschema==4.26.0")

root = Path(__file__).resolve().parent
registry = Registry()
for path in (root / "ofl-schema").glob("*.json"):
    schema = json.loads(path.read_text())
    registry = registry.with_resource(schema["$id"], Resource.from_contents(schema))
schema = json.loads((root / "ofl-schema/fixture.json").read_text())
validator = Draft202012Validator(schema, registry=registry, format_checker=FormatChecker())
with tempfile.TemporaryDirectory() as directory:
    subprocess.run([sys.argv[1], "nativeExportsPreserveSupportedParametersAndValidateWithOflSchema"],
                   env={**os.environ, "FLAME_OFL_EXPORT_DIR": directory}, check=True)
    files = list(Path(directory).glob("*.json"))
    assert len(files) == 8, f"Expected eight exported fixtures, got {len(files)}"
    files += [root / "fixtures/ofl-desk-channel.json", root / "fixtures/ofl-matrix.json"]
    for path in files:
        validator.validate(json.loads(path.read_text()))
        print(f"OFL 12.6.0 schema: {path.name} passed")
