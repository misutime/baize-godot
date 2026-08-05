#!/usr/bin/env python3
"""Stage C++ 路线 webview 构建产物到编辑器分发目录。

方案 B(混合构建):本脚本自含"预构建"步骤——用 cmake 直接构建 CefViewWing 宿主进程
与 libcef_dll_wrapper(不经 SCons、不经 Godot mySpawn),然后暂存到 bin/ 分发目录:
- 预构建:cmake -S thirdparty/cefviewcore -B bin/obj/webview/cefviewcore → output/Release/
  (Windows:bin/ 下 CefViewWing.exe + CEF 运行时;mac:5 个 helper .app bundle + framework)
- 版本校验:build 目录记 cef-version.txt(首行 CEF_SDK_VERSION,次行 SDK 内容指纹);
  libcef_dll_wrapper 存在且标记(版本+指纹)匹配 → 跳过 cmake 仅暂存;
  首次/换版本/SDK 内容变化(版本串未变)/产物缺失才真正构建。
- 平台:构建宿主自动判定(cef_dist.sdk_dir_suffix),Windows x64 与 macOS arm64/x64 双平台。
- 暂存:先全量拷到 bin/.webview-stage-tmp/ 校验后原子切换(旧文件先移入备份、失败
  回滚),中途失败不破坏 bin/(Windows:编辑器 exe 旁 DLL 搜索;mac:framework 与 helper
  bundle 需与 exe 同级);ui/ 页面 → <repo>/bin/webview/ui/
  (editor_web_dock 用 file:///<exe_dir>/webview/ui/index.html 加载 React 壳产物)

来源(路径直接关联,显式可见):
  BUILD_DIR    = bin/obj/webview/cefviewcore(cmake -B 固定路径,与 SCsub 切片一致)
  WING_RUNTIME = <BUILD_DIR>/output/Release/bin/(预构建产物:helper + CEF 运行时)
  WRAPPER_LIB  = <BUILD_DIR>/output/Release/lib/libcef_dll_wrapper.{lib|a}(SCsub 链接引用)
  UI 源      = web/ui/dist（React 壳，task ui-build 构建；editor_web_dock 加载 index.html）
  CEF 版本     = modules/att_webview/SCsub 的 CEF_SDK_VERSION 常量(单点,勿在多处硬编码)
  CEF SDK      = misc/scripts/cef_dist.py 缓存定位(默认 <repo>/bin/cef-dist/,CEF_DIST_ROOT 可覆盖,
                 缺失自动下载/手动放包,不再读 cef-dist 文本配置)

用法:
  python misc/scripts/stage_webview.py
"""

import hashlib
import os
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import cef_dist

REPO_ROOT = Path(__file__).resolve().parents[2]
BIN_DIR = REPO_ROOT / "bin"


def _host_arch() -> str:
    """构建宿主 CPU 架构(arm64/x86_64),供 cmake PROJECT_ARCH。与 cef_dist 平台判定同源。"""
    import platform as _sys_platform

    machine = _sys_platform.machine().lower()
    if machine in ("arm64", "aarch64"):
        return "arm64"
    return "x86_64"

# cmake -B 固定路径(与 SCsub 切片一致,增量构建快;产物在 output/Release/ 下)。
BUILD_DIR = REPO_ROOT / "bin" / "obj" / "webview" / "cefviewcore"
CEF_OUT_DIR = BUILD_DIR / "output" / "Release"
WING_RUNTIME = CEF_OUT_DIR / "bin"
CEF_LINK_LIB_DIR = CEF_OUT_DIR / "lib"

# 平台分支(双平台支持:Windows x64 MSVC 保持原状;macOS arm64/x64 新增)。
# 平台判定与 CEF 包后缀共用 cef_dist(同一套宿主检测),保证 URL/产物/缓存一致。
IS_WINDOWS = cef_dist.sdk_dir_suffix() == "windows64"
WRAPPER_LIB_NAME = "libcef_dll_wrapper.lib" if IS_WINDOWS else "libcef_dll_wrapper.a"
WRAPPER_LIB = CEF_LINK_LIB_DIR / WRAPPER_LIB_NAME
# SDK 指纹的“导入库”文件(Windows:libcef.lib;mac:framework 二进制——mac SDK 的
# Release/ 只有 framework,链接不直接用 libcef,运行时由 cef_load_library 加载)。
SDK_LIB_REL = "Release/libcef.lib" if IS_WINDOWS else "Release/Chromium Embedded Framework.framework/Chromium Embedded Framework"

# 版本标记:记录构建时的 CEF_SDK_VERSION(首行),用于跳过判定;换版本自动重建。
VERSION_MARKER = BUILD_DIR / "cef-version.txt"
# UI 暂存统一走 stage_ui.py（React 壳 web/ui/dist → bin/webview/ui/），此处仅保留目标路径。
WEBVIEW_DEST = BIN_DIR / "webview"
UI_DEST = WEBVIEW_DEST / "ui"
SCSUB = REPO_ROOT / "modules" / "att_webview" / "SCsub"
CEFVIEW_WING_NAME = "CefViewWing"

# 应用产物(Windows:CefViewWing.exe 及其调试符号;mac:helper app bundle——CEF 按
# CEF_HELPER_APP_SUFFIXES 从 browser_subprocess_path 推导 (Alerts)/(GPU)/(Plugin)/
# (Renderer) 后缀 bundle,故 5 个都要随 framework 同级分发)。目录条目递归拷贝,单文件直接拷。
WING_FILES = (
    ["CefViewWing.exe", "CefViewWing.pdb"]
    if IS_WINDOWS
    else [
        "CefViewWing.app",
        "CefViewWing (Alerts).app",
        "CefViewWing (GPU).app",
        "CefViewWing (Plugin).app",
        "CefViewWing (Renderer).app",
    ]
)

# CEF 运行时文件(Windows:与 libcef.dll 同级;需与编辑器 exe 同级以便 Windows DLL 搜索,
# helper 子进程路径也指向 exe_dir。mac:framework 自含资源,只需 framework 与 exe 同级,
# 主机进程 cef_load_library 与 helper 的 LoadCefLibrary 均按该布局解析)。
CEF_RUNTIME_FILES = (
    [
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
    if IS_WINDOWS
    else ["Chromium Embedded Framework.framework"]
)


def read_cef_constants() -> str:
    """从 modules/att_webview/SCsub 读取 CEF_SDK_VERSION(构建/暂存共用单点常量)。

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


def _read_marker_build_opts() -> str:
    """读版本标记第三行(构建选项指纹);无标记/损坏返回空串。"""
    try:
        lines = VERSION_MARKER.read_text(encoding="utf-8").splitlines()
        return lines[2].strip() if len(lines) > 2 else ""
    except (OSError, IndexError):
        return ""


def _cmake_platform_args() -> list:
    """平台专属 cmake 参数(Windows 保持原状;mac 开 USE_SANDBOX——见 prebuild 注释)。"""
    if IS_WINDOWS:
        return [
            "-A", "x64",
            "-DPROJECT_ARCH=x86_64",
            "-DSTATIC_CRT=ON",
        ]
    return [
        f"-DPROJECT_ARCH={_host_arch()}",
        "-DUSE_SANDBOX=ON",
    ]


def build_opts_fingerprint() -> str:
    """构建选项指纹(SHA-256 of 平台 cmake 参数):跳过判定与写入共用,换选项触发重建。"""
    return hashlib.sha256("\n".join(_cmake_platform_args()).encode("utf-8")).hexdigest()


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
    lib = sdk_dir / SDK_LIB_REL
    if lib.is_file():
        files.append(lib.relative_to(sdk_dir).as_posix())
    for rel in files:
        hasher.update(rel.encode("utf-8"))
        with (sdk_dir / rel).open("rb") as f:
            for chunk in iter(lambda: f.read(1024 * 1024), b""):
                hasher.update(chunk)
    return hasher.hexdigest()


def ensure_wrapper_lib() -> Path:
    """确保 libcef_dll_wrapper(静态库)位于约定链接路径(output/Release/lib/),供 SCsub 链接引用。

    正常由 CMake 的 CMAKE_ARCHIVE_OUTPUT_DIRECTORY 直接产出;若未落到该路径,
    从构建目录搜索并拷贝到约定路径。找不到则抛 RuntimeError。
    """
    if WRAPPER_LIB.is_file():
        return WRAPPER_LIB
    hits = list(BUILD_DIR.rglob(WRAPPER_LIB_NAME))
    if not hits:
        raise RuntimeError(f"cmake 构建结束但未生成 {WRAPPER_LIB_NAME}(已搜索 {BUILD_DIR})")
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
    marker_build_opts = _read_marker_build_opts()
    if sdk_fp and WRAPPER_LIB.is_file() and marker_version == cef_version and marker_fp == sdk_fp and marker_build_opts == build_opts_fingerprint():
        print(
            f"[stage-webview] CEF 预构建产物已就绪(version={cef_version}, "
            f"sdk_fp={sdk_fp[:12]}…, {WRAPPER_LIB}),跳过 cmake 构建"
        )
        return False

    if WRAPPER_LIB.is_file():
        if marker_version != cef_version:
            reason = f"CEF 版本变化({marker_version or '(无标记)'} -> {cef_version})"
        elif marker_fp != sdk_fp:
            reason = f"SDK 内容变化(指纹 {marker_fp[:12] or '(无)'} -> {sdk_fp[:12]})"
        else:
            reason = "构建选项变化(USE_SANDBOX 等)"
        print(f"[stage-webview] {reason}，重新预构建")
    else:
        print(f"[stage-webview] 未找到预构建产物 {WRAPPER_LIB}，开始 cmake 预构建")

    # stdout/stderr 直接继承终端(subprocess.run 默认),不走 PIPE,无大输出管道阻塞问题。
    cmake_cfg = [
        "cmake",
        "-S", str(REPO_ROOT / "thirdparty" / "cefviewcore"),
        "-B", str(BUILD_DIR),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_CXX_STANDARD=20",
        f"-DCEF_SDK_VERSION={cef_version}",
        f"-DCUSTOM_CEF_SDK_DIR={sdk_dir}",
        f"-DCEFVIEW_WING_NAME={CEFVIEW_WING_NAME}",
    ] + _cmake_platform_args()
    print(f"[stage-webview] cmake 配置: {' '.join(cmake_cfg)}")
    subprocess.run(cmake_cfg, cwd=REPO_ROOT, check=True)
    print("[stage-webview] cmake 构建 Release(含 CefViewWing + libcef_dll_wrapper)...")
    subprocess.run(
        ["cmake", "--build", str(BUILD_DIR), "--config", "Release"],
        cwd=REPO_ROOT,
        check=True,
    )

    ensure_wrapper_lib()
    # 版本标记:首行 CEF_SDK_VERSION(跳过判定用),次行 SDK 内容指纹,三行构建选项指纹
    # (平台 cmake 参数——换选项如 USE_SANDBOX 也触发重建,防静默复用旧产物),末行时间。
    VERSION_MARKER.write_text(
        f"{cef_version}\n{sdk_fp}\n{build_opts_fingerprint()}\n{time.strftime('%Y-%m-%d %H:%M:%S')}\n",
        encoding="utf-8",
    )
    print(f"[stage-webview] 预构建完成: {WING_RUNTIME} + {WRAPPER_LIB}(标记写入 {VERSION_MARKER})")
    return True


# helper bundle 的 bundle id(mac)。上游 CMakeLists 按 CEF_HELPER_APP_SUFFIXES 给每个
# helper 配了带后缀的 id(com.cefview.CefViewWing.gpu 等)——这会破坏 Chromium 的
# mach rendezvous:服务名 = BaseBundleID.MachPortRendezvousServer.<pid>,浏览器与 helper
# 各用自己进程的 BaseBundleID 构造,id 不一致则 bootstrap_look_up 失败、helper 启动即死。
# Chromium 标准做法:所有 helper 与主程序共享同一 bundle id。故统一为无后缀的
# com.cefview.CefViewWing(浏览器侧经 CefSettings.main_bundle_path 指向基础 helper
# bundle 取得同一 id,见 webview_core.cpp)。
_HELPER_BUNDLE_ID = f"com.cefview.{CEFVIEW_WING_NAME}"

# helper bundle 名列表(与 WING_FILES 的 mac 分支一致)。
_HELPER_BUNDLE_NAMES = [
    "CefViewWing.app",
    "CefViewWing (Alerts).app",
    "CefViewWing (GPU).app",
    "CefViewWing (Plugin).app",
    "CefViewWing (Renderer).app",
]


def patch_helper_plists(root: Path) -> None:
    """补齐 helper bundle Info.plist 的 Xcode 占位符并重新签名(仅 mac;Windows 无此问题)。

    CefWing/mac/info.plist 模板用 $(EXECUTABLE_NAME)/$(PRODUCT_BUNDLE_IDENTIFIER) 等
    Xcode 占位符,cmake 的 file(WRITE) 原样复制——只有 Xcode generator 会替换,Unix
    Makefiles/Ninja 下保持字面量。未展开的 CFBundleIdentifier 会让 mach rendezvous
    的 bootstrap 服务名变成字面量 "$(PRODUCT_BUNDLE_IDENTIFIER)",helper 连不上父进程
    (bootstrap_look_up Unknown service name → 启动即死),CEF 主进程随后 FATAL。
    暂存后把占位符替换为具体值,并统一 bundle id(见 _HELPER_BUNDLE_ID 注释)。

    重新签名:Apple Silicon 上 V8(renderer)与 GPU/Metal 需要 MAP_JIT,内核要求进程
    签名含 com.apple.security.cs.allow-jit entitlement(ad-hoc 签名=空 entitlement →
    v8_internal_simulator_ProbeMemory SIGTRAP / GPU process exit 5)。cmake 只做了
    链接器 ad-hoc 签名且不含 entitlements;plist 修改后原签名也已失效,统一在此
    用仓库内的 CefViewWing.entitlements(allow-jit + allow-unsigned-executable-memory
    + disable-library-validation)重新 ad-hoc 签名。
    """
    if IS_WINDOWS:
        return
    entitlements = REPO_ROOT / "thirdparty" / "cefviewcore" / "src" / "CefWing" / "mac" / "CefViewWing.entitlements"
    # entitlements 是 mac helper 签名的必需输入(缺 allow-jit 等 entitlement,Apple Silicon 上
    # renderer/GPU 会崩溃)——缺失必须显式失败,不静默跳过签名。
    if not entitlements.is_file():
        raise RuntimeError(f"mac helper 签名所需的 entitlements 缺失:{entitlements}")
    for bundle_name in _HELPER_BUNDLE_NAMES:
        bundle = root / bundle_name
        plist = bundle / "Contents" / "Info.plist"
        # plist 缺失/不可读 = 生成物损坏,必须显式失败(否则 bundle 以未补全/未签名状态安装,
        # 运行期 helper 因无效元数据无法启动)。
        if not plist.is_file():
            raise RuntimeError(f"helper bundle 缺少 Info.plist:{plist}(cmake 生成物损坏?)")
        # 可执行文件名 = bundle 名去 .app(上游按 CEF_HELPER_APP_SUFFIXES 的 OUTPUT_NAME
        # 命名,如 "CefViewWing (Renderer)";CFBundleExecutable 必须与之匹配,
        # 否则 codesign 与 LaunchServices 都会认错主可执行文件)。
        exe_name = bundle_name[:-4]  # strip ".app"
        try:
            text = plist.read_text(encoding="utf-8")
        except OSError as e:
            raise RuntimeError(f"读取 helper Info.plist 失败:{plist}: {e}") from e
        replaced = (
            text.replace("$(EXECUTABLE_NAME)", exe_name)
            .replace("$(PRODUCT_NAME)", CEFVIEW_WING_NAME)
            .replace("$(PRODUCT_BUNDLE_IDENTIFIER)", _HELPER_BUNDLE_ID)
            .replace("$(PRODUCT_BUNDLE_PACKAGE_TYPE)", "APPL")
            .replace("$(DEVELOPMENT_LANGUAGE)", "en")
            .replace("$(MACOSX_DEPLOYMENT_TARGET)", "12.0")
        )
        if replaced != text:
            plist.write_text(replaced, encoding="utf-8")
        # 上游 .entitlements 含 DOCTYPE,AMFI 解析器拒绝;plistlib 重写为无 DOCTYPE 的
        # 标准 XML,写临时文件供 codesign(原文件保持只读不动)。
        tmp_ent = None
        try:
            with open(entitlements, "rb") as f:
                ent_dict = plistlib.load(f)
            fd, tmp_ent = tempfile.mkstemp(suffix=".plist")
            with os.fdopen(fd, "wb") as f:
                plistlib.dump(ent_dict, f)
            subprocess.run(
                ["codesign", "--force", "--deep", "--sign", "-", "--entitlements", tmp_ent, str(bundle)],
                check=True,
            )
        finally:
            if tmp_ent:
                os.unlink(tmp_ent)


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

    # mac:helper bundle 的 Info.plist 占位符补全(Unix Makefiles/Ninja 生成器不替换
    # Xcode 变量;未展开的 CFBundleIdentifier 会让 helper 的 mach rendezvous 失败)。
    patch_helper_plists(tmp)

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
    """暂存 React 壳产物到 bin/webview/ui/（复用 stage_ui.py 单一入口）。

    UI 缺失只警告（页面 404，CEF 核心仍可运行）；页面为 <exe_dir>/webview/ui/index.html
    （editor_web_dock 加载，React 壳构建产物）。
    """
    from stage_ui import stage_ui_dist

    return stage_ui_dist()


def stage_bundles(bin_dir: Path, sign_identity: str = "") -> list:
    """mac 专用：把 CEF 运行时 + UI 暂存进 .app bundle（.app 自包含，启动台/双击可用）。

    CEF mac 标准布局：framework + 5 helper bundle 在 <App>.app/Contents/Frameworks/，
    UI/字体在 <App>.app/Contents/Resources/webview/ui/。浏览器进程的 seatbelt 沙箱只放行
    main bundle 内文件读——运行时在 bundle 外（bin/）时 helper 子进程 dlopen framework
    会被拒（实测）。scons generate_bundle 每次构建 rmtree 重建 bundle，故本函数必须在
    引擎构建**之后**运行（build.py 已内置该顺序）。
    sign_identity：发布签名构建（scons bundle_sign_identity=...）时传身份，外层 bundle
    重签名沿用该身份 + hardened runtime + 对应编辑器 entitlements（与 generate_bundle
    一致，dev/pro 按 bundle 名区分）；为空时 ad-hoc 签名（当前开发构建行为）。
    返回缺失/失败清单（空 = 成功）。
    """
    if IS_WINDOWS:
        return []
    bundles = sorted(bin_dir.glob("godot_macos_editor*.app"))
    if not bundles:
        print(
            "[stage-webview] WARNING: 未找到 mac 编辑器 bundle "
            "(bin/godot_macos_editor*.app)，跳过 bundle 内暂存（裸可执行文件流程不受影响）",
            file=sys.stderr,
        )
        return []

    missing = []
    for bundle in bundles:
        frameworks_dir = bundle / "Contents" / "Frameworks"
        resources_ui_dir = bundle / "Contents" / "Resources" / "webview" / "ui"
        # 1) framework + helpers → Contents/Frameworks（tmp 内补 plist 重签名后换入；
        #    旧树先原子改名备份，换入失败回滚，不破坏先前可用的运行时）。
        tmp = bundle / "Contents" / ".frameworks-stage-tmp"
        if tmp.exists():
            shutil.rmtree(tmp)
        tmp.mkdir(parents=True)
        bundle_missing = []
        for name in WING_FILES + CEF_RUNTIME_FILES:
            src = WING_RUNTIME / name
            try:
                if src.is_dir():
                    shutil.copytree(src, tmp / name)
                elif src.is_file():
                    shutil.copy2(src, tmp / name)
                else:
                    bundle_missing.append(f"{bundle.name}: {name}")
            except OSError as e:
                bundle_missing.append(f"{bundle.name}: {name} ({e})")
        if bundle_missing:
            shutil.rmtree(tmp, ignore_errors=True)
            missing.extend(bundle_missing)
            continue
        patch_helper_plists(tmp)
        old_fw = bundle / "Contents" / ".frameworks-old"
        if old_fw.exists():
            shutil.rmtree(old_fw)
        if frameworks_dir.exists():
            os.replace(frameworks_dir, old_fw)  # 旧树原子改名备份
        try:
            os.replace(tmp, frameworks_dir)  # 新树原子换入
        except OSError as e:
            if frameworks_dir.exists():
                shutil.rmtree(frameworks_dir, ignore_errors=True)
            if old_fw.exists():
                os.replace(old_fw, frameworks_dir)  # 回滚备份
            missing.append(f"{bundle.name}: Frameworks 换入失败 ({e})")
            continue
        if old_fw.exists():
            shutil.rmtree(old_fw, ignore_errors=True)

        # 2) UI → Contents/Resources/webview/ui（与 bin/webview/ui 同源；失败回滚且仅警告，
        #    与 stage_ui 的“UI 缺失只警告”契约一致——核心运行时不受影响）。
        if UI_DEST.is_dir():
            old_ui = bundle / "Contents" / "Resources" / "webview" / ".ui-old"
            if old_ui.exists():
                shutil.rmtree(old_ui)
            if resources_ui_dir.exists():
                os.replace(resources_ui_dir, old_ui)
            try:
                resources_ui_dir.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(UI_DEST, resources_ui_dir)
            except OSError as e:
                if resources_ui_dir.exists():
                    shutil.rmtree(resources_ui_dir, ignore_errors=True)
                if old_ui.exists():
                    os.replace(old_ui, resources_ui_dir)
                print(
                    f"[stage-webview] WARNING: bundle {bundle.name} UI 换入失败（保留旧 UI）: {e}",
                    file=sys.stderr,
                )
            else:
                if old_ui.exists():
                    shutil.rmtree(old_ui, ignore_errors=True)
        else:
            print(
                f"[stage-webview] WARNING: UI 页面缺失 ({UI_DEST})，bundle {bundle.name} 内不装 UI（页面将 404）",
                file=sys.stderr,
            )

        # 3) 外层 bundle 重签名（暂存改变了密封内容）：
        #    - sign_identity 为空：ad-hoc（当前开发构建，与 scons 不签名一致）；
        #    - sign_identity 非空：沿用该身份 + hardened runtime + 对应编辑器
        #      entitlements（镜像 generate_bundle 的参数，dev/pro 按 bundle 名区分）；
        #    均不用 --deep——嵌套 helper 已由 patch_helper_plists 带 allow-jit 等
        #    entitlements 签名，--deep 会覆盖掉它们。
        try:
            if sign_identity:
                ent_name = "editor_debug.entitlements" if bundle.name.endswith("_dev.app") else "editor.entitlements"
                sign_cmd = [
                    "codesign", "--force", "--sign", sign_identity,
                    "--options=runtime",
                    "--entitlements", str(REPO_ROOT / "misc" / "dist" / "macos" / ent_name),
                    str(bundle),
                ]
            else:
                sign_cmd = ["codesign", "--force", "--sign", "-", str(bundle)]
            subprocess.run(sign_cmd, check=True, capture_output=True)
        except (subprocess.SubprocessError, OSError) as e:
            err = e.stderr.decode("utf-8", "replace").strip() if isinstance(e, subprocess.CalledProcessError) else str(e)
            missing.append(f"{bundle.name}: codesign ({err})")

        print(
            f"[stage-webview] staged CEF runtime + UI -> {bundle.name}/"
            "Contents/Frameworks + Contents/Resources/webview/ui"
        )
    return missing


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description="Stage C++ webview build artifacts.")
    parser.add_argument(
        "--prebuild-only",
        action="store_true",
        help="仅执行 CEF 预构建（wrapper/helper），不暂存——build.py 引擎构建前调用以保链接产物",
    )
    parser.add_argument(
        "--sign-identity",
        default="",
        help="mac bundle 外层重签名身份（发布签名构建，build.py 从 scons bundle_sign_identity 透传）；"
        "为空时 ad-hoc 签名（开发构建）",
    )
    args = parser.parse_args()

    cef_version = read_cef_version()
    try:
        prebuild(cef_version)
    except cef_dist.CefDistError as e:
        print(f"[stage-webview] ERROR: CEF SDK 定位/下载失败: {e}", file=sys.stderr)
        return 2
    except (subprocess.SubprocessError, OSError, RuntimeError) as e:
        print(f"[stage-webview] ERROR: CEF 预构建失败: {e}", file=sys.stderr)
        return 2
    if args.prebuild_only:
        return 0

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
            "dock 页面将 404，CEF 核心可正常运行。构建: task ui-build",
            file=sys.stderr,
        )

    bundle_missing = stage_bundles(BIN_DIR, sign_identity=args.sign_identity)
    if bundle_missing:
        print(f"[stage-webview] ERROR: mac bundle 暂存失败: {bundle_missing}", file=sys.stderr)
        return 2

    manifest = WEBVIEW_DEST / "MANIFEST.txt"
    # UI 缺失时 stage_ui 不建 WEBVIEW_DEST(仅警告),MANIFEST 仍需落盘——父目录先确保。
    manifest.parent.mkdir(parents=True, exist_ok=True)
    file_count = sum(1 for _ in WEBVIEW_DEST.rglob("*") if _.is_file())
    manifest.write_text(
        "\n".join(
            [
                f"staged_at: {time.strftime('%Y-%m-%d %H:%M:%S')}",
                f"wing_runtime_source: {WING_RUNTIME}",
                f"cef_version: {cef_version}",
                f"ui_source: web/ui/dist (task ui-build)",
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
