def can_build(env, platform):
    # Godot Provider：Godot Core 的对外服务出口（Transport + Registry + Ops + Events）。
    # 仅编辑器构建（TOOLS_ENABLED）——能力面操作编辑器核心状态（场景/选中/undo）。
    # 2026-08-06 架构：Godot Core + Electron UI 路线，替代已删除的 att_webview/
    # att_nodejs_sidecar/att_editor_ops 旧架构。
    return env.editor_build


def get_opts(platform):
    return []


def configure(env):
    pass
