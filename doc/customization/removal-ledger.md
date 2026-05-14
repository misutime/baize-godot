# 删除与禁用台账

这个文件是 3D 定制引擎最重要的长期记录。任何功能被禁用、隐藏、删除、替换前，都要先在这里登记。

状态说明：

- `候选`：准备评估，还没改。
- `软禁用`：通过 SCons、build profile、editor feature profile 或配置禁用，源码仍保留。
- `源码删除`：已物理删除或大幅改写上游源码。
- `保留`：评估后决定暂时保留。
- `回滚`：曾经裁剪，后来恢复。

## 台账模板

复制下面模板新增记录。

```markdown
### YYYY-MM-DD: 功能名称

- 状态：
- 类型：构建选项 / 模块 / 编辑器功能 / 平台 / 运行时系统 / 第三方库 / 文档资源
- 上游路径：
- 相关 SCons 选项：
- 裁剪理由：
- 3D 项目影响：
- 编辑器影响：
- 已知依赖：
- 替代方案：
- 回滚方式：
- 官方同步策略：
- 验证命令：
- 验证结果：
- 决策人/记录人：
```

## 当前基线记录

### 2026-05-14: Direct3D 12 支持

- 状态：软禁用
- 类型：构建选项 / Windows 渲染后端
- 上游路径：`platform/windows`、`drivers/d3d12`、相关 thirdparty 依赖
- 相关 SCons 选项：`d3d12=no`
- 裁剪理由：早期开发只需要先编译和启动编辑器，D3D12 需要额外依赖，增加环境复杂度。
- 3D 项目影响：仍可使用 Vulkan 等后端进行 3D 开发。需要 D3D12 专项测试时再恢复。
- 编辑器影响：无已知基础启动影响。
- 已知依赖：DirectX Agility SDK、PIX 相关依赖。
- 替代方案：使用 Vulkan / OpenGL 相关路径。
- 回滚方式：移除 `d3d12=no`，按官方文档安装 D3D12 依赖后重新构建。
- 官方同步策略：D3D12 是 Windows 桌面 3D 的相关能力，不做源码删除。官方 D3D12 更新如果涉及 Windows 渲染抽象、RD、Shader 编译或通用 3D 修复，应优先评估合入。
- 验证命令：`scons platform=windows dev_build=yes d3d12=no accesskit=no angle=no -j8`
- 验证结果：已成功编译出 Windows editor dev 可执行文件。
- 决策人/记录人：Codex

### 2026-05-14: AccessKit 屏幕阅读器支持

- 状态：软禁用
- 类型：构建选项 / 无障碍支持
- 上游路径：`platform/windows`、AccessKit 相关依赖安装脚本
- 相关 SCons 选项：`accesskit=no`
- 裁剪理由：早期 3D 开发环境不需要屏幕阅读器支持，且该依赖会阻塞干净构建。
- 3D 项目影响：无直接影响。
- 编辑器影响：会失去对应无障碍支持，不影响普通编辑器启动。
- 已知依赖：`misc/scripts/install_accesskit.py`
- 替代方案：无障碍需求出现后恢复。
- 回滚方式：运行 `python misc\scripts\install_accesskit.py`，移除 `accesskit=no` 后重新构建。
- 官方同步策略：无障碍相关更新默认跳过，除非它影响编辑器基础 UI、Windows 消息处理或输入系统。
- 验证命令：`scons platform=windows dev_build=yes d3d12=no accesskit=no angle=no -j8`
- 验证结果：作为早期构建基线使用。
- 决策人/记录人：Codex

### 2026-05-14: ANGLE 渲染驱动

- 状态：软禁用
- 类型：构建选项 / Windows 渲染兼容层
- 上游路径：ANGLE 相关构建脚本和 Windows 渲染后端
- 相关 SCons 选项：`angle=no`
- 裁剪理由：早期开发不需要 OpenGL ES 到 Direct3D 的转换层，关闭后减少依赖安装。
- 3D 项目影响：如果未来需要 ANGLE 兼容路径，需要恢复。
- 编辑器影响：无已知基础启动影响。
- 已知依赖：`misc/scripts/install_angle.py`
- 替代方案：优先使用 Vulkan。
- 回滚方式：运行 `python misc\scripts\install_angle.py`，移除 `angle=no` 后重新构建。
- 官方同步策略：ANGLE 是 Windows 兼容层，默认低优先级；如果涉及 Windows 图形初始化、兼容性或渲染崩溃修复，再评估。
- 验证命令：`scons platform=windows dev_build=yes d3d12=no accesskit=no angle=no -j8`
- 验证结果：作为早期构建基线使用。
- 决策人/记录人：Codex

## 候选裁剪清单

### 2D 物理

- 状态：挂起
- 类型：运行时系统
- 上游路径：`servers/physics_2d`、相关 scene 2D 节点
- 相关 SCons 选项：`disable_physics_2d=yes`
- 裁剪理由：3D 专用引擎通常不需要 2D 物理。
- 风险：编辑器、导入器或测试可能间接引用 2D 类型。
- 验证命令：`scons profile=misc/customization/scons-profiles/windows_3d_soft_prune_experimental.py -j8`
- 验证结果：editor 构建不允许使用 `disable_physics_2d`；该选项只能用于 export template。当前阶段不做 export template 工作，因此挂起。
- 下一步：等重新启动 export template 裁剪后再评估。

### 2D 导航

- 状态：挂起
- 类型：模块 / 运行时系统
- 上游路径：`modules/navigation_2d`、`servers/navigation_2d`
- 相关 SCons 选项：`disable_navigation_2d=yes`
- 裁剪理由：3D 专用引擎优先保留 Navigation3D。
- 风险：类注册、文档生成、编辑器节点列表可能引用。
- 验证命令：`scons profile=misc/customization/scons-profiles/windows_3d_soft_prune_experimental.py -j8`
- 验证结果：editor 构建不允许使用 `disable_navigation_2d`；该选项只能用于 export template。当前阶段不做 export template 工作，因此挂起。
- 下一步：等重新启动 export template 裁剪后再评估。

### VR / XR / WebXR / Mobile VR

- 状态：软禁用实验中
- 类型：模块 / 平台功能
- 上游路径：`modules/openxr`、`modules/webxr`、`modules/mobile_vr`
- 相关 SCons 选项：`disable_xr=yes`、`module_openxr_enabled=no`、`module_webxr_enabled=no`、`module_mobile_vr_enabled=no`
- 裁剪理由：当前目标是普通 3D 游戏开发，不做 VR/XR。注意 Web 平台本身保留，候选裁剪的是 WebXR，不是 Web 导出链。
- 风险：3D 渲染路径可能有共享代码，删除源码前必须检查依赖。
- 验证命令：`.\misc\customization\build-windows.ps1 -Preset prune-vr-xr -Jobs 8`
- 验证结果：Windows editor dev 构建已通过。第一次不带 `d3d12=no` 的构建失败，原因是正在运行的 Godot 占用 `bin\D3D12Core.dll`，不是 VR/XR 裁剪错误；关闭编辑器后重新运行 `scons profile=misc/customization/scons-profiles/windows_3d_prune_vr_xr.py -j8` 构建通过，版本输出 `4.7.beta.custom_build.255a71746`。历史试验中曾在 `target=template_debug` 中和 2D 物理/导航组合软裁剪一起构建通过，但当前阶段不做 export template 工作，此结果不作为当前阶段晋升依据。
- 下一步：启动编辑器并打开 3D 项目做手动验证；如果通过，再考虑晋升到正式 `dev` profile 或进入硬裁剪评估。

### 2D 编辑器工作区

- 状态：候选
- 类型：编辑器功能
- 上游路径：`editor/plugins/canvas_item_editor_plugin.*` 等
- 相关 SCons 选项：暂无直接等价选项
- 裁剪理由：编辑器希望专注 3D。
- 风险：Godot 编辑器大量 UI、资源预览和 Control 仍依赖 2D/CanvasItem 概念。
- 下一步：先使用 editor feature profile 或小范围入口隐藏，不直接删源码。当前阶段这类编辑器体验裁剪比 SCons 构建裁剪更重要。
