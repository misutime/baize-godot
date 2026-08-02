#!/usr/bin/env python
"""Stage webview artifacts into the editor distribution directory.

4A 路线（M1b+）暂存：
- webview-helper.exe + CEF 运行时（libcef.dll 等）→ <repo>/bin/（exe 旁，DLL 搜索与子进程路径）
- ui/ 页面产物 → <repo>/bin/webview/ui/（file:// 加载）

来源（路径直接关联，显式可见）：
- HELPER   = crates/target/<triple>/release/webview-helper.exe（cargo 构建产物）
- CEF_RUNTIME = <CEF_DIST>/<version>/cef_windows_x86_64/（cef-dist.txt 配置的分发包）
- UI       = ../refers/cef-smoke-test/ui（MVP 阶段；后续为 ui/ 工程构建输出）

用法：
  python misc/scripts/stage_webview.py
"""

import shutil
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
BIN_DIR = REPO_ROOT / "bin"
UI_SOURCE = REPO_ROOT.parent / "refers" / "cef-smoke-test" / "ui"
HELPER_SRC = (
    REPO_ROOT
    / "crates"
    / "target"
    / "x86_64-pc-windows-msvc"
    / "release"
    / "webview-helper.exe"
)
UI_DEST = BIN_DIR / "webview" / "ui"


def read_cef_dist() -> str:
    """读 crates/cef-dist.txt（与 SCsub 同一配置文件，保证构建/暂存选同一分发包根）。"""
    cfg = REPO_ROOT / "crates" / "cef-dist.txt"
    for line in cfg.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            return line
    raise SystemExit(f"[stage-webview] ERROR: {cfg} 无有效路径")


CEF_DIST = Path(read_cef_dist())


# CEF 运行时文件（与 libcef.dll 同级；需与编辑器 exe 同级以便 Windows DLL 搜索）。
CEF_RUNTIME_FILES = [
    "libcef.dll",
    "chrome_elf.dll",
    "libEGL.dll",
    "libGLESv2.dll",
    "d3dcompiler_47.dll",
    "dxcompiler.dll",
    "dxil.dll",
    "vk_swiftshader.dll",
    "vk_swiftshader_icd.json",
    "vulkan-1.dll",
    "icudtl.dat",
    "resources.pak",
    "chrome_100_percent.pak",
    "chrome_200_percent.pak",
    "v8_context_snapshot.bin",
    "locales",
]

# 必需页面文件（缺失即失败）。
REQUIRED_UI_FILES = ["bridge.html"]


def find_cef_runtime_dir() -> Path:
    """定位 CEF 分发包运行时目录（<CEF_DIST>/<version>/cef_windows_x86_64/）。"""
    if not CEF_DIST.is_dir():
        return Path()
    for version_dir in sorted(CEF_DIST.iterdir(), reverse=True):
        candidate = version_dir / "cef_windows_x86_64"
        if (candidate / "libcef.dll").is_file():
            return candidate
    return Path()


def stage_4a() -> int:
    """暂存 helper + CEF 运行时到 bin/（exe 旁）。"""
    if not HELPER_SRC.is_file():
        print(
            f"[stage-webview] ERROR: helper not found at {HELPER_SRC} — "
            "run `task dev` (or cargo build) first.",
            file=sys.stderr,
        )
        return 2

    cef_runtime = find_cef_runtime_dir()
    if not cef_runtime.is_dir():
        print(
            f"[stage-webview] ERROR: CEF runtime not found under {CEF_DIST} — "
            "check crates/cef-dist.txt and rebuild.",
            file=sys.stderr,
        )
        return 2

    shutil.copy2(HELPER_SRC, BIN_DIR / "webview-helper.exe")
    missing = []
    for name in CEF_RUNTIME_FILES:
        src = cef_runtime / name
        try:
            if src.is_dir():
                shutil.copytree(src, BIN_DIR / name, dirs_exist_ok=True)
            elif src.is_file():
                shutil.copy2(src, BIN_DIR / name)
            else:
                missing.append(name)
        except OSError as e:
            missing.append(f"{name} ({e})")
    if missing:
        print(f"[stage-webview] ERROR: CEF 运行时缺失: {missing}", file=sys.stderr)
        return 2

    UI_DEST.mkdir(parents=True, exist_ok=True)
    missing_ui = [f for f in REQUIRED_UI_FILES if not (UI_SOURCE / f).is_file()]
    if missing_ui:
        print(f"[stage-webview] ERROR: UI 文件缺失: {missing_ui}", file=sys.stderr)
        return 2
    for f in REQUIRED_UI_FILES:
        shutil.copy2(UI_SOURCE / f, UI_DEST / f)

    manifest = BIN_DIR / "webview" / "MANIFEST.txt"
    manifest.write_text(
        "\n".join(
            [
                f"staged_at: {time.strftime('%Y-%m-%d %H:%M:%S')}",
                f"helper_source: {HELPER_SRC}",
                f"cef_runtime: {cef_runtime}",
                f"ui_source: {UI_SOURCE if UI_SOURCE.is_dir() else '(missing)'}",
                "",
            ]
        ),
        encoding="utf-8",
    )
    print(f"[stage-webview] staged 4A artifacts -> {BIN_DIR}")
    return 0


def main() -> int:
    return stage_4a()


if __name__ == "__main__":
    sys.exit(main())
