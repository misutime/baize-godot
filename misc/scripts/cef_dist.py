#!/usr/bin/env python3
"""CEF 分发包定位/下载/解压工具（共享模块：SCsub 与 stage_webview.py 共同引用）。

设计（业界主流：依赖缓存 + 自动下载 + 手动覆盖）：
- 默认缓存根：<repo>/bin/cef-dist/（git 忽略，贴近开发者，直观可见）
- 环境变量覆盖：CEF_DIST_ROOT=<任意位置>（CI/共享缓存/用户级）
- 缓存结构：<root>/<CEF_SDK_VERSION>/cef_binary_<CEF_SDK_VERSION>_<平台后缀>/
- 平台后缀由构建宿主决定：windows64 / macosarm64 / macosx64（双平台支持）
- 定位优先级：
    ① 已解压 SDK 目录存在 → 直接用
    ② 缓存有 .tar.bz2 包 → 解压（手动放包=离线模式）
    ③ allow_download → 自动下载官方包 → 解压
    ④ 否则抛异常（SCsub 配置期用，提示先跑 stage）
- 下载源：https://cef-builds.spotifycdn.com/cef_binary_<CEF_SDK_VERSION>_<平台后缀>.tar.bz2
"""

import hashlib
import os
import platform as _sys_platform
import shutil
import sys
import tarfile
import urllib.request
from pathlib import Path

# 环境变量覆盖（最高优先：CI/共享缓存/用户级）。
ENV_OVERRIDE = "CEF_DIST_ROOT"

# 官方下载源模板：{version} + {suffix}（后缀见 sdk_dir_suffix()）。
CEF_DOWNLOAD_URL = "https://cef-builds.spotifycdn.com/cef_binary_{version}_{suffix}.tar.bz2"

# 固定 SHA-256（完整性校验，防损坏/截断缓存）：key = CEF_SDK_VERSION，value = 按平台
# 后缀的官方 tar.bz2 包哈希。升级 CEF 时必须先下载新包并把哈希登记到此处；未登记的
# 版本退化为仅长度校验并打印警告（宁可显式告警也不静默接受）。
CEF_ARCHIVE_SHA256 = {
    "151.3.12+gd9cea67+chromium-151.0.7922.47": {
        "windows64": "5042ede3a508244f6c7465c88efca807055255419404ce5b6581da37083359c6",
        "macosarm64": "79d59f2bbde7556a3be2698268dffea8546a3edc7ab739c7ac07f3d493c63601",
        # macosx64: 未登记（Rosetta 场景再补，登记前仅长度校验并告警）
    },
}

# SDK 解压完整性哨兵文件（缺一即视为损坏/不完整缓存，重新获取），按平台后缀区分：
# - windows64：导入库 libcef.lib 存在
# - macos*：framework 二进制存在（mac SDK 的 Release/ 只有 framework，资源在其内部）
SDK_SENTINEL_FILES = {
    "windows64": [
        "include/cef_api_hash.h",
        "include/cef_app.h",
        "Release/libcef.lib",
    ],
    "macosarm64": [
        "include/cef_api_hash.h",
        "include/cef_app.h",
        "Release/Chromium Embedded Framework.framework/Chromium Embedded Framework",
    ],
    "macosx64": [
        "include/cef_api_hash.h",
        "include/cef_app.h",
        "Release/Chromium Embedded Framework.framework/Chromium Embedded Framework",
    ],
}

# 官方包平台后缀（构建宿主平台决定）。key = (系统, 机器架构)。
# macosarm64 = Apple Silicon，macosx64 = Intel/Rosetta；Windows 仅 x64（与既有行为一致）。
_SDK_SUFFIX_BY_HOST = {
    ("Windows", "AMD64"): "windows64",
    ("Windows", "x86_64"): "windows64",
    ("Darwin", "arm64"): "macosarm64",
    ("Darwin", "aarch64"): "macosarm64",
    ("Darwin", "x86_64"): "macosx64",
}


def sdk_dir_suffix() -> str:
    """返回当前构建宿主的 CEF 包平台后缀；未知平台抛错（不静默猜）。"""
    key = (_sys_platform.system(), _sys_platform.machine())
    suffix = _SDK_SUFFIX_BY_HOST.get(key)
    if not suffix:
        raise CefDistError(
            f"不支持的平台 {key[0]}/{key[1]}（当前支持 Windows x64、macOS arm64/x64）"
        )
    return suffix


class CefDistError(RuntimeError):
    """CEF 分发包定位/下载失败（调用方决定如何呈现）。"""


def get_dist_root(repo_root: Path) -> Path:
    """返回 CEF 分发包根目录：环境变量优先，否则 <repo>/bin/cef-dist/。"""
    env = os.environ.get(ENV_OVERRIDE)
    if env:
        return Path(env)
    return repo_root / "bin" / "cef-dist"


def get_sdk_dir(dist_root: Path, sdk_version: str) -> Path:
    """返回解压后的 SDK 目录：<root>/<version>/cef_binary_<version>_<平台后缀>/。"""
    return dist_root / sdk_version / f"cef_binary_{sdk_version}_{sdk_dir_suffix()}"


def get_archive_path(dist_root: Path, sdk_version: str) -> Path:
    """返回下载的 tar.bz2 包路径：<root>/<version>/cef_binary_<version>_<平台后缀>.tar.bz2。"""
    return dist_root / sdk_version / f"cef_binary_{sdk_version}_{sdk_dir_suffix()}.tar.bz2"


def _expected_sha256(sdk_version: str) -> str | None:
    """当前平台下该版本的固定 SHA-256；未登记返回 None（调用方退化为长度校验+告警）。"""
    return CEF_ARCHIVE_SHA256.get(sdk_version, {}).get(sdk_dir_suffix())


def _verify_archive(archive: Path, sdk_version: str) -> None:
    """校验已存在包（缓存/离线放包）的完整性：长度非空 + 固定 SHA-256；失败抛 CefDistError。"""
    size = archive.stat().st_size
    hasher = hashlib.sha256()
    with archive.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            hasher.update(chunk)
    digest = hasher.hexdigest()
    expected = _expected_sha256(sdk_version)
    if expected and digest.lower() != expected.lower():
        raise CefDistError(
            f"缓存包 SHA-256 校验失败 {archive}\n"
            f"  期望: {expected}\n  实际: {digest}\n"
            f"请删除该文件后重试；若为官方包更新，请同步更新 cef_dist.CEF_ARCHIVE_SHA256。"
        )
    if not expected and size == 0:
        raise CefDistError(f"缓存包为空文件 {archive}；请删除后重试。")


def _download(url: str, dest: Path, sdk_version: str) -> None:
    """下载 url 到 dest：校验长度与固定 SHA-256 通过后才原子改名成最终包；失败抛 CefDistError。"""
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    print(f"[cef-dist] 下载 {url}")
    hasher = hashlib.sha256()
    try:
        # 流式下载：先探大小，再分块写临时文件，边写边算哈希。
        with urllib.request.urlopen(url) as resp, tmp.open("wb") as out:
            total = int(resp.headers.get("Content-Length", 0))
            done = 0
            while True:
                chunk = resp.read(1024 * 256)
                if not chunk:
                    break
                out.write(chunk)
                hasher.update(chunk)
                done += len(chunk)
                if total:
                    pct = done * 100 // total
                    print(f"\r[cef-dist] {done // (1024 * 1024)}MB / {total // (1024 * 1024)}MB ({pct}%)", end="", flush=True)
            print()
    except Exception as e:  # 网络错误/中断等——显式失败，不静默。
        tmp.unlink(missing_ok=True)
        raise CefDistError(f"下载失败 {url}: {e}")

    # 完整性：长度（Content-Length 已知时）+ 固定 SHA-256，任一不过即丢弃损坏缓存。
    if total and done != total:
        tmp.unlink(missing_ok=True)
        raise CefDistError(f"下载不完整 {url}: 期望 {total} 字节, 实际 {done} 字节（已删除损坏缓存）")
    digest = hasher.hexdigest()
    expected = _expected_sha256(sdk_version)
    if expected and digest.lower() != expected.lower():
        tmp.unlink(missing_ok=True)
        raise CefDistError(
            f"SHA-256 校验失败 {url}: 期望 {expected}, 实际 {digest}"
            f"（已删除损坏缓存；若为官方包更新，请同步更新 cef_dist.CEF_ARCHIVE_SHA256）"
        )
    if not expected:
        print(
            f"[cef-dist] WARNING: {sdk_version} 未登记固定 SHA-256，仅校验长度（{done} 字节）",
            file=sys.stderr,
        )
    tmp.rename(dest)


def _sdk_is_complete(sdk_dir: Path) -> bool:
    """哨兵校验：关键头文件与平台对应的导入产物都在才认为 SDK 完整（防损坏缓存被复用）。"""
    return all((sdk_dir / rel).is_file() for rel in SDK_SENTINEL_FILES[sdk_dir_suffix()])


def _extract(archive: Path, sdk_dir: Path) -> None:
    """原子解压：先解到 <父目录>.tmp，哨兵校验通过后 rename 原子替换最终目录。

    中断/损坏不会污染最终缓存（最终目录要么完整要么不存在）；失败时清理 .tmp 并
    保留已校验的包，下次可直接重试解压。失败抛 CefDistError。
    """
    parent = sdk_dir.parent
    tmp_parent = parent.with_name(parent.name + ".tmp")
    print(f"[cef-dist] 解压 {archive.name}")
    try:
        if tmp_parent.exists():
            shutil.rmtree(tmp_parent)
        tmp_parent.mkdir(parents=True, exist_ok=True)
        # tar.bz2 内含顶层目录 cef_binary_<version>_<平台后缀>/，解压到临时父目录。
        with tarfile.open(archive, "r:bz2") as tf:
            tf.extractall(tmp_parent, filter="data")
    except Exception as e:
        shutil.rmtree(tmp_parent, ignore_errors=True)
        raise CefDistError(f"解压失败 {archive}: {e}")

    tmp_sdk = tmp_parent / sdk_dir.name
    if not tmp_sdk.is_dir() or not _sdk_is_complete(tmp_sdk):
        shutil.rmtree(tmp_parent, ignore_errors=True)
        raise CefDistError(f"解压结果不完整（哨兵校验失败）: {tmp_sdk}（来自 {archive}）")

    try:
        # 原子替换：先移除旧目录（旧实现中断遗留的损坏缓存），再同卷 rename。
        if parent.exists():
            shutil.rmtree(parent, ignore_errors=True)
        os.rename(tmp_parent, parent)
    except OSError as e:
        # 保留 .tmp 以便重试（最终目录要么完整要么不存在，不被污染）。
        raise CefDistError(f"解压产物原子替换失败 {tmp_parent} -> {parent}: {e}") from e


def ensure_sdk(repo_root: Path, sdk_version: str, allow_download: bool) -> Path:
    """确保 CEF SDK 可用，返回 SDK 目录。

    allow_download=True（stage）：缺失自动下载。
    allow_download=False（SCsub 配置期）：缺失抛错提示先跑 stage——不在 SCons 配置期做网络操作。
    """
    dist_root = get_dist_root(repo_root)
    sdk_dir = get_sdk_dir(dist_root, sdk_version)
    archive = get_archive_path(dist_root, sdk_version)

    if sdk_dir.is_dir():
        if _sdk_is_complete(sdk_dir):
            return sdk_dir
        # 哨兵缺失 = 损坏/不完整缓存（旧版中断遗留）：清除后重新获取。
        print(f"[cef-dist] 检测到不完整 SDK 缓存 {sdk_dir}，重新获取", file=sys.stderr)
        shutil.rmtree(sdk_dir)
    if archive.is_file():
        # 已缓存包（含离线手动放包）也校验固定哈希，防损坏缓存被使用。
        _verify_archive(archive, sdk_version)
        _extract(archive, sdk_dir)
        return sdk_dir
    if allow_download:
        _download(CEF_DOWNLOAD_URL.format(version=sdk_version, suffix=sdk_dir_suffix()), archive, sdk_version)
        _extract(archive, sdk_dir)
        return sdk_dir

    raise CefDistError(
        f"CEF SDK 未缓存:{sdk_dir}\n"
        f"请先运行 task stage-webview(自动下载 {CEF_DOWNLOAD_URL.format(version=sdk_version, suffix=sdk_dir_suffix())} 到 {dist_root})。\n"
        f"离线可用:手动下载该 tar.bz2 放到 {archive},或设置 {ENV_OVERRIDE} 指向已解压的 SDK 目录。"
    )


if __name__ == "__main__":
    # 直接运行：打印当前定位结果（诊断用），不下载。
    repo = Path(__file__).resolve().parents[2]
    version = sys.argv[1] if len(sys.argv) > 1 else "151.3.12+gd9cea67+chromium-151.0.7922.47"
    root = get_dist_root(repo)
    sdk = get_sdk_dir(root, version)
    print(f"CEF_DIST_ROOT  = {root}")
    print(f"平台后缀        = {sdk_dir_suffix()}")
    print(f"SDK dir        = {sdk}")
    print(f"archive        = {get_archive_path(root, version)}")
    print(f"SDK 已就绪     = {sdk.is_dir()}")
