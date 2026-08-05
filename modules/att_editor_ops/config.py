def can_build(env, platform):
    # 编辑器语义能力面（原 modules/ai 的能力部分）：Registry（能力注册表）+
    # Ops（语义操作）+ UITree（语义 UI 树）。零依赖——能力面被各通道
    # （nodejs_sidecar / webview WebBridge 委托）消费，本身不依赖任何暴露层。
    # 2026-08-05 架构决策：与 sidecar 通道拆分（原 ai 模块解体），放弃 ai 历史命名。
    return env.editor_build


def get_opts(platform):
    return []


def configure(env):
    pass
