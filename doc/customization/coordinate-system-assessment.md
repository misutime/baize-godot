# 3D 坐标系改造评估

记录时间：2026-05-15

## 结论

不建议第一步直接把 Godot 内部 3D 坐标系整体改成 `X` 左右、`Y` 前后、`Z` 上下。

更新说明：2026-05-15 用户已确认接受破坏性修改，项目路线从“先做显示层适配”改为“全面迁移到 `+Y` 前、`+Z` 上”。本文件保留作为早期风险评估；实际实施状态以后以 `doc/customization/coordinate-system-migration-plan.md` 为准。

更稳的路线是先做“编辑器显示层适配”：在 3D 视图、坐标轴控件、Inspector 提示、默认相机/导航交互里，把新手看到和操作的概念包装成 `X` 左右、`Y` 前后、`Z` 上下；内部仍保留 Godot 上游的 `+X` 右、`+Y` 上、`-Z` 前。等显示层跑通、测试项目稳定后，再评估是否要动核心数学和资源格式。

原因很直接：Godot 的坐标系不是集中在一个配置里，而是穿透了数学类型、脚本 API、相机、灯光、物理、导航、导入导出、编辑器 gizmo、文档和大量项目资源。直接改核心语义会变成一次大规模兼容性破坏。

## 目标坐标系

希望对用户呈现为：

- `+X` 向右。
- `+Y` 向前，也就是屏幕里面。
- `+Z` 向上。

这仍然可以保持右手系：`X` 右、`Y` 前、`Z` 上时，`X × Y = Z`。

现有 Godot 约定是：

- `+X` 向右。
- `+Y` 向上。
- `-Z` 向前，`+Z` 向后。

本地官方文档证据：

- `references/godot-docs/classes/class_transform3d.rst`：说明 Godot 使用右手系，内置相机等类型约定 `-Z` forward、`+X` right、`+Y` up、`+Z` back。
- `references/godot-docs/classes/class_basis.rst`：同样说明 `-Z` forward、`+Y` up。
- `references/godot-docs/classes/class_node3d.rst`：`Node3D.look_at()` 会让本地 forward 轴 `-Z` 指向目标。
- `references/godot-docs/classes/class_vector3.rst`：`Vector3.FORWARD` 是前进方向，另有 `MODEL_FRONT` 用于模型资产方向。
- `references/godot-docs/tutorials/3d/introduction_to_3d.rst`：3D 入门章节说明 Godot 是右手坐标系。

## 源码现状

### 核心数学类型

关键文件：

- `core/math/vector3.h`
- `core/math/basis.h`
- `core/math/basis.cpp`
- `core/math/transform_3d.h`
- `core/math/transform_3d.cpp`
- `core/variant/variant_call.cpp`

重要事实：

- `Vector3::UP = { 0, 1, 0 }`。
- `Vector3::FORWARD = { 0, 0, -1 }`。
- `Vector3::BACK = { 0, 0, 1 }`。
- `Vector3::MODEL_TOP = { 0, 1, 0 }`。
- `Vector3::MODEL_FRONT = { 0, 0, 1 }`。

`Basis::looking_at()` 默认以 `Vector3::UP` 作为上方向，并通过 `p_use_model_front` 区分普通节点 forward 和模型资产 front。`Transform3D::looking_at()`、`Transform3D::set_look_at()` 继续复用这套逻辑。

影响判断：

- 如果只改 `Vector3::UP/FORWARD` 常量，编译可能能过，但会让所有依赖这些常量的脚本、编辑器、导入器和算法同时变义，风险极高。
- 如果改 `Basis::looking_at()`，相机、灯光、骨骼约束、路径跟随、粒子和插件都会受影响。
- 如果改 `Axis` 枚举含义或 `coord[3]` 顺序，会破坏序列化、脚本属性、数组访问、Inspector、资源文件、GDExtension ABI，基本不可取。

### Node3D 和公开脚本 API

关键文件：

- `scene/3d/node_3d.h`
- `scene/3d/node_3d.cpp`
- `doc/classes/Node3D.xml`
- `doc/classes/Vector3.xml`
- `doc/classes/Basis.xml`
- `doc/classes/Transform3D.xml`

重要事实：

- `Node3D::look_at()` 和 `look_at_from_position()` 默认参数是 `Vector3::UP`。
- 绑定到脚本时也使用 `DEFVAL(Vector3::UP)`。
- 官方文档写明本地 forward 轴是 `-Z`。

影响判断：

- 直接改 API 语义会破坏教程、示例、插件和已有脚本。
- 即使只面向新手，脚本里 `position.y` 已经长期代表高度。改成 `position.z` 代表高度，会让 Godot 生态里的几乎所有 3D 代码示例失效。
- 如果要推进，必须准备项目迁移器，把旧场景、动画轨道、脚本约定和导入资源做坐标映射。这个工作量远大于普通编辑器定制。

### 3D 编辑器视图和坐标轴控件

关键文件：

- `editor/scene/3d/node_3d_editor_plugin.h`
- `editor/scene/3d/node_3d_editor_plugin.cpp`
- `editor/scene/3d/node_3d_editor_gizmos.h`
- `editor/scene/3d/node_3d_editor_gizmos.cpp`
- `editor/scene/3d/gizmos/`

重要事实：

- 3D 视图定义了 `VIEW_TOP`、`VIEW_BOTTOM`、`VIEW_LEFT`、`VIEW_RIGHT`、`VIEW_FRONT`、`VIEW_REAR`。
- 右上角坐标轴控件直接按 `X/Y/Z` 绘制文字，并绑定 `VIEW_RIGHT`、`VIEW_TOP`、`VIEW_FRONT` 等视图。
- 视图游标用 `cursor.x_rot`、`cursor.y_rot`、`cursor.distance` 组合相机姿态。
- `_menu_option()` 中 `VIEW_TOP` 对应 `x_rot = PI / 2`，`VIEW_FRONT` 对应 `x_rot = 0, y_rot = 0`，这是典型 `Y` 上、`Z` 前后的编辑器相机逻辑。
- `_init_grid()` 维护 `grid[3]`、`grid_instance[3]`、`grid_visible[3]`，说明网格平面按三轴分别生成和显示。
- `_compute_transform()`、`_transform_gizmo_select()`、`update_transform_gizmo_view()` 负责移动/旋转/缩放手柄的选择和实际变换。

影响判断：

- 如果只想让用户“看起来”是 `Z` 上，可以优先改这一层：视图名称、坐标轴控件、网格默认平面、相机快捷视角、Inspector 显示映射。
- 这一层仍有风险：gizmo 的拖拽方向、局部/全局坐标、吸附、框选、相机预览和插件转发都依赖原始 `Transform3D`。需要统一做一组“显示坐标 <-> Godot 内部坐标”的转换函数，不能零散改。

### 相机、灯光和渲染

关键文件：

- `scene/3d/camera_3d.h`
- `scene/3d/camera_3d.cpp`
- `servers/rendering/renderer_scene_cull.cpp`
- `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp`
- `servers/rendering/renderer_rd/forward_mobile/render_forward_mobile.cpp`
- `scene/3d/lightmap_gi.cpp`
- `servers/rendering/renderer_rd/environment/gi.cpp`

重要事实：

- `Camera3D` 把自己的 `Transform3D` 传给 `RenderingServer::camera_set_transform()`。
- 渲染剔除和近裁剪面使用 `-basis.get_column(Vector3::AXIS_Z)` 当作相机前方。
- 灯光烘焙和 GI 多处使用 `-basis.get_column(Vector3::AXIS_Z)` 当作灯光方向。

影响判断：

- 如果核心改成 `+Y` 前，相机和灯光方向必须同步改，否则视图、投影、剔除、阴影、GI 会出现方向不一致。
- 渲染后端内部和 GPU shader 通常还有自己的坐标约定，不能只改 scene 层。
- 这类改动需要渲染截图回归，不适合作为早期小补丁。

### 物理、重力和碰撞形状

关键文件：

- `servers/physics_3d/physics_server_3d.cpp`
- `scene/3d/physics/static_body_3d.cpp`
- `scene/3d/physics/vehicle_body_3d.cpp`
- `scene/3d/physics/joints/`
- `scene/resources/3d/`

重要事实：

- 默认重力方向是 `physics/3d/default_gravity_vector = Vector3(0, -1, 0)`。
- HeightMap 相关代码用 `Vector3(w, height, d)`，即 `Y` 存高度、`X/Z` 做地面平面。
- Vehicle、Joint、Capsule、Cylinder 等逻辑多处默认使用 `Y` 作为竖直或悬挂方向。

影响判断：

- 如果只改编辑器显示，可以保持物理内部 `Y` 为高度，显示时把内部 `Y` 映射成用户看到的 `Z`。
- 如果真改核心，所有物理形状、默认重力、车辆、关节限制、碰撞调试绘制和保存数据都要一起改。

### Navigation3D

关键文件：

- `modules/navigation_3d/nav_map_3d.h`
- `modules/navigation_3d/3d/godot_navigation_server_3d.cpp`
- `scene/3d/navigation/navigation_obstacle_3d.cpp`
- `modules/navigation_3d/editor/navigation_obstacle_3d_editor_plugin.cpp`

重要事实：

- `NavMap3D` 默认 `up = Vector3(0, 1, 0)`。
- `NavigationObstacle3D` 注释明确写了障碍物投影到 `xz-plane`，所以只考虑绕 `y-axis` 的旋转。
- 调试线和投影障碍使用 `Vector3(point.x, height, point.z)`。

影响判断：

- 导航是最明显的 `Y` 上系统之一。
- 如果编辑器显示成 `Z` 上，导航属性里的 height、cell_height、agent height 需要文案和可视化适配，避免用户以为高度在内部 `z`。
- 如果核心改成 `Z` 上，导航烘焙、避障、投影、调试绘制都要重写和回归。

### 资源导入导出

关键文件：

- `modules/gltf/gltf_document.cpp`
- `modules/fbx/fbx_document.cpp`
- `modules/gltf/extensions/`
- `editor/import/`

重要事实：

- glTF / FBX 解析会把外部文件的矩阵列读入 Godot `Transform3D` 的 `Basis` 列。
- glTF 生态本身通常是 `Y` 上，Godot 内部也按 `Y` 上处理，导入链路相对自然。
- Godot 额外区分 `FORWARD` 和 `MODEL_FRONT`，说明“引擎前方”和“资产模型正面”本来就不是同一层概念。

影响判断：

- 如果内部改成 `Z` 上，glTF / FBX 导入导出都要加坐标系转换，并且要决定导出时是保持 Godot 定制坐标还是兼容标准 glTF。
- 骨骼、动画、root motion、碰撞扩展、物理扩展都会跟着受影响。
- 如果只做编辑器显示层适配，导入导出可以暂时不动，只在 UI 提示里说明内部兼容 Godot/glTF。

### 文档、类参考和教程

关键文件：

- `doc/classes/Vector3.xml`
- `doc/classes/Basis.xml`
- `doc/classes/Transform3D.xml`
- `doc/classes/Node3D.xml`
- `doc/classes/Camera3D.xml`
- `doc/classes/NavigationServer3D.xml`
- `doc/classes/NavigationObstacle3D.xml`
- `doc/customization/README.md`

重要事实：

- `doc/customization/README.md` 已经把“坐标系按 `X` 左右、`Y` 前后、`Z` 上下组织”列为长期目标。
- 官方类文档仍然会说明 `Y` 上、`-Z` 前。

影响判断：

- 如果走显示层适配，需要新增“用户显示坐标”和“Godot 内部坐标”的说明，避免未来维护者误解。
- 如果走核心改造，类参考和教程必须大面积改写，否则新手看到文档会和编辑器行为冲突。

## 可选方案

### 方案 A：只改显示文案和坐标轴标签

做法：

- 坐标轴控件中把用户看到的上方向标成 `Z`。
- Inspector 里把位置/旋转/缩放显示为用户坐标。
- 3D 视图菜单把 Top/Front 等概念按新坐标解释。

优点：

- 改动最小。
- 不破坏运行时、导入、脚本、资源格式。
- 适合快速验证新手是否更容易理解。

缺点：

- 用户写脚本时仍会遇到 `position.y` 才是真实高度。
- 如果没有明显提示，会形成“编辑器显示”和“脚本实际”的双重概念。

适用阶段：

- 推荐作为第一阶段原型。

### 方案 B：编辑器显示层做完整坐标映射

做法：

- 新增一组集中函数，例如“用户坐标”和“Godot 内部坐标”互转。
- 3D 视图、坐标轴控件、网格、Inspector 3D transform、相机快捷视角、gizmo 拖拽都通过这组函数显示和输入。
- 内部资源、脚本 API、物理、导航、渲染仍保持 Godot 原约定。

建议映射：

```text
用户 X = Godot X
用户 Y = Godot -Z
用户 Z = Godot Y

Godot X = 用户 X
Godot Y = 用户 Z
Godot Z = -用户 Y
```

这个映射保持右手系：用户 `X × Y = Z`。

优点：

- 用户大部分编辑器操作会符合 `Z` 上、`Y` 前。
- 不需要立刻破坏脚本、资源和导入导出。
- 可以集中在编辑器层，补丁边界比核心改造清楚。

缺点：

- Inspector 和脚本之间会存在显示差异。
- 插件 API、EditorNode3DGizmoPlugin、工具脚本如果直接使用 Godot 内部坐标，仍需说明。
- Transform 的旋转欧拉角映射比位置复杂，不能只交换分量。

适用阶段：

- 推荐作为正式第一阶段。

### 方案 C：核心语义整体改成 Z-up / Y-forward

做法：

- 改 `Vector3::UP/FORWARD/BACK/MODEL_*`。
- 改 `Basis::looking_at()`、`Transform3D::looking_at()`、`Node3D.look_at()`。
- 改 Camera3D、Light3D、渲染剔除、GI、物理默认重力、导航、导入导出、编辑器 gizmo、类文档、迁移器。

优点：

- 表面最统一，编辑器和脚本都能符合目标坐标系。

缺点：

- 破坏上游兼容性最大。
- Godot 插件、教程、项目资源、GDExtension、脚本示例基本都需要重看。
- 后续同步上游会非常痛苦，因为大量上游修复默认仍按 `Y` 上、`-Z` 前写。
- 一旦做到一半，会出现渲染、物理、导航、导入资源互相不一致的中间状态。

适用阶段：

- 当前阶段不建议做。
- 只有在确认这个定制引擎不再追求 Godot 生态兼容，并且有专门迁移器和测试矩阵后，才考虑。

## 推荐推进路线

### 第 1 阶段：只做编辑器显示层原型

目标：

- 不改 `core/math`。
- 不改资源保存格式。
- 不改脚本 API。
- 只让 3D 编辑器主界面初步呈现 `X` 左右、`Y` 前后、`Z` 上下。

建议范围：

- `editor/scene/3d/node_3d_editor_plugin.cpp`
- `editor/scene/3d/node_3d_editor_plugin.h`
- 必要时少量改 `editor/inspector/` 中的 Transform 属性显示。

重点：

- 坐标轴控件：点击 `+Z` 看 Top，点击 `+Y` 看 Front。
- 默认网格：用户看到地面是 `X/Y` 平面，内部仍是 Godot `X/Z` 平面。
- 视图菜单：Top/Front/Right 的说明按用户坐标表达。
- 增加内部注释：这是“显示坐标映射”，不是核心坐标系切换。

### 第 2 阶段：统一编辑器坐标映射入口

目标：

- 不让坐标转换散落在各个 gizmo 和菜单分支里。

建议新增位置：

- 优先考虑 `editor/scene/3d/` 下的小工具函数。
- 也可以考虑 `editor/editor_coordinate_mapping.*`，但只有多个编辑器模块复用时再拆。

建议函数：

- `to_display_position(Vector3 godot_pos)`
- `to_godot_position(Vector3 display_pos)`
- `to_display_basis(Basis godot_basis)`
- `to_godot_basis(Basis display_basis)`
- `to_display_transform(Transform3D godot_transform)`
- `to_godot_transform(Transform3D display_transform)`

注意：

- 位置可以简单映射分量。
- Basis / Transform 必须用一个固定转换矩阵包起来，不能只交换欧拉角。
- 欧拉角显示要单独评估，因为旋转顺序和万向节问题会让直觉变差。

### 第 3 阶段：补 Inspector、导入提示和教程

目标：

- 新手看到的 Transform 面板、3D 视图、创建默认场景和教程说明一致。

建议范围：

- Transform Inspector。
- 3D 新建场景默认 Camera / Light 摆放。
- `doc/customization/` 增加“坐标显示规则”说明。
- 必要时给项目设置增加开关，例如“使用新手坐标显示”。

### 第 4 阶段：只在证据充分后评估核心改造

进入条件：

- 编辑器显示层原型已通过真实 3D 项目验证。
- 已列出必须兼容和可以破坏的生态边界。
- 已有资源迁移方案。
- 已有渲染、物理、导航、导入导出测试。

## 受影响文件清单

高优先级：

- `core/math/vector3.h`：方向常量和轴枚举源头。
- `core/math/basis.cpp`：`looking_at()` 的 forward/up 规则。
- `core/math/transform_3d.*`：Transform look_at 包装。
- `scene/3d/node_3d.*`：Node3D 公开 API 和默认 up。
- `scene/3d/camera_3d.*`：相机 transform、投影、射线。
- `editor/scene/3d/node_3d_editor_plugin.*`：3D 视图、坐标轴、网格、相机游标、gizmo 操作主入口。
- `editor/scene/3d/node_3d_editor_gizmos.*`：gizmo 基类、handle、subgizmo。
- `editor/scene/3d/gizmos/`：各类节点 gizmo。

中高优先级：

- `servers/rendering/renderer_scene_cull.cpp`
- `servers/rendering/renderer_rd/`
- `scene/3d/lightmap_gi.cpp`
- `servers/physics_3d/physics_server_3d.cpp`
- `scene/3d/physics/`
- `modules/navigation_3d/`
- `scene/3d/navigation/`
- `modules/gltf/`
- `modules/fbx/`

文档和资源：

- `doc/classes/Vector3.xml`
- `doc/classes/Basis.xml`
- `doc/classes/Transform3D.xml`
- `doc/classes/Node3D.xml`
- `doc/classes/Camera3D.xml`
- `doc/classes/NavigationServer3D.xml`
- `doc/classes/NavigationObstacle3D.xml`
- `doc/customization/README.md`
- `doc/customization/document-map.md`

## 主要风险

### 1. 生态兼容风险

Godot 教程和插件默认都认为 `Y` 是高度、`-Z` 是前方。核心改造后，外部教程会变成误导材料。

### 2. 资源兼容风险

`.tscn`、`.tres`、动画轨道、导入缓存、glTF/FBX 保存的 Transform 都会受坐标语义影响。旧项目需要迁移。

### 3. 渲染方向风险

相机和灯光都依赖 `-Z` 作为前方。只改编辑器不改渲染，用户看到的“前”可能和阴影、GI、Camera3D 实际 forward 不一致；只改渲染不改编辑器，gizmo 又会错。

### 4. 物理和导航风险

默认重力、HeightMap、导航投影、障碍物高度都以 `Y` 为竖直方向。核心改造会影响游戏运行结果。

### 5. 同步上游风险

Godot 官方后续所有 3D 修复都默认原坐标约定。核心坐标改造会让每次合并都需要人工判断方向语义。

### 6. 初学者理解风险

如果只做显示层适配，不解释内部 Godot 坐标，用户从编辑器进入脚本时会困惑。必须在教程和提示里说明“编辑器显示坐标”和“脚本内部坐标”的关系，或进一步做脚本层封装。

## 不确定点

- Inspector 的 Transform 属性是否已有集中编辑器入口，可以只在一处做显示映射；需要继续查 `editor/inspector/` 和属性编辑器。
- 3D gizmo 操作是否能完全通过少量转换函数包住；旋转和局部坐标模式可能需要实测。
- 编辑器插件 API 是否要暴露用户坐标，还是继续暴露 Godot 内部坐标。
- 是否要给项目设置加开关，让高级用户切回 Godot 原始坐标显示。
- 脚本层是否要提供面向新手的封装，例如 `position_z_up` 或教程层的辅助函数；这会影响长期 API 设计。

## 验证建议

文档阶段：

```powershell
git diff -- doc/customization/coordinate-system-assessment.md doc/customization/document-map.md
```

显示层原型阶段：

```powershell
scons platform=windows dev_build=yes d3d12=no accesskit=no angle=no -j8
.\bin\godot.windows.editor.dev.x86_64.console.exe --version
```

手动验证：

- 启动 Project Manager。
- 新建或打开一个 3D 项目。
- 新建 Node3D 场景。
- 添加 MeshInstance3D、Camera3D、DirectionalLight3D。
- 在 3D 视图检查坐标轴控件：`+X` 右、`+Y` 前、`+Z` 上。
- 检查 Top / Front / Right 快捷视角是否符合新坐标解释。
- 拖动移动 gizmo 的 `X/Y/Z` 轴，确认显示值和物体移动方向一致。
- 切换局部/全局坐标，检查旋转后的轴向是否一致。
- 开启网格，确认默认地面显示为用户理解里的 `X/Y` 平面。
- 运行场景，确认 Camera3D 看到的画面、灯光方向、阴影没有异常。

核心改造阶段还必须增加：

- glTF 导入和导出回归。
- FBX 导入回归。
- HeightMapShape3D、NavigationRegion3D、NavigationObstacle3D 回归。
- RigidBody3D 默认重力回归。
- Camera3D `project_ray_normal()`、`unproject_position()` 回归。
- Light3D、LightmapGI、VoxelGI、ReflectionProbe 回归。
- 编辑器 gizmo 全量手动回归。

## 当前建议

下一步不要改 `core/math`。建议先做一个小型编辑器原型：

1. 在 `editor/scene/3d/node_3d_editor_plugin.*` 中集中加显示坐标转换函数。
2. 只改 3D 视图右上角坐标轴、视图快捷方向和网格显示。
3. 不碰脚本 API、资源格式、导入导出、物理、导航、渲染。
4. 用一个简单 3D 场景验证新手看到的方向是否符合预期。

如果这个原型体验确实更好，再扩展到 Inspector 和教程。核心坐标语义改造应作为长期专项，不应混在当前 editor 阶段的小范围定制里。
