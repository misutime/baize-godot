#!/usr/bin/env python3
"""Stage godot-cef build artifacts into the editor distribution directory.

godot-cef 与 baize-godot 独立开发：本脚本只消费 godot-cef 的**编译产物**
（addons/godot_cef/ 完整 addon），不引入其源码。产物暂存到 <repo>/bin/webview/，
由引擎模块在编辑器启动时自动加载（与打开的项目无关）。

产物来源（路径直接关联，显式可见）：
  ADDON_SOURCE = ../refers/godot-cef/addons/godot_cef（相对本仓库）
  UI_SOURCE    = ../refers/cef-smoke-test/ui（MVP 阶段页面产物；后续为 ui/ 工程构建输出）

用法：
  python misc/scripts/stage_webview.py
"""

import shutil
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
ADDON_SOURCE = REPO_ROOT.parent / "refers" / "godot-cef" / "addons" / "godot_cef"
UI_SOURCE = REPO_ROOT.parent / "refers" / "cef-smoke-test" / "ui"
DEST = REPO_ROOT / "bin" / "webview"


def main() -> int:
    descriptor = ADDON_SOURCE / "godot_cef.gdextension"
    if not descriptor.is_file():
        print(
            f"[stage-webview] ERROR: addon not found at {ADDON_SOURCE} "
            "(missing godot_cef.gdextension) — build godot-cef first "
            "(`just gdcef-build` in that repo).",
            file=sys.stderr,
        )
        return 2

    # 全量重建分发目录（保证与来源一致，不残留旧文件）。
    if DEST.exists():
        shutil.rmtree(DEST)
    shutil.copytree(ADDON_SOURCE, DEST)

    ui_dest = DEST / "ui"
    ui_dest.mkdir(parents=True, exist_ok=True)
    if not UI_SOURCE.is_dir():
        print(f"[stage-webview] WARNING: UI source {UI_SOURCE} not found — dock page will 404.", file=sys.stderr)
    else:
        for f in sorted(UI_SOURCE.glob("*.html")):
            shutil.copy2(f, ui_dest / f.name)

    # 可追溯清单：记录来源与时间（版本锁定另见实施记录；此处记录实际来源路径）。
    manifest = DEST / "MANIFEST.txt"
    file_count = sum(1 for _ in DEST.rglob("*") if _.is_file())
    manifest.write_text(
        "\n".join(
            [
                f"staged_at: {time.strftime('%Y-%m-%d %H:%M:%S')}",
                f"addon_source: {ADDON_SOURCE}",
                f"ui_source: {UI_SOURCE if UI_SOURCE.is_dir() else '(missing)'}",
                f"file_count: {file_count}",
                "",
            ]
        ),
        encoding="utf-8",
    )

    print(f"[stage-webview] staged {ADDON_SOURCE} -> {DEST} ({file_count} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
