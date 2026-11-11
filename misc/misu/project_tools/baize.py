"""Apply and check native GDScript warning defaults and format project scripts."""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import tempfile
from importlib.metadata import version
from pathlib import Path

from gdtoolkit.formatter import check_formatting_safety, format_code

HERE = Path(__file__).resolve().parent
POLICY_PATH = HERE / "policy.json"
POLICY = json.loads(POLICY_PATH.read_text(encoding="utf-8"))
ERROR_LOG = re.compile(r"^\s*(?:SCRIPT ERROR|ERROR):", re.MULTILINE)


def run_engine(engine, project, *args):
    result = subprocess.run(
        [str(engine), "--headless", "--path", str(project), *args],
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        timeout=120,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
    )
    log = result.stdout + result.stderr
    return result.returncode == 0 and not ERROR_LOG.search(log), log


def run_policy(engine, project, command, output):
    ok, log = run_engine(
        engine, project, "--script", str(HERE / "project_policy.gd"),
        "--", command, str(POLICY_PATH), str(output),
    )
    if not ok or not output.is_file():
        raise ValueError(log.strip() or "Godot did not produce the policy output")


def write_file(path, data):
    stream = tempfile.NamedTemporaryFile(dir=path.parent, delete=False)
    temporary = Path(stream.name)
    try:
        with stream:
            stream.write(data)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def adopt(project, engine):
    config = project / "project.godot"
    original = config.read_bytes()
    with tempfile.TemporaryDirectory() as scratch:
        output = Path(scratch) / "project.godot"
        run_policy(engine, project, "adopt", output)
        updated = output.read_bytes()
    if config.read_bytes() != original:
        raise ValueError("project.godot changed during adoption")
    if updated != original:
        write_file(config, updated)
    return {"updated": updated != original}


def scripts(project):
    for directory, folders, files in os.walk(project):
        base = Path(directory)
        if ".gdignore" in files or (base != project and "project.godot" in files):
            folders.clear()
            continue
        folders[:] = sorted(
            name for name in folders
            if not name.startswith(".") and (base / name).resolve() == base / name
        )
        for name in sorted(files):
            file = base / name
            if not name.startswith(".") and file.suffix == ".gd" and file.resolve() == file:
                yield file


def formatted_bytes(original):
    source = original.decode("utf-8-sig").replace("\r\n", "\n").replace("\r", "\n")
    width = POLICY["line_length"]
    formatted = format_code(source, width)
    check_formatting_safety(source, formatted, width)
    return formatted.encode("utf-8")


def formatting(project, files, *, write=False):
    changes = {}
    diagnostics = []
    for file in files:
        path = "res://" + file.relative_to(project).as_posix()
        try:
            original = file.read_bytes()
            formatted = formatted_bytes(original)
        except Exception as exc:
            diagnostics.append({"code": "FORMAT_UNSUPPORTED", "path": path, "message": str(exc)})
            continue
        if formatted != original:
            if write:
                changes[file] = (original, formatted)
            else:
                diagnostics.append({"code": "FORMAT", "path": path, "message": "Run baize.py format"})

    if diagnostics:
        return {"passed": False, "formatted": [], "diagnostics": diagnostics}

    for file, (original, _) in changes.items():
        if file.read_bytes() != original:
            raise ValueError(f"Script changed during formatting: {file}")
    for file, (_, formatted) in changes.items():
        write_file(file, formatted)
    return {"passed": True, "formatted": [file.relative_to(project).as_posix() for file in changes]}


def check(project, engine):
    diagnostics = []
    ok, log = run_engine(engine, project, "--editor", "--import")
    if not ok:
        diagnostics.append({"code": "ENGINE_IMPORT", "path": "res://project.godot", "message": log})

    with tempfile.TemporaryDirectory() as scratch:
        output = Path(scratch) / "settings.json"
        run_policy(engine, project, "check", output)
        settings = json.loads(output.read_text(encoding="utf-8"))

    for name, expected in POLICY["warnings"].items():
        if settings["warnings"][name] != expected:
            diagnostics.append({
                "code": "POLICY_SETTING",
                "path": "res://project.godot",
                "message": f"gdscript/warnings/{name}: expected {expected}, got {settings['warnings'][name]}",
            })

    files = list(scripts(project))
    for file in files:
        path = "res://" + file.relative_to(project).as_posix()
        ok, log = run_engine(engine, project, "--script", path, "--check-only")
        if not ok:
            diagnostics.append({"code": "ENGINE_SCRIPT", "path": path, "message": log})
    diagnostics.extend(formatting(project, files).get("diagnostics", []))
    return {
        "passed": not diagnostics,
        "scripts_checked": len(files),
        "warning_directory_rules": settings["directory_rules"],
        "diagnostics": diagnostics,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["adopt", "check", "format"])
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--engine", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    project = args.project.resolve()
    if not (project / "project.godot").is_file():
        parser.error("--project must contain project.godot")
    if args.command in {"adopt", "check"} and not args.engine:
        parser.error("--engine is required for adopt and check")
    try:
        if args.command != "adopt" and "gdtoolkit==" + version("gdtoolkit") != POLICY["formatter"]:
            raise ValueError(f"Install pinned requirements: {POLICY['formatter']}")
        if args.command == "adopt":
            result = adopt(project, args.engine.resolve())
        elif args.command == "check":
            result = check(project, args.engine.resolve())
        else:
            result = formatting(project, scripts(project), write=True)
    except (OSError, ValueError, subprocess.TimeoutExpired) as exc:
        result = {"passed": False, "error": str(exc)}

    report = json.dumps(result, ensure_ascii=True, indent=2) + "\n"
    if args.output:
        write_file(args.output.resolve(), report.encode("utf-8"))
    print(report, end="")
    return 0 if result.get("passed", True) else 1


if __name__ == "__main__":
    raise SystemExit(main())
