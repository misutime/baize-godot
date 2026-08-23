<!-- SPDX-License-Identifier: MIT -->
# O1 GameObject 语义契约 —— 纯 GameWorld 内核的实现规范

> 方针文档：`D:\MisuNotes\3D游戏开发\Godot_ALL_IN_C#\Godot_Fork_GameObject-Components替换Node_源码级落地方案.md`（§4/§14）
> 阶段：O1（Headless GameObject Kernel，修订路线 §14.10：**GameObject 语义契约 + EntityId/RuntimeHandle + 纯 GameWorld**）
> 日期：2026-08-22
> 约束：纯 .NET（net11）、不引用 Friflo/Baize.Ecs/Godot、不接触 Node、headless 可测、确定性。
> 本文档回答 §14.8 的 10 个语义问题；**先定契约，再写代码**（§14.8：契约未定不前移）。

---

## 0. 分层与命名（§14.5 / §4.2）

```text
GameWorld                 // 纯运行时世界，可测试、可服务器复用（本阶段实现）
BaizeMainLoop : MainLoop  // Godot 进程宿主（O5）
GameWorldNodeHost : Node  // 迁移期 SceneTree 宿主（O5，桥接）
EditorPreviewHost         // 编辑器预览宿主（O7）
```

- C# 类型名：`GameObject` / `GameWorld` / `GameComponent`；命名空间 `Baize.GameObject`；程序集 `Baize.GameObject`（模块 `modules/gameobject/`）。
- 身份分层（§14.7）：`AuthoringObjectId`（.bscene/.bprefab 内稳定作者 ID，O4 用）与 `EntityId`（运行时身份 = Index + Generation，防 ID 复用）。`RuntimeGameObjectHandle` 即 `EntityId`。

## 1. Component 是否允许重复

**默认单实例**：同一类型每 GameObject 最多一个。
- `[GameComponent(AllowMultiple = true)]` 显式开启多实例。
- 单实例语义：`AddComponent<T>()` 若已存在 → `InvalidOperationException`（显式报错，不静默覆盖）；`GetComponent<T>()` 取唯一实例；`RemoveComponent<T>()` 移除唯一实例。
- 多实例语义：`AddComponent<T>()` 总是新增；`GetComponent<T>()` 取第一个；`GetComponents<T>()` 取全部（插入序）；`RemoveComponent<T>()` 移除第一个。

## 2. Component 依赖和冲突

- `[GameComponent(Requires = typeof(...), ...)]` 声明**必需依赖**：添加组件时校验，缺依赖 → `InvalidOperationException`（报出缺哪个类型）。
- O1 不做互斥/冲突检测（O3 Schema 完善后做）；契约预留 `ExclusiveGroup` 概念。
- 依赖单向足够 O1；环依赖在添加时即报错（A 需要 B 且 B 需要 A → 先加谁都失败，提示顺序）。

## 3. GameObject enabled 与 Component enabled 的传播

- `GameObject.Enabled` 与 `GameComponent.Enabled` 是**两个独立标志**。
- 有效状态 = `IsEffectivelyEnabled(obj)`：`obj.Enabled && (父链全 Enabled) && !world.Paused`。
- 组件只在其 owner 有效且自身 `Enabled` 时才参与 tick。
- 父对象禁用 → 子对象 `Enabled` 标志**不变**，但整棵子树**有效禁用**（effective 计算沿父链）。
- `OnEnable/OnDisable` 只在**有效状态翻转**时调用一次（禁止每帧重复触发）。

## 4. 生命周期顺序（确定性）

```
AddComponent（对象已在世界中）
  → OnCreate()      立即（不管 enabled——代表"已注册"）
  → OnEnable()      仅当 effective enabled（对象有效且组件 Enabled）
  → OnStart()       第一次有效 tick 前调用一次（在 OnEnable 之后，本帧 OnTick 之前）
  → OnTick(delta)   每 variable tick（有效组件，稳定遍历序）
  → OnFixedTick(delta) 每 fixed tick
Disable / 父链禁用 / Pause
  → OnDisable()     有效状态变为 false 时（仅翻转时）
移除组件 / 销毁对象
  → OnDestroy()     组件移除或 owner 销毁时（同步）
```

- **遍历序确定性**：所有已注册组件按「对象创建序 → 组件插入序」稳定排序；tick 遍历基于**快照**（tick 期间的结构变更不影响本轮遍历）。
- `OnStart` 的"一次"以有效 tick 的第一次为准；若组件在 Start 前被禁用，恢复有效后仍以第一次有效 tick 为准。
- `delta`：`OnTick` 用 variable delta（秒）；`OnFixedTick` 用 `world.FixedDelta`（固定步长）。

## 5. Tick 中添加/删除 Component 的生效时机

- **外部（tick 之外）调用：同步立即生效**——AddComponent 立即可见（GetComponent 立即可查到）、立即 OnCreate/OnEnable；RemoveComponent 立即移除 + OnDestroy。
- **tick 期间调用：同样立即生效，但本轮遍历不受影响**（快照遍历防迭代器失效）；新加组件的 OnStart/OnTick 从**下一轮**开始（本轮已经遍历过它的位置）。
- **销毁走删除队列语义（O1 简化版）**：`Destroy(obj)` 同步置 `IsDestroyed`、句柄失效（Generation++）、从 registry/层级/关系图同步移除、级联销毁子树并立即回调 OnDestroy。**O1 采用同步销毁**（保证 headless 测试确定性）；延迟删除队列与调度器一起在 O5 引入。

## 6. Destroy 后 C# handle 行为

- `GameObject` 是托管引用：`Destroy` 后 `obj.IsDestroyed == true`、`world.IsAlive(id) == false`、`world.GetObject(id) == null`。
- **读操作安全**：`GetComponent<T>()` 返回 `null`；`Enabled` 读取返回 false；不抛异常。
- **结构操作拒绝**：对已销毁对象调用 `AddComponent/RemoveComponent/SetParent/Destroy` → `InvalidOperationException`（防静默误用）。
- **EntityId 永不复活**：Index 槽位可复用但 Generation 递增，旧 `EntityId` 永不等同于新对象（§14.7 防"旧引用指到新对象"）。

## 7. Parent/Children 承担什么

- **承担**：场景组织、所有权、生命周期归属（销毁级联）、遍历。
- **不承担**：空间继承（Transform 由 `TransformComponent` + TransformBackend 承担，O6；层级内核不存位置）。
- 支持 `SetParent(obj, newParent)`（可为 null → 顶层）；**禁止环**（沿父链检测，成环 → `InvalidOperationException`）。
- 销毁父对象 → 整棵子树（children 深度优先）全部销毁。

## 8. Prefab Override（O4 详定，本契约只记原则）

- 运行时 `GameObject` 预留 `SourceTemplate`（prefab 引用）+ 实例元数据字段；O1 只留字段不实现解析。
- Override 记录形式：`(ObjectId, ComponentType, PropertyPath, Value)`，与静态实例分离存储（§1.2 静态/运行时分离）。
- 详细记录方式在 O4（`.bscene/.bprefab` 格式）定义，本契约不提前承诺。

## 9. Relation 的序列化和清理

- `GameRelation` 是一等数据：`Source`/`Target` 均存 `EntityId`（非裸引用，随身份安全）。
- `RelationGraph` 维护双向索引（outgoing/incoming）；`GameObject.Relations.Get<T>()` 便捷门面。
- **任一端点销毁 → 自动移除该对象全部进出关系**（同步，随 Destroy 一起）。
- Relation 数据进入 `GameWorldSnapshot`（见 §10），序列化/反序列化 round-trip 保真。

## 10. 确定性序列化与 Backend Observation

- **确定性序列化**：`GameWorldSerializer` 导出 `GameWorldSnapshot`（对象记录：Id/Name/Parent/Enabled/组件序 + 组件属性 + Relations），round-trip 可重建（Restore 后 hash 相等）。
- **确定性 hash**：FNV-1a 64 风格，遍历顺序敏感（对象序/组件序/属性序全部稳定）——与 shooter-poc 的确定性验证口径一致。
- **Backend Observation**（O5/O6 详定）：Backend 只能通过显式 Port（Event/Command/Observation）回传；GameWorld 在 fixed tick 边界收集；Backend 永不隐式修改 Gameplay 状态（§14.6 权威矩阵）。O1 无 Backend，仅预留 `service` 端口。
- 支持属性类型（O1 序列化白名单）：`int/float/double/bool/string` 及可空同族、`enum`（按底层值）、其他类型报错（防止隐性不确定序列化）。

## 11. Services（§4.6 / §14.6 端口预留）

- `world.AddService<T>(T)` / `world.GetService<T>()`：服务单例容器（输入帧、配置、后端端口等在 O2+ 挂入）。
- Service 不属于任何 GameObject；销毁对象不影响 Service。

## 12. 本契约生效范围与验证

- 本契约的所有条目由 `test-projects/gameobject-core-tests/`（headless 纯 .NET 控制台）逐条断言验证。
- 变更契约必须先改本文档再改代码（文档是权威）。

## 13. reviewer 审查后补充决策（2026-08-22，已实现）

| # | 决策 | 来源 |
|---|---|---|
| R1 | 同一组件实例**禁止重复挂载**（Owner 非空即拒绝）；Schema 一律取运行时类型 | reviewer P1 |
| R2 | `RemoveComponent(obj, component)` 先按引用验证归属（Owner==obj 且 Store.Contains），不满足返回 false、不触发生命周期 | reviewer P1 |
| R3 | `Destroy` 两阶段：先整棵子树句柄失效（registry/Generation/层级/关系/enabled），再基于**组件快照**执行 OnDisable/OnDestroy——回调期间任何重入/结构操作被契约 §6 拒绝；已销毁再 Destroy 抛异常 | reviewer P1 |
| R4 | tick 顺序 = **对象创建序 → 组件插入序**（CreationIndex + Revision 有序插入），与调用时序无关 | reviewer P1 |
| R5 | `FixedTick` 与 `Tick` **共用 OnStart 门禁**；fixed 回调固定传 `world.FixedDelta`（忽略入参 delta） | reviewer P1 |
| R6 | 所有结构操作（Add/Remove/SetParent/Destroy/SetEnabled）走 `EnsureOwnedAndAlive`——**拒绝跨世界对象**；**读路径（GetComponent/GetComponents/GetComponentList）同样做归属防护**（跨世界返回 null/空，不污染本世界 Store）；`RelationGraph` 绑定所属 GameWorld，拒跨世界端点 | reviewer P1 |
| R7 | 序列化类型键统一为**稳定全限定名**（组件 `FullName`、关系 `StableTypeKey`），注册表写入前冲突校验（原子，防半注册） | reviewer P1 |
| R8 | `CreateObject` 事务步骤持**可更新句柄**：Redo 新建对象替换引用，未完成前再次 Undo 销毁当前存活实例（不泄漏） | reviewer P1 |
| R9 | hash 规范化：null/字符串显式区分（类型标签 + 长度前缀）；枚举按底层类型输出**完整位模式**（`X16`，防 ulong 溢出） | reviewer P2 |
| R10 | GameWorld **单线程亲和**（Tick/Paused/结构操作无锁）；销毁逐组件 `_tickOrder.Remove` 为 O(C²)，大世界后续改索引/墓碑 | reviewer P3 |
| R11 | 事务内置对象句柄重映射（`_createdHandles` + `RequireResolved/TryResolved`）：`CreateGameObject` 与同事务后续步骤在 Redo 后操作**当前存活实例**；Create+编辑组合事务可完整 Undo/Redo 闭环 | reviewer P1（第二轮） |
| R12 | `Destroy` 阶段 2 逐组件 **try/finally** 清理：回调异常不中断 Detach/移除 tick 表/_stores 清理，全部完成统一抛 `AggregateException`——不残留半清理组件 | reviewer P1（第二轮） |
| R13 | tick 快照记录 **(组件, Revision)**：同一轮内被移除又重挂的组件（Revision 已变）本轮跳过，符合「tick 内 Add 从下一轮开始」 | reviewer P1（第二轮） |
| R14 | 依赖校验用 `Store.ContainsType`（同时查单/多实例容器）——Requires 支持多实例组件类型 | reviewer P1（第二轮） |
| R15 | Relation `GetFrom/GetTo` 跨世界对象返回**空数组**（读安全，不命中本地同 ID 关系） | reviewer P1（第二轮） |
| R16 | hash 对象名/组件名/关系名统一**长度前缀**编码，杜绝任意字符串拼接歧义 | reviewer P2（第二轮） |
| R17 | 事务句柄提升到 **GameWorld 级**：`GameObject.TransactionId`（稳定逻辑句柄）+ `_transactionObjects` 世界映射——**跨事务** Undo/Redo 链（tx1 建、tx2 编辑，双撤销/双重做）完整解析重建实例 | reviewer P1（第三轮） |
| R18 | `Destroy` 阶段 2 **OnDisable/OnDestroy 分离捕获**：OnDisable 异常不吞同组件的 OnDestroy，两者均被尝试并聚合 | reviewer P1（第三轮） |
| R19 | `Tick/FixedTick` 在 `EnsureStarted(OnStart)` **之后重新验证 Revision + IsTickable**：OnStart 内禁用/销毁/重挂自身则不再回调 | reviewer P1（第三轮） |
