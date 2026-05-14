# 3D 定制规则与阶段计划

## 项目定位

我们要做的是一个基于 Godot 的中小型风格化 3D 游戏开发定制引擎。它不是通用 Godot 的完整替代品，也不是只给某一个游戏项目用的最小运行时，更不是面向 AAA 写实大项目的全能引擎。

目标用户：

- 青少年/新手入门 3D 游戏开发者。
- 独立开发者、个人开发者和小团队。
- 想做中小规模、非写实、风格化 3D 游戏的人。

优先服务对象：

- 清晰、少干扰、适合新手理解的 3D 游戏编辑器工作流。
- 中小型 3D 场景、风格化材质、动画、光照、后处理和导入管线。
- 3D 物理、导航、碰撞、调试工具。
- Windows 和 macOS 桌面编辑器开发、本地调试。
- 后续可同步 Godot 官方更新的长期分支。

不优先服务：

- AAA 写实渲染制作流程。
- 大型开放世界和超大团队协作流程。
- 专业影视、建筑可视化、数字孪生等非游戏工作流。
- VR/XR 专用项目。
- 以复杂网络架构为核心的大型多人在线项目。

## 裁剪原则

1. 先隐藏，再禁用，后删除。

    当前阶段只做 editor 定制。编辑器可用的 SCons 裁剪选项很少，主路线应是 editor feature profile、默认工作区、插件/菜单隐藏和小范围源码定制。SCons 只用于少数 editor 可用开关，例如 VR/XR、可选依赖。只有当一个功能长期不需要、依赖边界清楚、验证充分、台账完整时，才允许物理删除源码。

2. 先保留编辑器可用性。

    这个项目第一目标是 3D 开发体验，不是最小运行时。任何裁剪如果会破坏 Project Manager、Inspector、SceneTree、资源导入、3D Viewport 或脚本调试，都需要降级为候选项。

3. 保持补丁可同步。

    能通过配置解决的，不改上游源码。必须改源码时，改动要小，位置要集中，注释说明“这是 3D 定制引擎裁剪点”。

4. 删除必须可追溯。

    每个删除或禁用项都要进入 `removal-ledger.md`。记录不完整时，不允许合入。

5. 不把“与 3D 无关”简单等同于“可以删”。

    很多看似 2D 或通用的系统被编辑器、UI、导入器或调试工具依赖。比如 Control/UI、Resource、Variant、Image、Input、Audio 不能粗暴删除。

6. 暂时不硬删除 2D 和 C# 支持。

    当前核心目标仍然是 3D 编辑器体验，但 2D 和 C# 以后可能作为可选支持保留。没有明确决策前，只允许隐藏入口、降低优先级或记录候选项，不物理删除相关源码、模块和编辑器功能。

## 功能分级

### A 级：核心保留

- 3D 节点和场景系统：Node3D、Camera3D、Light3D、MeshInstance3D、Skeleton3D 等。
- 3D 渲染：Vulkan/RD、材质、Shader、Environment、后处理，优先服务风格化和非写实美术。
- 资源系统：Resource、PackedScene、Image、Texture、Mesh、Animation。
- 编辑器核心：Project Manager、SceneTree、Inspector、FileSystem、Import Dock、3D Viewport。
- 3D 导入：glTF、FBX、材质贴图、骨骼动画、网格优化，优先保证常见 DCC 到引擎的入门链路顺畅。
- 3D 物理和导航：PhysicsServer3D、Jolt 或 GodotPhysics3D、NavigationServer3D。
- 脚本和扩展：GDScript、GDExtension、ClassDB、Variant。
- 基础平台：Windows、macOS、Android、iOS、Web、文件系统、输入、线程、网络基础能力。

### B 级：默认保留但可精简

- 音频系统：保留基础播放，格式支持可按项目需要减少。
- GUI/Control：编辑器依赖强，不能整体删除，但可评估高级控件。
- 多语言文本：早期保留高级文本服务，后续可评估 fallback text server。
- 导出系统：当前阶段不做 export template 相关工作，先保持上游默认状态，不裁剪、不验证、不维护发布模板。
- C# / .NET / Mono 支持：当前不是核心目标，但可能作为可选脚本支持保留；暂不硬删除相关代码和编辑器功能。
- Multiplayer / WebSocket / WebRTC：如果项目不做网络游戏，可进入候选裁剪。
- AAA 写实项目专用或重型团队流程相关入口：默认低优先级，除非它也明显改善中小型风格化 3D 项目。

### C 级：优先候选裁剪

- 2D 专用编辑器工作区和 CanvasItem 编辑工具。当前只评估隐藏或弱化入口，不硬删除源码。
- 2D 物理和 2D 导航运行时。当前阶段不处理，因为它们只能用于 export template 裁剪，不能用于 editor 构建；未来确认是否作为可选 2D 支持保留。
- VR/XR 相关模块，例如 OpenXR、Mobile VR、WebXR。
- AAA 写实、重型多人、大型团队协作专用入口。
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

### 第 1 阶段：编辑器软裁剪

目标：只针对 editor，让编辑器体验先聚焦 3D，不删除源码。

构建层可用项很少，当前只保留：

- `d3d12=no`
- `accesskit=no`
- `angle=no`
- `module_openxr_enabled=no`
- `module_webxr_enabled=no`
- `module_mobile_vr_enabled=no`

编辑器体验层优先项：

- 隐藏或弱化 2D 工作区入口。
- 默认打开 3D 工作区。
- 隐藏 VR/XR 相关菜单、节点、项目设置入口。
- 整理项目管理器和新建项目默认模板，让它更像 3D 专用引擎。

要求：

- 每增加一个构建禁用项，都要独立构建验证。
- 每增加一个编辑器体验隐藏项，都要记录影响范围和手动验证步骤。
- 失败时记录依赖原因，不硬删。
- 不做 export template 相关裁剪；`disable_physics_2d`、`disable_navigation_2d` 这类只能用于模板构建的选项暂时挂起。

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
4. 2D 编辑器插件。
5. 2D 运行时服务。当前阶段不处理，等重新启动 export template 工作后再评估。

注意：2D 和 C# 支持目前不进入硬裁剪推荐顺序。以后如果要改变这个边界，需要先更新本文件和 `removal-ledger.md`，再做独立验证。

## 每次定制提交的检查清单

- 是否更新了 `removal-ledger.md`？
- 是否说明了这个功能为什么与 3D 定制目标无关？
- 是否记录了回滚方式？
- 是否保留了官方更新同步判断依据？
- 是否跑过至少一次构建或给出明确未验证原因？

## 编辑器裁剪晋升规则

`soft-prune` 是实验场，`dev` 是正式开发基线。

软裁剪项的生命周期：

1. 先加入 `misc/customization/scons-profiles/windows_3d_soft_prune_experimental.py`。
2. 用 `soft-prune` preset 编译和启动验证。
3. 如果失败，记录原因并从实验 profile 移除。
4. 如果稳定，再写入 `removal-ledger.md`。
5. 决定下一步：继续留在实验 profile、晋升到正式 `dev` profile 作为长期软禁用，或进入源码级硬裁剪评估。
6. 如果已经硬裁剪源码，正式构建仍应回到 `dev` preset。硬裁剪后的源码本身应该让 `dev` 构建自然通过，而不是长期依赖实验 profile。

不要直接把 `soft-prune` 当成日常开发配置。它可以同时包含多个风险项，适合验证影响，不适合作为团队稳定基线。

编辑器体验裁剪项的生命周期：

1. 先用配置、feature profile 或小范围代码隐藏入口。
2. 记录到 `removal-ledger.md`。
3. 手动验证 Project Manager、3D Viewport、Inspector、资源导入、场景运行。
4. 稳定后再考虑是否删除对应插件或菜单代码。
