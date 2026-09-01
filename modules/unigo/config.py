#!/usr/bin/env python
# UniGo C ABI 模块构建选项。
# 纯 C ABI 模块:任何平台/构建形态都可编(模块本身只依赖 core/main)。


def can_build(env, platform):
    # 第一阶段仅 Windows(UniGo 当前宿主平台);后续可放开。
    return platform == "windows"


def configure(env):
    pass
