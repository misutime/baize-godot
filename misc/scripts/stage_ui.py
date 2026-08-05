"""WebDock React 壳产物暂存：web/ui/dist → bin/webview/ui/（原子替换 ui 子目录）。

入口：task ui-build（pnpm build 后调用）；stage-webview 复用同一函数（单一 UI 暂存点）。
dist 缺失/无 index.html → 返回缺失项（调用方决定警告或报错），不静默。
React 壳为 dock 唯一页面源（editor_web_dock 加载 webview/ui/index.html），
旧 stub（modules/att_webview/ui/bridge.html）不再暂存。
"""

import os
import shutil
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
UI_DIST = REPO_ROOT / "web" / "ui" / "dist"
UI_DEST = REPO_ROOT / "bin" / "webview" / "ui"
# 默认字体（Noto Sans CJK SC = 思源黑体，SIL OFL）：源在 web/ui/public/fonts/
# （Vite public 机制构建自动进 dist/fonts/），此处随 dist 整体拷贝——编辑器默认字体
# （editor_fonts.cpp 外部优先加载）与 WebDock 页面共享同一文件，两边字形一致。


def stage_ui_dist() -> list:
    """原子替换 bin/webview/ui（含 dist 自带的 fonts/ 默认字体）。返回缺失项列表（空 = 成功）。"""
    if not UI_DIST.is_dir():
        return ["web/ui/dist 缺失（先运行 task ui-build 构建 React 壳）"]
    tmp = UI_DEST.parent / ".ui-tmp"  # 与 stage_webview 的 .webview-ui-tmp 区分
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    shutil.copytree(UI_DIST, tmp, dirs_exist_ok=True)  # 内容直接拷到 tmp（无 ui 子层）
    if not (tmp / "index.html").is_file():
        shutil.rmtree(tmp, ignore_errors=True)
        return ["web/ui/dist/index.html 缺失"]
    if not (tmp / "fonts" / "NotoSansCJKsc-Regular.otf").is_file():
        shutil.rmtree(tmp, ignore_errors=True)
        return ["dist/fonts/NotoSansCJKsc-Regular.otf 缺失（public/fonts 未进构建，编辑器回退 Inter）"]
    if UI_DEST.exists():
        shutil.rmtree(UI_DEST)
    UI_DEST.parent.mkdir(parents=True, exist_ok=True)
    os.replace(tmp, UI_DEST)  # tmp → bin/webview/ui（同父目录原子替换）
    return []


if __name__ == "__main__":
    missing = stage_ui_dist()
    if missing:
        print(f"[stage-ui] ERROR: {'; '.join(missing)}", file=sys.stderr)
        sys.exit(1)
    print(f"[stage-ui] staged React 壳 -> {UI_DEST}")
