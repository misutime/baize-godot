#!/usr/bin/env python3
"""Stage C++ 路线 webview 构建产物到编辑器分发目录。

方案 B(混合构建):本脚本自含"预构建"步骤——用 cmake 直接构建 CefViewWing 宿主进程
与 libcef_dll_wrapper(不经 SCons、不经 Godot mySpawn),然后暂存到 bin/ 分发目录:
- 预构建:cmake -S thirdparty/cefviewcore -B bin/obj/webview/cefviewcore → output/Release/
  (bin/: CefViewWing.exe + CEF 运行时;lib/: libcef_dll_wrapper.lib 等)
- 版本校验:build 目录记 cef-version.txt(首行 CEF_SDK_VERSION,次行 SDK 内容指纹);
  libcef_dll_wrapper.lib 存在且标记(版本+指纹)匹配 → 跳过 cmake 仅暂存;
  首次/换版本/SDK 内容变化(版本串未变)/产物缺失才真正构建。
- 暂存:先全量拷到 bin/.webview-stage-tmp/ 校验后原子切换(旧文件先移入备份、失败
  回滚),中途失败不破坏 bin/(编辑器 exe 旁:Windows DLL 搜索与 helper 子进程路径
  都需要 exe 同目录);ui/ 页面 → <repo>/bin/webview/ui/
  (editor_web_dock 用 file:///<exe_dir>/webview/ui/bridge.html 加载)

来源(路径直接关联,显式可见):
  BUILD_DIR    = bin/obj/webview/cefviewcore(cmake -B 固定路径,与 SCsub 切片一致)
  WING_RUNTIME = <BUILD_DIR>/output/Release/bin/(预构建产物:CefViewWing + CEF 运行时)
  WRAPPER_LIB  = <BUILD_DIR>/output/Release/lib/libcef_dll_wrapper.lib(SCsub 链接引用)
  UI_SOURCE    = ../refers/cef-smoke-test/ui(MVP 阶段页面产物;后续为 ui/ 工程构建输出)
  CEF 版本     = modules/webview/SCsub 的 CEF_SDK_VERSION 常量(单点,勿在多处硬编码)
  CEF SDK      = misc/scripts/cef_dist.py 缓存定位(默认 <repo>/bin/cef-dist/,CEF_DIST_ROOT 可覆盖,
                 缺失自动下载/手动放包,不再读 cef-dist 文本配置)

用法:
  python misc/scripts/stage_webview.py
"""

import hashlib
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

import cef_dist

REPO_ROOT = Path(__file__).resolve().parents[2]
BIN_DIR = REPO_ROOT / "bin"

# cmake -B 固定路径(与 SCsub 切片一致,增量构建快;产物在 output/Release/ 下)。
BUILD_DIR = REPO_ROOT / "bin" / "obj" / "webview" / "cefviewcore"
CEF_OUT_DIR = BUILD_DIR / "output" / "Release"
WING_RUNTIME = CEF_OUT_DIR / "bin"
CEF_LINK_LIB_DIR = CEF_OUT_DIR / "lib"
WRAPPER_LIB = CEF_LINK_LIB_DIR / "libcef_dll_wrapper.lib"
# 版本标记:记录构建时的 CEF_SDK_VERSION(首行),用于跳过判定;换版本自动重建。
VERSION_MARKER = BUILD_DIR / "cef-version.txt"
UI_SOURCE = REPO_ROOT.parent / "refers" / "cef-smoke-test" / "ui"
WEBVIEW_DEST = BIN_DIR / "webview"
UI_DEST = WEBVIEW_DEST / "ui"
SCSUB = REPO_ROOT / "modules" / "webview" / "SCsub"
CEFVIEW_WING_NAME = "CefViewWing"

# 应用产物（CefViewWing.exe 及其调试符号；与 CEF 运行时同目录，一并拷到 bin/）。
WING_FILES = [
    "CefViewWing.exe",
    "CefViewWing.pdb",
]

# CEF 运行时文件（与 libcef.dll 同级；需与编辑器 exe 同级以便 Windows DLL 搜索，
# helper 子进程路径也指向 exe_dir）。目录条目递归拷贝，单文件直接拷。
CEF_RUNTIME_FILES = [
    "libcef.dll",
    "bootstrap.exe",
    "bootstrapc.exe",
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


def read_cef_constants() -> str:
    """从 modules/webview/SCsub 读取 CEF_SDK_VERSION(构建/暂存共用单点常量)。

    新缓存结构按版本目录组织(无 dist-name 层),只取 CEF_SDK_VERSION;
    SCsub 若保留 CEF_DIST_DIR_NAME(删除切片的依赖)则忽略。读取失败返回 "unknown"。
    """
    try:
        text = SCSUB.read_text(encoding="utf-8")
    except OSError:
        text = ""
    m = re.search(r'CEF_SDK_VERSION\s*=\s*"([^"]+)"', text)
    sdk_version = m.group(1) if m else ""
    if not sdk_version:
        print(
            f"[stage-webview] WARNING: 无法从 {SCSUB} 读取 CEF_SDK_VERSION，版本记 unknown",
            file=sys.stderr,
        )
    return sdk_version or "unknown"


def read_cef_version() -> str:
    """读取 CEF_SDK_VERSION(MANIFEST 与预构建版本判定共用)。"""
    return read_cef_constants()


def _read_marker_version() -> str:
    """读版本标记首行(构建时的 CEF_SDK_VERSION);无标记/损坏返回空串。"""
    try:
        return VERSION_MARKER.read_text(encoding="utf-8").splitlines()[0].strip()
    except (OSError, IndexError):
        return ""


def _read_marker_sdk_fingerprint() -> str:
    """读版本标记第二行(SDK 内容指纹);无标记/损坏返回空串。"""
    try:
        lines = VERSION_MARKER.read_text(encoding="utf-8").splitlines()
        return lines[1].strip() if len(lines) > 1 else ""
    except (OSError, IndexError):
        return ""


def sdk_fingerprint(sdk_dir: Path) -> str:
    """SDK 内容指纹(SHA-256):include/ 全部头文件 + Release/libcef.lib。

    覆盖完整 API 面(头文件)与链接导入库;SDK 内容变化(版本串未变)会反映到指纹,
    从而触发重新预构建,避免复用旧 ABI 产物。文件按相对路径排序,结果确定。
    返回空串表示 SDK 目录不可用(此时跳过判定不成立,必然走重建)。
    """
    if not sdk_dir.is_dir():
        return ""
    hasher = hashlib.sha256()
    files = []
    inc = sdk_dir / "include"
    if inc.is_dir():
        files.extend(sorted(p.relative_to(sdk_dir).as_posix() for p in inc.rglob("*") if p.is_file()))
    lib = sdk_dir / "Release" / "libcef.lib"
    if lib.is_file():
        files.append(lib.relative_to(sdk_dir).as_posix())
    for rel in files:
        hasher.update(rel.encode("utf-8"))
        with (sdk_dir / rel).open("rb") as f:
            for chunk in iter(lambda: f.read(1024 * 1024), b""):
                hasher.update(chunk)
    return hasher.hexdigest()


def ensure_wrapper_lib() -> Path:
    """确保 libcef_dll_wrapper.lib 位于约定链接路径(output/Release/lib/),供 SCsub 链接引用。

    正常由 CMake 的 CMAKE_ARCHIVE_OUTPUT_DIRECTORY 直接产出;若未落到该路径,
    从构建目录搜索并拷贝到约定路径。找不到则抛 RuntimeError。
    """
    if WRAPPER_LIB.is_file():
        return WRAPPER_LIB
    hits = list(BUILD_DIR.rglob("libcef_dll_wrapper.lib"))
    if not hits:
        raise RuntimeError(f"cmake 构建结束但未生成 libcef_dll_wrapper.lib(已搜索 {BUILD_DIR})")
    CEF_LINK_LIB_DIR.mkdir(parents=True, exist_ok=True)
    shutil.copy2(hits[0], WRAPPER_LIB)
    return WRAPPER_LIB


def prebuild(cef_version: str) -> bool:
    """预构建 CefViewWing + libcef_dll_wrapper(cmake 直接调用,不经 SCons)。

    跳过判定:WRAPPER_LIB 存在 AND 版本标记 == 当前 CEF_SDK_VERSION AND SDK 内容指纹一致
    → 跳过构建(仅暂存)。SDK 指纹覆盖 API 头文件与导入库,CEF 内容变化(版本串未变)
    也触发重建,杜绝复用旧 ABI 产物。其余情况(首次/换版本/SDK 变化/产物缺失)执行
    cmake 配置+构建;构建失败抛异常由 main 显式报错退出。
    返回是否实际执行了构建。
    """
    # SDK 定位经共享模块 cef_dist(CEF_DIST_ROOT 覆盖 / <repo>/bin/cef-dist/ 缓存,
    # 缺失自动下载),不再读 cef-dist 文本配置。
    sdk_dir = cef_dist.ensure_sdk(REPO_ROOT, cef_version, allow_download=True)
    sdk_fp = sdk_fingerprint(sdk_dir)
    marker_version = _read_marker_version()
    marker_fp = _read_marker_sdk_fingerprint()
    if sdk_fp and WRAPPER_LIB.is_file() and marker_version == cef_version and marker_fp == sdk_fp:
        print(
            f"[stage-webview] CEF 预构建产物已就绪(version={cef_version}, "
            f"sdk_fp={sdk_fp[:12]}…, {WRAPPER_LIB}),跳过 cmake 构建"
        )
        return False

    if WRAPPER_LIB.is_file():
        if marker_version != cef_version:
            reason = f"CEF 版本变化({marker_version or '(无标记)'} -> {cef_version})"
        else:
            reason = f"SDK 内容变化(指纹 {marker_fp[:12] or '(无)'} -> {sdk_fp[:12]})"
        print(f"[stage-webview] {reason}，重新预构建")
    else:
        print(f"[stage-webview] 未找到预构建产物 {WRAPPER_LIB}，开始 cmake 预构建")

    # stdout/stderr 直接继承终端(subprocess.run 默认),不走 PIPE,无大输出管道阻塞问题。
    cmake_cfg = [
        "cmake",
        "-S", str(REPO_ROOT / "thirdparty" / "cefviewcore"),
        "-B", str(BUILD_DIR),
        "-A", "x64",
        "-DPROJECT_ARCH=x86_64",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_CXX_STANDARD=20",
        "-DSTATIC_CRT=ON",
        f"-DCEF_SDK_VERSION={cef_version}",
        f"-DCUSTOM_CEF_SDK_DIR={sdk_dir}",
        f"-DCEFVIEW_WING_NAME={CEFVIEW_WING_NAME}",
    ]
    print(f"[stage-webview] cmake 配置: {' '.join(cmake_cfg)}")
    subprocess.run(cmake_cfg, cwd=REPO_ROOT, check=True)
    print("[stage-webview] cmake 构建 Release(含 CefViewWing + libcef_dll_wrapper)...")
    subprocess.run(
        ["cmake", "--build", str(BUILD_DIR), "--config", "Release"],
        cwd=REPO_ROOT,
        check=True,
    )

    ensure_wrapper_lib()
    # 版本标记:首行 CEF_SDK_VERSION(跳过判定用),次行 SDK 内容指纹,末行构建时间(诊断用)。
    VERSION_MARKER.write_text(
        f"{cef_version}\n{sdk_fp}\n{time.strftime('%Y-%m-%d %H:%M:%S')}\n",
        encoding="utf-8",
    )
    print(f"[stage-webview] 预构建完成: {WING_RUNTIME} + {WRAPPER_LIB}(标记写入 {VERSION_MARKER})")
    return True


def stage_runtime(bin_dir: Path) -> list:
    """原子暂存 CefViewWing + CEF 运行时到 bin/(exe 旁)。返回缺失文件清单。

    先全量拷贝到 bin/.webview-stage-tmp/(同卷),校验无缺失后再切换:旧文件先改名进
    bin/.webview-stage-old(备份),新文件 rename 到目标;任一步失败回滚备份——
    避免"先删 bin 再拷"中途失败破坏可用的编辑器分发目录。
    """
    tmp = bin_dir / ".webview-stage-tmp"
    backup = bin_dir / ".webview-stage-old"
    for d in (tmp, backup):
        if d.exists():
            shutil.rmtree(d)
    tmp.mkdir(parents=True)

    missing = []
    for name in WING_FILES + CEF_RUNTIME_FILES:
        src = WING_RUNTIME / name
        try:
            if src.is_dir():
                shutil.copytree(src, tmp / name)
            elif src.is_file():
                shutil.copy2(src, tmp / name)
            else:
                missing.append(name)
        except OSError as e:
            missing.append(f"{name} ({e})")
    if missing:
        shutil.rmtree(tmp)
        return missing

    names = [n for n in WING_FILES + CEF_RUNTIME_FILES if (tmp / n).exists()]
    backup.mkdir(parents=True)
    installed = []
    try:
        for name in names:
            target = bin_dir / name
            if target.exists() or target.is_symlink():
                os.replace(target, backup / name)  # 旧文件暂存备份(可回滚)
            os.replace(tmp / name, target)  # 同卷原子改名
            installed.append(name)
    except OSError as e:
        failed_name = name  # 失败时正在处理的条目（回滚循环会重绑 name，先保存）
        # 回滚:已安装位置先清掉,再还原备份的旧文件,恢复可用分发。
        for rollback_name in reversed(installed):
            target = bin_dir / rollback_name
            if target.is_dir():
                shutil.rmtree(target, ignore_errors=True)
            elif target.exists() or target.is_symlink():
                target.unlink()
            old = backup / rollback_name
            if old.exists() or old.is_symlink():
                os.replace(old, target)
        shutil.rmtree(tmp, ignore_errors=True)
        shutil.rmtree(backup, ignore_errors=True)
        return [f"切换失败 {failed_name} ({e})"]

    # 成功:清理备份与临时目录;清单外旧条目(版本切换残留)与旧行为一致地删除。
    shutil.rmtree(backup, ignore_errors=True)
    shutil.rmtree(tmp, ignore_errors=True)
    for name in WING_FILES + CEF_RUNTIME_FILES:
        if name in names:
            continue
        p = bin_dir / name
        if p.is_dir():
            shutil.rmtree(p, ignore_errors=True)
        elif p.is_file() or p.is_symlink():
            p.unlink(missing_ok=True)
    return missing


def stage_ui() -> list:
    """全量重建 bin/webview/(原子:先建 .tmp 再整体替换)并拷页面产物。

    UI 缺失只警告（页面 404，CEF 核心仍可运行）。
    """
    tmp = BIN_DIR / ".webview-ui-tmp"
    if tmp.exists():
        shutil.rmtree(tmp)
    if not UI_SOURCE.is_dir():
        return ["ui 源目录缺失"]
    tmp.mkdir(parents=True)
    for f in sorted(UI_SOURCE.glob("*.html")):
        shutil.copy2(f, tmp / f.name)
    if not (tmp / "bridge.html").is_file():
        shutil.rmtree(tmp, ignore_errors=True)
        return ["bridge.html"]
    if WEBVIEW_DEST.exists():
        shutil.rmtree(WEBVIEW_DEST)
    os.replace(tmp, WEBVIEW_DEST)
    return []


def main() -> int:
    cef_version = read_cef_version()
    try:
        prebuild(cef_version)
    except cef_dist.CefDistError as e:
        print(f"[stage-webview] ERROR: CEF SDK 定位/下载失败: {e}", file=sys.stderr)
        return 2
    except (subprocess.SubprocessError, OSError, RuntimeError) as e:
        print(f"[stage-webview] ERROR: CEF 预构建失败: {e}", file=sys.stderr)
        return 2

    if not WING_RUNTIME.is_dir():
        print(
            f"[stage-webview] ERROR: 预构建产物目录不存在: {WING_RUNTIME} — "
            "cmake 未产出 CEF 运行时。",
            file=sys.stderr,
        )
        return 2

    missing = stage_runtime(BIN_DIR)
    if missing:
        print(f"[stage-webview] ERROR: CEF/应用产物缺失: {missing}", file=sys.stderr)
        return 2

    ui_missing = stage_ui()
    if ui_missing:
        print(
            f"[stage-webview] WARNING: UI 页面缺失 ({', '.join(ui_missing)}) — "
            f"dock 页面将 404，CEF 核心可正常运行。来源: {UI_SOURCE}",
            file=sys.stderr,
        )

    manifest = WEBVIEW_DEST / "MANIFEST.txt"
    file_count = sum(1 for _ in WEBVIEW_DEST.rglob("*") if _.is_file())
    manifest.write_text(
        "\n".join(
            [
                f"staged_at: {time.strftime('%Y-%m-%d %H:%M:%S')}",
                f"wing_runtime_source: {WING_RUNTIME}",
                f"cef_version: {cef_version}",
                f"ui_source: {UI_SOURCE if UI_SOURCE.is_dir() else '(missing)'}",
                f"file_count: {file_count}",
                "",
            ]
        ),
        encoding="utf-8",
    )
    print(f"[stage-webview] staged C++ webview artifacts -> {BIN_DIR} (ui -> {UI_DEST})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
