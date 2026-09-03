# UniGo Godot 内核定制构建

本目录是 UniGo 对 Godot 内核的**定制构建体系**。目标:用最小必要模块集构建出符合 UniGo 需求(纯净渲染内核:template_release + C ABI,Godot 渲染进宿主窗口)的 Godot 内核。

> **上游关系**:已决策**完全分叉**(不再自动同步上游,仅季度核查核心层 cherry-pick)。
> 权威规则见 `docs/architecture/UniGo-Fork上游关系规范-完全分叉.md`。

## 为什么需要这个目录

Godot 自带 66 个模块(physics/gdscript/gltf/fbx/mono/openxr/vr/…),绝大部分我们不需要,但默认全编。我们只需要一个"渲染/窗口/输入/文本/资源导入 + 我们的 C ABI"的最小核心。

Godot 官方 SConstruct **已内置模块开关机制**,我们不 fork 它,只用它:

- `modules_enabled_by_default=no` → 默认禁用全部模块
- `module_<name>_enabled=yes` → 白名单逐个启用
- 各模块 `config.py` 的 `env.module_add_dependencies()` → 依赖自动补全

本目录提供**白名单配置 + 依赖校验闭环 + 统一构建入口**,把"手输长 scons 参数"变成一条命令。

## 文件说明

| 文件 | 作用 |
|---|---|
| `unigo_modules.cfg` | **模块白名单**(唯一裁剪入口)。未列出的模块一律不编译。 |
| `unigo_build_profile.txt` | 官方 build profile(功能开关:`deprecated`/`disable_3d`/`nomono` 等)。 |
| `unigo_build.py` | 统一构建入口:读配置 → 校验依赖 → 生成 scons 命令 → 构建 → 校验产物。 |
| `unigo_module_deps.json` | 依赖图(脚本自动生成,供审计,勿手改)。 |
| `README.md` | 本说明。 |

## 用法

```bash
cd vendor/godot

# 校验配置 + 打印将执行的命令(不实际构建)
python unigo/unigo_build.py --dry-run

# 实际构建(纯净渲染内核 DLL,默认)
python unigo/unigo_build.py -j16

# 清理
python unigo/unigo_build.py --clean
```

产物:`bin/godot.windows.template_release.x86_64.pure.dll`(已导出 `unigo_engine_*` C ABI 符号)。

## 维护指南

### 新增模块
1. 在 `unigo_modules.cfg` 追加一行模块名(`modules/<name>` 目录名)。
2. 若该模块有依赖(见其 `config.py` 的 `module_add_dependencies`),用 `[deps: a, b]` 声明,或直接依赖自动补全。
3. 跑 `python unigo/unigo_build.py --dry-run` 确认依赖图无缺失、无排除集被拉回。

### 删除模块
1. 删除 `unigo_modules.cfg` 对应行。
2. 若它被其他白名单模块依赖,构建脚本会报警,需处理。

### 排除集
`unigo_build.py` 里的 `EXCLUDED_MODULES` 是"明确排除"清单。**若某被排除模块被白名单依赖自动拉回,构建脚本会报错**,防止编出违背意图的内核。新增排除项时加进该集合即可。

## 依赖图(当前白名单,自动生成)

| 模块 | 直接依赖 |
|---|---|
| unigo | (无,纯 C ABI) |
| mbedtls | (无;core/crypto 硬依赖,不可裁) |
| zip | (无;target=editor 硬依赖,android_sdk_manager 无条件使用) |
| godot_physics_2d | (无) |
| godot_physics_3d | (无) |
| gltf | csg, gridmap |
| fbx | gltf |
| text_server_adv | freetype, msdfgen, svg |
| basis_universal | tinyexr |
| ktx | basis_universal |
| svg | jpg, webp |
| msdfgen | freetype |
| freetype | (无) |
| jpg | (无) |
| webp | (无) |

> 注:白名单里没有但被依赖拉回的模块(如 `csg`/`gridmap`/`tinyexr`/`jsonrpc` 等)由 scons 自动补全,不在此清单。
>
> **mbedtls 例外**:`core/crypto` 的 light 降级路径在 mbedTLS 4.x 下已损坏(零长数组 C2229、缺 `MBEDTLS_ECP_MAX_BITS`),故保留 mbedtls 全量模块——它是 core 硬依赖,不属于“可选模块”。

## 与架构文档的关系

- **上游关系规范**(权威):`docs/architecture/UniGo-Fork上游关系规范-完全分叉.md`——完全分叉决策、同步规则、精简清单、季度核查机制。
- 总体架构 §6.3 红线已随完全分叉决策**更新**:物理精简允许(按上游关系规范 §3 范围/流程),不再"只配置不删代码"。
- `UniGo项目结构与划分规则.md` §7.1:`vendor/godot` 子模块,构建产物是 C ABI pure DLL。
