# 左手 Y-Up 坐标系修改记录

记录时间：2026-05-16

## 目标

本轮保持 Godot 原本更容易兼容的 Y-Up 工作流，只调整默认前方：

- `+X` 向右。
- `+Y` 向上。
- `+Z` 向前。
- 地面仍是 `XZ` 平面。

这意味着公开 3D 语义从 Godot 默认的 `-Z` 前，改为 `+Z` 前；重力、导航、角色 up direction、PrimitiveMesh 的高度方向仍保持 `+Y`。

## 当前权威规则

后文保留了排查过程中的错误判断和修正记录。继续修改时优先按本节判断，不要从中间过程段落倒推当前规则。

- 内部 3D 场景、脚本 API、编辑器相机和渲染主路径统一使用 `+X` 右、`+Y` 上、`+Z` 前。
- 相机本地 `+Z` 是视线前方，near/far、屏幕射线、遮挡剔除和 view-space depth 都按正向 `z` 推导。
- Node3D、Camera3D、SpotLight3D、AreaLight3D、PathFollow3D 的默认“前方”都是本地 `+Z`。
- DirectionalLight / SpotLight / AreaLight 的节点本地 `+Z` 表示光线射出方向；DirectionalLight 写给 shader BRDF 的 `direction` 是表面指向光源，所以等于 `-本地 +Z`。
- 地面继续是 `XZ`，高度、重力、导航 up 和角色 up direction 继续使用 `Y` 轴。
- 外部格式导入导出、旧资源迁移和特殊图形 API 边界允许做显式转换，但不在引擎内部长期维护一套隐藏的 `-Z` 前方协议。
- VR/XR/OpenXR/WebXR 相关功能已经列入长期裁剪方向，不再作为本坐标系迁移的投入范围；如果官方同步触及这些路径，按裁剪台账处理，不为它们补坐标系兼容层。

## 已修改范围

- `Vector3` / `Vector3i` 方向常量：`FORWARD` 改为 `(0, 0, 1)`，`BACK` 改为 `(0, 0, -1)`；`UP/DOWN` 不变。
- C# `Vector3` / `Vector3I` 同步方向常量。
- `Basis::looking_at()` 与 C# `Basis.LookingAt()`：本地 `+Z` 指向目标，本地 `+Y` 尽量贴近 up。
- `Projection` 透视、正交、frustum 矩阵改为相机本地 `+Z` 可见空间：可见深度沿 `+Z` 增加，透视投影使用 `w = z`。
- `Camera3D` 对外射线、`project_position()` 深度切片、投影/反投影、behind 判断、送入 `RenderingServer` 的 transform 都按 `+Z` 前工作。
- RD 渲染后端的 view-space 深度、cluster、体积雾、forward shader、灯光方向改为按 `+Z` 前解释，不再通过旧 `-Z` 渲染协议做边界补偿。
- Raycast occlusion culling 的 near 面、相机方向和 ray sort 输入改为相机本地 `+Z`。
- GPU 粒子的 view-depth sort 和 Z billboard view axis 已收束为直接使用相机本地 `+Z`，不再靠旧 `-Z` 注释和双重反号抵消。
- VoxelGI 动态物体体素化写回按正向 `+Z` depth 修正 z 推进方向；法线还原仍保留独立符号合同，需要真实 VoxelGI 场景继续验证。
- 多视图合并相机的主 basis 已按本地 `+Z` 前方重算，左右眼 frustum 合并时不再把侧面 outward normal 的平均值当成相机前方。
- GI fallback 重建、copy linearize、SSAO/SSIL depth downsample、cubemap 转双抛物面阴影等 screen-space 深度路径已按 reverse-Z + 正向 `+Z` depth 修正。
- volumetric fog、SSR、TAA、SSAO/SSIL normal 读取端继续按正向 `+Z` view-space 收口。
- 编辑器框选和运行时节点选择的 3D frustum far 面已改为直接放在相机本地 `+Z` 的 far 距离处，避免重复叠加 near 偏移。
- `Curve3D`、`PathFollow3D`、CSG 路径挤出改为沿本地 `+Z` 前进，up 仍为 `+Y`。
- 灯光 AABB、LightmapGI 烘焙方向、灯光 gizmo、相机 gizmo 改为 `+Z` 前。
- `SpringArm3D` 明确使用 `Vector3::BACK` 作为默认后方，避免常量翻转后语义反掉。
- 3D 编辑器相机控制的距离方向改为适配 `+Z` 前。
- 3D 编辑器视角控制补充修正：
  - 自由视角和右键看向逻辑不再使用硬编码 `(0, 0, -1)`，统一以相机本地 `+Z` 作为前方。
  - 编辑器环绕相机不翻转 basis。本轮重新推导后确定：Front 视图相机应在 `-Z` 一侧，basis 保持 identity，本地 `+Z` 看向原点；默认斜视通过调整默认 pitch 保持在地面上方。
  - Top/Bottom 预设与环绕吸附的 pitch 符号同步改为 `Top = -PI/2`、`Bottom = +PI/2`。
  - 选区框/屏幕到空间的辅助点复用 `View3DController::to_camera_transform()`，避免和真实编辑器相机出现前后符号不一致。
  - 从外部相机 transform 反推编辑器环绕中心时，按 `眼睛 + 前方 * 距离` 计算中心点。
  - 右上角坐标指示器正对某轴时，显示的正负轴和点击触发的视图保持一致。

## 明确不改的范围

- 不改 `Vector3::Axis` 枚举顺序。
- 不改 `x/y/z` 字段存储顺序。
- 不改默认重力：仍是 `Vector3(0, -1, 0)`。
- 不改 Navigation3D up：仍是 `+Y`。
- 不改 CharacterBody3D 默认 up direction：仍是 `+Y`。
- 不把 PrimitiveMesh、NavigationObstacle、GridMap 地面逻辑迁移到 XY；地面继续使用 XZ。

## 风险点

- 公开语义是左手 Y-Up，但底层数学叉乘仍按普通向量叉乘工作，不能把 `cross()` 当成“左手规则”自动结果。
- 渲染和投影底层已经改为 `+Z` 前协议；后续若再发现反向问题，必须继续追查哪个模块仍假设 `-Z` 前，不能用局部反号、投影镜像或剔除反转隐藏问题。
- 外部资源导入、骨骼、粒子和部分编辑器插件可能仍有写死 `-Z` 前的逻辑，需要通过测试和手动体验继续排查。VR/XR 相关路径按长期裁剪处理，不再列为坐标系迁移风险。
- 编辑器视角类代码历史上默认相机本地 `-Z` 是前方，后续排查时优先搜索硬编码 `(0, 0, -1)`、`basis.z` 取反、屏幕深度负号这三类痕迹。

## 专项排查：天空、右键视角、右上角指示器

### 天空 / 地面

`ProceduralSkyMaterial` 仍使用 `EYEDIR.y` 区分天空和地面，`PhysicalSkyMaterial` 也继续以 `UP = vec3(0.0, 1.0, 0.0)` 作为天顶方向。这和本轮目标 `Y-Up` 一致，所以没有把天空/地面的上下轴改到 `Z`。

需要注意的是，`sky.glsl` 里的全景图经纬采样仍带有旧 Godot 的 `-Z` 前向习惯。它主要影响全景贴图的前后朝向，不应该把天空和地面上下颠倒。若后续发现 PanoramaSky 的“正前方贴图”相反，再单独处理贴图朝向转换。

### 鼠标右键按住后旋转视角

右键环绕的核心在 `View3DController::_to_camera_transform()`：中心点是 `cursor.pos`，相机眼睛位于 `中心点 - 本地前方 * distance`。在 `+Z` 前坐标系里，Front 视图对应相机在 `-Z` 一侧、看向 `+Z`。

本次发现自由视角、右键 look、插值同步仍有几处旧逻辑使用 `(0, 0, -1)` 当作前方，会导致按住右键后移动方向、焦点同步、视角惯性出现前后反向或漂移。已统一改为本地 `+Z`。

### 右上角坐标指示器

指示器通过当前编辑器相机的逆 basis 把世界 `X/Y/Z` 轴投到 2D 圆盘上。常规角度下，正轴和负轴分别对应 `axis = 0..2` 与 `axis = 3..5`。

本次发现一个只在“几乎正对某个轴”时触发的特殊分支：画面上显示正轴字母，但 `axis` 编号会落到负轴视图，导致视觉和点击目标不一致。已修正为显示哪个轴就点击哪个轴。

## 验证建议

自动化：

```text
scons platform=windows target=editor dev_build=yes tests=yes arch=x86_64 -j4
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test
```

2026-05-16 已执行：

```text
scons platform=windows target=editor dev_build=yes tests=yes arch=x86_64 -j4
```

结果：构建成功。构建末尾有 AccessKit/ANGLE 可选依赖提示，不影响本次编译产物。

```text
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test
```

结果：`1357` 个 test case 全部通过，`419283` 个 assertion 全部通过。测试输出里仍有 OptimizedTranslation 和 CanvasItem 的既有警告/错误日志，但 doctest 状态为 `SUCCESS`。

2026-05-16 追加编辑器视角专项修正后再次执行：

```text
scons platform=windows target=editor dev_build=yes tests=yes arch=x86_64 -j4
```

结果：构建成功。第一次链接时因为旧 Godot 编辑器进程占用 exe 失败，结束进程后重跑通过；AccessKit/ANGLE 仍为可选依赖提示。

```text
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test
```

结果：`1358` 个 test case 全部通过，`419287` 个 assertion 全部通过。新增了编辑器相机环绕方向测试；既有 OptimizedTranslation 和 CanvasItem 日志仍存在，doctest 状态为 `SUCCESS`。

同日根据实际视口截图发现“物体像从地下/反面观察”的问题后，回到相机公式重新推导：Front 视图应为相机在 `-Z`、本地 `+Z` 指向原点；默认斜视的 pitch 需要从 `+0.5` 改成 `-0.5`，Top/Bottom 也要同步调换符号。修正后再次执行：

```text
scons platform=windows target=editor dev_build=yes tests=yes arch=x86_64 -j4
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test
```

结果：构建成功；`1358` 个 test case 全部通过，`419291` 个 assertion 全部通过。测试增加了默认编辑器相机必须位于地面上方、basis 行列式为正、且 `相机位置 + 本地 +Z 前方 * 距离 = 环绕中心` 的断言。

随后继续根据实际截图发现两个问题：物体 Y 轴在屏幕中朝下、右键飞行模式按 `D` 表现为向左。原因是之前按现象临时翻了编辑器相机 basis，破坏了“本地 X 是右、本地 Y 是上”的基础契约；这类修法已撤回。进一步检查后确认：相机 transform 不能只在 projection 里翻 Z，否则相机、剔除和渲染后端看到的视空间合同不一致。当前规则是：

- 编辑器环绕相机不翻 basis，只通过 `cursor.x_rot / cursor.y_rot / distance` 得到合法旋转矩阵。
- `Camera3D` 对外 API 和编辑器相机保持场景坐标契约：`+X` 右、`+Y` 上、`+Z` 前。
- 当前已经撤掉旧渲染边界转换：`Camera3D` 直接把 `+Z` 前的 transform 送入 `RenderingServer`，`Projection` 本身也按 `+Z` 前生成矩阵。
- 普通相机 projection 不再翻屏幕 X，Forward+ / Mobile 也不再按 projection X 符号反转剔除。面朝向问题必须继续从 view-space、投影矩阵、front face/cull 合同排查。
- 灯光实例和阴影相机不再转旧协议，场景层和 RD 层统一把灯光方向理解为本地 `+Z`。
- Transform gizmo 的基础箭头网格必须沿本地 `+Z` 建立；外层 `Basis::looking_at()` 会把 `+Z` 对准目标轴。旧的 `-Z` 箭头会直接导致 X/Y/Z 箭头整体反向。
- Top/Bottom 语义必须按相机所在位置判断：Top 是相机在 `+Y`，看向 `-Y`；Bottom 是相机在 `-Y`，看向 `+Y`。右上角指示器正对某轴时，要用相机局部深度判断正负轴，不能只按屏幕圆点位置猜。
- 右键飞行模式的上下拖拽要按 `+Z` 相机前方重新看符号：默认鼠标向上移动应抬头，`freelook_invert_y_axis` 开启后才反向。
- 右键飞行模式的左右拖拽也要按 `View3DController::_to_camera_transform()` 的实际符号判断：`cursor.y_rot` 生成相机 basis 时会取负号，所以鼠标向右移动必须减少 `cursor.y_rot`，这样相机本地 `+Z` 前方才会偏向世界 `+X`。
- 运行时 3D 节点选择和编辑器 gizmo 线段拾取里的 near plane 也要重新按 `+Z` 前方推导：判断“相机前方距离”时用相机本地 `+Z`，而构造框选 frustum 的 near 面外法线要朝相机后方，也就是 `-basis Z`。
- `RuntimeNodeSelect` 的导航设置里发现一个旧笔误：`invert_y_axis` 被错误写进了 `set_invert_x_axis()`。这会让运行时节点选择视图里的上下反转设置失效，并可能覆盖 X 轴反转。
- 普通相机 projection 为了抵消旧渲染边界的 X 镜像，会让屏幕空间三角形绕序反转。Forward+ 和 Mobile 的 `reverse_cull` 不能只看相机 basis determinant，还要把 projection 的 X 翻转算进去，否则默认 `CULL_BACK` 会剔掉正面，出现“背面亮、正面看不到”的问题。

再次执行：

```text
scons platform=windows target=editor dev_build=yes tests=yes arch=x86_64 -j4
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test
```

结果：构建成功；第一次完整测试在 `[Image] Saving and loading` 处出现一次未复现的 SIGSEGV；随后单独执行 `*[View3DController]*` 通过，重跑完整测试通过：`1358` 个 test case 全部通过，`419297` 个 assertion 全部通过。测试补充确认 Front 视图相机本地 `+X = Vector3::RIGHT`、本地 `+Y = Vector3::UP`，默认相机在地面上方，Top 视图在 `+Y` 一侧。

继续把相机 transform/projection 边界重新收束到 `Camera3D -> RenderingServer` 和灯光渲染边界，并把 transform gizmo 箭头改回本地 `+Z` 后再次执行：

```text
.\misc\customization\build-windows.ps1 -Preset dev -Jobs 4 tests=yes
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test --test-case="*[View3DController]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test --test-case="*[Camera3D]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test --test-case="*[Curve3D]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test --test-case="*[PathFollow3D]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test
```

结果：构建成功；`View3DController` 专项 1 个 test case 通过；`Camera3D` 专项 4 个 test case 通过；`Curve3D` 专项 7 个 test case 通过；`PathFollow3D` 专项 7 个 test case 通过；完整测试 `1358` 个 test case 全部通过，`419297` 个 assertion 全部通过。完整测试需要写入 `C:\Users\Misu\AppData\Local\godot_test`，在沙盒里会导致 DDS 测试创建目录失败；已在本机真实环境复测通过。既有 OptimizedTranslation / CanvasItem 日志仍存在，doctest 状态为 `SUCCESS`。

随后按“不能用补丁隐藏坐标系问题”的原则，撤掉旧渲染协议转换方案，改为从底层统一：

- 删除临时 `CoordinateSystem3D` 边界转换。
- `Projection` 直接使用 `+Z` 可见空间。
- `Camera3D`、`RenderingServer`、`CameraData`、RD shader 都按 `+Z` 前传递。
- 移除 projection X 镜像和 RD `reverse_cull` 补偿。
- RD cluster、体积雾、forward shader 的 view-space 深度从 `-z` 改为 `+z`。
- Directional shadow fade、spot/area/omni light cluster、灯光方向统一按 `+Z` 前推导。
- Forward+ / Mobile 3D 管线的基础 `front_face` 合同改为 `COUNTER_CLOCKWISE`。原因是视空间迁移到左手 `+Z` 前后，投影到屏幕的正面绕序会整体变为 CCW；这是管线级 front-face 定义，不是按某个相机、材质或 projection 做局部反剔除。

再次执行：

```text
.\misc\customization\build-windows.ps1 -Preset dev -Jobs 4 tests=yes
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test "*[Projection]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test "*[Camera3D]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test "*[View3DController]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test
```

结果：构建成功，shader 头文件重新生成；三个专项测试和完整测试进程退出码均为 `0`。当前终端只额外输出 `Failed to delete files` 的既有清理日志，未输出 doctest 统计行。

再次根据编辑器实测“正面透明、背面有颜色”排查后确认：`+Z` 前的左手视空间会改变 3D 管线 front-face 绕序。已把 Forward+ / Mobile 的 `PipelineRasterizationState.front_face` 统一为 `POLYGON_FRONT_FACE_COUNTER_CLOCKWISE`。随后执行：

```text
.\misc\customization\build-windows.ps1 -Preset dev -Jobs 4 tests=yes
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test "*[Projection]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test "*[Camera3D]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test "*[View3DController]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test
```

结果：构建成功；专项和完整测试退出码均为 `0`，仍只输出测试清理临时文件的既有 `Failed to delete files` 日志。

根据编辑器实测“物体在 0,0,0 但看起来像漂浮”的截图继续排查后，确认 X 轴和 Y 轴穿过物体中心，物体坐标本身没有偏离原点；视觉问题来自编辑器网格/原点线与真实几何共面时的深度关系。网格 shader 会进入透明 pass，RD 当前使用反向 Z 和 `GREATER_OR_EQUAL` 深度比较，共面时辅助线会通过等深测试并压到物体表面上，容易让地面/网格看起来在物体背后或物体像悬空。

本次处理没有修改模型坐标、相机投影或剔除规则，只给编辑器辅助显示建立一致的遮挡合同：

- 真实几何优先于编辑器网格和原点线。
- 编辑器网格/原点线与真实几何共面时，在 shader 里把 `DEPTH` 轻微写远一点。
- 因为 Godot 使用反向 Z，写远意味着 `FRAGCOORD.z - 0.00001`。
- 这个规则只影响编辑器辅助显示层，目的是让坐标系验证更直观，不让辅助线遮住真实表面。

继续根据换角度后的截图排查“物体像一半在地面、一半在空中”后，确认问题不在物体高度，而在天空背景的屏幕射线仍按旧坐标系生成。`sky.glsl` 的非 multiview 背景路径手写了相机空间方向：

```glsl
cube_normal.z = -1.0;
```

这属于 Godot 原始 `-Z` 前方约定。迁移到 `+Z` 前后，天空背景必须沿相机本地 `+Z` 反推出屏幕射线，否则程序天空里的 `EYEDIR.y` 会来自相机背后的方向，地平线会和真实 XZ 地面错位。已改为：

- 背景天空射线使用 `cube_normal.z = 1.0`。
- `SKY_COORDS` 的水平角基准从 `-Z` 改到 `+Z`，保证 panorama sky 也遵守新前方。

继续根据“按住中键旋转时上下左右都反了”排查后，确认 `cursor_orbit()` 的鼠标 delta 仍保持旧符号。当前 `View3DController::_to_camera_transform()` 生成相机 basis 时会对 `cursor.x_rot / cursor.y_rot` 取负号，而编辑器快捷键已经约定：

- Orbit View Up：增加 `x_rot`。
- Orbit View Right：减少 `y_rot`。

旧鼠标逻辑正好相反：向上拖减少 `x_rot`，向右拖增加 `y_rot`。已把 orbit 鼠标映射改成：

- 鼠标向上拖：增加 `x_rot`。
- 鼠标向右拖：减少 `y_rot`。
- `invert_x_axis / invert_y_axis` 仍只做用户偏好反转。

并新增 `View3DController` 鼠标 orbit 方向测试，避免以后再退回旧符号。

继续排查 Front/Rear 视图语义后，确认之前按旧习惯把前后反了。当前项目的物体前方是 `+Z`，所以：

- 前视图：相机应站在 `+Z` 一侧，朝 `-Z` 看物体正面。
- 后视图：相机应站在 `-Z` 一侧，朝 `+Z` 看物体背面。

`View3DController` 的相机本地 `+Z` 是视线前方，因此前视图对应 `cursor.y_rot = PI`，后视图对应 `cursor.y_rot = 0`。已同步修正菜单入口、Orbit Snap 的 Front/Rear 识别，以及测试里的 Front/Rear 断言。

继续检查右上角视口旋转指示器后，确认它的深度排序仍按旧相机理解：把相机空间 `z` 越大当成越靠近观察者。现在相机本地 `+Z` 是看向场景深处，所以靠近观察者反而是更小的相机空间 `z`。已同步修正：

- 正轴圆点的 `z_axis` 改为 `-axis_3d.z`。
- 负轴圆点的 `z_axis` 改为 `axis_3d.z`。
- 当视角正对某个轴时，`axis_3d.z < 0` 表示正轴贴近观察者。

因此从 `+Z` 向 `-Z` 看前视图时，右上角指示器中心应显示正 `Z` 在前，并且点击它会进入前视图。

默认新建 3D 场景的用户视角也重新核对过：`View3DController::Cursor()` 里硬编码的 `x_rot = -0.5`、`y_rot = -0.5` 会把相机放在后上方，也就是 `Y > 0`、`Z < 0` 一侧，并沿本地 `+Z` 看向原点，符合当前 `+Z Forward` 下“从后看向前方”的默认观察习惯。原注释还写着旧的 `+X +Y +Z`，已改成新坐标系语义；测试也补充断言默认相机 `origin.z < 0` 且视线 `basis.z.z > 0`。

继续核对固定正交视图后，确认判断标准应统一为“相机站在哪个世界轴方向，并沿相机本地 `+Z` 看回原点”：

- 顶视图：相机在 `+Y`，看向 `-Y`。
- 底视图：相机在 `-Y`，看向 `+Y`。
- 右视图：相机在 `+X`，看向 `-X`。
- 左视图：相机在 `-X`，看向 `+X`。

按这个标准复查 `View3DController::_to_camera_transform()` 后，顶/底已经正确：`x_rot = -PI/2` 会把相机放到 `+Y`，`x_rot = PI/2` 会把相机放到 `-Y`。左/右原来反了：`y_rot = PI/2` 实际是相机在 `+X` 看 `-X`，应属于右视图；`y_rot = -PI/2` 实际是相机在 `-X` 看 `+X`，应属于左视图。已同步修正菜单入口、快捷键共用的 `_menu_option()` 映射，以及 orbit snap 的 `VIEW_TYPE_LEFT/RIGHT` 识别；测试补充断言 Left/Right/Top/Bottom 四个方向的相机位置、看向方向和 basis determinant。

继续排查“物体没有阴影”后，确认阴影生成链路里仍有旧 `-Z` 前方残留。主相机、投影和 forward shader 已经统一为本地 `+Z` 前方，但 DirectionalLight 的 cascade shadow camera 仍按旧逻辑站在光空间 `z_max` 远端。这样 shadow pass 生成的深度范围与采样端的 `+Z` 深度方向相反，会让 directional shadow map 看起来失效。已把 directional cascade 的正交相机 origin 改到光空间 `z_min` 近端，沿本地 `+Z` 穿过整个 cascade，并把 soft shadow 的 `range_begin` 同步改为 `z_min`。AreaLight 的 paraboloid shadow culling 也从旧 `-Z` 展开改为本地 `+Z` 展开。SpotLight 使用 light transform + projection planes，Omni cube 使用已经迁移过的 `Basis::looking_at()`，本轮未发现同类旧方向。

继续对比原版 Godot 的空场景预览太阳后，确认“没有灯光和阴影”不是场景缺少 `DirectionalLight3D`，而是编辑器 3D 视口的 `preview_sun` 存在但没有正确参与漫反射。原因是 DirectionalLight 有两个方向概念：

- 节点本地 `+Z`：当前定制坐标系下的光线射出方向。
- shader 的 `L`：BRDF 里从表面指向光源的方向。

迁移后 `LightStorage::update_light_buffers()` 仍把本地 `+Z` 直接传给 shader，当作 `L` 使用，等价于让方向光从背面照明，所以模型只剩环境光，看起来没有预览太阳，也不会产生可见阴影。已把 DirectionalLight 写入 shader 的 `direction` 改成 `-本地 +Z`；SpotLight/AreaLight 的 `direction` 在 shader 中用于“光线射出方向”或裁剪朝向，和 DirectionalLight 的 `L` 语义不同，不能一起反号。

继续根据“物体已有明暗，但地面和立方体顶面完全发黑”排查后，确认编辑器预览太阳的角度 UI 仍保留旧 `-Z` 前方符号。原版 Godot 里正的太阳高度角会被写成负的 `sun_rotation.x`；迁移到相机/灯光本地 `+Z` 前方后，这会让预览太阳的本地 `+Z` 射出方向指向天空上方。由于 DirectionalLight 送给 shader 的 `L` 又必须是 `-本地 +Z`，最终水平面法线 `+Y` 会和 `L` 反向，地面、立方体顶面就只剩环境光。

本轮把预览太阳的高度角合同统一为：

- UI 里的正 altitude 表示太阳在天空中。
- 预览太阳本地 `+Z` 是光线射出方向，所以正 altitude 要让本地 `+Z` 带负 `Y` 分量，向下照到 XZ 地面。
- shader 里的 DirectionalLight `direction` 继续使用 `-本地 +Z`，表示表面指向光源的方向。

因此 `_preview_settings_changed()`、默认预览设置、环境刷新和 undo/redo 都改为直接使用 `Math::rad_to_deg(sun_rotation.x)` / `Math::deg_to_rad(altitude)`，不再在预览太阳 UI 边界额外取负。

另外，编辑器会把 `preview_sun_env.sun_rotation` 保存到场景编辑器状态里。旧状态没有版本号，会继续保存 `-60°` 这种旧语义值，导致重启后默认太阳仍从下往上照。已给新状态写入 `sun_rotation_coordinate_version = 1`；加载缺少该版本号的旧状态时，只迁移一次 `sun_rotation.x = -sun_rotation.x`，避免旧本地配置继续污染新坐标系验证。

继续排查“地面已有光照但没有 cube 阴影”后，确认 directional shadow 的投射物裁剪边界还有旧假设。上一轮已经把 shadow camera 放到光空间 `z_min_cam` 近端，并沿本地 `+Z` 渲染整个 cascade，但用于收集 shadow caster 的 frustum 近端仍使用主相机视锥端点的 `z_min`。在 `+Z` 光线方向下，cube 顶部这类比接收地面更靠近光源的几何可能落在 `z_min` 外，被提前裁掉，结果 shadow map 里没有 cube，地面自然采不到投影。

已把 directional shadow caster frustum 的近端同步改为 `z_min_cam`，让投射物收集范围和实际 shadow camera 深度范围一致。

根据后续复盘，已新增 `doc/customization/z-semantics-audit.md` 作为 Z 语义审计清单。初扫高价值模式约命中 `1146` 行、涉及约 `227` 个文件，本轮先按高风险排查并处理：

- `RenderingLightCuller`：DirectionalLight caster culling 使用本地 `+Z` 光线射出方向，cascade split 平面沿相机本地 `+Z` 向前推进。
- RD forward clustered/mobile：正交排序用的 near plane 放到相机前方 `z_near`。
- SDFGI / VoxelGI：light buffer 中的 directional/spot/area 方向与 LightmapGI、Sky 一致，统一为本地 `+Z` 光线射出方向。
- 位置光 shadow atlas 覆盖率估算：camera near plane 和 SpotLight cone base 改为 `+Z` 前方语义。

仍保留待查项：ClusterBuilder spot/area culling、VoxelGI 动态物体写回、其他离屏/烘焙相机、位置光 projector / 双抛物面阴影效果和外部格式导入导出边界。

继续按 Z 语义审计排查后，确认还有两处不能留给后续“碰到再补”的渲染方向问题：

- forward clustered 的 soft directional shadow 半径用 `shadow_range_begin` 和当前片元沿光线方向的距离计算。`shadow_range_begin` 来自 shadow camera 的本地 `+Z` 投影轴，而 shader `direction` 对 DirectionalLight 来说是 BRDF 的 `L`，方向正好相反。已把 soft shadow 的 `range_pos` 改为 `dot(-direction, v.xyz)`，让软阴影半径和 shadow camera 深度轴一致。
- ClusterBuilder 的 SpotLight 非宽角光锥 near-touch 检查仍把旧 `-Z` 当作光锥方向。已改为本地 `+Z` 光锥方向，否则贴近相机的 SpotLight 可能被错误分配 cluster，表现为局部灯光/阴影在某些视角消失。

随后继续复查 `RenderingLightCuller` 时发现一个更直接解释“有光照但没有 cube 阴影”的根因：cascade 分割面的位置确实应沿相机本地 `+Z` 推进，但 near 边界面的法线必须朝相机外侧，也就是 `-Z`。前一轮只改了位置，没有同步修正法线语义，导致 frustum 内部被判成正距离，shadow caster 可能在进入 directional shadow pass 前被裁掉。已把 cascade boundary plane 改为 `Plane(-camera_forward, camera_origin + camera_forward * distance)`，far 面仍通过取反得到朝场景深处外侧的 `+Z` 法线。

Raycast occlusion culling 继续按相机本地 `+Z` 统一：near 面局部坐标使用正向 `z_near`，ray 的 camera_dir 和排序方向都直接使用相机本地 `+Z`。对应 `Projection` 的矩阵、平面和 endpoints 测试已经同步到 `+Z` 可见空间，避免数学层和遮挡层各自维护一套符号。

`Camera3D::project_position()` 也跟随 `+Z` 深度合同修正：给定的 `z_depth` 表示从相机沿本地 `+Z` 向前的正向距离，因此局部切片平面应为 `z = +z_depth`。这里不能沿用旧 `Plane(Vector3(0, 0, 1), -p_z_depth)`，否则屏幕坐标反投影会落到相机身后。

VoxelGI 动态物体写回继续排查后，确认它有两套不同语义，不能一起反号：`_render_material()` 在 `+Z` 视空间下写出的 depth 已是正向 `vertex.z`，因此 `z_sign` 要跟随体素化相机本地 `+Z` 的世界轴符号；但 `voxel_gi.glsl` 里 `z_dir` 同时参与 `normal = ... - vec3(params.z_dir) * normal.z`，这是视空间法线还原到体素轴的旧符号合同，暂时保留并加注释。该项仍需要真实 VoxelGI 动态物体场景验证法线和写回方向。

粒子碰撞 heightfield 的离屏相机也已归类：它不是旧 `-Z` 前方残留，而是从碰撞体上方沿局部 `-Y` 往下烘高度图；在当前 `set_look_at()` 合同下，相机本地 `+Z` 正好指向目标点。`up = -local_Z` 只决定高度图纹理的 XZ 朝向，不能当作 forward 语义去翻。

editor gizmo 的 billboard handle 同样已归类：`t.origin - camera_xform.basis.get_column(2)` 表示让把手本地 `+Z` 朝向相机侧，和“面向相机”的可视把手语义一致，不是旧 Godot 的 `-Z forward` 残留。

`scene_forward_lights_inc.glsl` 中 SpotLight / AreaLight 的主方向语义继续保留为“灯本地 `+Z` 是光线射出方向”：Spot 的 cone attenuation 使用 `dot(spot_dir, light_to_vertex)`，Area 的正面判断使用 `dot(direction, vertex - light_pos)`。本轮发现 SpotLight 透射路径手写反解 shadow depth 时仍按普通 `clip = depth * 2 - 1` 处理，但 spot shadow matrix 使用了 reverse-Z，near 深度更大，far 深度更小；已改为先用 `clip = 1 - depth * 2` 还原，再反解 light-space 距离。位置光 projector 和双抛物面阴影仍需要真实材质/投影贴图场景看最终方向。

多视图合并相机继续修正：`Projection` 的 side plane normal 是 outward normal，在 `+Z` forward 下左右侧面法线相加会指向相机后方，所以合并相机的本地 `+Z` 必须取反；同时 `y` 轴 cross 顺序、far plane 法线和 near/far 距离符号也跟着同步，避免多视图主相机和单眼相机使用两套相反的 basis。VR/XR 多视图入口后续随模块裁剪，不再追加迁移成本。

screen-space 深度路径继续统一为 reverse-Z + 正向 `+Z` view depth：GI fallback 重建、SSAO/SSIL depth downsample、`copy_depth_to_rect_and_linearize()` 和 cubemap 转双抛物面阴影都先把硬件深度按 `clip = 1 - depth * 2` 还原，再反解为正向 view depth。这样后续 AO/IL/GI/调试拷贝拿到的线性深度不再暗含旧 `-Z` 视空间。

编辑器框选和运行时节点选择的 3D frustum far 面也已收口：near 面仍是相机后向法线并放在 `z_near` 处，但 far 面直接用相机本地 `+Z` 法线放到 `z_far`，不再通过 `-near_plane` 后继续 `+z_far`，否则实际 far 会变成 `z_near + z_far`。

volumetric fog 继续修正了两处旧深度假设：体积雾 cell 的 `view_pos.z` 已经是正向 `+Z` 深度，所以 temporal reprojection 还原上一帧 z 时不能再除以负的 frustum end，cluster z 查询也不需要 `abs(view_pos.z)`。SSR 同步把正交视线从旧 `-Z` 改为 `+Z`，并把反射线裁到 near plane 的条件改为只处理跑到相机后方的情况。TAA 的 closest-depth velocity 选择也按 reverse-Z 改为取最大 depth。SSAO/SSIL 的 normal buffer 读取端不再额外反转 normal.z，和 normal 写入端、GI、SSR 共用同一 view-space 法线合同。

手动：

- 启动 Project Manager。
- 打开一个 3D 项目。
- 新建 Camera3D，确认默认中心射线朝 `+Z`。
- 执行 `Node3D.look_at(Vector3(0, 0, 10))`，确认本地 `+Z` 指向目标。
- 检查 Front/Rear/Top/Right 视角、相机 gizmo、SpotLight3D gizmo、路径跟随和 SpringArm3D。
