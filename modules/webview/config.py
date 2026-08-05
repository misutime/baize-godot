def can_build(env, platform):
    # WebBridge 委托直接引用 SemanticRegistry 符号（ai 模块）：必需依赖（不传 True）——
    # optional 依赖不强制检查，disable ai 时 webview 仍编译 → 链接失败（复审 P1）。
    env.module_add_dependencies("webview", ["ai"])
    return env.editor_build


def get_opts(platform):
    return []


def configure(env):
    pass
