<!-- SPDX-License-Identifier: MIT -->
# O6 实施：Transform/Mesh/StaticCollider backend + 最小 vertical slice

> 阶段：O6（§14.10 修订路线：O6 = Transform/Mesh/StaticCollider backend + 最小 vertical slice）。
> 本文是 **O6 阶段权威**：Gateway 投影契约、组件定义、vertical slice 验收、§14.6 权威矩阵落地。
> 决策权威：`D:\MisuNotes\3D游戏开发\Godot_ALL_IN_C#\Godot_Fork_GameObject-Components替换Node_源码级落地方案.md`（§14.6/§15.6/§4.7）。
> 契约：`O1-GameObject语义契约.md` §10（Backend Observation）/§7（Transform 不属于层级内核，由 TransformComponent+TransformGateway 承担）。
> 双世界：组件在 **Runtime World** 侧（GameObject 实例持有），Gateway 投影是其触碰 Godot 内核的全部触点（状态 C：Server-backed，无 Node）。
> 实现：`modules/gameobject/`（数据组件）+ `test-projects/vertical-slice-tests/`（纯 .NET headless 验证）+ `test-projects/godot-slice/`（Godot RenderingServer 真桥）。

## 1. 目标与非目标

- **目标**：
  1. **数据组件**：`TransformComponent` / `MeshComponent` / `StaticColliderComponent`（纯 .NET，Schema 驱动，O4 格式可序列化）；
  2. **投影路径**：backend 从 GameWorld 读组件状态 → 投影到 Server（§14.6 矩阵：Kinematic Transform/Mesh = Command 下行）；
  3. **最小 vertical slice**：一个带 Transform+Mesh+StaticCollider 的 GameObject 经 Gateway 投影可 headless 验证；
  4. **Godot 真桥**：godot-slice 里 `IRenderGateway` 实现用 RenderingServer 建 RID（编译验证）。
- **非目标**：
  - Dynamic RigidBody（物理 Observation 上行留到 physics 域完整时）；
  - 编辑器显示（O7）；
  - 动画/UI/Audio（O8 域迁移）。

## 2. 数据组件（modules/gameobject，纯 .NET 零依赖）

```csharp
[GameComponent] public sealed class TransformComponent : GameComponent {
    [GameProperty] public Vector3 Position;   // System.Numerics，O1 白名单扩展？→ 见 §2.1
    [GameProperty] public Quaternion Rotation;
    [GameProperty] public Vector3 Scale = Vector3.One;
}
[GameComponent] public sealed class MeshComponent : GameComponent {
    [GameProperty] public string MeshPath = "";      // 资源路径（O4 资源引用 token 前置）
    [GameProperty] public string MaterialPath = "";
}
[GameComponent] public sealed class StaticColliderComponent : GameComponent {
    [GameProperty] public Vector3 BoxSize = Vector3.One;
}
```

### 2.1 数值类型进白名单（必要扩展）

O1 序列化白名单（契约 §10）为 `int/float/double/bool/string/enum`。O6 需 `Vector3`/`Quaternion` 进白名单——**本文档先改契约 §10**：白名单增加 `Vector3`（x/y/z 各 float）与 `Quaternion`（x/y/z/w），序列化编码为 `Float Token × 分量`（确定性：分量序固定）。O1 契约 §14 增补 R27。

## 3. Gateway 投影路径（§14.6 矩阵落地）

```text
GameWorld（语义权威）
    │ 每帧 SceneProjector 读组件的语义状态
    ▼
CommandBus（下行：请把 Mesh 画出来/设 Transform）
    ▼
IRenderGateway.Consume(commands)  →  RenderingServer.RID 建/改（Godot 侧真实现）
```

- **方向唯一**（§14.6）：Kinematic Transform/Mesh 权威在 GameWorld → backend 只读投影，**永不隐式改 Gameplay**；
- 上行（Physics Observation）在本轮只留接口契约，不由 Render 域实现（O6 物理域后续）；
- `SceneProjector`：遍历世界中有 `TransformComponent` 的 GameObject，产 `RenderCommand`（含对象 Uid/ObjectId、position/rotation/scale、mesh 路径）。

## 4. 最小 vertical slice（验收）

```text
GameObject "Cube"
├─ TransformComponent(x=0,y=0,z=0)
├─ MeshComponent("res://primitive/cube.mesh")
├─ StaticColliderComponent(BoxSize=1)
        │ SceneProjector.Project
        ▼
CommandBus: RenderCommand{Transform, MeshPath}
        ▼
FakeRenderGateway（headless 测试）：记录收到的命令
```

**验收清单**：
- [x] 数据组件可 Add/Remove/Get（GameObject 生命周斯走通）；
- [x] 白名单扩展：含 Vector3 属性的组件可 Capture→Serialize→Deserialize→Restore 往返（hash 相等）；
- [x] `SceneProjector` 从世界投影出正确 RenderCommand（位置/缩放/mesh 路径）；
- [x] backend 消费命令不反写 Gameplay（投影单向断言）；
- [x] godot-slice `GodotRenderGateway` 编译通过（RenderingServer API 接入）；
- [x] 全部既有基线（O1-O4 242 + O5 14）不回归。

## 5. Godot 真桥（godot-slice，状态 C 第一块实体砖）

```csharp
public sealed class GodotRenderGateway : IRenderGateway {
    // 每帧 Consume(commands)：
    //   RenderCommand.CreateMesh → RenderingServer.MeshCreate() 建 RID
    //   RenderCommand.SetTransform → RenderingServer.InstanceSetTransform(rid, ...)
}
```

- 只做最小：Mesh RID 建立 + Transform 投影（StaticCollider 物理域后续）；
- 真窗口运行按 AGENTS §9 三十秒规则取舍；交互验证走用户协助流程（禁止模拟点击）。

## 6. 验证链路（延续 headless 优先）

1. `vertical-slice-tests`（新）：纯 .NET，断言投影命令流与往返——**不碰 Godot**；
2. `godot-slice`：GodotRenderingServer 真桥编译级验证（能跑则 30 秒规则跑一次 headless）；
3. 全量基线回归。