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
| Metal 后端(subpass) | 2 | ⚠️ Mac/iOS 目标平台用 Metal,相关修复季度核查留意 |
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

- **范围**:仅核心层的 **bug 修复/性能优化**,按目标平台矩阵(§3.0):
  - 通用核心:`core/` `servers/rendering` `servers/audio` `core/input` `main/`
  - Windows:`platform/windows` `drivers/vulkan` `drivers/windows`
  - mac/iOS:`platform/macos` `platform/ios` `drivers/vulkan` `drivers/metal`
  - Android:`platform/android` `drivers/vulkan`
- **排除**:新功能、重构、编辑器相关、确定不用的平台(按平台矩阵,如 D3D12/Web 未定先留)
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

### 3.0 目标平台矩阵(决定裁剪的权威依据,先定此再谈删)

> **已定架构决策(决策记录 §4.4)**:目标平台 = **Win / Mac / Android / iOS**,
> 这是当初选 Godot 整引擎而非剥 RenderingServer 的核心原因之一
> (三需求:坑少 / **多平台** / 风格化够用)。渲染目标限定 Mobile 也因"目标支持手机端"。

| 平台 | 状态 | 对裁剪的影响 |
|---|---|---|---|
| Windows | ✅ 当前主目标 | platform/windows、drivers/{vulkan,windows,png} 保留 |
| macOS | ✅ 目标平台 | **platform/macos、drivers/{vulkan,metal}、drivers/{coreaudio 等 mac 音频} 保留** |
| Android | ✅ 目标平台(手机端) | **platform/android、drivers/vulkan(Android 也用 Vulkan)、音频驱动保留** |
| iOS | ✅ 目标平台(手机端) | **platform/ios、drivers/{vulkan,metal} 保留** |
| Linux/BSD | ⚠️ 可能未来(低成本开源向) | 不确定 → **先保留**(platform/linuxbsd、x11) |
| Web | ⚠️ 可能未来(HTML5 向) | 不确定 → 先保留(platform/web) |
| VisionOS | ❓ 不确定 | 先保留(成本低,确认前不删) |

**铁律**:
1. 只删**确定不可能用的**;
2. 目标平台相关(Win/Mac/Android/iOS)+ 不确定的,**一律保留**;
3. 每删一项须在本清单标注"依据:xxx"(对应某个确定性判断),
   不允许"当前不用就删"。

### 3.1 顶层目录保留/删除

| 目录 | 保留/精简 | 说明 |
|---|---|---|
| `core/` | ✅ 全保留 | 硬依赖(math/string/variant/OS/Thread/资源) |
| `servers/` | ✅ 保留 | 渲染核心;内部可删子目录见 3.2 |
| `scene/` | ⚠️ 需保留结构 | 渲染单位 Viewport/Window 在 scene;详见 3.3 |
| `drivers/` | ⚠️ 按平台矩阵保留 | **vulkan/metal(目标平台用)/windows 平台驱动保留**;只删**确定不用**的(见 3.2 注) |
| `platform/` | ⚠️ 按平台矩阵保留 | **windows/macos/android/ios 保留**(目标平台);linuxbsd/web/visionos 不确定 → 先留 |
| `modules/` | ⚠️ 白名单已裁 | 构建层已白名单(见 unigo_modules.cfg);**物理删除暂不含模块源码**(gdscript/mono 等虽确定不用,但删收益低、影响大,待第一轮精简后评估) |
| `editor/` | 🗑️ **可删** | template_release 不编译;46k 行。**确认**:editor 仅编辑器构建用,运行时不编(已验) |
| `main/` | ✅ 保留 | 启动/迭代(我们改了) |
| `tests/` | 🗑️ 可删 | 上游测试(74k 行);我们用自己的测试 |
| `thirdparty/` | ⚠️ 按需 | 只删**确认无引用**的(如只给被删模块用的);不确定保留 |
| `doc/` `docs/` `misc/` | 🗑️ 可删 | 上游文档/杂项(不属运行时,确定不用) |
| `unigo/` | ✅ 保留 | 我们自己的构建体系 |
| `modules/unigo` | ✅ 保留 | 我们的 C ABI 模块 |

> **白名单 ≠ 实际编译(重要澄清,gltf/fbx 教训)**:即使模块在白名单,
> 其 SCsub 可能仍有 `env.editor_build` 条件——例如 gltf/fbx 的 `editor/*.cpp`
> 只在编辑器构建编译,但核心 `*.cpp + structures/*.cpp` **无条件编译**。
> 因此判断"某模块/某目录是否编译"**必须以 SCsub 实读为准**,不能凭模块名或
> 单个 grep 下结论。精简判断铁律:不确定 = 保留。

### 3.2 servers/ 内部精简

> **drivers/ 补充**:删驱动须按平台矩阵判断——macOS/iOS 用 metal,Android 用 vulkan,
> 各平台音频驱动(coreaudio/alsa/pulseaudio 等)跟着平台走,**不确定的先留**。
> 真正"确定不用"的示例:`d3d12`(我们已定 vulkan,决策记录明确 d3d12=no)。

| 子目录 | 保留处遇 | 说明(引用依据) |
|---|---|---|---|
| `rendering/` | ✅ 保留 | 渲染核心 |
| `audio/` | ✅ 保留 | 音频服务器(核心) |
| `physics_2d/` `physics_3d/` | ✅ 保留 | 物理服务器(核心) |
| `display/` | ✅ **必须保留** | **DisplayServer 无条件注册**(register_server_types.cpp:404)——窗口方案核心 |
| `camera/` | ⚠️ 保留 | CameraServer 无条件注册(同:403);无依据不删 |
| `text/` | ✅ 保留 | TextServer(text_server_adv 模块实现引用) |
| `xr/` | ⚠️ 保留待定 | XR 未来未定,决策未定不删 |
| `navigation_2d/` `navigation_3d/` | ⚠️ 保留待定 | 游戏寻路未来可能用,先留 |
| `debugger/` | ⚠️ 保留 | 脚本调试器;确认注销路径前不删 |
| `movie_writer/` | ⚠️ 可条件裁 | 已有 `GD_IS_CLASS_ENABLED(MovieWriterPNGWAV)` 条件(378/391 行);**用开关而非删文件** |

### 3.3 scene/ 精简(最难,谨慎)

- **保留**:`main/{viewport,window}.{h,cpp}`、`3d/` 渲染相关节点、`resources/`
  渲染相关(材质/纹理/Mesh);**2D 相关先留**(Godot 2D 游戏也可能未来做,且
  2D/3D 共用 CanvasItem 基础,删错代价大);
- **可删**(确认不用的):如 editor 相关专用类;
- **铁律**:删前必须 `scons` 验证(引用爆炸点最可能在这);
- 第一轮**只删最高确定性项**(editor/tests/上游文档),scene/servers/平台相关
  一律不动,留到有明确需求判断时逐块评估(复杂度控制)。

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
# 通用核心
core/  servers/rendering/  servers/audio/  core/input/  main/
# 目标平台(按 §3.0 矩阵)
platform/windows/  platform/macos/  platform/android/  platform/ios/
drivers/vulkan/    drivers/metal/
# 注意:每次核查记下 upstream/master 新到哪,作为下次起点
```

### 4.3 核查流程

```bash
cd vendor/godot
git fetch upstream                                # 1. 拉取(不合并)
# 2. 看本季度上游核心层提交
git log --oneline <上次核查点>..upstream/master -- core/ servers/rendering/ \
    servers/audio/ core/input/ main/ platform/windows/ platform/macos/ \
    platform/android/ platform/ios/ drivers/vulkan/ drivers/metal/
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
