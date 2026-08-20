def can_build(env, platform):
    # Godot Provider：Godot Core 的对外服务出口（Transport + Registry + Ops + Events）。
    # 仅编辑器构建（TOOLS_ENABLED）——能力面操作编辑器核心状态（场景/选中/undo）。
    # 2026-08-07 架构：AI-first 对接层（Godot Core + gd_provider），替代已删除的
    # Electron UI / CEF / Node sidecar / att_* 旧架构。
    return env.editor_build


def get_opts(platform):
    return []


def configure(env):
    pass
