def can_build(env, platform):
    # WebBridge 委托直接引用 Registry 符号（editor_ops 模块）：必需依赖（不传 True）——
    # optional 依赖不强制检查，disable editor_ops 时 webview 仍编译 → 链接失败（复审 P1）。
    env.module_add_dependencies("att_webview", ["att_editor_ops"])
    return env.editor_build


def get_opts(platform):
    return []


def configure(env):
    pass
