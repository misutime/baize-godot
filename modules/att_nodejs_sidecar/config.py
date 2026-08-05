def can_build(env, platform):
    # NodeJS sidecar 通道（原 modules/ai 的 sidecar 部分）：SidecarServer（WS server +
    # spawn 管主）+ ProcessSupervisor（进程监督）。消费 editor_ops 能力面（Registry
    # 分派）。2026-08-05 架构决策：与能力面拆分，放弃 ai 历史命名。
    # 必需依赖（不传 True）：optional 依赖不强制检查，disable 对应模块时仍编译 → 链接失败（复审 P1）。
    env.module_add_dependencies("att_nodejs_sidecar", ["att_editor_ops", "websocket"])
    return env.editor_build


def get_opts(platform):
    return []


def configure(env):
    pass
