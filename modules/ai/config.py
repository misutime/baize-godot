def can_build(env, platform):
    # AI Bridge 是编辑器功能（TOOLS_ENABLED 门控在代码内）。
    return env.editor_build


def get_opts(platform):
    return []


def configure(env):
    pass
