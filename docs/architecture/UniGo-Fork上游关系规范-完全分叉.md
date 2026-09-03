# UniGo Godot Fork 上游关系规范(完全分叉决策)

> 状态:**已决策(2026-09)** · 决策:方案 B **完全分叉**,不维护与上游的自动同步;
> 上游仅作"参考实现",按需季度核查核心层 cherry-pick。
> 范围:本规范是 `vendor/godot`(Godot 内核 fork)与上游 godotengine/godot 关系的**权威规则**。
> 适用:团队任何人拉取上游、分析上游改动、裁剪/精简内核时,以本文档为准。

---

## 1. 决策背景与依据

### 1.1 为什么完全分叉(决策依据)

调研取证(2026-09,上游 #121392 之后 74 提交全量分析):

| 上游 74 提交类别 | 数量 | 与 UniGo 相关性 |
|---|---|---|
| 编辑器 UI(主题/GridMap/BlendSpace/图标) | ~25 | ❌ 我们不用编辑器(已 template_release) |
| Android 编辑器/SDK | ~10 | ❌ |
| GDScript 语言/补全 | ~6 | ❌ |
| 文档/README | ~8 | ❌ |
| AudioEffectPanner 防爆音 | 2 | ❌ 不用该 Effect |
| Retarget 动画 | 2 | ❌ |
| Metal 后端(subpass) | 2 | ❌ 用 Vulkan |
| iOS/Android luminance | 1 | ❌ |
| AHashMap 微优化 | 1 | ⚠️ 低价值 |
| **核心渲染/输入/窗口 bug 修复** | **≈0** | —— |

**结论**:上游核心(servers/rendering + core/input + platform/windows)在 4.x 已成熟,
74 个提交中 core/input **零改动**、真实渲染路径 ≈0。同步成本(merge 冲突随 fork
深度指数上升)远大于收益(近乎无核心 bug 修复可拿)。

### 1.2 同步成本随 fork 深度增长(为什么现在切)

- 我们 fork 已改 `platform/windows/display_server_windows.cpp`(未来外部窗方案
  **还会深度改**)、`main/*`、`modules/unigo`、`SConstruct`;
- 上游每次动这些文件(编辑器相关提交常连带),merge 即冲突;
- **越晚切分叉,切分叉成本越高**(冲突累积 + 我们改动深度增加)。

### 1.3 决策

**完全分叉(方案 B)**:
- 不再把上游作为同步源自动 merge;
- `upstream` remote **保留**(参考用,不自动 merge);
- 核心层有价值修复 = 按季度人工核查 + cherry-pick(见 §4)。

---

## 2. 上游同步规则(权威)

### 2.1 禁止事项

1. **禁止自动 merge upstream**(`git merge upstream/master`)、禁止 rebase 上游;
2. **禁止批量 cherry-pick** 上游提交;
3. **禁止**为了"跟上上游"而升级 fork 基线。

### 2.2 唯一允许的上游操作

1. `git fetch upstream`(随时可做,只拉取不合并);
2. **季度核查**(见 §4)核心层提交,人工判断是否 cherry-pick;
3. 参考上游实现(读源码/查历史),用于解决我们自己的问题。

### 2.3 cherry-pick 规则(仅限季度核查命中时)

- **范围**:仅 `core/`、`servers/rendering`、`servers/audio`、`core/input`、
  `platform/windows`、`drivers/vulkan`、`main/` 内的 **bug 修复/性能优化**;
- **排除**:新功能、重构、编辑器相关、我们不用的平台(Metal/D3D12/移动);
- **流程**:① 评估影响面(是否触及我们改过的 dsw.cpp/main/unigo);
  ② 有冲突则**手解**并回归验证;③ 无冲突 cherry-pick 后全量构建+测试;
- **记录**:每次 cherry-pick 在 `unigo/README.md` 追加一行(上游 commit + 日期 + 内容)。

### 2.4 fork 基线(冻结)

- fork 基线 = 上游 `20a52a8988`(Merge #121392 metal_improvements);
- 我们相对上游:领先 53(自研)、落后 74(不再追赶);
- 基线**冻结**,不随上游移动。

---

## 3. 内核精简清单(核心功能删除/保留)

> 原则:**构建层白名单 + 物理源码精简双轨**。已决策 template_release(非 editor),
> 故凡不被白名单模块 + 渲染核心编译引用的源码,可物理删除。
> 物理删除前先 `scons` 空跑验证依赖。

### 3.1 顶层目录保留/删除

| 目录 | 保留/精简 | 说明 |
|---|---|---|
| `core/` | ✅ 全保留 | 硬依赖(math/string/variant/OS/Thread/资源) |
| `servers/` | ✅ 保留 | 渲染核心;内部可删子目录见 3.2 |
| `scene/` | ⚠️ 需保留结构 | 渲染单位 Viewport/Window 在 scene;2D 节点/大部分节点类可删(见 3.3) |
| `drivers/` | ⚠️ 精简 | 只留 vulkan + windows + png(+ egl 若需);删 d3d12/metal/alsa/coreaudio/pulseaudio/wasapi 等 |
| `platform/` | ⚠️ 精简 | 只留 windows;删 android/ios/macos/web/linuxbsd/x11 |
| `modules/` | ⚠️ 精简 | 只留白名单(见 unigo_modules.cfg);物理删 gdscript/mono/editor 等大模块 |
| `editor/` | 🗑️ **可删** | template_release 不编译;46k 行 |
| `main/` | ✅ 保留 | 启动/迭代(我们改了) |
| `tests/` | 🗑️ 可删 | 上游测试(74k 行);我们用自己的测试 |
| `thirdparty/` | ⚠️ 精简 | 只留实际用到的(glslang/mbedtls/…),删其余 |
| `doc/` `docs/` `misc/` | 🗑️ 可删 | 上游文档/杂项 |
| `unigo/` | ✅ 保留 | 我们自己的构建体系 |
| `modules/unigo` | ✅ 保留 | 我们的 C ABI 模块 |

### 3.2 servers/ 内部精简

| 子目录 | 保留/删除 | 说明 |
|---|---|---|
| `rendering/` | ✅ 全保留 | 渲染核心 |
| `audio/` | ✅ 保留 | 音频(窗口零耦合,可独立) |
| `physics_2d/` `physics_3d/` | ✅ 保留 | 物理服务器 |
| `camera/` `text/` `xr/` | 🗑️ 可删 | 不用 |
| `navigation_2d/` `navigation_3d/` | 🗑️ 可删 | 不用(需确认无引用) |
| `display/` `movie_writer/` `debugger/` | 🗑️ 可删 | 编辑/调试相关 |

### 3.3 scene/ 精简(最难,谨慎)

- **保留**:`main/{viewport,window}.{h,cpp}`、`3d/` 中渲染相关节点、`resources/`
  中渲染相关(材质/纹理/Mesh)等被渲染核心引用的;
- **可删**:`2d/` 大部分、`gui/`(若不用 Control)、`animation/` 大部分、
  不用的节点类;
- **铁律**:删前必须 `scons` 验证(引用爆炸点最可能在这);
- 第一轮**建议只删明确的 editor/tests/无用平台**,scene/servers 内部留到
  有把握时再逐块删(复杂度控制)。

### 3.4 已做的精简(记录,勿重复)

| 项 | 状态 |
|---|---|
| 模块白名单(modules_enabled_by_default=no,~17 模块) | ✅ 已做(unigo_modules.cfg) |
| template_release 构建(非 editor) | ✅ 已做(pure.dll) |
| nomono / deprecated=no / 非 vulkan 驱动关闭 | ✅ 已做(unigo_build_profile.txt) |
| editor/scene 物理删除 | ❌ 未做(源码仍在,仅不编译) |

### 3.5 我们的独有改动(不可被上游覆盖/删除)

| 文件 | 内容 |
|---|---|
| `modules/unigo/*` | C ABI 外壳(unigo_engine_*) |
| `platform/windows/display_server_windows.cpp` | 窗口契约/vsync/外部窗补丁 |
| `main/main.cpp` | 启动参数/unigo 注入 |
| `SConstruct` | 模块开关/构建参数 |
| `unigo/` | 构建体系白名单 |

---

## 4. 定期核查核心层机制(权威流程)

### 4.1 周期

**每季度一次**(3/6/9/12 月),10-15 分钟。

### 4.2 核查范围(只看这些,其余无视)

```
core/
servers/rendering/
servers/audio/
core/input/
platform/windows/
drivers/vulkan/
main/
```

### 4.3 核查流程

```bash
cd vendor/godot
git fetch upstream                                # 1. 拉取(不合并)
# 2. 看本季度上游核心层提交
git log --oneline <上次核查点>..upstream/master -- core/ servers/rendering/ \
    servers/audio/ core/input/ platform/windows/ drivers/vulkan/ main/
# 3. 逐个看内容(git show),判断:
#    - bug 修复/性能优化且我们用到 → 按 §2.3 cherry-pick
#    - 新功能/重构/不用平台/编辑器 → 跳过
# 4. 更新核查点记录(见 unigo/README.md)
```

### 4.4 核查记录(每次追加到 unigo/README.md)

```text
## 上游核查日志
- 2026-Q4(检查到 upstream X):核心层提交 N 个,cherry-pick 0 个。
  忽略:全部为 [编辑器/新功能/不用平台]。
```

### 4.5 什么情况下重新考虑"恢复同步"

- 上游核心层出现**重大架构变化**(如 5.0 渲染重构)且我们想跟随;
- 上游修复了**我们实际遇到的 bug**(此时按 bug 报告 cherry-pick 单条,不需全同步);
- 上述均为**单点决策**,不自动恢复同步,需团队讨论。

---

## 5. 与既有文档的关系

- 本规范**取代** unigo/README.md 中"与上游最小冲突/target=editor"的旧表述
  (该 README 已过时,需同步修订);
- 架构决策记录 §6.3"不做激进物理删除"红线:**随本决策更新**——
  完全分叉后,物理精简是允许的(按本规范 §3 范围与流程);
- 本规范落位子模块 `vendor/godot/docs/architecture/`,主仓引用之。
