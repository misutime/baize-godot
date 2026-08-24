<!-- SPDX-License-Identifier: MIT -->
# Gateway 对接地图与迁移路线（后续开发参考）

> 本文是**跨 O 阶段的后续参考**：Godot 内核需要对接的 Gateway 全集、当前实现状态、已立样板模式、优先级路线与关键坑。做任何新域迁移（动画/音频/导航/UI/相机…）前先读本文与 `O8-交接文档.md`。
> 定位一句话：**Gateway = Runtime World 与 Godot Core 的唯一对接点（唯一跨语言缝），经 Server API 触碰保留的 C++ 内核子集**；GameWorld 纯 C# 零跨语言，每个域一个 Gateway、低频投影。
> 权威来源（冲突时以上级为准）：`O0-源码依赖地图.md`（Node 派生类 → Gateway 映射全集）、`O1-GameObject语义契约.md`（§10 Gateway Observation）、`O5-GameWorldHost与ServerPorts.md`（Port 三通道）、`O6-GameWorldGateway与垂直切片.md`、`O8-交接文档.md`、`术语表-双世界与内核边界.md`、`Node能力去向与复用边界.md`、`性能路径与跨语言边界.md`。

---

## 1. 代码现状：接口族与真桥状态

接口族定义在 `modules/mainloop/Sola3dMainLoop.cs`（O5，纯 .NET 零依赖）：

| 接口 | 职责 | 实现状态 |
|---|---|---|
| `IGateway` | 帧生命周期钩子（BeginFrame/EndFrame） | ✅ 基类 |
| `IInputGateway` | 输入采集（fixed 边界采样 → InputFrame） | ✅ `GodotInputGateway`（InputMap 动作采样，WASD/空格；`InputPreview` --input 演示，headless 空帧 + 真窗口按键待用户协助验证） |
| `IWindowGateway` | 窗口宿主 | ⚠️ 接口已定义，无真桥 |
| `IRenderGateway` | 渲染世界宿主 | ✅ `GodotRenderGateway` 完整（O6/O7.5 建立，O8-B 闭合生命周期） |
| `IPhysicsGateway` | 物理世界宿主 | ✅ `GodotPhysicsGateway` 第一刀（O8 物理域：下落 + 位姿回传） |
| `IUIGateway` | UI 宿主 | ⚠️ 接口已定义，无真桥 |

Port 三通道（`modules/mainloop/Ports.cs`）：`EventBus`（Gateway→Gameplay 碰撞/命中）、`CommandBus`（Gameplay→Gateway 画/注册）、`ObservationBus`（Gateway→Gameplay 权威位姿回传，fixed 边界统一分发）。

## 2. 规划全集：Godot 内核待对接点（O0 地图汇总）

| 域 | Gateway | 对接的 Godot Core / Server API | 数据组件（O0 规划目标态） | 状态 |
|---|---|---|---|---|
| 渲染 | `RenderGateway` | RenderingServer（mesh/light/camera/viewport/shader/particles） | `MeshComponent`/`LightComponent`/`VfxComponent` | ✅ 真桥已建（俯视 cube 级），Light/Camera/Vfx 子域待扩 |
| 空间变换 | `TransformGateway` | Node3D Transform 语义（O1 §7） | `TransformComponent` | ⚠️ 当前 Transform 是纯数据组件，投影直读；空间继承/插值是后续 |
| 相机 | `CameraGateway` | camera_3d | `CameraComponent` | ⚠️ 未定义；演示用 Camera3D 节点过渡 |
| 物理 | `PhysicsGateway` | PhysicsServer3D/Jolt（collision/rigid/joint/area/raycast） | `StaticColliderComponent`/`RigidBodyComponent` + Joint/Area/Raycast | ✅ 第一刀（Box static+rigid）；关节/区域/射线待扩 |
| 动画 | `AnimationGateway` | AnimationMixer（scene/animation） | `AnimatorComponent`（O8）；骨骼 `skeleton_3d.h` 族 | ❌ 未定义未实现 |
| 音频 | `AudioGateway` | AudioStreamPlayer 2D/3D、AudioStreamPreviewGenerator | `AudioComponent` | ❌ 未定义未实现 |
| UI/2D | `UIGateway` | CanvasItem/CanvasLayer/StatusIndicator | UI/2D 组件 | ⚠️ 接口已定义无真桥；产品聚焦风格化 3D，UI 长期保留 backend |
| 导航 | `NavigationGateway` | NavigationAgent/Region/Link/Obstacle | `NavigationComponent` | ❌ 未定义未实现 |
| 输入/窗口 | `InputGateway`/`WindowGateway` | 输入底层/平台窗口后端 | — | ✅ 输入真桥（GodotInputGateway）；窗口仍走 SceneTree |

**后移/不迁移**（O0 标注）：GI（lightmap/lightmap_probe，Render 后移）、XR（平台域保留 backend）、HTTPRequest/Multiplayer（NetService 后移）、弹簧约束/IK（高级后移）。

## 3. 已立样板：两个可复制的模式

### 3.1 渲染域模式（O6 → O7.5 → O8-B）

```text
Transform/Mesh 数据组件（gameobject）
  → SceneProjector（modules/editor，纯 .NET 投影 → PreviewRenderCommand）
  → RenderSnapshotTracker（帧差异：删除消失 Uid / 同帧去重）
  → GodotRenderGateway.Consume（test-projects/godot-slice）
  → RenderingServer RID（MeshCreate + AddCubeSurface 绑 shader 材质 + InstanceCreate2 + InstanceSetTransform）
生命周期：多 MeshPath 独立 surface；同 Uid 换 mesh 用 InstanceSetBase rebase；PreviewRemoveCommand 释放实例；Dispose 幂等
```

### 3.2 物理域模式（O8 第一刀）

```text
StaticCollider/RigidBody 数据组件（gameobject）
  → PhysicsProjector（modules/editor，纯 .NET 投影 → PhysicsBodyCommand）
  → GodotPhysicsGateway.Consume（内部存活差异：上帧有本帧无 → 释放 RID）
  → PhysicsServer3D RID（SpaceCreate/SetActive → BodyCreate → BodySetSpace → BoxShapeCreate → BodyAddShape → 模式/质量/初速/初始 Transform）
  → 每帧 EndFrame：BodyGetState(Transform) 采样 → PhysicsObservation → ObservationBus → Gameplay
```

### 3.3 通用要点（新域照抄）

1. **命令/观察负载放 `modules/editor`（纯 .NET）**：引 gameobject+mainloop，零 Godot 依赖，可 headless 单测；Godot 实现放 `test-projects/godot-slice/Runtime/`。
2. **单向投影**：Gateway 永不隐式修改 Gameplay；回传只经 ObservationBus，GameWorld 在 fixed tick 边界收集（O1 §10）。
3. **身份语义**：物理/运行时用 `ObjectId`（Index+Generation，防复用）；渲染演示用文档 `Uid`（文件层稳定身份）。两者勿混。
4. **验证三件套**：纯 .NET 投影断言（editor-core-tests）→ headless e2e（godot-slice，`--quit-after` + 日志断言）→ 真窗口冒烟（AGENTS §9 三十秒 + .tmp 日志）。

## 4. 优先级路线（参考 O8 §3；决策以总方案为准）

| 优先级 | 切片 | 内容 |
|---|---|---|
| P0 | **物理 → 渲染联动闭环** | ✅ 已完成：`PhysicsRenderPreview`（--physics-render）——`PhysicsObservation` 写回 GameWorld Transform → 渲染投影跟随，真窗口可见下落（两个 Gateway 首次协作） |
| P1 | 动画域 | `AnimatorComponent` + AnimationGateway（AnimationMixer Server API） |
| P2 | 音频域 / 导航域 | `AudioComponent` + AudioGateway；`NavigationComponent` + NavigationGateway |
| P3 | UI 域 | UIGateway 真桥（后端元素走 CanvasItem）；产品聚焦 3D，优先级最低 |
| P4 | 相机 / 空间继承 | CameraComponent + CameraGateway；TransformComponent 空间继承/插值（TransformGateway） |

每一刀验收标准：新数据组件（真实命名）+ 投影器 + Gateway 实现 + 测试（投影断言 / headless e2e 断言 / 30 秒冒烟）→ `dotnet build Sola3d.slnx` 全绿。

## 5. 关键坑（本轮实测，勿重踩）

1. **PhysicsServer3D 无公开 `step`**：物理由 SceneTree 每个 physics tick 自动步进所有 active space（`SpaceSetActive(true)` 后即被步进）；自定义 MainLoop 下需另想办法（O8 §3② 耦合点）。
2. **动态刚体每帧禁止重设 Transform**：GameWorld 投影的初始位姿每帧 `BodySetState(Transform)` 会把刚体钉回起点，重力积分被重置——刚体权威位姿只在物理侧，经 ObservationBus 回传（静态体可每帧跟随）。
3. **用 `BodyAddShape` 而非 `BodySetShape`**：后者要求 shape 索引已存在，否则 `Index p_index = 0 is out of bounds`。
4. **RenderingServer 手建 mesh 必须绑真实 shader 材质**：`MaterialCreate()` 是空容器，必须 `ShaderCreate→ShaderSetCode(spatial)→MaterialSetShader→MeshSurfaceSetMaterial`，否则两渲染器都不显示；`MeshAddSurfaceFromArrays` 调用别删（否则 surface_count=0）。
5. **身份区分**：`GameWorld.CreateGameObject` 不分配文件层 Uid（恒 0），物理命令必须用运行时 `ObjectId`；渲染 `SceneProjector` 已做回退（Uid 有效用 Uid，否则编码 `ObjectId`），直接 GameWorld 投影（P0 联动）依赖此回退
6. **演示未挂 Sola3dMainLoop 时**：ObservationBus 的 `Dispatch()` 需手动驱动（每帧调用），否则观察只入队不派发。

## 6. 验证命令速查

```text
# 物理域 headless e2e（360 帧自动退出；日志含 observations=N / lastY < initialY 断言）
bin\godot.windows.editor.dev.x86_64.mono.console.exe --headless --path test-projects\godot-slice --quit-after 400 -- --physics

# 渲染域真窗口冒烟（300 帧自动截图退出 → user://demo_cube.png）
bin\godot.windows.editor.dev.x86_64.mono.console.exe --path test-projects\godot-slice

# 单元测试（纯 .NET）
dotnet run --project test-projects\editor-core-tests --no-restore   # O7 编辑 + O8-A/B/C
dotnet run --project test-projects\mainloop-core-tests --no-restore # O5 Host/Port
dotnet run --project test-projects\vertical-slice-tests --no-restore # O6 垂直切片
dotnet run --project test-projects\gameobject-core-tests --no-restore# O1-O4 内核

# 统一构建
dotnet build Sola3d.slnx --no-restore
```

日志一律放 `.tmp/`；真窗口按 AGENTS §9 三十秒规则 + 用户协助（禁模拟点击）；结束后清理 godot 进程。