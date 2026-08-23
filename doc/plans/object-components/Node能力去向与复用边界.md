<!-- SPDX-License-Identifier: MIT -->
# Node 能力去向与复用边界（团队必读）

> 定位：O6 完成后的**概念澄清文档**——帮助团队成员理解"Godot 约 1000 个 Node 类型在我们引擎里怎么办"。
> 一句话结论：**不是全部重写**。Node 的能力分三类——机器能力复用 Godot Server（不重写）、
> 组织能力已重写为 GameObject 内核（O1–O4 完成）、编辑器壳 O7 重做。
> 决策权威：`D:\MisuNotes\3D游戏开发\Godot_ALL_IN_C#\Godot_Fork_GameObject-Components替换Node_源码级落地方案.md` §3/§4/§5/§7。
> 与本仓库其他文档的关系：O5/O6 文档讲"怎么做"，本文讲"为什么不用做那么多"。

## 1. 核心结论

```text
Node 能力
 ├─ 机器密集计算（渲染/物理/寻路/音频/资源） → 复用 Godot Server，不重写（§3.2）
 ├─ 场景实体组织（生命周期/层级/序列化/身份） → 已重写为 GameObject + Components（O1-O4）
 └─ 编辑器工具壳（SceneTreeDock/3D 编辑器）    → O7 重做（§7）
```

**关键量级感**：真正"从零重写"的只有 GameObject 内核本身（O1，已完成）。
其余全是"接线"——把 Godot 已有的 Server 能力接到我们的组件上，每个域写一个 Gateway。

## 2. 三类能力明细

### ① 复用（不重写）—— Godot 底层 Server（§3.1/§3.2）

| Godot 能力 | 我们怎么用 |
|---|---|
| RenderingServer | `MeshComponent` → RID（O6 `GodotRenderGateway` 已直连） |
| PhysicsServer / Jolt | `ColliderComponent` → shape RID（物理域，O8 起） |
| NavigationServer | 寻路域（O8 起） |
| AudioServer | `AudioComponent` → 音频后端（O8 起） |
| GPU 粒子后端 | 粒子域（O8 起） |
| Mesh/Material/Texture 资源系统 | 资源层直接复用 |
| 输入底层 / 平台窗口后端 | `InputGateway`/`WindowGateway` 复用 |
| C# Roslyn / Reload 基础设施 | 直接复用 |

这些服务**不需要 GameObject 继承 Node**——组件直接持有 backend handle（§3.1）：

```text
MeshComponent        → RenderingServer RID
TransformComponent   → transform update
PhysicsBodyComponent → PhysicsServer body RID
ColliderComponent    → shape RID
AudioComponent       → 音频后端
```

### ② 重构（"重实现"但已是我们的形态）—— 场景实体组织层（§3.3）

| Node 侧 | 我们的形态 | 阶段 |
|---|---|---|
| SceneTree/MainLoop 主模型 | `Sola3dMainLoop` | O5 |
| Node 生命周期 | `GameComponent` 生命周期 | O1 |
| Node parent/children | `GameObject` 层级（ObjectHierarchy） | O1 |
| PackedScene/SceneState | `.bscene` / `.bprefab` | O4 |
| NodePath 语义引用 | `Uid`（@引用） | O4 |
| C# 默认 API | `GameObject` API | O1–O2 |
| 场景导入/实例化 | `BSceneLoader` | O4 |

**这一层不是"逐个 Node 重写"，而是抽象层重写一次**——O1/O4/O5 已覆盖大半。

### ③ 编辑器工具壳（O7 起，§7）

| Node 侧 | 我们的形态 |
|---|---|
| SceneTreeDock | `ObjectsDock` |
| Node3DEditor / Gizmo | `ObjectSelection` + `ComponentGizmoAdapter` |

不能只把 Dock 的文字从 Node 改成 Object——编辑器数据结构整体换。

## 3. 认知纠正：跳过 Node 壳，直连 Server

最常见的误解是"Node3D 的渲染能力要被我们的组件重写一遍"。

**真相**：`Node3D`/`StaticMesh3D` 等"表现节点"本身就是 Node 给 Server 能力包了一层壳。
我们要做的不是重写它们的渲染能力（那是 RenderingServer 的事），而是**跳过那层壳**：

```text
旧：StaticMesh3D（Node）→ 内部再调 RenderingServer
新：MeshComponent（组件）→ RenderGateway → RenderingServer   ← 中间无 Node
```

这正是方案 §5.3 目标态（状态 C：Server-backed）——我们已经按此做（O6 `GodotRenderGateway`，
从第一天就没有"挂 Node3D 的桥"，§15.1 已定不提供 Node 过渡路径）。

## 4. 工具类 Node 的处理原则（避免过度设计）

**不是每个 Node 都有对应组件**。像 `Timer`/`Tween`/`AudioStreamPlayer` 这类纯工具/纯封装节点：

- 要么吸进 `GameWorld` 服务（如 `world.CreateTimer(...)`）；
- 要么组件内部直接用底层类（`Time`/`Tween`）；
- **只重写承载语义的部分**，不为"凑对应"造组件。

判断标准（§15.2 精神）：开发者不需要看到的东西，就不组件化——开发者面只有
`Objects + Components + GameObject + GameWorld + .bscene + .bprefab`。

## 5. 已落地对照（截至 O6）

| 阶段 | 做了什么 | 属于哪类 |
|---|---|---|
| O1 | GameObject 内核（层级/生命周期/身份/序列化） | ②组织层重写 |
| O2 | GameObject-first Shooter（零 ECS/Node 玩法闭环） | ②验证 |
| O3 | Schema 元数据 + 可读文本格式 | ②纵深 |
| O4 | .bscene/.bprefab + 实例化 + override | ②纵深 |
| O5 | `Sola3dMainLoop` + Backend 接口 + Port | ②+①接线 |
| O6 | Transform/Mesh/StaticCollider 组件 + RenderGateway 直连 RenderingServer | ①复用接通（状态 C 第一块砖） |
| O7 | 编辑器第一切片（3D view 显示 Design World 对象） | ③ |
| O8 | 按域迁移（物理/动画/UI/Nav/Audio） | ①逐个接通 |
| O9 | 关闭 Node API，物理删除 | 收尾 |

## 6. 常见误解（FAQ）

- **"我们要写 1000 个组件对应 1000 个 Node？"** 不是。只有承载语义的才组件化；工具类吸进服务；表现类直连 Server。
- **"TransformComponent 是不是 Node3D 的重写？"** 是语义层的对应，但**不含** Node3D 的插值/可见性/gizmo 等表现聚合——那些归 Gateway 投影域（§2.3 拆解）。
- **"Godot 的渲染物理还算数吗？"** 算。我们复用 RenderingServer/PhysicsServer 全部能力，只是调用入口从 Node 换成 Gateway。
- **"迁移期要双写吗？"** 不。§15.1 已定：不提供 Node-first 过渡路径，用户可见模型一步到位 Object-first。