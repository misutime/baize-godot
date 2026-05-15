# 3D 坐标系全面迁移实施清单

记录时间：2026-05-15

## 结论

现在目标已经不是“尽量兼容 Godot”，而是把引擎内部、编辑器和文档统一迁移到：

- `+X` 向右。
- `+Y` 向前。
- `+Z` 向上。

这会破坏 Godot 原来的 `+Y` 向上、`-Z` 向前约定。后续修改要按“新引擎坐标基准”重建，而不是在局部打补丁。

最关键的第一步不是改某一处常量，而是先统一三个基础规则：

1. 世界坐标：`X` 右、`Y` 前、`Z` 上。
2. 内置节点前方：建议同步改为本地 `+Y`，否则用户坐标和 Camera/Light/Node3D 的 forward 会继续冲突。
3. 模型资产正面：建议继续单独保留 `MODEL_FRONT` 概念，用导入器把 glTF/FBX 等外部坐标转换进新世界坐标。

如果第 2 点不改，迁移只完成了一半；如果第 3 点不保留，导入导出会和外部 DCC/格式生态硬碰硬。

## 新旧坐标映射

为了迁移旧 Godot 逻辑，可以把旧坐标转换成新坐标：

```text
新 X = 旧 X
新 Y = -旧 Z
新 Z = 旧 Y

旧 X = 新 X
旧 Y = 新 Z
旧 Z = -新 Y
```

这个映射保持右手系：新世界里 `X × Y = Z`。

建议在源码里集中定义转换矩阵或工具函数，用于导入旧资源、导入标准格式、迁移测试期对照。不要在各文件里手写 `Vector3(x, -z, y)`。

## 当前实施状态

2026-05-15 已完成第一轮源码迁移。当前公开场景层/API 目标已经按 `+X` 右、`+Y` 前、`+Z` 上推进：

- `Vector3` / `Vector3i` 方向常量改为 `UP=(0,0,1)`、`FORWARD=(0,1,0)`。
- `Basis::looking_at()`、`Transform3D::looking_at()`、`Node3D.look_at()` 改为本地 `+Y` 指向目标，本地 `+Z` 作为上方向。
- `Camera3D` 对外射线、投影/反投影、behind 判断按 `+Y` 前、`+Z` 上工作。
- `Curve3D`、`PathFollow3D`、CSG 路径挤出改为路径前进沿本地 `+Y`，姿态上方向为 `+Z`。
- 默认导航 up 改为 `+Z`，默认 3D 重力改为 `-Z`。
- `Light3D` 本地 AABB、编辑器灯光 gizmo、LightmapGI 烘焙灯光方向改为本地 `+Y` 前、`+Z` 上；AreaLight3D 面片改为 X/Z 平面。
- `CharacterBody3D` / `KinematicCollision3D` 默认 up 改为 `Vector3::UP`，Vehicle 悬挂方向和转向轴改为 Z-up 语义，SpringArm3D 默认向本地后方 `Vector3::BACK` 伸出。
- Navigation3D 的 2D 避障、NavigationObstacle3D 投影/编辑器绘制、NavigationAgent3D path height offset 已从 XZ/Y-up 改为 XY/Z-up。
- C# `Vector3` / `Vector3I` / `Basis.LookingAt()` 与 C++ 方向常量同步。
- 3D 编辑器视角相机、freelook/pan、右上角坐标轴控件、默认网格平面、Transform/Camera/Path gizmo 已按 `+Y` 前、`+Z` 上调整。

### 仍保留旧协议的边界

目前不是所有底层数学都已经重写成新坐标。为了先保证实时渲染稳定，以下底层协议仍保留 Godot/图形管线常见的传统相机空间：

- `Projection` 和渲染相机空间仍按本地 `-Z` 为前、本地 `+Y` 为屏幕上。
- RD 渲染灯光存储仍按旧的 `-Z` 前、`+Y` 上消费灯光 transform。

这些旧协议不再散落在 `Camera3D` 或 `Light3D` 里手写转换，而是集中放在 `core/math/coordinate_system.h`：

- `CoordinateSystem3D::scene_to_legacy_z_forward_local()`
- `CoordinateSystem3D::legacy_z_forward_to_scene_local()`
- `CoordinateSystem3D::scene_to_legacy_z_forward_basis()`
- `CoordinateSystem3D::scene_to_legacy_z_forward_transform()`

这意味着当前策略是：场景层、脚本 API、编辑器可见行为尽量使用新坐标；到渲染后端这类旧协议边界时显式转换。后续如果要“从里到外”继续深入，应该优先消灭这些边界函数的调用点，而不是新增隐式转换。

### 已补自动化测试

已新增或更新以下坐标系回归测试：

- `tests/core/math/test_vector3.cpp`
- `tests/core/math/test_vector3i.cpp`
- `tests/core/math/test_basis.cpp`
- `tests/core/math/test_transform_3d.cpp`
- `tests/scene/test_node_3d.cpp`
- `tests/scene/test_camera_3d.cpp`
- `tests/scene/test_curve_3d.cpp`
- `tests/scene/test_path_follow_3d.cpp`
- `tests/scene/test_light_3d.cpp`
- `tests/scene/test_character_body_3d.cpp`
- `tests/scene/test_navigation_agent_3d.cpp`
- `tests/servers/test_navigation_server_3d.cpp`
- `tests/servers/test_physics_server_3d.cpp`

已验证：

```text
scons platform=windows target=editor dev_build=yes tests=yes arch=x86_64 -j4
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test
```

测试结果：`1366` 个 test case 全部通过，`419299` 个 assertion 全部通过。

2026-05-15 继续推进 3D 编辑器视角/网格/Transform gizmo 后，再次验证：

```text
scons platform=windows target=editor dev_build=yes tests=yes arch=x86_64 -j4
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test
```

测试结果：`1367` 个 test case 全部通过，`419306` 个 assertion 全部通过。

本轮新增编辑器视角自动化哨兵：`tests/scene/test_camera_3d.cpp` 覆盖 `View3DController::to_camera_transform()`，确认 Front/Top/Right 编辑器视角分别使用 `+Y` 前、`+Z` 上的相机外显语义。

### 后续重点排查

下一轮建议继续排查这些高风险区域：

- 3D 编辑器手动体验：视角快捷键、坐标轴控件、Transform gizmo 拖拽、吸附、局部坐标、框选和路径编辑。
- 物理形状默认轴向，尤其 Capsule、Cylinder、HeightMap、Vehicle、Joint。
- glTF/FBX 导入导出、骨骼动画、root motion。
- GPUParticles/CPUParticles、碰撞/吸引器、可视化调试 mesh。
- XR 节点、AudioListener3D、Listener/Camera 跟随逻辑。
- Shader、RD uniform、cluster/forward/mobile 渲染路径中直接读取 `-Z` 前或 `Y` 上的地方。

## 必须先定的内部约定

### 1. `Vector3` 方向常量

目标：

- `Vector3::RIGHT = (1, 0, 0)` 保持不变。
- `Vector3::UP = (0, 0, 1)`。
- `Vector3::DOWN = (0, 0, -1)`。
- `Vector3::FORWARD = (0, 1, 0)`。
- `Vector3::BACK = (0, -1, 0)`。

需要处理：

- `core/math/vector3.h`
- `core/math/vector3i.h`
- `core/variant/variant_call.cpp`
- `doc/classes/Vector3.xml`
- `doc/classes/Vector3i.xml`
- `tests/core/math/test_vector3.cpp`
- `tests/core/math/test_vector3i.cpp`

注意：

- `Vector3::Axis` 枚举顺序不要改，仍然是 `AXIS_X`、`AXIS_Y`、`AXIS_Z`。改枚举或数组顺序会破坏序列化和 ABI。
- `x/y/z` 字段语义要变成真实用户语义，不再只是显示映射。

### 2. `Basis` 的列含义

目标：

- Basis 第 0 列仍是本地 `+X`。
- Basis 第 1 列改为本地 `+Y`，也就是 forward。
- Basis 第 2 列改为本地 `+Z`，也就是 up。

需要处理：

- `core/math/basis.h`
- `core/math/basis.cpp`
- `tests/core/math/test_basis.cpp`

重点函数：

- `Basis::looking_at()`
- `Basis::from_euler()`
- `Basis::get_euler()`
- `Basis::get_rotation_axis_angle()`
- `Basis::rotate_to_align()`

当前 `Basis::looking_at()` 逻辑是：

```cpp
Vector3 v_z = p_target.normalized();
if (!p_use_model_front) {
    v_z = -v_z;
}
Vector3 v_x = p_up.cross(v_z);
Vector3 v_y = v_z.cross(v_x);
basis.set_columns(v_x, v_y, v_z);
```

这套逻辑把 `-Z` 当内置 forward。迁移后建议改成：

- 普通节点：本地 `+Y` 指向目标。
- 本地 `+Z` 尽量靠近 `up`。
- 本地 `+X` 由 forward/up 叉乘得到。

需要特别验证 `p_use_model_front`。如果模型资产仍保留独立正面约定，这个参数不能简单删除。

### 3. `Transform3D` 和 `Node3D.look_at`

需要处理：

- `core/math/transform_3d.h`
- `core/math/transform_3d.cpp`
- `scene/3d/node_3d.h`
- `scene/3d/node_3d.cpp`
- `doc/classes/Transform3D.xml`
- `doc/classes/Node3D.xml`
- `tests/core/math/test_transform_3d.cpp`
- 需要新增 `tests/scene/test_node_3d.cpp` 或补已有 3D 节点测试。

目标行为：

- `Node3D.look_at(target)` 后，本地 `+Y` 指向 `target - origin`。
- 默认 up 参数应改为新 `Vector3::UP = (0, 0, 1)`。
- `Transform3D::looking_at()` 与 `Node3D.look_at()` 行为一致。

验证点：

- `look_at(Vector3(0, 10, 0))` 后，本地 `basis.get_column(AXIS_Y)` 应接近 `(0, 1, 0)`。
- 本地 `basis.get_column(AXIS_Z)` 应尽量接近 `(0, 0, 1)`。
- `to_local()`、`to_global()` 不应因为坐标改造破坏矩阵逆变换。

## 修改包清单

### 包 1：核心数学与公开常量

文件：

- `core/math/vector3.h`
- `core/math/vector3i.h`
- `core/math/basis.*`
- `core/math/transform_3d.*`
- `core/variant/variant_call.cpp`
- `core/variant/variant_construct.cpp` 如有默认构造或说明联动。

必须先做：

- 改 `Vector3` / `Vector3i` 常量。
- 改 `Basis::looking_at()`。
- 改测试期需要的旧新坐标转换工具。建议先放在测试或定制目录里，不要急着做公开 API。

风险：

- Euler 顺序历史上默认偏向 `YXZ`，很多代码把 `Y` 当 yaw。迁移到 `Z` 上后，直觉 yaw 应绕 `Z`。是否改默认 EulerOrder 是重大决策。
- 如果默认 Euler 解释也改，动画轨道和 Inspector 旋转都会受影响。

建议：

- 第一轮不要改 `EulerOrder` 枚举顺序。
- 先让 `look_at`、相机、导航、编辑器视图跑通，再评估 Inspector 旋转显示。

### 包 2：Camera3D 和投影/射线

文件：

- `scene/3d/camera_3d.h`
- `scene/3d/camera_3d.cpp`
- `scene/3d/xr/xr_nodes.cpp`
- `tests/scene/test_camera_3d.cpp`
- `tests/scene/test_viewport.cpp`

当前假设：

- `project_local_ray_normal()` 中正交相机射线是 `Vector3(0, 0, -1)`。
- 透视相机局部射线用 `z = -near`。
- `is_position_behind()` 用 `-basis.get_column(2)` 当视线方向。
- `project_position()` 用局部 `-z_depth`。

目标：

- 相机本地 forward 改为 `+Y`。
- 局部屏幕坐标建议保持 `x` 横向、`z` 竖向、`y` 深度。也就是相机局部 near 点从 `(x, y, -near)` 改成 `(x, near, z)` 或 `(x, +near, z)`，具体要和 `Projection` 类配合确认。

风险：

- Godot 的 `Projection` 矩阵和 GPU clip/depth 仍然可能假设相机空间 `-Z` 前方。
- 可以在 Camera3D 外层做变换，也可以重写 Projection 使用方式。前者改动小，后者更彻底但风险更高。

建议：

- 第一轮保留底层 Projection 的相机空间 `-Z`，在 Camera3D 和 RenderingServer 交界处做“新世界相机 forward +Y -> 渲染相机 -Z”的转换。
- 等渲染稳定后，再决定是否深入 RD/Shader 相机空间。

### 包 3：渲染、灯光、阴影和 GI

文件：

- `servers/rendering/renderer_scene_cull.cpp`
- `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp`
- `servers/rendering/renderer_rd/forward_mobile/render_forward_mobile.cpp`
- `servers/rendering/renderer_rd/renderer_scene_render_rd.cpp`
- `servers/rendering/renderer_rd/environment/gi.cpp`
- `servers/rendering/renderer_rd/environment/fog.cpp`
- `scene/3d/lightmap_gi.cpp`
- `modules/lightmapper_rd/`
- `editor/scene/3d/node_3d_editor_plugin.cpp` 中太阳预览方向。

当前假设：

- 多处 `-basis.get_column(Vector3::AXIS_Z)` 表示相机或灯光 forward。
- `basis.get_column(AXIS_Y)` 常用于上方向或面积灯高度方向。

目标：

- 对外 Node3D/Light3D/Camera3D 使用 `+Y` forward、`+Z` up。
- 渲染内部如果继续使用传统 camera space，要在提交 `camera_set_transform()` 和 light transform 时转换。

风险：

- 只改场景层不改渲染剔除会导致看得到但剔除/阴影错。
- 只改渲染方向不改 lightmap/GI，会导致实时光和烘焙光方向不一致。

验证：

- Camera3D 正前方物体可见，背后物体 `is_position_behind()` 为 true。
- DirectionalLight3D 指向场景时阴影方向正确。
- SpotLight3D 朝 `+Y` 照射。
- LightmapGI / VoxelGI 基础场景方向一致。

### 包 4：物理、重力和形状

文件：

- `servers/physics_3d/physics_server_3d.cpp`
- `scene/3d/physics/static_body_3d.cpp`
- `scene/3d/physics/vehicle_body_3d.cpp`
- `scene/3d/physics/physical_bone_3d.cpp`
- `scene/3d/physics/joints/`
- `scene/resources/3d/box_shape_3d.*`
- `scene/resources/3d/capsule_shape_3d.*`
- `scene/resources/3d/cylinder_shape_3d.*`
- `scene/resources/3d/height_map_shape_3d.*`
- 物理后端模块，如 GodotPhysics3D、Jolt 如果启用。

当前假设：

- 默认重力是 `Vector3(0, -1, 0)`。
- HeightMap 用 `Vector3(w, height, d)`，高度在 `Y`。
- Vehicle 轮子、悬挂、转向多处用 `Y` 作为上方向。
- Capsule/Cylinder 通常沿 `Y` 做高度。

目标：

- 默认重力改为 `Vector3(0, 0, -1)`。
- HeightMap 顶点改为 `Vector3(w, d, height)` 或等价新世界表达。
- Capsule/Cylinder 高度轴改为 `Z`。
- Vehicle 悬挂方向改为 `-Z`，转向 yaw 改为绕 `Z`。

风险：

- 后端 shape 数据可能假设 Y-up，需要查每个后端转换。
- Capsule/Cylinder 资产、碰撞 debug mesh、gizmo 都要同步。

验证：

- RigidBody3D 默认下落方向是 `-Z`。
- HeightMapShape3D 的高度沿 `Z`。
- CapsuleShape3D / CylinderShape3D 竖直方向沿 `Z`。
- VehicleBody3D 轮子悬挂沿 `Z` 工作。

### 包 5：Navigation3D

文件：

- `modules/navigation_3d/nav_map_3d.*`
- `modules/navigation_3d/3d/godot_navigation_server_3d.*`
- `modules/navigation_3d/3d/nav_mesh_generator_3d.cpp`
- `scene/3d/navigation/navigation_link_3d.cpp`
- `scene/3d/navigation/navigation_obstacle_3d.cpp`
- `scene/3d/navigation/navigation_region_3d.cpp`
- `modules/navigation_3d/editor/navigation_*`
- `tests/servers/test_navigation_server_3d.cpp`
- `tests/scene/test_navigation_region_3d.cpp`
- `tests/scene/test_navigation_agent_3d.cpp`
- `tests/scene/test_navigation_obstacle_3d.cpp`

当前假设：

- `NavMap3D::up = Vector3(0, 1, 0)`。
- 障碍物投影到 `xz-plane`。
- 障碍物只考虑绕 `y-axis` 旋转。
- 调试绘制用 `Vector3(point.x, height, point.z)`。

目标：

- `NavMap3D::up = Vector3(0, 0, 1)`。
- 地面平面改为 `X/Y`。
- 障碍物投影到 `xy-plane`。
- 障碍物只考虑绕 `z-axis` 旋转。
- 调试绘制用 `Vector3(point.x, point.y, height)`。

验证：

- NavigationRegion3D 从平面几何烘焙成功。
- Agent 在 `X/Y` 地面平面寻路。
- Obstacle 高度沿 `Z`。
- 2D avoidance 的“无高度轴”注释和实际恢复逻辑从 `Y` 改为 `Z`。

### 包 6：路径、曲线、骨骼、动画和 IK

文件：

- `scene/resources/curve.cpp`
- `scene/3d/path_3d.cpp`
- `scene/3d/skeleton_modifier_3d.*`
- `scene/3d/look_at_modifier_3d.*`
- `scene/3d/spring_bone_*`
- `scene/3d/two_bone_ik_3d.h`
- `scene/3d/iterate_ik_3d.h`
- `editor/scene/3d/skeleton_3d_editor_plugin.cpp`
- `editor/scene/3d/gizmos/spring_bone_3d_gizmo_plugin.cpp`
- `tests/scene/test_curve_3d.cpp`
- `tests/scene/test_copy_transform_modifier_3d.cpp`
- `tests/scene/test_convert_transform_modifier_3d.cpp`

当前假设：

- `Curve3D::sample_baked_with_rotation()` 测试中有写死 Basis，明显依赖 `-Z`/`Y`。
- Skeleton modifier、spring bone、IK 多处把 `Y` 当旋转轴或默认方向。

目标：

- 路径跟随默认 forward 改为 `+Y`。
- 曲线 frame 的 up 改为 `+Z`。
- 骨骼默认轴要重新评估：骨骼数据来自外部资产，不能简单全局替换。

风险：

- 骨骼和动画导入可能是最难验证的区域之一。
- 外部模型骨架约定可能仍是 glTF/Y-up，需要导入时转换。

验证：

- `test_curve_3d.cpp` 更新到新 Basis 预期。
- 简单 Skeleton3D 动画播放正常。
- LookAtModifier3D 目标方向正确。
- SpringBone 碰撞体和 debug gizmo 沿 `Z` 竖直。

### 包 7：资源导入导出

文件：

- `modules/gltf/gltf_document.*`
- `modules/gltf/structures/gltf_camera.*`
- `modules/gltf/extensions/gltf_light.*`
- `modules/gltf/extensions/physics/`
- `modules/fbx/fbx_document.*`
- `modules/fbx/editor/`
- `modules/gltf/editor/editor_scene_importer_blend.cpp`
- `tests/scene/test_gltf_document.cpp`
- `modules/gltf/tests/`

当前假设：

- FBX 使用 `ufbx_axes_right_handed_y_up`。
- Blender 导入参数 `export_yup = true`。
- glTF 读写直接进入 Godot `Transform3D`，默认和 Y-up 世界相容。

目标：

- 外部 glTF/FBX/Blender 导入时，从外部 Y-up 坐标转换到内部 Z-up/Y-forward。
- 导出 glTF/FBX 时，从内部 Z-up/Y-forward 转回标准外部坐标。
- Camera/Light/Bone/Animation/Physics extension 都必须同一套转换。

风险：

- 只转 Mesh 不转 Animation 会错。
- 只转 Node Transform 不转 Camera/Light 会错。
- 只转导入不转导出会导致往返测试失败。

验证：

- glTF 导入后，模型高度沿 `Z`。
- Camera/Light 导入后 forward 正确。
- glTF 导出再导入，Transform 近似一致。
- FBX 导入常见模型姿态正确。
- Blender `.blend` 导入如果继续通过 glTF 中间文件，需要确认 `export_yup` 是否改为 false 或导入端额外转换。

### 包 8：编辑器 3D 体验

文件：

- `editor/scene/3d/node_3d_editor_plugin.*`
- `editor/scene/3d/node_3d_editor_gizmos.*`
- `editor/scene/3d/gizmos/`
- `editor/scene/3d/mesh_editor_plugin.cpp`
- `editor/scene/3d/skeleton_3d_editor_plugin.cpp`
- `editor/scene/3d/camera_3d_editor_plugin.cpp`
- `editor/scene/3d/mesh_instance_3d_editor_plugin.cpp`
- `editor/themes/theme_modern.cpp`
- `editor/themes/theme_classic.cpp`

当前假设：

- Top/Front/Rear 视图用 `x_rot/y_rot` 计算，默认 `Y` 上。
- 视图移动里 forward 通过 `Vector3(0, 0, delta)` 再绕 `Y` 旋转。
- 网格默认平面和三轴网格使用 `grid_xy_plane`、`grid_yz_plane`、`grid_xz_plane`。
- 很多 gizmo 用 `Vector3(0, 1, 0)` 或 `Vector3(0, 0, 1)` 生成视觉网格、骨骼胶囊、灯光箭头。

本轮已处理：

- `View3DController::_to_camera_transform()`、pan、freelook 进入/移动改为本地 `+Y` 前、本地 `+Z` 上。
- `Node3DEditorViewport` 的相机法线、屏幕到空间转换、预览相机同步、Transform gizmo 屏幕缩放和 trackball 旋转改为新相机轴。
- 默认 perspective 地面网格从 `XZ` 改为 `XY`，`EditorSettings` 文档同步说明 Z-up 默认地面。
- `EditorNode3DGizmo` billboard、选择图标、collision segment/mesh 命中平面改为使用相机 `+Y` 视线和 `+Z` 屏幕上方。
- `Camera3D` 编辑器 gizmo 的视锥绘制、FOV/Size 拖拽控制改为新相机局部坐标。
- `Path3D` 编辑器拖点平面和路径 ribbon/fishbone 方向跟随 `Curve3D` 的 `+Y` 前、`+Z` 上。
- 右上角坐标轴点击映射改为 `X=Right/Left`、`+Y=Front`、`-Y=Rear`、`Z=Top/Bottom`。这样点 `-Y` 时相机在 `-Y` 侧往前看，显示为后视图，屏幕右侧仍是 `+X`。
- 右上角坐标轴在视线正对某条轴时，中心圆点的文字按当前朝向显示正负号，例如后视图中心显示 `-Y`。
- Procedural/Physical sky 和 RD sky 全景坐标改为用 `Z` 判断上下，避免天空/地面左右分割。

目标：

- Top 视图沿 `+Z/-Z` 看。
- Front 视图沿 `+Y/-Y` 看。
- 默认地面网格是 `X/Y`。
- 平移/旋转/缩放 gizmo 的 `Z` 轴竖直，`Y` 轴前后。
- 相机游走的“前进/后退”沿新 `+Y/-Y`。

验证：

- 右上角坐标轴 `X/Y/Z` 点击结果正确。
- 快捷键 `KP_7` Top、`KP_1` Front、`Alt+KP_1` Rear 结果正确。
- 移动物体：拖 `+Z` 向上，拖 `+Y` 向前。
- 局部坐标旋转后，gizmo 方向仍正确。
- 标尺、吸附、框选、subgizmo 选择正常。

## 推荐实施顺序

### 第 0 步：补测试和迁移工具

先补最小测试，不急着大改源码：

- `Vector3` 方向常量测试。
- `Basis::looking_at()` 新 forward/up 测试。
- `Node3D.look_at()` 新 forward/up 测试。
- `Camera3D.project_ray_normal()` 中心射线测试。
- `NavigationServer3D` 默认 up 测试。
- `RigidBody3D` 默认重力方向测试。

当前已安排的第一批自动化哨兵测试：

| 范围 | 文件 | 覆盖目标 | 当前源码预期 |
| --- | --- | --- | --- |
| 方向常量 | `tests/core/math/test_vector3.cpp`、`tests/core/math/test_vector3i.cpp` | `RIGHT/LEFT/FORWARD/BACK/UP/DOWN` 改为 `X` 右、`Y` 前、`Z` 上，并保持 `RIGHT × FORWARD = UP` | 先失败 |
| Basis | `tests/core/math/test_basis.cpp` | `Basis::looking_at(Vector3(0, 1, 0), Vector3(0, 0, 1))` 后本地 `+Y` 前、本地 `+Z` 上 | 先失败 |
| Transform3D | `tests/core/math/test_transform_3d.cpp` | `Transform3D::looking_at()` 与 `Basis::looking_at()` 的方向约定一致 | 先失败 |
| Node3D | `tests/scene/test_node_3d.cpp` | `Node3D.look_at()` 后全局 Basis 的 `AXIS_Y` 指向目标，`AXIS_Z` 指向上 | 先失败 |
| Camera3D | `tests/scene/test_camera_3d.cpp` | 透视/正交相机中心射线对外为 `+Y`，`+Y` 点在前、`-Y` 点在后 | 先失败 |
| Navigation3D | `tests/servers/test_navigation_server_3d.cpp` | 新建导航 map 的默认 up 为 `Vector3(0, 0, 1)` | 先失败 |
| Physics3D | `tests/servers/test_physics_server_3d.cpp` | 默认重力方向为 `Vector3(0, 0, -1)`，也就是新世界的向下 | 先失败 |

这些测试故意按目标坐标系写，而不是按当前 Godot 行为写。它们是迁移红灯，不代表当前源码有普通回归；后续改到对应包时，应该把同包里的旧测试一起翻成新语义。

理由：

- 坐标系改造很容易“编译过但方向错”。
- 先写测试能让后续每包修改有一个基本红绿反馈。

### 第 1 步：核心数学和 Node3D

改：

- `Vector3/Vector3i` 常量。
- `Basis::looking_at()`。
- `Transform3D::looking_at()`。
- `Node3D.look_at()` 文档和测试。

先不改：

- Projection/RD 底层相机空间。
- glTF/FBX 导入。
- 复杂编辑器 gizmo。

验证：

- 跑 core math 和 Node3D 新测试。

### 第 2 步：Camera3D + 渲染适配层

改：

- Camera3D 射线、behind、project/unproject。
- Camera transform 提交到 RenderingServer 时的方向适配。
- 渲染剔除和近裁剪面方向。

验证：

- 跑 `tests/scene/test_camera_3d.cpp`。
- 手动打开简单 3D 场景，看前方物体、背后物体、阴影。

### 第 3 步：编辑器 3D 视图

改：

- Top/Front/Rear 视角。
- 右上角坐标轴。
- 默认地面网格。
- 视图移动和 orbit。
- 平移/旋转/缩放 gizmo。

验证：

- 手动验证 3D 编辑器操作。
- 后续可加编辑器截图测试，但第一轮手动更现实。

### 第 4 步：物理和导航

改：

- 默认重力。
- HeightMap。
- Capsule/Cylinder 竖直轴。
- Navigation map up、投影平面、障碍高度。

验证：

- 跑 physics/navigation 相关测试。
- 手动运行一个落体、一个导航烘焙场景。

### 第 5 步：导入导出和动画

改：

- glTF/FBX/Blend 导入坐标转换。
- Camera/Light/Bone/Animation/Physics extension 同步转换。
- Curve3D、Path3D、Skeleton/IK/SpringBone。

验证：

- glTF 往返测试。
- 简单骨骼动画。
- 简单路径跟随。

### 第 6 步：文档和迁移说明

改：

- `doc/classes/`
- `doc/customization/`
- 入门教程、编辑器说明、坐标图。

验证：

- 查找残留 `Y is up`、`-Z forward`、`xz-plane`、`y-axis`。

## 测试计划

### 推荐先新增或改的自动测试

1. `tests/core/math/test_vector3.cpp`

检查：

- `Vector3::UP == Vector3(0, 0, 1)`。
- `Vector3::FORWARD == Vector3(0, 1, 0)`。
- `Vector3::RIGHT.cross(Vector3::FORWARD) == Vector3::UP`。

2. `tests/core/math/test_basis.cpp`

检查：

- `Basis::looking_at(Vector3::FORWARD)` 后第 1 列是 `Vector3::FORWARD`。
- 第 2 列接近 `Vector3::UP`。
- `Basis::looking_at(Vector3::RIGHT)` 仍保持正交。

3. `tests/core/math/test_transform_3d.cpp`

检查：

- `Transform3D().looking_at(Vector3(0, 10, 0))` 的 basis 第 1 列朝 `+Y`。

4. 新增 `tests/scene/test_node_3d.cpp`

检查：

- `Node3D.look_at()` 使用本地 `+Y` 指向目标。
- `look_at_from_position()` 保持 scale。

5. `tests/scene/test_camera_3d.cpp`

检查：

- 中心 `project_local_ray_normal()` 朝局部 `+Y`。
- `is_position_behind(Vector3(0, -10, 0)) == true`。
- `project_position()` 深度沿 `+Y`。

6. `tests/servers/test_navigation_server_3d.cpp`

检查：

- 默认 map up 是 `Vector3(0, 0, 1)`。
- 设置 map up 后 query 不崩。

7. 物理测试

如果当前没有专门 RigidBody3D 重力测试，建议新增一个小型 SceneTree 测试：

- 新建 RigidBody3D。
- 读取默认 gravity vector。
- 确认方向是 `Vector3(0, 0, -1)`。

8. `tests/scene/test_curve_3d.cpp`

更新 `sample_baked_with_rotation()` 的 Basis 预期。

9. glTF 往返测试

在 `tests/scene/test_gltf_document.cpp` 或 `modules/gltf/tests/` 增加：

- 一个 Node3D 位于 `(1, 2, 3)`，导出 glTF 再导入后仍是新引擎语义的 `(1, 2, 3)`。
- 一个 Camera3D 朝 `+Y`，往返后仍朝 `+Y`。
- 一个 Light3D 朝 `+Y`，往返后仍朝 `+Y`。

### 构建和测试命令

先构建：

```powershell
scons platform=windows dev_build=yes d3d12=no accesskit=no angle=no tests=yes -j8
```

核心测试建议：

```powershell
.\bin\godot.windows.editor.dev.x86_64.console.exe --test --test-case="[Vector3]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --test --test-case="[Basis]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --test --test-case="[Transform3D]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --test --test-case="[SceneTree][Camera3D]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --test --test-case="[Navigation3D]*"
```

如果本地 test runner 的参数和上面不一致，以 `.\bin\godot.windows.editor.dev.x86_64.console.exe --help` 输出为准。

### 手动验证清单

每个阶段至少做：

- 启动 Project Manager。
- 打开 3D 项目。
- 新建 Node3D 场景。
- 添加 MeshInstance3D、Camera3D、DirectionalLight3D。
- 确认 `+Z` 是上，`+Y` 是前，`+X` 是右。
- 运行场景，确认 Camera3D 看向 `+Y`。
- RigidBody3D 默认向 `-Z` 掉落。
- NavigationRegion3D 在 `X/Y` 平面烘焙。
- 导入一个 glTF 模型，确认高度沿 `Z`，正面朝预期方向。

## 查找残留的推荐命令

查旧 forward / up 常量：

```powershell
rg -n "Vector3::UP|Vector3::FORWARD|Vector3\\(0, 1, 0\\)|Vector3\\(0, 0, -1\\)|Vector3\\(0, 0, 1\\)" core scene editor modules servers tests doc -g "*.h" -g "*.cpp" -g "*.xml" -g "*.md"
```

查旧平面/轴文案：

```powershell
rg -n "Y is up|\\+Y is up|-Z|forward|xz-plane|y-axis|Y axis|Z axis|height" doc scene editor modules servers tests -g "*.h" -g "*.cpp" -g "*.xml" -g "*.md"
```

查渲染相机方向：

```powershell
rg -n "basis\\.get_column\\(Vector3::AXIS_Z\\)|basis\\.get_column\\(2\\)|near_plane|camera_z|view_position\\.z|screen_ray_dir\\.z" servers/rendering scene/3d modules -g "*.h" -g "*.cpp" -g "*.glsl"
```

查导航/高度假设：

```powershell
rg -n "xz-plane|y-axis|cell_height|map_get_up|set_up|safe_scale\\.y|Vector3\\(point\\.x, height, point\\.z\\)|Vector3\\(w, map_data" modules/navigation_3d scene/3d servers/physics_3d -g "*.h" -g "*.cpp"
```

## 当前最小下一步

建议下一步先写第 0 步测试，不直接改实现：

- `Vector3` 常量测试。
- `Basis::looking_at()` 测试。
- `Camera3D` 中心射线测试。
- `NavigationServer3D` 默认 up 测试。

这些测试会先红，之后按包推进实现。这样每次改一片，都能确认方向不是“看起来对、内部歪”。

如果要直接开始实现，第一批只动核心数学和测试：

- `core/math/vector3.h`
- `core/math/vector3i.h`
- `core/math/basis.cpp`
- `core/math/transform_3d.*`
- `scene/3d/node_3d.*`
- `tests/core/math/test_vector3.cpp`
- `tests/core/math/test_basis.cpp`
- `tests/core/math/test_transform_3d.cpp`
- 新增或补充 `tests/scene/test_node_3d.cpp`

这批完成并通过后，再进入 Camera3D 和渲染层。

## 已完成的第一轮实现记录

记录时间：2026-05-15

本轮已经把自动化测试覆盖到的基础 3D 坐标语义迁到：

- `+X` 向右。
- `+Y` 向前。
- `+Z` 向上。

已修改范围：

- 核心方向常量：`Vector3`、`Vector3i`、Variant 常量出口、C# `Vector3/Vector3I` 常量。
- 基础朝向：`Basis::looking_at()`、`Transform3D::looking_at()`、`Node3D.look_at()`。
- 相机 API：`Camera3D` 对外射线、投影/反投影、前后判断使用 `+Y` 前、`+Z` 上；提交到 RenderingServer 时保留内部旧相机空间转换。
- 导航/物理默认方向：`navigation/3d/default_up = (0, 0, 1)`，`physics/3d/default_gravity_vector = (0, 0, -1)`，并同步 `Area3D`、`RayCast3D`、`ShapeCast3D` 默认下方向。
- 路径姿态：`Curve3D`、`PathFollow3D` 的路径切线改为本地 `+Y`，up 改为本地 `+Z`。
- CSG path 挤出：path 模式截面改放在局部 `X/Z` 平面，沿局部 `+Y` 挤出。
- API 文档：已同步 `Vector3.xml`、`Vector3i.xml` 常量值。

已验证：

```text
scons platform=windows target=editor dev_build=yes tests=yes arch=x86_64 -j4
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test
```

完整 doctest 结果：

```text
test cases: 1362 | 1362 passed | 0 failed | 3 skipped
assertions: 419294 | 419294 passed | 0 failed
```

仍需继续排查：

- Light3D、AudioStreamPlayer3D、CPU particles、LookAtModifier3D、Skeleton/IK 等运行时方向逻辑。
- 编辑器 3D 视图、gizmo、网格、坐标轴文字和相机导航。
- glTF/FBX 导入导出坐标转换和模型正面约定。
- 文档中仍提到 `-Z direction`、`+Z axis as forward` 的类说明，需要在对应源码行为确认后逐个同步。

## 编辑器 3D 同类问题核查记录

记录时间：2026-05-15

本轮按“仍把 `-Z` 当相机前方、仍把 `Y` 当上方”的规则继续排查。已确认并修复：

- 3D 正交网格：`Node3DEditor::_init_grid()` 还用相机本地 `-Z` 求网格交点，已改为相机本地 `+Y`。
- Path3D 编辑器：倾斜手柄、倾斜圆盘和删除命中检测仍把路径姿态的 `-Z` 当切线、`Y` 当 up，已统一为 `+Y` 切线、`+Z` up。
- Mesh 资源预览：预览相机和拖拽旋转仍按旧的 `Y` 上、`Z` 前布局，已改为相机位于 `-Y`、拖拽 yaw 围绕 `+Z`。
- Camera3D API：`get_near_plane_points()` 仍直接返回内部旧投影空间点，已转换成对外的 `+Y` 前、`+Z` 上。
- 3D 音源：启用 emission angle 时仍用本地 `Z` 判断正面，已改为本地 `+Y`。
- NavigationRegion3D 调试：region up 检查和边连接调试线仍使用 `Y` up，已改为 `Vector3::UP` / 本地 `+Z`。
- GridMap 导航调试：导航连接调试面仍使用 `Y` up 求左右边，已改为 `Vector3::UP`。

本轮同步补了一个 Camera3D 哨兵测试：

- `Camera3D.get_near_plane_points()` 返回的近裁剪面角点必须落在本地 `+Y` 近裁剪距离上，并沿本地 `X/Z` 展开。

仍需要后续单独核查，暂不在本轮硬改：

- `Skeleton3DEditor`、`TwoBoneIK3D`、`Joint3D` gizmo：这些地方有骨骼、物理关节或模型局部轴约定，不能只凭 `Vector3(0, 1, 0)` 判定为旧世界 up。
- `MultiMeshEditor` 的 Populate Surface：`Mesh Up Axis` 与 `Basis::set_look_at()` 迁移后可能需要重新校准，建议做一个三轴测试网格再确认。
- `HeightMapShape3D`、粒子发射方向、XR 节点、Lightmap/voxelizer：搜索结果显示仍有不少 `Y` 高度或旧相机空间痕迹，需要按运行时模块分批验证。
