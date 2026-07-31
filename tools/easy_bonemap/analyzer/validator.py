"""Explicit integration with the Khronos glTF Validator npm package."""
from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path
from typing import Any

_SCRIPT = r"""
const fs = require('fs');
const validator = require('gltf-validator');
const chunks = [];
process.stdin.on('data', chunk => chunks.push(chunk));
process.stdin.on('end', () => validator.validateBytes(new Uint8Array(Buffer.concat(chunks)))
  .then(report => process.stdout.write(JSON.stringify(report)))
  .catch(error => { process.stderr.write(String(error)); process.exit(2); }));
"""


def validate_asset(path: str | Path) -> dict[str, Any]:
    package_root = Path(__file__).resolve().parents[1]
    node = shutil.which("node")
    if node is None:
        raise RuntimeError("Node.js is required for glTF validation")
    if not (package_root / "node_modules/gltf-validator/package.json").is_file():
        raise RuntimeError("gltf-validator is not installed in tools/easy_bonemap")
    result = subprocess.run([node, "-e", _SCRIPT], cwd=package_root, input=Path(path).read_bytes(), capture_output=True, check=False)
    if result.returncode != 0:
        message = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"glTF validator failed: {message}")
    return json.loads(result.stdout.decode("utf-8"))
