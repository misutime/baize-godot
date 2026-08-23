<!-- SPDX-License-Identifier: MIT -->
# O5 实施：GameWorldHost / MainLoop / Server Ports

> 阶段：O5（§14.10 修订路线：O5 = GameWorldHost/MainLoop/Viewport/Server Ports）。
> 本文是 **O5 阶段权威**：分层、端口契约、InputFrame 流程、社区实现对照。
> 决策权威：`D:\MisuNotes\3D游戏开发\Godot_ALL_IN_C#\Godot_Fork_GameObject-Components替换Node_源码级落地方案.md`（§14.5/§14.6/§15.5）。
> 契约：`O1-GameObject语义契约.md` §10（Backend Observation）/§11（Resources 端口）。
> 双世界：本层全部在 **Runtime World** 侧（进程宿主与端口；GameWorld 是语义核心，本层是可替换的进程外壳）。
> 实现：`modules/mainloop/`（纯 .NET 抽象，零 Godot 依赖）+ `test-projects/godot-slice/`（Godot 适配壳）。

## 1. 目标与非目标

- **目标**：
  1. `Sola3dMainLoop` 抽象（Runtime World 进程宿主）：GameWorld 挂载 + 双轨 tick 驱动 + Host 注册；
  2. **Host 接口集**：WindowHost / RenderWorld / PhysicsWorld / InputHost / UIHost——每类封装一块引擎能力；
  3. **Port 三通道**：Event / Command / Observation——Backend 回传的唯一通道（§14.6 权威矩阵落地）；
  4. **InputFrame 端口**：InputHost 收集平台输入 → 生成 InputFrame → 注入 GameWorld（复用 §11 `AddResource` 端口）；
  5. **Godot 适配壳**：`GodotSola3dMainLoop : MainLoop`（在 godot-slice 内，验证 Godot API 桥接）。
- **非目标**：
  - 真实窗口/渲染/物理实现（O6：Transform/Mesh/StaticCollider backend + vertical slice）；
  - 编辑器预览宿主（O7）；
  - 完整 UI 系统（O8 域迁移）。

## 2. 分层（§14.5 修订落地）

```text
GameWorld                    // 纯运行时世界（O1，0 依赖）——语义核心，不依赖本层
Sola3dMainLoop               // 进程宿主抽象（本层）——Runtime World 的外壳
├─ Host 注册表                // IHost 集合（有序，Unity PlayerLoopSystem 同构）
│   ├─ IWindowHost           // 窗口/平台事件
│   ├─ IRenderWorld          // RenderingServer/RID（O6 实现，O5 壳）
│   ├─ IPhysicsWorld         // PhysicsServer/Jolt（O6 实现，O5 壳）
│   ├─ IInputHost            // 输入采集 → InputFrame
│   └─ IUIHost               // 最小 UI 宿主（O5 留接口）
└─ Port 通道                 // EventBus / CommandBus / ObservationBus

GodotSola3dMainLoop : MainLoop   // Godot 适配壳（godot-slice 内）—— Godot 进程入口
```

关键原则（§14.5）：
- **GameWorld 不依赖本层**：纯 .NET / 服务器 / 编辑器预览 / Godot 进程四环境复用；
- **Host 只通过 Port 与 GameWorld 对话**：Backend 永不隐式修改 Gameplay 状态（§14.6 禁止项）；
- Host 顺序确定（PlayerLoopSystem 分层同构），保证每帧推进次序稳定。

## 3. Port 三通道（§14.6 权威矩阵落地）

```csharp
// 通道语义（本层契约；O6 起由各 Backend 填充具体负载）
IEventBus      // 事件：Backend → Gameplay（碰撞/命中/UI 点击——"发生了什么"）
ICommandBus    // 命令：Gameplay → Backend（"请把 Mesh 画出来/把 Collider 注册"）
IObservationBus // 观察：Backend → Gameplay（Physics 权威的 RigidBody 位姿回传——"权威在那边"）
```

| 数据/事件（§14.6 矩阵） | 权威源 | 通道 |
|---|---|---|
| Kinematic Transform | GameWorld | GameWorld → Backend（Command） |
| Mesh/Material | GameWorld/Asset | GameWorld → Renderer（Command） |
| Dynamic RigidBody Pose | Physics Backend | Physics → GameWorld（Observation） |
| Collision/Trigger | Physics Backend | Physics → Tick Event（Event） |
| Input | Platform Host | Host → GameWorld InputFrame（注入） |
| UI Click/Focus | UI Backend | UI → GameWorld（Event/Command） |

## 4. GameLoop 双轨驱动（Bevy MainSchedule/FixedMain 同构）

- **variable tick**：每帧一次（`GameWorld.Tick(delta)`，`TickIndex++`）——与渲染帧对齐；
- **fixed tick**：`GameWorld.FixedTick()`（固定 `FixedDelta`）——由**累计时间**决定本帧跑几次
  （参考 Bevy `RunFixedMainLoop`：`elapsed / fixedDelta` 整数次），保证服务器/客户端同节奏。

```csharp
// 伪码：Sola3dMainLoop.Frame(delta)
void Frame(float delta) {
    _accumulator += delta;
    while (_accumulator >= world.FixedDelta) {   // Bevy RunFixedMainLoop 同构
        inputHost.Sample();                       // 输入 → InputFrame
        inputPort.Inject(world);                  // 注入 GameWorld（§11 AddResource）
        world.FixedTick();                        // fixed 步进（物理权威域）
        _accumulator -= world.FixedDelta;
    }
    var frame = inputHost.LastFrame();            // 每帧最新输入
    inputPort.Inject(world);
    world.Tick(delta);                            // variable 步进（游戏逻辑）
    observationBus.Dispatch();                    // Backend → Gameplay 观察回传
}
```

- 确定性保持：fixed 域内 `FixedDelta` 恒定、`FixedTickIndex` 单调、输入在 fixed 边界采样
  （§14.6：GameWorld 在 fixed tick 边界收集 Observation）。

## 5. InputFrame

```csharp
public readonly record struct InputFrame(ulong TickIndex, IReadOnlyList<InputSample> Samples);
// InputSample：Key(Press/Release) 或 Axis(Value) 或 Pointer(pos/delta)
```

- InputHost 负责采集 → 生成 `InputFrame` → 存入共享资源（`world.AddResource(InputState)` 或每帧替换）；
- Gameplay 组件经 `GetResource<InputState>()` 读取——**不直接摸平台 API**；
- headless 模式（`DisplayServer.get_name() == "headless"`）不采集真实输入，测试注入合成帧。

## 6. 社区实现对照（2026-08-23 调研，设计依据）

| 我们的构件 | 社区实现 | 借鉴点 |
|---|---|---|
| `Sola3dMainLoop : MainLoop` | Godot 官方自定义 MainLoop（SceneTree 只是默认实现，可自定义 C# 子类） | 直接可用：`_Initialize/_Process/_Finalize`；headless 检测（`DisplayServer.get_name()=="headless"` → 纯逻辑跑） |
| Tick 双轨（variable+fixed） | Bevy `MainSchedule` + `FixedMain` + `RunFixedMainLoop` | `elapsed/fixedDelta` 判帧节奏；固定步子调度独立于渲染帧 |
| Host 列表（Window/Render/Physics/Input/UI） | Unity `PlayerLoopSystem` 层级（subSystemList 嵌套阶段） | 有序子系统列表 + 插入式扩展；官方建议插入而非整体替换 |
| sim/render 隔离 + Port | Dom Williams 引擎架构（simulation 完全独立，输入批量注入、渲染命令回传） | 每 tick 输入批量注入、后端只回传命令，永不隐式改逻辑 |

参考链接：
- Godot MainLoop 官方文档：https://docs.godotengine.org/en/stable/classes/class_mainloop.html
- Bevy main_schedule.rs：https://docs.rs/bevy_app/latest/src/bevy_app/main_schedule.rs.html
- Unity Player Loop 定制：https://docs.unity3d.com/Manual/player-loop-customizing.html
- Dom Williams 引擎架构：https://domwillia.ms/devlog2/

## 7. 验证清单（O5 验收）

- [x] `Sola3dMainLoop` 纯 .NET 抽象（零 Godot 依赖）可编译、headless 可测；
- [x] 模拟 Host 驱动 GameWorld 固定 N 帧：TickIndex/FixedTickIndex 计数正确（双轨判帧）；
- [x] InputFrame 注入：合成输入 → Gameplay 组件可读（GetResource<InputState>）；
- [x] Observation/Event 回传：Backend 观察不隐式改 Gameplay（只进通道）；
- [x] Godot 适配壳编译通过（Godot.NET.Sdk 可用）；
- [x] O1–O4 242 项基线不回归。