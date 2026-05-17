# Z 语义审计

目标坐标系：

- `+X` 右
- `+Y` 上
- `+Z` 前
- 地面为 `XZ`
- 相机、灯光、路径切线等“前方”语义默认使用本地 `+Z`

本审计用于避免坐标系迁移变成零散补丁。所有新发现的问题都先归入这里，再判断是否需要修改、测试或保留。

## 搜索范围

初始扫描目录：

- `core`
- `scene`
- `editor`
- `servers`
- `modules`
- `tests`

初始高价值模式：

```text
Vector3(0, 0, -1)
Vector3(0, 0, 1)
Vector3::FORWARD
Vector3::BACK
Vector3::AXIS_Z
get_column(2)
get_column(Vector3::AXIS_Z)
vertex.z
-vertex.z
z_min / z_max
znear / zfar / z_near / z_far
```

当前扫描结果：

- 命中约 `1146` 行。
- 涉及约 `227` 个文件。

这些命中不能机械替换。普通坐标分量、AABB 尺寸、2D z-index、颜色/SH 通道、测试样本值等不一定和“前方”有关。

## 判断规则

### 必须按 `+Z` 前重新判断

- 相机视线、投影、屏幕射线、近远平面。
- DirectionalLight / SpotLight / AreaLight 的照射方向。
- 阴影相机、阴影裁剪、shadow matrix、shadow sampling。
- shader 中 view-space depth：`vertex.z` / `-vertex.z`。
- cluster、fog、DOF、SSAO、SSR 等依赖 view-space depth 的逻辑。
- 编辑器 3D 视口、gizmo、右上角方向指示器、固定视图。
- 路径跟随、SpringArm、音频方向、风向等把本地 Z 当“前方”的逻辑。

### 通常不应按前方语义修改

- AABB 的 z 尺寸、普通 `position.z`、随机测试数据。
- 2D canvas 的 z-index。
- 纯数学 API 的 `AXIS_Z` 枚举。
- 物理 joint 的局部轴编号，除非它明确代表“前方”。
- glTF/FBX 等外部格式转换的中间坐标，必须单独按格式合同判断。

## 当前已确认

### 数学基础

状态：已修改并有测试。

- `Vector3::FORWARD = +Z`
- `Vector3::BACK = -Z`
- `Basis::looking_at()` 使用本地 `+Z` 指向目标。
- `Projection` 透视/正交/frustum 使用本地 `+Z` 深度。

风险：

- 外部格式导入导出可能仍需要格式边界转换，不能直接套内部约定。

### Camera3D / 编辑器相机

状态：已修改并有测试。

- 屏幕中心射线沿本地 `+Z`。
- 编辑器默认相机位于后上方，看向原点。
- Top/Bottom/Left/Right/Front/Rear 固定视图已按“相机站在哪一侧，看回原点”重算。
- 右键飞行、鼠标 orbit、方向指示器已按本地 `+Z` 前修正。

待继续排查：

- `runtime_node_select.cpp` 的选择 frustum 与 near/far 面，目前代码已带 `+Z` 注释，但仍需纳入专项验证。

### 编辑器预览太阳

状态：已修改并实测反馈变好。

- 预览太阳本地 `+Z` 是光线射出方向。
- shader DirectionalLight 的 `direction` 是 BRDF 的 `L`，也就是表面指向光源，所以写入 `-本地 +Z`。
- 预览太阳 altitude 已从旧负号迁移为正 altitude 表示太阳在天空中。
- 旧 editor state 用 `sun_rotation_coordinate_version` 做一次性迁移。

风险：

- Directional shadow 还需要把“光线射出方向”和 shader `L` 区分到底。

## 高风险待查清单

### RendererSceneCull directional shadow

状态：已修改，待手动验证。

已处理：

- shadow camera 从光空间 `z_min_cam` 近端沿本地 `+Z` 渲染。
- caster frustum 近端同步改为 `z_min_cam`，避免把地面上方投影物裁掉。

已处理：

- `RenderingLightCuller` 的 directional caster culling 已从旧 `-basis.z` 改为本地 `+Z` 光线射出方向。
- cascade split 边界平面已沿相机本地 `+Z` 距离构建。
- forward clustered 的 soft directional shadow 半径计算已改用光线射出方向，也就是 shader `L` 的反向。

### RenderingLightCuller

状态：已修改，待构建验证。

发现：

- DirectionalLight 的 `lsource.dir` 仍为 `-basis.get_column(2)`。
- cascade boundary plane 使用 `origin + camera_normal * -distance`。

处理：

- 现在 DirectionalLight 本地 `+Z` 是光线射出方向，caster culling 应使用 `basis.get_column(2)`。
- 现在相机本地 `+Z` 是视线前方，cascade 距离平面位置应沿 `+camera_normal * distance` 放置。
- near 边界面的法线不能跟着改成 `+Z`，而要朝相机外侧 `-Z`，否则 frustum 内部会变成正距离，shadow caster 会被错误裁掉。

### RD render list near plane

状态：已修改，待构建验证。

发现：

- forward clustered/mobile 的 orthogonal sorting near plane 仍用 `-cam + d += znear` 这套旧写法。

处理：

- 这里主要影响正交相机下的实例深度排序，不一定影响当前阴影，但属于 `+Z` 前方迁移范围。
- near 面法线仍朝相机后方，但平面位置从 `d += z_near` 改为 `d -= z_near`，对应相机前方 `origin + forward * z_near`。

### ClusterBuilder

状态：部分已修改，待构建和手动验证。

发现：

- SpotLight base plane 仍使用 `-xform.basis.get_column(Z)`。
- Area/box depth 已有部分使用 `Vector3::FORWARD`。

处理：

- SpotLight 非宽角光锥的 near-touch 检查已改为本地 `+Z` 光锥方向。
- cluster culling 影响灯光是否参与某个屏幕 tile/cluster，属于渲染高风险项，需要继续用 SpotLight/AreaLight 场景手动验证。

### Environment GI / lightmap

状态：部分已修改，待构建验证。

发现：

- `environment/gi.cpp` 多处 `-light_transform.basis.get_column(Z)`。
- `lightmap_gi.cpp` 已有 `+basis.get_column(Z)`。

处理：

- 实时 GI 和烘焙 GI 对光线方向语义可能不同，不能机械改。需要按每个 API 的方向含义确认：是光线射出方向，还是表面指向光源方向。
- `LightmapGI` 和天空方向已经使用 `+basis.z`。本轮把 SDFGI / VoxelGI light buffer 中的 directional/spot/area 方向也统一为本地 `+Z` 光线射出方向。
- VoxelGI 体素化相机已按 `+Z` depth 修正 `z_sign`；`z_dir` 仍保留法线还原的独立符号合同，后续用真实动态物体场景验证。

### 粒子 / Billboard / GPU 排序

状态：已归类，仍需手动看效果。

发现：

- GPU 粒子排序和 Z billboard 过去通过 `-cam_transform.basis.get_column(2)` 加 shader 侧反号抵消旧 `-Z` 前方。
- editor gizmo 的 billboard handle 使用 `t.origin - camera_forward`，语义是让把手面向相机侧。

判断：

- GPU 粒子 view axis 已改为直接接收相机本地 `+Z`；editor gizmo 的 billboard handle 不需要反号。
- GPU 粒子定向吸引器已改为正 strength 推向本地 `+Z`，负 strength 推向本地 `-Z`，和公开前方语义一致。
- 粒子面向相机、排序和 motion vector 仍建议用真实粒子场景手动看一遍。

### Positional shadow atlas 覆盖率

状态：已修改，待构建验证。

发现：

- `renderer_scene_cull.cpp` 中位置光阴影覆盖率估算的 camera near plane 仍放在相机后方。
- SpotLight cone base 仍按 `origin - basis.z * d` 计算。

处理：

- camera near plane 改为 `origin + camera_forward * z_near`，法线仍朝相机后方。
- SpotLight cone base 改为 `origin + light_forward * d`，符合本地 `+Z` 照射方向。

## 排查顺序

1. Directional shadow 完整链路。
2. RenderingLightCuller 和 cascade caster culling。
3. forward clustered/mobile 正交排序 near plane。
4. ClusterBuilder 灯光体。
5. Environment GI / LightmapGI。
6. 编辑器 gizmo 和资源预览。
7. 路径、SpringArm、音频、粒子等场景组件。
8. 外部格式导入导出边界。

## 验证要求

每轮修改至少执行：

```powershell
& "C:\ce\_env_df9004\.pixi\envs\default\python.exe" -m SCons profile=misc/customization/scons-profiles/windows_3d_dev.py -j4 tests=yes
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test "*[Camera3D]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test "*[View3DController]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test
git diff --check
```

手动重点场景：

- Cube + Plane，无场景 DirectionalLight，使用编辑器 preview sun。
- Cube 顶面、侧面和地面都有光照层次。
- Cube 在地面上产生阴影。
- Front/Rear/Top/Right 固定视图和右上角指示器一致。

## 2026-05-16 全局 Z 语义排查进度

本节用于后续继续排查时快速接上上下文，不依赖聊天记录。

### 当前目标

继续把 Godot 默认 `-Z` 前方彻底迁移为内部统一坐标系：

- `+X` 右
- `+Y` 上
- `+Z` 前
- 地面 `XZ`
- 相机本地 `+Z` 是视线前方
- DirectionalLight / SpotLight / AreaLight 的节点本地 `+Z` 是光线射出方向

重要原则：

- 用户反馈的画面异常只作为定位线索，不能用翻转屏幕、反剔除、额外偏移等补丁隐藏问题。
- 每个修改必须能解释它在数据链路里的坐标语义。
- 找不到根因时，优先加日志、构造测试或继续缩小链路，不做“看起来对”的临时修补。

### 当前全局状态

已经进入“全面迁移后逐层查漏”的阶段。当前不是只查阴影，而是把所有旧 Godot `-Z` 前方语义逐个改成内部 `+Z` 前方。排查时按“底层合同 -> 场景节点 -> 编辑器交互 -> 渲染管线 -> 辅助系统 -> 外部格式边界”的顺序推进。

已基本确认并修改的区域：

- 数学基础：`Vector3::FORWARD/BACK`、`Vector3i` 文档、`Basis::looking_at()`、`Projection` 透视/正交/frustum 及对应矩阵/平面/endpoints 测试。
- Camera3D：屏幕射线、`project_position()` 正向深度切片、projection depth、测试。
- Raycast occlusion culling：near 面局部坐标、ray camera_dir 和 ray sort 输入已按相机本地 `+Z` 前统一。
- GPU 粒子：view-depth sort 和 Z billboard 的 view axis 已改为直接接收相机本地 `+Z`，移除旧 `-Z` 前方注释和双重反号。
- 编辑器 3D 视图：默认视角、前后左右顶底固定视图、右键飞行、鼠标 orbit、右上角方向指示器。
- 编辑器预览：preview sun 方向、preview sky/horizon、编辑器网格与真实几何共面时的显示关系。
- 常用 Node3D 相关：Path3D / PathFollow3D、SpringArm3D、Area3D 风向、AudioStreamPlayer3D 朝向。
- 多声道 3D 音频：front-left / front-right / center 声道方向已从旧 `-Z` 前方改为本地 `+Z` 前方，rear 声道同步反向。
- Forward clustered/mobile 主视图深度：`vertex.z` 作为正向视深，cluster z、fog、DOF/SS effects 等已做第一轮迁移。
- 渲染 front-face：左手 `+Z` 视空间下 Forward+ / Mobile raster front face 已调整。
- DirectionalLight/SpotLight/AreaLight：节点本地 `+Z` 作为光线射出方向；DirectionalLight shader `direction` 作为 BRDF `L` 单独反向处理。
- Directional shadow：shadow camera、caster frustum、soft shadow range 和 culling 已做本轮修正。
- GI / Lightmap：SDFGI / VoxelGI light buffer 与 LightmapGI 的主要灯光方向已按本地 `+Z` 光线射出方向统一。
- 多视图合并相机：双眼 frustum 合并得到的主相机 basis 已按本地 `+Z` 前方重算，near/far 距离符号已同步。
- 编辑器框选 / 运行时节点选择：3D 框选 frustum 的 far 面已改为直接使用相机本地 `+Z` 的 far 距离，不再把 near 偏移重复叠到 far 上。
- screen-space depth：GI fallback 重建、copy linearize、SSAO/SSIL depth downsample、cubemap 转双抛物面阴影都已按 reverse-Z + 正向 `+Z` depth 修正。
- volumetric fog：view-space z、temporal reprojection 和 cluster z 查询已按正向 `+Z` 深度修正。
- SSR/TAA：SSR 正交视线和 near-plane clip 已按 `+Z` 视空间修正；TAA closest-depth velocity 选择已按 reverse-Z 改为取最大 depth。
- SSAO/SSIL：normal buffer 读取端不再单独反转 normal.z，和 normal 写入端、GI、SSR 保持同一 view-space 法线合同。
- 文档/测试：核心数学和场景测试已同步一部分。

仍明确待查的区域：

- `environment/gi.cpp` 中 VoxelGI 六面体体素化相机已经按 `+Z` depth 修正 `z_sign`，但仍需要用真实 VoxelGI 动态物体验证法线/写回方向。
- editor gizmo 中“面向相机”的 billboard handle 已确认不是旧 `-Z` forward：它让把手本地 `+Z` 指向相机侧，也就是 `t.origin - camera_forward`。
- `renderer_scene_render_rd.cpp` 的粒子碰撞 heightfield 离屏相机已确认不是旧 `-Z` forward：它从上方沿局部 `-Y` 烘高度，`up = -local_Z` 只是纹理朝向。
- 其他离屏/烘焙相机 `set_look_at()` 仍需专项审计。
- `scene_forward_lights_inc.glsl` 的 SpotLight / AreaLight 主方向语义已确认使用本地 `+Z` 光线射出方向；SpotLight 透射路径的 reverse-Z shadow depth 反解已修正。
- volumetric fog 的 directional shadow 采样已按当前 shader `L` 语义保留，但还需要真实体积雾 + DirectionalLight 阴影场景看效果。
- SSR/TAA/SSAO/SSIL 已做静态语义修正，但仍需要真实 screen-space effects 场景看画面稳定性。
- 外部格式导入导出，例如 glTF/FBX/Mono API 文档边界，不能机械套内部 `+Z`，需要按格式合同单独确认。
- VR/XR/OpenXR/WebXR 已确定为长期裁剪方向，不再作为坐标系迁移风险投入；相关路径只按 `removal-ledger.md` 和构建裁剪策略维护。CameraFeed/AR 这类依赖 XR 的入口随裁剪策略处理。

### 全局方向语义约定

后续排查时优先按这些合同判断，而不是按旧 Godot 习惯判断：

- 相机本地 `+Z` 是视线前方。
- 节点本地 `+Z` 是“前方”，包括 Node3D、Camera3D、SpotLight3D、AreaLight3D、路径跟随默认前向。
- DirectionalLight / SpotLight / AreaLight 的节点本地 `+Z` 是光线射出方向。
- shader view-space 深度默认 `vertex.z > 0` 表示在相机前方，越大越远。
- editor 固定视图按“相机站在哪一侧，看回原点”定义：
  - Front：相机在 `+Z`，看向 `-Z`。
  - Rear：相机在 `-Z`，看向 `+Z`。
  - Right：相机在 `+X`，看向 `-X`。
  - Left：相机在 `-X`，看向 `+X`。
  - Top：相机在 `+Y`，看向 `-Y`。
  - Bottom：相机在 `-Y`，看向 `+Y`。
- DirectionalLight shader 里的 `direction` 是 BRDF 用的 `L`，表示表面指向光源，和节点本地 `+Z` 光线射出方向相反。
- shadow camera、caster culling、GI/lightmap/light voxel 等“光实际往哪里走”的系统应使用节点本地 `+Z` 光线射出方向。

### 已确认的阴影链路

DirectionalLight 至少有两个方向概念，不能混用：

- 节点本地 `+Z`：光线射出方向，用于 shadow camera、caster culling、GI/lightmap 等“光实际往哪里走”的逻辑。
- shader 的 `direction`：Forward shader 里给 BRDF 用的 `L`，表示从表面指向光源，所以等于节点本地 `+Z` 的反方向。

已确认并修正：

- `LightStorage::update_light_buffers()` 写入 DirectionalLight shader `direction` 时使用 `Vector3::BACK`，也就是 `-本地 +Z`。
- `RendererSceneCull::_light_instance_setup_directional_shadow()` 的 cascade shadow camera 已从光空间 `z_min_cam` 近端开始，沿本地 `+Z` 渲染到 `z_max`。
- directional shadow caster frustum 近端已用 `z_min_cam`，避免 cube 顶部这类更靠近光源的投射物被裁掉。
- `RenderingLightCuller` 的 directional caster culling 已使用本地 `+Z` 光线射出方向。
- `RenderingLightCuller` cascade split 平面位置沿相机本地 `+Z` 放置，但 near 面法线必须朝相机外侧 `-Z`，否则 frustum 内部会变成正距离并裁掉 caster。
- forward clustered soft directional shadow 的 `range_pos` 已改为 `dot(-direction, v.xyz)`，和 shadow camera 的 `+Z` 深度轴一致。

### 本轮最新修改

涉及文件：

- `servers/rendering/rendering_light_culler.cpp`
- `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl`
- `servers/rendering/renderer_rd/cluster_builder_rd.h`
- `doc/customization/z-semantics-audit.md`
- `doc/customization/left-handed-y-up-coordinate-plan.md`

核心结论：

- “有光照但 cube 没有投影”的最可疑根因是 `RenderingLightCuller` 的 cascade boundary plane 法线错误。
- 前一轮只把 plane 位置从旧 `-distance` 改成新 `+distance`，但没有同步保持 near 面法线朝相机外侧。
- 这会让 shadow caster culling 在进入 directional shadow pass 前错误剔除几何。

最新修正：

```cpp
boundary_planes[i] = Plane(
        -camera_normal,
        data.camera_transform.origin + camera_normal * plane_distance);
```

这里 `camera_normal` 是相机本地 `+Z` 的世界方向。plane 位置在相机前方，法线朝相机后方，frustum 内部仍保持负距离。

### 已跑验证

最新一轮已执行：

```powershell
& "C:\ce\_env_df9004\.pixi\envs\default\python.exe" -m SCons profile=misc/customization/scons-profiles/windows_3d_dev.py -j4 tests=yes
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test "*[Camera3D]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test "*[View3DController]*"
.\bin\godot.windows.editor.dev.x86_64.console.exe --headless --test
git diff --check
```

结果：

- 构建成功。
- Camera3D 专项测试退出码 `0`。
- View3DController 专项测试退出码 `0`。
- 完整 headless 测试退出码 `0`。
- 测试进程仍输出既有 `Failed to delete files` 清理日志。
- `git diff --check` 只提示 `AGENTS.md` CRLF 警告。

### 下次继续排查的总入口

先做全局手动验证，不要只看阴影：

1. 使用最新构建的 `bin/godot.windows.editor.dev.x86_64.exe` 打开 3D 项目。
2. 新建或打开一个包含 Cube、Plane、Camera3D、SpotLight3D、AreaLight3D、Path3D、SpringArm3D 的小测试场景。
3. 所有节点位置先放在容易判断的整数坐标，例如 `+X`、`+Y`、`+Z` 三个方向各放一个不同颜色物体。
4. 检查默认编辑器视角是否从 `-Z` 后方看向 `+Z` 前方。
5. 检查 Front/Rear/Left/Right/Top/Bottom 固定视图和右上角指示器是否一致。
6. 检查右键飞行：`W` 向相机前方 `+Z`，`S` 后退，`D` 向右，鼠标左右/上下旋转方向自然。
7. 检查中键 orbit：拖动方向、快捷键 orbit up/right、方向指示器点击目标一致。
8. 检查 Camera3D 预览和实际运行画面：屏幕中心射线应沿相机本地 `+Z`。
9. 检查 PathFollow3D 默认朝向：沿路径前进时本地 `+Z` 朝运动方向。
10. 检查 SpotLight3D / AreaLight3D：节点本地 `+Z` 是照射方向，gizmo、实际光照、阴影方向一致。
11. 检查 preview sun / DirectionalLight3D：地面、顶面、侧面有合理光照，cube 能投影。
12. 检查天空地平线、网格、地面视觉关系是否和 XZ 地面一致。
13. 检查透明面、背面剔除、法线显示，确认不是靠反剔除隐藏坐标问题。

如果仍然没有阴影，下一步不要改 shader 采样端，先检查 shadow pass 是否拿到了 caster：

- 在 `RendererSceneCull` render shadows 阶段临时统计 `scene_cull_result.directional_shadows[i].cascade_geometry_instances[j].size()`。
- 如果数量为 `0`，继续查 `RenderingLightCuller::cull_directional_light()` 和 `IN_FRUSTUM(cascade.frustum)`。
- 如果数量大于 `0`，再查 shadow matrix / depth compare / sampler compare。

### 后续全局高风险清单

这些项都和 Z 语义有关，不应等用户报一个现象才查一个：

- `RendererSceneCull` directional cascade frustum 六个平面的正负距离是否全部一致。
- `RenderingLightCuller::add_light_camera_planes_directional()` 中 `pt2 = pt0 - p_light_source.dir` 是否仍符合 `p_light_source.dir = 光线射出方向`。
- `scene_forward_lights_inc.glsl` 的位置光 projector / 双抛物面阴影仍需要真实 Spot/Omni/Area 场景手动看一遍，尤其是投影贴图方向。
- `volumetric_fog_process.glsl` 中 directional shadow 采样是否也混用了 shader `L` 和光线射出方向。
- `ClusterBuilder` 的 SpotLight/AreaLight 还需要手动验证，当前只修了已确认的 SpotLight near-touch 旧 `-Z` 语义。
- VoxelGI 动态物体写回仍保留在待查列表里。
- `screen_space_reflection.glsl`、TAA、SSAO/SSIL、subsurface、bokeh DOF 等 screen-space shader 需要继续按正向 `vertex.z` 验证。
- `debug_effects.cpp`、PSSM split debug、shadow atlas debug 等调试视图需要跟新深度语义一致。
- `runtime_node_select.cpp` 和编辑器点选/框选需要确认 near/far frustum 没有反。
- `Camera3D` cull mask、LOD、visibility range fade 以距离为主，理论上不受前方符号影响，但要确认没有用 `-z` 快捷判断。
- `AudioStreamPlayer3D`、Area3D 风向、SpringArm3D、RayCast3D/ShapeCast3D 默认方向如果有旧 `Vector3(0,0,-1)` 需要逐个归类。
- 外部资源导入导出边界要单独建表，记录“内部 `+Z`”和“格式原生坐标”的转换点，不能混在内部迁移里机械替换。
- VR/XR/OpenXR/WebXR 不进入这张 Z 语义排查表的后续实现清单；发现残留时优先确认是否已经被裁剪或隐藏。
