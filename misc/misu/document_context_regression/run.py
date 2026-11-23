#!/usr/bin/env python3
"""Runs editor document-context acceptance scenarios in a headless editor."""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent


def seed_script_workspace(project: Path) -> None:
    long_line = "# " + "Script viewport regression. " * 32 + "\n"
    (project / "restore_script.gd").write_text("extends Node\n\n" + long_line * 64, encoding="utf-8")
    editor = project / ".godot/editor"
    editor.mkdir(parents=True)
    (editor / "editor_layout.cfg").write_text(
        '[ScriptEditor]\nopen_scripts=["res://restore_script.gd"]\nselected_script="res://restore_script.gd"\n'
        '[EditorDocuments]\norder=[{"type": "workspace", "path": "editor://scripts"}]\n'
        'active={"type": "workspace", "path": "editor://scripts"}\n',
        encoding="utf-8",
    )
    (editor / "script_editor_cache.cfg").write_text(
        '[res://restore_script.gd]\nstate={"row": 20, "column": 44, "scroll_position": 8.0, '
        '"h_scroll_position": 192, "selection": false}\n',
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--godot", required=True)
    parser.add_argument("--out", default=None)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()

    out = Path(args.out).resolve() if args.out else Path(tempfile.mkdtemp(prefix="document-context-regression-"))
    project = out / "project"
    if project.exists():
        shutil.rmtree(project)
    shutil.copytree(HERE / "addons", project / "addons")
    (project / "project.godot").write_text(
        'config_version=5\n\n[application]\n\nconfig/name="DocumentContextRegression"\n\n'
        '[editor_plugins]\n\nenabled=PackedStringArray("res://addons/regression/plugin.cfg")\n',
        encoding="utf-8",
    )
    seed_script_workspace(project)
    results = out / "results.json"
    if results.exists():
        results.unlink()

    log = out / "editor.log"
    with log.open("w", encoding="utf-8", errors="replace") as stream:
        try:
            code = subprocess.run(
                [str(Path(args.godot).resolve()), "--headless", "--editor", "--path", str(project), "--", str(out).replace("\\", "/")],
                stdout=stream,
                stderr=subprocess.STDOUT,
                timeout=args.timeout,
            ).returncode
        except subprocess.TimeoutExpired:
            code = -1

    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=HERE, capture_output=True, text=True).stdout.strip()
    dirty = bool(subprocess.run(["git", "status", "--porcelain"], cwd=HERE, capture_output=True, text=True).stdout.strip())
    print(f"source HEAD {head}{' + uncommitted changes' if dirty else ''}")
    print(f"output {out}")
    if not results.exists():
        print(f"FAILED: no result file (editor exit code {code}). See {log}")
        return 2
    summary = json.loads(results.read_text(encoding="utf-8"))
    print(f"engine {summary['engine']}")
    for entry in summary["results"]:
        print(("PASS " if entry["passed"] else "FAIL ") + entry["scenario"])
        for failure in entry["failures"]:
            print("     " + failure[:300])
    print(f"{summary['scenarios'] - summary['failed']}/{summary['scenarios']} scenarios passed, editor exit code {code}")
    return 0 if code == 0 and summary["failed"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
