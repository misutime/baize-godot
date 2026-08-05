def can_build(env, platform):
    # AI Bridge / Sidecar 是编辑器功能（TOOLS_ENABLED 门控在代码内）。
    # SidecarServer 复用引擎 websocket 模块（wslay）做 WS server（§5.3）。
    # 必需依赖（不传 True）：optional 依赖不强制检查，disable websocket 时 AI 仍编译 → 链接失败（复审 P1）。
    env.module_add_dependencies("ai", ["websocket"])
    return env.editor_build


def get_opts(platform):
    return []


def configure(env):
    pass
