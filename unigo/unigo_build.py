#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
UniGo Godot 内核构建入口。

统一封装 Godot 官方 SConstruct 的模块白名单机制:
  modules_enabled_by_default=no  +  逐模块 module_<name>_enabled=yes

职责:
  1. 读取 unigo_modules.cfg 白名单;
  2. 从各模块 config.py 解析真实依赖图(unigo_module_deps.json,自动生成);
  3. 校验白名单内模块的必需依赖是否显式启用(缺依赖直接报错,不产出半成品);
  4. 校验依赖名称和明确排除模块,避免 SCons 静默禁用目标模块;
  5. 生成完整 scons 命令并执行;
  6. 校验产物 DLL 存在。

用法:
  python unigo/unigo_build.py [--dry-run] [--jobs N] [--clean]

说明:
  - 本脚本不 fork SConstruct 逻辑,只用官方机制,上游升级冲突最小。
  - 构建形态固定为**纯净渲染内核**:target=template_release + shared_library,
    无 TOOLS_ENABLED/无 ProjectManager/无编辑器代码,渲染由 C# 命令缓冲控制。
  - 产物命名: godot.windows.template_release.x86_64.pure.dll。
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

# 仓库根 = 本文件所在目录的上一级(vendor/godot)
REPO_ROOT = Path(__file__).resolve().parent.parent
UNIGO_DIR = REPO_ROOT / "unigo"
MODULES_DIR = REPO_ROOT / "modules"
CFG_FILE = UNIGO_DIR / "unigo_modules.cfg"
DEPS_FILE = UNIGO_DIR / "unigo_module_deps.json"
BUILD_PROFILE = UNIGO_DIR / "unigo_build_profile.txt"

# 默认构建形态:纯净渲染内核(纯运行内核 + C ABI 导出)。
# target=template_release:无 TOOLS_ENABLED/无 ProjectManager/无编辑器代码。
DEFAULT_SCONS_ARGS = {
    "platform": "windows",
    "arch": "x86_64",
    "target": "template_release",
    "library_type": "shared_library",
    "nomono": "yes",
    "modules_enabled_by_default": "no",
    "deprecated": "no",
    "use_mingw": "no",
    # 渲染驱动:只用 Vulkan(Forward+)。d3d12 默认关闭;
    # accesskit(屏幕阅读器)不需要,显式关闭防被依赖拉回。
    # angle=no:禁用 ANGLE(OpenGL 实现层),强制走 Vulkan。
    "vulkan": "yes",
    "d3d12": "no",
    "accesskit": "no",
    "angle": "no",
    # 外部窗直渲(UNIGO_EXTERNAL_ONLY):编译期强制外部窗——自建窗分支
    # 常量折叠剔除,产物 = 只渲染进 C# 宿主窗(UniGo 唯一窗口模式)。
    # 注意:必须是 scons 选项注册的小写 "cppdefines"(SConstruct 读 env.get("cppdefines"));
    # 大写 CPPDEFINES 不匹配 opts.Add,宏不进入编译环境(实测产物含自建窗符号——曾静默失效)。
    "cppdefines": "UNIGO_EXTERNAL_ONLY",
    # 产物命名:godot.windows.template_release.x86_64.pure.dll
    "extra_suffix": "pure",
}

# 明确排除、且若被依赖自动拉回需要报警的模块
EXCLUDED_MODULES = {
    "gdscript", "jsonrpc", "websocket",  # GDScript 业务逻辑
    "mono",                              # C# 绑定(与 nomono=yes 双保险)
    "openxr", "mobile_vr", "visionos_xr",  # XR
    "jolt_physics",                      # 高级物理
    "navigation_2d", "navigation_3d",    # 导航
    "texture_streaming",                 # 纹理流送
    "interactive_music",                 # 交互音乐
    "objectdb_profiler",                 # 性能分析
}


def parse_whitelist(cfg_text: str) -> tuple[set[str], dict[str, list[str]]]:
    """解析白名单文件。返回 (模块集合, {模块: 显式声明的依赖})。"""
    modules: set[str] = set()
    declared_deps: dict[str, list[str]] = {}
    for line in cfg_text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        # 允许行尾注释(空格 + # 开头);[deps: ...] 可选
        line = line.split(" #", 1)[0].strip()
        m = re.match(r"^([a-z0-9_]+)(\s*\[deps:\s*([^]]+)\])?$", line)
        if not m:
            print_error(f"白名单格式错误: {line!r}")
            sys.exit(1)
        name = m.group(1)
        modules.add(name)
        if m.group(3):
            declared_deps[name] = [d.strip() for d in m.group(3).split(",") if d.strip()]
    return modules, declared_deps


def extract_dependencies_from_configs() -> tuple[dict[str, list[str]], dict[str, list[str]]]:
    """解析真实依赖图,返回(全部依赖,必需依赖)。

    Godot 的第三个参数名为 optional:省略或 False 表示必需依赖,
    True 表示仅在依赖模块已启用时参与排序的可选依赖。
    """
    deps: dict[str, list[str]] = {}
    required_deps: dict[str, list[str]] = {}
    if not MODULES_DIR.is_dir():
        return deps, required_deps
    for config_file in MODULES_DIR.glob("*/config.py"):
        module_name = config_file.parent.name
        text = config_file.read_text(encoding="utf-8", errors="ignore")
        for m in re.finditer(
            r'module_add_dependencies\(\s*"([a-z0-9_]+)"\s*,\s*\[([^\]]*)\]\s*(?:,\s*(True|False))?\s*\)',
            text,
        ):
            dep_module = m.group(1)
            dep_list = [d.strip().strip('"').strip("'") for d in m.group(2).split(",") if d.strip()]
            if dep_module != module_name:
                continue
            deps.setdefault(module_name, []).extend(dep_list)
            if m.group(3) != "True":
                required_deps.setdefault(module_name, []).extend(dep_list)
    return deps, required_deps


def build_dependency_graph() -> tuple[dict[str, list[str]], dict[str, list[str]]]:
    """构建全量依赖图和必需依赖图,白名单声明只补充审计信息。"""
    graph, required_graph = extract_dependencies_from_configs()
    _, declared = parse_whitelist(CFG_FILE.read_text(encoding="utf-8"))
    for module, dependencies in declared.items():
        current = graph.setdefault(module, [])
        current.extend(dep for dep in dependencies if dep not in current)
    return graph, required_graph


def resolve_transitive(module: str, graph: dict[str, list[str]], seen: set[str] | None = None) -> set[str]:
    """求模块的传递闭包依赖。"""
    seen = seen or set()
    if module in seen:
        return seen
    seen.add(module)
    for dep in graph.get(module, []):
        resolve_transitive(dep, graph, seen)
    return seen


def check_dependencies(
    whitelist: set[str], graph: dict[str, list[str]], required_graph: dict[str, list[str]]
) -> list[str]:
    """校验依赖名称有效,并确保必需依赖已显式加入白名单。

    SCons 只检查必需依赖是否启用,不会替调用方自动启用依赖模块;
    缺失时目标模块会被静默禁用,因此构建前必须在这里失败。
    """
    problems = []
    for module in sorted(whitelist):
        for dependency in graph.get(module, []):
            if not (MODULES_DIR / dependency).is_dir():
                problems.append(f"{module} -> {dependency}(依赖未知,modules/{dependency} 不存在)")
        for dependency in required_graph.get(module, []):
            if dependency not in whitelist:
                problems.append(f"{module} -> {dependency}(必需依赖未加入白名单)")
    return problems


def check_excluded_dependencies(whitelist: set[str], required_graph: dict[str, list[str]]) -> list[str]:
    """校验白名单模块的必需依赖链是否触及明确排除模块。"""
    closure_all = set()
    for module in whitelist:
        closure_all |= resolve_transitive(module, required_graph)
    return sorted(excluded for excluded in EXCLUDED_MODULES if excluded in closure_all)


def print_error(msg: str) -> None:
    print(f"[UniGoBuild][ERROR] {msg}", file=sys.stderr)


def print_info(msg: str) -> None:
    print(f"[UniGoBuild][INFO] {msg}")


def generate_deps_file(graph: dict[str, list[str]]) -> None:
    """把依赖图写成 unigo_module_deps.json,供校验/审计。"""
    DEPS_FILE.write_text(json.dumps(graph, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print_info(f"依赖图已写入 {DEPS_FILE.relative_to(REPO_ROOT)}")


def build_scons_command(whitelist: set[str], args: argparse.Namespace) -> list[str]:
    """组装 scons 命令行。"""
    cmd = ["scons"]
    for k, v in DEFAULT_SCONS_ARGS.items():
        cmd.append(f"{k}={v}")
    # 逐个启用白名单模块(其余模块因 modules_enabled_by_default=no 默认关闭)
    for m in sorted(whitelist):
        cmd.append(f"module_{m}_enabled=yes")
    if args.jobs:
        cmd.append(f"-j{args.jobs}")
    if args.clean:
        cmd.append("-c")
    return cmd


def main() -> None:
    parser = argparse.ArgumentParser(description="UniGo Godot 内核构建入口")
    parser.add_argument("--dry-run", action="store_true", help="只校验配置并打印命令,不执行")
    parser.add_argument("--jobs", "-j", type=int, default=None, help="并行编译任务数(默认 scons 自动)")
    parser.add_argument("--clean", action="store_true", help="清理构建产物")
    args = parser.parse_args()

    cfg_text = CFG_FILE.read_text(encoding="utf-8")
    whitelist, _ = parse_whitelist(cfg_text)
    graph, required_graph = build_dependency_graph()

    print_info(f"白名单模块({len(whitelist)}): {', '.join(sorted(whitelist))}")

    # 依赖校验闭环
    missing = check_dependencies(whitelist, graph, required_graph)
    if missing:
        print_error("以下白名单模块存在缺失/未知依赖,请修正 unigo_modules.cfg:")
        for item in missing:
            print_error(f"  - {item}")
        sys.exit(1)

    excluded_dependencies = check_excluded_dependencies(whitelist, required_graph)
    if excluded_dependencies:
        print_error(f"以下明确排除的模块是白名单模块的必需依赖: {', '.join(excluded_dependencies)}")
        print_error("请调整白名单模块,或从明确排除集合移除对应依赖。")
        sys.exit(1)

    generate_deps_file(graph)

    # 构建形态固定为纯净渲染内核(默认参数已配置 target=template_release + extra_suffix=pure)。
    mode_suffix = "pure"

    cmd = build_scons_command(whitelist, args)
    print_info("构建命令:")
    print_info("  " + " ".join(cmd))

    if args.dry_run:
        print_info("--dry-run 模式,未实际构建。")
        return

    print_info("开始构建...")
    result = subprocess.run(cmd, cwd=str(REPO_ROOT))
    if result.returncode != 0:
        print_error(f"scons 构建失败(exit={result.returncode})")
        sys.exit(result.returncode)
    if args.clean:
        print_info("清理完成。")
        return

    # 校验产物:纯净渲染内核 = godot.windows.template_release.x86_64.pure.dll
    dll = REPO_ROOT / "bin" / "godot.windows.template_release.x86_64.pure.dll"
    if dll.exists():
        print_info(f"构建成功,产物: {dll.relative_to(REPO_ROOT)}")
    else:
        print_error(f"产物未找到: {dll}")
        sys.exit(1)


if __name__ == "__main__":
    main()
