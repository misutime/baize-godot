#!/usr/bin/env python3
"""Runs the skeletal animation acceptance scenarios in a headless editor.

Usage: python run.py --godot <editor console executable> [--out <directory>]

A fresh empty project is created for every run. The exit code is 0 only when the
editor exits cleanly, the result file exists and every scenario passed.
"""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent


def check_selected_scene_export(godot: str, project: Path, out: Path, timeout: int) -> bool:
    with tempfile.TemporaryDirectory(prefix="selected-scene-", dir=out) as temporary:
        fixture = Path(temporary)
        source = fixture / "source"
        runtime = fixture / "runtime"
        runtime.mkdir()
        shutil.copytree(project / "characters", source / "characters")
        # Exercise both the default library and a named library in the packed skeleton.
        catalog = source / "characters/hero_anims"
        (catalog / "Combat").mkdir()
        (catalog / "Jump.skanim").rename(catalog / "Combat/Jump.skanim")
        (source / "project.godot").write_text(
            'config_version=5\n[application]\nrun/main_scene="res://main.tscn"\n', encoding="utf-8"
        )
        (source / "main.tscn").write_text(
            '[gd_scene format=3]\n\n[ext_resource type="PackedScene" path="res://characters/hero.skmesh" id="1"]\n'
            '\n[sub_resource type="AnimationNodeAnimation" id="Jump"]\nanimation = &"Combat/Jump"\n'
            '\n[node name="Player" type="Node3D"]\n\n[node name="Model" parent="." instance=ExtResource("1")]\n'
            '\n[node name="AnimationTree" type="AnimationTree" parent="."]\nactive = false\n'
            'tree_root = SubResource("Jump")\nanim_player = NodePath("../Model/AnimationPlayer")\n',
            encoding="utf-8",
        )
        (source / "export_presets.cfg").write_text(
            '[preset.0]\nname="Selected scenes"\nplatform="Windows Desktop"\nrunnable=true\n'
            'export_filter="scenes"\nexport_files=PackedStringArray("res://main.tscn")\n'
            'include_filter=""\nexclude_filter=""\n\n[preset.0.options]\nbinary_format/embed_pck=false\n',
            encoding="utf-8",
        )
        pack = out / "selected-scenes.pck"
        pack.unlink(missing_ok=True)
        log = out / "selected-scenes.log"
        with log.open("w", encoding="utf-8", errors="replace") as stream:
            commands = [
                ["--headless", "--editor", "--path", str(source), "--export-pack", "Selected scenes", str(pack)],
                [
                    "--headless",
                    "--path",
                    str(runtime),
                    "--main-pack",
                    str(pack),
                    "--script",
                    str(HERE / "selected_pack_check.gd"),
                ],
            ]
            try:
                for command in commands:
                    subprocess.run(
                        [godot, *command], stdout=stream, stderr=subprocess.STDOUT, timeout=timeout, check=True
                    )
                # Rebind in place, retaining both the path and the export cache from the first pack.
                clip = catalog / "Combat/Jump.skanim"
                lines = clip.read_text(encoding="utf-8").splitlines()
                assert 'skeleton_path = "res://characters/hero.sk"' in lines
                clip.write_text("\n".join(
                    'skeleton_path = "res://characters/copy.sk"' if line.startswith("skeleton_path = ") else line
                    for line in lines if not line.startswith("_skeleton_uid = ")
                ) + "\n", encoding="utf-8")
                subprocess.run([godot, *commands[0]], stdout=stream, stderr=subprocess.STDOUT, timeout=timeout, check=True)
                subprocess.run([godot, *commands[1], "--", "rebound"], stdout=stream, stderr=subprocess.STDOUT, timeout=timeout, check=True)
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
                return False
        output = log.read_text(encoding="utf-8", errors="replace")
        return "SELECTED_ANIMATION_PACK_PASS" in output and "REBOUND_ANIMATION_PACK_PASS" in output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--godot", required=True)
    parser.add_argument("--out", default=None)
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()

    out = Path(args.out).resolve() if args.out else Path(tempfile.mkdtemp(prefix="animation-regression-"))
    godot = str(Path(args.godot).resolve())
    project = out / "project"
    if project.exists():
        shutil.rmtree(project)
    shutil.copytree(HERE / "addons", project / "addons")
    (project / "project.godot").write_text(
        'config_version=5\n\n[application]\n\nconfig/name="AnimationRegression"\n\n'
        '[editor_plugins]\n\nenabled=PackedStringArray("res://addons/regression/plugin.cfg")\n',
        encoding="utf-8",
    )
    results = out / "results.json"
    if results.exists():
        results.unlink()

    log = out / "editor.log"
    with log.open("w", encoding="utf-8", errors="replace") as stream:
        try:
            code = subprocess.run(
                [godot, "--headless", "--editor", "--path", str(project), "--", str(out).replace("\\", "/")],
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
    if code != 0 or summary["failed"] != 0:
        return 1
    if check_selected_scene_export(godot, project, out, args.timeout):
        print("PASS selected-scene PCK loads its animation libraries and plays through a role-owned AnimationTree")
        return 0
    print(f"FAIL selected-scene PCK. See {out / 'selected-scenes.log'}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
