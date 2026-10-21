#!/usr/bin/env python3
"""Build, verify, and package the native Windows x86_64 Godot editor."""

import argparse
import datetime
import hashlib
import json
import os
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "bin" / "misu"
EXECUTABLE = "godot.windows.editor.x86_64.exe"
RUNTIME_FILES = (EXECUTABLE, "godot.windows.editor.x86_64.console.exe", "D3D12Core.dll", "d3d12SDKLayers.dll")
MANIFEST = OUTPUT / "editor-build.json"

SMOKE_SCRIPT = """extends SceneTree

func _initialize() -> void:
    call_deferred("_verify")

func _verify() -> void:
    if not ClassDB.class_exists("GDScript") or ClassDB.class_exists("CSharpScript"):
        push_error("Expected GDScript support without the Mono module.")
        quit(1)
        return
    var node := Node3D.new()
    root.add_child(node)
    node.position = Vector3(1, 2, 3)
    if node.global_position != Vector3(1, 2, 3):
        quit(1)
        return
    var script := GDScript.new()
    script.source_code = "extends RefCounted\\nfunc answer():\\n    return 42\\n"
    if script.reload() != OK:
        quit(1)
        return
    var instance = script.new()
    if instance.answer() != 42:
        quit(1)
        return
    print("MISU_NATIVE_SMOKE_OK: GDScript, Node3D, runtime compilation; no CSharpScript")
    quit(0)
"""


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, encoding="utf-8").strip()


def digest(path: Path) -> str:
    checksum = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            checksum.update(chunk)
    return checksum.hexdigest()


def source_state() -> str:
    checksum = hashlib.sha256(git("rev-parse", "HEAD").encode())
    checksum.update(subprocess.check_output(["git", "diff", "--binary", "HEAD", "--"], cwd=ROOT))
    untracked = subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard", "-z"], cwd=ROOT)
    for name in sorted(filter(None, untracked.split(b"\0"))):
        checksum.update(name + b"\0")
        checksum.update(digest(ROOT / os.fsdecode(name)).encode())
    return checksum.hexdigest()


def run_logged(command: list[str], log: Path) -> None:
    log.parent.mkdir(parents=True, exist_ok=True)
    print(f"Running: {subprocess.list2cmdline(command)}", flush=True)
    print(f"Log: {log}", flush=True)
    environment = os.environ.copy()
    environment.update(PYTHONUTF8="1", PYTHONUNBUFFERED="1", VSLANG="1033")
    with log.open("w", encoding="utf-8", newline="\n") as stream:
        with subprocess.Popen(
            command,
            cwd=ROOT,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        ) as process:
            assert process.stdout is not None
            for line in process.stdout:
                stream.write(line)
                stream.flush()
                print(line, end="", flush=True)
            result = process.wait()
    if result:
        raise RuntimeError(f"Command failed with exit code {result}; see {log}")


def check_dependencies() -> Path:
    if sys.platform != "win32":
        raise RuntimeError("This entry point builds Windows with MSVC; run it from Windows.")
    if (ROOT / "custom.py").exists():
        raise RuntimeError("custom.py can override upstream defaults; inspect it before using this native build entry.")
    local_appdata = os.environ.get("LOCALAPPDATA")
    if not local_appdata:
        raise RuntimeError("LOCALAPPDATA is required to find the official build dependencies.")
    deps = Path(local_appdata) / "Godot" / "build_deps"
    required = {
        "install_d3d12_sdk_windows.py": ("mesa-x86_64-msvc", "agility_sdk"),
        "install_accesskit.py": ("accesskit",),
        "install_angle.py": ("angle-x86_64-msvc",),
    }
    missing = [
        f"python misc/scripts/{installer}"
        for installer, folders in required.items()
        if any(not (deps / folder).is_dir() for folder in folders)
    ]
    if missing:
        raise RuntimeError("Install the missing upstream dependencies first:\n" + "\n".join(missing))
    return deps


def build(jobs: int) -> None:
    check_dependencies()
    initial_state = source_state()
    command = [
        sys.executable,
        "-m",
        "SCons",
        "platform=windows",
        "target=editor",
        "arch=x86_64",
        "module_mono_enabled=no",
        "module_gdscript_enabled=yes",
        "d3d12=yes",
        "accesskit=yes",
        "angle=yes",
        f"-j{jobs}",
    ]
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_logged(command, OUTPUT / "logs" / f"editor-{stamp}.log")

    enabled = (ROOT / "modules" / "modules_enabled.gen.h").read_text(encoding="utf-8")
    if "#define MODULE_GDSCRIPT_ENABLED" not in enabled or "#define MODULE_MONO_ENABLED" in enabled:
        raise RuntimeError("Generated module configuration does not match the native GDScript build.")
    for name in RUNTIME_FILES:
        if not (ROOT / "bin" / name).is_file():
            raise RuntimeError(f"Build did not produce the expected runtime file: {name}")
    version = subprocess.check_output(
        [str(ROOT / "bin" / EXECUTABLE), "--headless", "--version"], cwd=ROOT, encoding="utf-8"
    ).strip()
    smoke = OUTPUT / "smoke"
    smoke.mkdir(parents=True, exist_ok=True)
    (smoke / "project.godot").write_text(
        'config_version=5\n[application]\nconfig/name="Native build smoke"\n', encoding="utf-8", newline="\n"
    )
    script = smoke / "native_smoke.gd"
    script.write_text(SMOKE_SCRIPT, encoding="utf-8", newline="\n")
    run_logged(
        [str(ROOT / "bin" / EXECUTABLE), "--headless", "--path", str(smoke), "--script", str(script)],
        OUTPUT / "logs" / f"smoke-{stamp}.log",
    )
    if source_state() != initial_state:
        raise RuntimeError("Source files changed during the build; rerun to verify a consistent source tree.")
    manifest = {
        "source_commit": git("rev-parse", "HEAD"),
        "source_state": initial_state,
        "working_tree_status": git("status", "--porcelain=v1"),
        "engine_version": version,
        "built_at_utc": stamp,
        "command": command,
        "runtime_sha256": {name: digest(ROOT / "bin" / name) for name in RUNTIME_FILES},
        "validation": "GDScript execution, Node3D transforms, runtime script compilation; no CSharpScript",
    }
    MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Verified editor: {version}", flush=True)


def package() -> Path:
    deps = check_dependencies()
    if not MANIFEST.is_file():
        raise RuntimeError("No verified build record exists; run the build action first.")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest["source_commit"] != git("rev-parse", "HEAD"):
        raise RuntimeError("HEAD changed after the verified build; rebuild before packaging.")
    if manifest["source_state"] != source_state():
        raise RuntimeError("Source files changed after verification; rebuild before packaging.")
    for name, expected in manifest["runtime_sha256"].items():
        if digest(ROOT / "bin" / name) != expected:
            raise RuntimeError(f"Runtime file changed after verification: {name}")
    agility_license = deps / "agility_sdk" / "LICENSE.txt"
    if not agility_license.is_file():
        raise RuntimeError(f"Missing Agility SDK license: {agility_license}")

    version = manifest["engine_version"].split(".custom_build", 1)[0]
    name = f"godot-{version}-windows.x86_64-{manifest['source_commit'][:10]}"
    archive = OUTPUT / "packages" / f"{name}.zip"
    archive.parent.mkdir(parents=True, exist_ok=True)
    files = {entry: ROOT / "bin" / entry for entry in RUNTIME_FILES}
    files.update({entry: ROOT / entry for entry in ("LICENSE.txt", "COPYRIGHT.txt", "AUTHORS.md")})
    files["licenses/Direct3D12-Agility-SDK-LICENSE.txt"] = agility_license
    files["build-info.json"] = MANIFEST
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for entry, source in files.items():
            output.write(source, arcname=f"{name}/{entry}")
        output.writestr(f"{name}/_sc_", "")
    checksum = archive.with_suffix(".zip.sha256")
    checksum.write_text(f"{digest(archive)}  {archive.name}\n", encoding="utf-8", newline="\n")
    print(f"Package: {archive}", flush=True)
    print(f"SHA-256: {checksum}", flush=True)
    return archive


def positive_jobs(value: str) -> int:
    jobs = int(value)
    if jobs < 1:
        raise argparse.ArgumentTypeError("jobs must be positive")
    return jobs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", nargs="?", choices=("build", "package", "all"), default="all")
    parser.add_argument("--jobs", type=positive_jobs, default=min(os.cpu_count() or 1, 16))
    args = parser.parse_args()
    try:
        if args.action in ("build", "all"):
            build(args.jobs)
        if args.action in ("package", "all"):
            package()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
