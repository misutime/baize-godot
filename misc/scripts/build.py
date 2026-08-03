#!/usr/bin/env python3
"""Cross-platform scons build wrapper for baize-godot.

Replaces build-windows.ps1 and build-macos.sh.
Usage:
    python misc/scripts/build.py --preset dev --jobs 16
    python misc/scripts/build.py -p pro -j 8 -- extra-scons-arg
"""

import argparse
import os
import subprocess
import sys

REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
)

PRESET_MAP: dict[str, dict[str, str]] = {
    "dev": {
        "windows": "misc/customization/scons-profiles/windows_3d_dev.py",
        "darwin": "misc/customization/scons-profiles/macos_3d_dev.py",
    },
    "pro": {
        "windows": "misc/customization/scons-profiles/windows_3d_pro.py",
        "darwin": "misc/customization/scons-profiles/macos_3d_pro.py",
    },
}


def detect_platform() -> str:
    if sys.platform.startswith("win"):
        return "windows"
    if sys.platform == "darwin":
        return "darwin"
    return "linux"


def main() -> None:
    parser = argparse.ArgumentParser(description="Build Godot engine (baize).")
    parser.add_argument("--preset", "-p", choices=["dev", "pro"], default="dev")
    parser.add_argument("--jobs", "-j", type=int, default=8)
    args, extra = parser.parse_known_args()

    platform = detect_platform()
    profile = PRESET_MAP[args.preset].get(platform)
    if not profile:
        print(f"Unsupported platform: {sys.platform}", file=sys.stderr)
        sys.exit(2)

    scons_args = [f"profile={profile}", f"-j{args.jobs}"] + extra
    os.chdir(REPO_ROOT)

    print(f"Running preset '{args.preset}': {' '.join(scons_args)}")

    # 引擎链接依赖 CEF 预构建产物（libcef_dll_wrapper.a）——先确保（已就绪时秒级跳过）。
    pre_stage = subprocess.run(
        [sys.executable, "misc/scripts/stage_webview.py", "--prebuild-only"],
        cwd=REPO_ROOT,
    )
    if pre_stage.returncode != 0:
        print("[build.py] ERROR: CEF 预构建失败（见上方 stage-webview 输出）", file=sys.stderr)
        sys.exit(pre_stage.returncode)

    try:
        subprocess.run(["scons"] + scons_args, check=True)
    except FileNotFoundError:
        subprocess.run(
            [sys.executable, "-m", "SCons.Script"] + scons_args, check=True
        )

    # 引擎构建后暂存 CEF 运行时：bin/（裸可执行文件流程）+ mac .app bundle 内
    # （scons generate_bundle 每次 rmtree 重建 bundle，必须在构建后重新暂存，
    # 否则启动台/双击启动的 .app 缺运行时——见 stage_webview.stage_bundles）。
    # 发布签名构建（bundle_sign_identity=... 传入 scons）时透传该身份，bundle 外层
    # 重签名沿用正式身份而非 ad-hoc（否则会降级 scons 的签名，见 stage_bundles）。
    sign_identity = ""
    for arg in extra:
        if arg.startswith("bundle_sign_identity="):
            sign_identity = arg.split("=", 1)[1]
            break
    post_stage_cmd = [sys.executable, "misc/scripts/stage_webview.py"]
    if sign_identity:
        post_stage_cmd.append(f"--sign-identity={sign_identity}")
    post_stage = subprocess.run(post_stage_cmd, cwd=REPO_ROOT)
    if post_stage.returncode != 0:
        print("[build.py] ERROR: CEF 运行时暂存失败（见上方 stage-webview 输出）", file=sys.stderr)
        sys.exit(post_stage.returncode)


if __name__ == "__main__":
    main()
