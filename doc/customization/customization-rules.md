# 3D 定制规则与阶段计划

## 项目定位

我们要做的是一个基于 Godot 的 3D 游戏开发定制引擎。它不是通用 Godot 的完整替代品，也不是只给某一个游戏项目用的最小运行时。

优先服务对象：

- 3D 游戏编辑器工作流。
- 3D 场景、材质、动画、光照、后处理和导入管线。
- 3D 物理、导航、碰撞、调试工具。
- Windows 和 macOS 桌面开发、本地调试、桌面 3D 发布链。
- Android、iOS、Web 的 3D 发布链。
- 后续可同步 Godot 官方更新的长期分支。

## 裁剪原则

1. 先禁用，后删除。

   优先使用 SCons 选项、模块开关、build profile、editor feature profile。只有当一个功能长期不需要、依赖边界清楚、验证充分、台账完整时，才允许物理删除源码。

2. 先保留编辑器可用性。

   这个项目第一目标是 3D 开发体验，不是最小运行时。任何裁剪如果会破坏 Project Manager、Inspector、SceneTree、资源导入、3D Viewport 或脚本调试，都需要降级为候选项。

3. 保持补丁可同步。

   能通过配置解决的，不改上游源码。必须改源码时，改动要小，位置要集中，注释说明“这是 3D 定制引擎裁剪点”。

4. 删除必须可追溯。

   每个删除或禁用项都要进入 `removal-ledger.md`。记录不完整时，不允许合入。

5. 不把“与 3D 无关”简单等同于“可以删”。

   很多看似 2D 或通用的系统被编辑器、UI、导入器或调试工具依赖。比如 Control/UI、Resource、Variant、Image、Input、Audio 不能粗暴删除。

## 功能分级

### A 级：核心保留

- 3D 节点和场景系统：Node3D、Camera3D、Light3D、MeshInstance3D、Skeleton3D 等。
- 3D 渲染：Vulkan/RD、材质、Shader、Environment、后处理。
- 资源系统：Resource、PackedScene、Image、Texture、Mesh、Animation。
- 编辑器核心：Project Manager、SceneTree、Inspector、FileSystem、Import Dock、3D Viewport。
- 3D 导入：glTF、FBX、材质贴图、骨骼动画、网格优化。
- 3D 物理和导航：PhysicsServer3D、Jolt 或 GodotPhysics3D、NavigationServer3D。
- 脚本和扩展：GDScript、GDExtension、ClassDB、Variant。
- 基础平台：Windows、macOS、Android、iOS、Web、文件系统、输入、线程、网络基础能力。

### B 级：默认保留但可精简

- 音频系统：保留基础播放，格式支持可按项目需要减少。
- GUI/Control：编辑器依赖强，不能整体删除，但可评估高级控件。
- 多语言文本：早期保留高级文本服务，后续可评估 fallback text server。
- 导出系统：保留 Windows、macOS、Android、iOS、Web。平台导出链不是当前裁剪目标。
- Multiplayer / WebSocket / WebRTC：如果项目不做网络游戏，可进入候选裁剪。

### C 级：优先候选裁剪

- 2D 专用编辑器工作区和 CanvasItem 编辑工具。
- 2D 物理和 2D 导航运行时。
- VR/XR 相关模块，例如 OpenXR、Mobile VR、WebXR。
- 各平台专属的可选额外依赖。比如 Windows 的 AccessKit、ANGLE、D3D12 依赖，macOS 的可选系统集成或额外打包依赖。平台发布链本身默认保留。
- 示例、测试、文档中与发行无关的大型资源，注意不要影响上游同步。

### D 级：禁止粗暴删除

- `core/` 的 Variant、Object、Resource、String、Math、IO 等基础设施。
- `scene/gui` 和 `editor` 的基础 UI。编辑器本身需要它们。
- `thirdparty` 中被 3D 导入、纹理、字体、压缩依赖的库。
- 构建系统和平台检测脚本。

## 阶段计划

### 第 0 阶段：建立规则

目标：只建文档和流程，不做大规模删除。

产物：

- `AGENTS.md` 增加主规则。
- `doc/customization/` 增加详细规则、删除台账、同步策略和构建计划。
- 保留可成功编译的基线命令。

验证：

```powershell
scons platform=windows dev_build=yes d3d12=no accesskit=no angle=no -j8
.\bin\godot.windows.editor.dev.x86_64.exe --version
```

### 第 1 阶段：软裁剪

目标：用构建参数和配置关闭明显不需要的功能，不删除源码。

候选：

- `d3d12=no`
- `accesskit=no`
- `angle=no`
- `disable_physics_2d=yes`
- `module_navigation_2d_enabled=no`
- `module_openxr_enabled=no`
- `module_webxr_enabled=no`
- `module_mobile_vr_enabled=no`

要求：

- 每增加一个禁用项，都要独立构建验证。
- 失败时记录依赖原因，不硬删。

### 第 2 阶段：编辑器体验裁剪

目标：让编辑器默认聚焦 3D。

方式：

- 先使用 editor feature profile 隐藏 2D 工作区和不需要的节点。
- 再评估是否修改编辑器入口，让 3D 工作区成为默认。
- 最后才考虑删除 2D 编辑器插件代码。

风险：

- Godot 编辑器大量 UI 和资源预览依赖 2D/CanvasItem 概念。
- 隐藏功能和删除功能是两件事，不能混在同一个提交里。

### 第 3 阶段：源码级裁剪

目标：物理删除已验证长期不用的模块或平台。

允许条件：

- 已有至少一个阶段的软裁剪记录。
- 删除项在台账中有完整记录。
- 能说明上游更新触及该区域时如何处理。
- 有构建和启动验证。

推荐顺序：

1. VR/XR 相关模块。
2. 明确不服务 3D 发布链的可选模块。
3. 网络和音视频可选模块。
4. 2D 运行时服务。
5. 2D 编辑器插件。

## 每次定制提交的检查清单

- 是否更新了 `removal-ledger.md`？
- 是否说明了这个功能为什么与 3D 定制目标无关？
- 是否记录了回滚方式？
- 是否保留了官方更新同步判断依据？
- 是否跑过至少一次构建或给出明确未验证原因？

## 软裁剪晋升规则

`soft-prune` 是实验场，`dev` 是正式开发基线。

软裁剪项的生命周期：

1. 先加入 `misc/customization/scons-profiles/windows_3d_soft_prune_experimental.py`。
2. 用 `soft-prune` preset 编译和启动验证。
3. 如果失败，记录原因并从实验 profile 移除。
4. 如果稳定，再写入 `removal-ledger.md`。
5. 决定下一步：继续留在实验 profile、晋升到正式 `dev` profile 作为长期软禁用，或进入源码级硬裁剪评估。
6. 如果已经硬裁剪源码，正式构建仍应回到 `dev` preset。硬裁剪后的源码本身应该让 `dev` 构建自然通过，而不是长期依赖实验 profile。

不要直接把 `soft-prune` 当成日常开发配置。它可以同时包含多个风险项，适合验证影响，不适合作为团队稳定基线。
