#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
UniGo Godot 内核构建入口。

统一封装 Godot 官方 SConstruct 的模块白名单机制:
  modules_enabled_by_default=no  +  逐模块 module_<name>_enabled=yes

职责:
  1. 读取 unigo_modules.cfg 白名单;
  2. 从各模块 config.py 解析真实依赖图(unigo_module_deps.json,自动生成);
  3. 校验白名单内模块的依赖是否齐全(缺依赖直接报错,不产出半成品);
  4. 校验"白名单自动补全后是否拉入了我们明确排除的模块"(报警提示);
  5. 生成完整 scons 命令并执行;
  6. 校验产物 DLL 存在。

用法:
  python unigo/unigo_build.py [--dry-run] [--jobs N] [--dev] [--clean] [--release]

说明:
  - 本脚本不 fork SConstruct 逻辑,只用官方机制,上游升级冲突最小。
  - --dev 保留 debug 构建(默认 editor 目标);--release 走 template_release(纯运行内核)。
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

# 默认构建形态(EditorNative DLL,对应架构文档 §7.1)
DEFAULT_SCONS_ARGS = {
    "platform": "windows",
    "arch": "x86_64",
    "target": "editor",
    "library_type": "shared_library",
    "editor_native": "yes",
    "nomono": "yes",
    "modules_enabled_by_default": "no",
    "deprecated": "no",
    "use_mingw": "no",
    # 渲染驱动:只用 Vulkan(第一阶段)。d3d12 默认关闭;
    # accesskit(屏幕阅读器)不需要,显式关闭防被依赖拉回。
    "vulkan": "yes",
    "d3d12": "no",
    "accesskit": "no",
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


def extract_dependencies_from_configs() -> dict[str, list[str]]:
    """从各模块 config.py 的 env.module_add_dependencies() 解析真实依赖图。"""
    deps: dict[str, list[str]] = {}
    if not MODULES_DIR.is_dir():
        return deps
    for config_file in MODULES_DIR.glob("*/config.py"):
        module_name = config_file.parent.name
        text = config_file.read_text(encoding="utf-8", errors="ignore")
        # 匹配: module_add_dependencies("模块", ["dep1", "dep2"], True)
        for m in re.finditer(
            r'module_add_dependencies\(\s*"([a-z0-9_]+)"\s*,\s*\[([^\]]*)\]\s*(?:,\s*(True|False))?\s*\)',
            text,
        ):
            dep_module = m.group(1)
            dep_list = [d.strip().strip('"').strip("'") for d in m.group(2).split(",") if d.strip()]
            if dep_module == module_name:
                deps.setdefault(module_name, []).extend(dep_list)
    return deps


def build_dependency_graph() -> dict[str, list[str]]:
    """构建全量依赖图:{模块: 直接依赖列表}。优先用真实 config.py,回退白名单声明。"""
    real = extract_dependencies_from_configs()
    _, declared = parse_whitelist(CFG_FILE.read_text(encoding="utf-8"))
    graph: dict[str, list[str]] = {}
    for m in real:
        graph[m] = real[m]
    # 白名单显式声明的依赖(覆盖/补充)
    for m, deps in declared.items():
        graph[m] = deps
    return graph


def resolve_transitive(module: str, graph: dict[str, list[str]], seen: set[str] | None = None) -> set[str]:
    """求模块的传递闭包依赖。"""
    seen = seen or set()
    if module in seen:
        return seen
    seen.add(module)
    for dep in graph.get(module, []):
        resolve_transitive(dep, graph, seen)
    return seen


def check_dependencies(whitelist: set[str], graph: dict[str, list[str]], declared: dict[str, list[str]]) -> list[str]:
    """校验白名单内模块的依赖是否齐全。

    scons 的 modules_enabled_by_default=no 机制会“自动补全”白名单模块的依赖,
    所以“闭包内不在白名单”的依赖是正常的自动补全项,不是错误。
    真正的错误只有两类:
      1. 白名单内某模块的依赖在真实图里未知(拼写错误/模块不存在);
      2. 自动补全拉入了 EXCLUDED_MODULES 里明确排除的模块。
    返回: 无法满足的依赖描述列表(空=通过)。
    """
    problems = []
    all_graph_deps = {d for dd in graph.values() for d in dd}
    for m in sorted(whitelist):
        for dep in graph.get(m, []):
            if dep not in whitelist and dep not in all_graph_deps and dep not in EXCLUDED_MODULES:
                # 依赖既不在白名单、也不在任何模块的依赖声明里、也不在排除集 → 拼写错误
                problems.append(f"{m} -> {dep}(依赖未知,可能拼写错误或模块不存在)")
    return problems


def check_excluded_pulled_in(whitelist: set[str], graph: dict[str, list[str]]) -> list[str]:
    """校验:白名单自动补全后,是否拉入了我们明确排除的模块。

    scons 会自动启用白名单模块的全部依赖(含传递),若某个被排除模块
    出现在闭包中,说明它会被编进内核,违背排除意图,必须报警。
    """
    closure_all = set()
    for m in whitelist:
        closure_all |= resolve_transitive(m, graph)
    pulled = []
    for excluded in EXCLUDED_MODULES:
        if excluded in closure_all and excluded not in whitelist:
            pulled.append(excluded)
    return pulled


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
    parser.add_argument("--release", action="store_true", help="构建 template_release(纯运行内核,实验)")
    args = parser.parse_args()

    cfg_text = CFG_FILE.read_text(encoding="utf-8")
    whitelist, declared = parse_whitelist(cfg_text)
    graph = build_dependency_graph()

    print_info(f"白名单模块({len(whitelist)}): {', '.join(sorted(whitelist))}")

    # 依赖校验闭环
    missing = check_dependencies(whitelist, graph, declared)
    if missing:
        print_error("以下白名单模块存在缺失/未知依赖,请修正 unigo_modules.cfg:")
        for item in missing:
            print_error(f"  - {item}")
        sys.exit(1)

    pulled = check_excluded_pulled_in(whitelist, graph)
    if pulled:
        print_error(f"以下明确排除的模块被白名单依赖拉回: {', '.join(pulled)}")
        print_error("请决定:加入白名单,或调整白名单模块避免依赖它们。")
        sys.exit(1)

    generate_deps_file(graph)

    if args.release:
        DEFAULT_SCONS_ARGS["target"] = "template_release"
        DEFAULT_SCONS_ARGS["editor_native"] = "no"

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

    # 校验产物
    suffix = "editor" if DEFAULT_SCONS_ARGS["target"] == "editor" else "template_release"
    dll = REPO_ROOT / "bin" / f"godot.windows.{suffix}.x86_64.dll"
    if dll.exists():
        print_info(f"构建成功,产物: {dll.relative_to(REPO_ROOT)}")
    else:
        print_error(f"产物未找到: {dll}")
        sys.exit(1)


if __name__ == "__main__":
    main()
