"""
test_eus.py  —  EU template compile and REPL validation tests
─────────────────────────────────────────────────────────────────────────────
These tests are UNIT tests (no HEA server required).  They verify:
  1. All templates in eu_registry/ are valid Jinja2
  2. KLayout Python templates pass REPL sandbox validation
  3. eu_compiler.compile_eu() renders templates without error

Run with:
  pytest tests/hea/test_eus.py -v
"""

from __future__ import annotations

import asyncio
import json
import sys
from pathlib import Path

import pytest

# Make src/ importable
_SRC = Path(__file__).parent.parent.parent / "src"
if str(_SRC) not in sys.path:
    sys.path.insert(0, str(_SRC))

REGISTRY_ROOT = Path(__file__).parent.parent.parent / "src" / "orchestrator" / "eu_registry"


# ── Fixture: load eu_registry manifest ────────────────────────────────────────
@pytest.fixture(scope="session")
def manifest() -> dict:
    manifest_path = REGISTRY_ROOT / "manifest.json"
    assert manifest_path.exists(), f"manifest.json not found at {manifest_path}"
    return json.loads(manifest_path.read_text())


# ── Test 1: manifest is valid JSON with required fields ───────────────────────
def test_manifest_structure(manifest):
    assert "templates" in manifest
    assert isinstance(manifest["templates"], list)
    for entry in manifest["templates"]:
        assert "id"       in entry
        assert "host"     in entry
        assert "language" in entry
        assert "file"     in entry


# ── Test 2: all template files referenced in manifest exist ──────────────────
def test_all_template_files_exist(manifest):
    missing = []
    for entry in manifest["templates"]:
        path = REGISTRY_ROOT / entry["file"]
        if not path.exists():
            missing.append(str(path))
    assert not missing, f"Missing template files:\n" + "\n".join(missing)


# ── Test 3: all Jinja2 templates parse without errors ─────────────────────────
def test_jinja2_templates_parse(manifest):
    pytest.importorskip("jinja2", reason="jinja2 not installed")
    from jinja2 import Environment, FileSystemLoader, TemplateSyntaxError

    errors = []
    for entry in manifest["templates"]:
        path = REGISTRY_ROOT / entry["file"]
        if not path.exists():
            continue
        try:
            env = Environment(loader=FileSystemLoader(str(path.parent)))
            env.parse(path.read_text())
        except TemplateSyntaxError as e:
            errors.append(f"{entry['id']}: {e}")

    assert not errors, "Jinja2 syntax errors:\n" + "\n".join(errors)


# ── Test 4: KLayout Python template renders and passes REPL ──────────────────
@pytest.mark.asyncio
async def test_klayout_template_repl_validation():
    pytest.importorskip("jinja2", reason="jinja2 not installed")

    from orchestrator.eu_compiler import compile_eu
    from orchestrator.python_repl import run_in_repl

    # Render the zoom_and_highlight template with dummy args
    script = compile_eu(
        host="klayout",
        template_name="zoom_and_highlight",
        args={
            "x1": 0.0, "y1": 0.0,
            "x2": 10.0, "y2": 10.0,
            "layer_name": "M1",
            "net_name": "VDD",
            "margin_um": 1.0,
        },
    )
    assert len(script) > 10, "Rendered script is too short"

    # Run in the sandbox REPL (with KLayout stubs)
    ok, error = await run_in_repl(script, timeout_s=10.0)
    assert ok, f"REPL validation failed:\n{error}"


# ── Test 5: eu_compiler raises FileNotFoundError for missing template ─────────
def test_eu_compiler_missing_template():
    pytest.importorskip("jinja2", reason="jinja2 not installed")
    from orchestrator.eu_compiler import compile_eu

    with pytest.raises(FileNotFoundError):
        compile_eu("cadence_virtuoso", "nonexistent_template_xyz", {})


# ── Test 6: list_templates returns non-empty dict ─────────────────────────────
def test_list_templates():
    pytest.importorskip("jinja2", reason="jinja2 not installed")
    from orchestrator.eu_compiler import list_templates

    result = list_templates()
    assert isinstance(result, dict)
    # At least one of our known hosts should be present
    known_hosts = {"cadence_virtuoso", "synopsys_icc2", "klayout"}
    assert known_hosts & result.keys(), f"No known host found in {list(result.keys())}"
