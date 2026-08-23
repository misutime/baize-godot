<!-- SPDX-License-Identifier: MIT -->
# O1 GameObject 语义契约 —— 纯 GameWorld 内核的实现规范

> 方针文档：`D:\MisuNotes\3D游戏开发\Godot_ALL_IN_C#\Godot_Fork_GameObject-Components替换Node_源码级落地方案.md`（§4/§14）
> 阶段：O1（Headless GameObject Kernel，修订路线 §14.10：**GameObject 语义契约 + ObjectId/RuntimeHandle + 纯 GameWorld**）
> 日期：2026-08-22
> 约束：纯 .NET（net11）、不引用 Friflo/Sola3d.Ecs/Godot、不接触 Node、headless 可测、确定性。
> 本文档回答 §14.8 的 10 个语义问题；**先定契约，再写代码**（§14.8：契约未定不前移）。

---

## 0. 分层与命名（§14.5 / §4.2）

```text
GameWorld                 // 纯运行时世界，可测试、可服务器复用（本阶段实现）
Sola3dMainLoop : MainLoop  // Godot 进程宿主（O5）
GameWorldNodeHost : Node  // 迁移期 SceneTree 宿主（O5，桥接）
EditorPreviewHost         // 编辑器预览宿主（O7）
```

- C# 类型名：`GameObject` / `GameWorld` / `GameComponent`；命名空间 `Sola3d.GameObject`；程序集 `Sola3d.GameObject`（模块 `modules/gameobject/`）。
- 身份分层（§14.7）：`Uid`（.bscene/.bprefab 内稳定作者 ID，O4 用）与 `ObjectId`（运行时身份 = Index + Generation，防 ID 复用）。`RuntimeGameObjectHandle` 即 `ObjectId`。

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
- **ObjectId 永不复活**：Index 槽位可复用但 Generation 递增，旧 `ObjectId` 永不等同于新对象（§14.7 防"旧引用指到新对象"）。

## 7. Parent/Children 承担什么

- **承担**：场景组织、所有权、生命周期归属（销毁级联）、遍历。
- **不承担**：空间继承（Transform 由 `TransformComponent` + TransformGateway 承担，O6；层级内核不存位置）。
- 支持 `SetParent(obj, newParent)`（可为 null → 顶层）；**禁止环**（沿父链检测，成环 → `InvalidOperationException`）。
- 销毁父对象 → 整棵子树（children 深度优先）全部销毁。

## 8. Prefab Override（O4 详定，本契约只记原则）

- 运行时 `GameObject` 预留 `SourceTemplate`（prefab 引用）+ 实例元数据字段；O1 只留字段不实现解析。
- Override 记录形式：`(ObjectId, ComponentType, PropertyPath, Value)`，与静态实例分离存储（§1.2 静态/运行时分离）。
- 详细记录方式在 O4（`.bscene/.bprefab` 格式）定义，本契约不提前承诺。

## 9. Relation 的序列化和清理

- `GameRelation` 是一等数据：`Source`/`Target` 均存 `ObjectId`（非裸引用，随身份安全）。
- `RelationGraph` 维护双向索引（outgoing/incoming）；`GameObject.Relations.Get<T>()` 便捷门面。
- **任一端点销毁 → 自动移除该对象全部进出关系**（同步，随 Destroy 一起）。
- Relation 数据进入 `GameWorldSnapshot`（见 §10），序列化/反序列化 round-trip 保真。

## 10. 确定性序列化与 Gateway Observation

- **确定性序列化**：`GameWorldSerializer` 导出 `GameWorldSnapshot`（对象记录：Id/Name/Parent/Enabled/组件序 + 组件属性 + Relations），round-trip 可重建（Restore 后 hash 相等）。
- **确定性 hash**：FNV-1a 64 风格，遍历顺序敏感（对象序/组件序/属性序全部稳定）——与 shooter-poc 的确定性验证口径一致。
- **Gateway Observation**（O5/O6 详定）：Gateway 只能通过显式 Port（Event/Command/Observation）回传；GameWorld 在 fixed tick 边界收集；Gateway 永不隐式修改 Gameplay 状态（§14.6 权威矩阵）。O1 无 Gateway，仅预留 `service` 端口。
- 支持属性类型（O1 序列化白名单）：`int/float/double/bool/string` 及可空同族、`enum`（按底层值）、其他类型报错（防止隐性不确定序列化）。**R27（O6 扩展）**：白名单增加 `System.Numerics.Vector3`（x/y/z，各 float）与 `Quaternion`（x/y/z/w）——分量序固定、逐分量 Float token 编码，确定性往返。数字类型清单见 `PropertySchema.IsWhitelisted`。

## 11. Resources（§4.6 / §14.6 端口预留）

- `world.AddResource<T>(T)` / `world.GetResource<T>()`：资源单例容器（输入帧、配置、后端端口等在 O2+ 挂入）。
- Resource 不属于任何 GameObject；销毁对象不影响 Resource。

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
| R20 | **社区借鉴决策**（对照 `doc/plans/object-components/社区对象模型对照与借鉴.md`）：B1 Required Components（O2 前）/*B2 DataContract 序列化对齐（O3）/*B3 prefab override 照 Unity（O4）/ 默认不借鉴 DOTS Archetype 存储与 EnTT 池布局（有意决策，非缺失） | 社区调研（Unity/Flecs/Bevy/EnTT/Stride/Wave/Defold） |
| R21 | **世界重置与同帧追踪**（O2 需求，O1 受控扩展）：`GameWorld.Reset()`（清对象 + TickIndex/FixedTickIndex 归零 + 事务栈/句柄映射清零，**事务逻辑 ID 世界生命周期内单调不复用**）；`GameObject.CreatedAtTickIndex`（创建时世界 Tick，O2 回滚本帧创建用）+ 保留 `Uid`（O1 契约） | O2 实施 + reviewer P1（第三轮） |

## 14. O3 补充：Schema 驱动元数据层与校验整合（2026-08-23，实施中）

O3（修订路线 §14.10）= **GameObject C# bindings + Component Schema**。本节是 O1 契约在 O3 的扩展——Schema 是**序列化与编辑器 Inspector 的同一事实源**（采纳决策 B2：WaveEngine DataContract 模式）。下列条目在 `ComponentSchema.cs` / `GameWorldSerializer.cs` 落地，并由 `gameobject-core-tests` 断言：

| # | 决策 |
|---|---|
| R22 | **Schema 驱动属性访问**：`PropertySchema` 注册时**编译 get/set 委托**（表达式树，一次性开销）；`Capture/Restore/编辑器` 一律走 `PropertySchema.GetValue/SetValue`，**禁止散落 `PropertyInfo.GetValue/SetValue`**。标记 `[GameProperty]` 的属性必须同时有可读可写访问器，否则注册报错（防作者误用）。 |
| R23 | **Inspector 元数据与序列化共用同一 Schema（B2）**：`[GameComponent(DisplayName=, Group=)]`、`[GameProperty(DisplayName=, Group=, ReadOnly=, DefaultValue=)]`；缺省显示名 = 属性名/类名。`ComponentSchema` 暴露 `DisplayName/Group`，`PropertySchema` 暴露 `DisplayName/Group/IsReadOnly/DefaultValue`——编辑器 O7 直接读 Schema，不再另立一份元数据。 |
| R24 | **未知组件/未知属性容错策略**：`Restore` 支持 `RestoreOptions`（`UnknownComponentPolicy` / `UnknownPropertyPolicy` = `Throw`（默认，保持现状）\| `Skip`）。`Skip` = 丢弃该条记录继续恢复，用于格式演进/插件缺装时宽容加载；`Throw` 的**报错必须带上下文**：对象名 + 快照索引 + 组件类型名 + 属性名，杜绝「只说未注册」的盲错。 |
| R25 | **Schema 实例化收拢**：`ComponentSchema.CreateInstance()`（无参构造，Restore 与编辑器共用）；`ComponentSchemaRegistry.CreateInstance(typeName)` 按稳定名创建，未注册时报错并列出已注册类型数——杜绝各调用点自行 `Activator.CreateInstance`。 |
| R26 | **可读文本格式契约草案（O3 工件 2，供 O4 `.bscene/.bprefab` 落地引用）**：`GameWorldTextSerializer` 做 `GameWorldSnapshot ↔ 文本` 双向编码，`Capture→Serialize→Deserialize→Restore→Capture` 后 hash 相等、同快照 Serialize 两次字节相等、Serialize→Deserialize→Serialize 幂等（可 diff、Git 友好）。格式头强制 `format = "sola3d.v1"` + `kind = "scene"`（方案 §6.3；评审修订：首条有效行 format、次条 kind、各一次、正文前，任一违反报错）。**形态 = 平铺 + @uid 引用（uid-only，用户裁定）**（选型见 O3 草案 §2.1）：对象行 `object [@<uid>] "<名字>"`（**无序号字段**，物理顺序即索引）、`parent = @<父uid>`、`[component]` 块头切块、关系 `relation <稳定名> @<源> -> @<目标>`；**对象名仅作展示标签可重复、不参与引用**（任意合法快照含重名对象都可序列化；无 @uid 快照序列化时按出现序自动分配临时 uid，跳过已用值）；**DFS 前序严格校验**（祖先栈）+ Serialize 前置 ValidateSnapshot（非法 ParentIndex 抛错）+ 引用未注册/自引用校验。**类型键**沿用 R7 稳定全限定名；**属性行** `名 = 值`，值编码 = **token 系统**：`null`→Null / `true|false`→Bool / 整数→Int / 含小数点或指数→Float / 带引号严格转义（未知转义/悬空反斜杠/未转义内部引号 → 语法错误）→String / 裸词→Bare（enum 名，冲突时输出底层数值）；**非有限浮点 NaN/±Infinity → Serialize 拒绝**；Restore 按 **strict 转换矩阵**（int 只收 Int、string 只收 String、bool 只收 Bool、enum 收 Bare/数值/String、float/double 收 Int/Float 超范围报错、null 只允许可空/引用），错误带对象+组件+属性+目标类型+原字面量上下文；重复属性名 → Deserialize 报错；关系行/对象行/头部转移重置组件上下文（属性不得归属旧组件）；未知关系类型恒为错误（R24 Skip 不适用关系）。O4 落地指引（Uid 稳定身份/document model 未知数据保留/资源引用/迁移版本/性能限额）见草案 §6。 |

设计约束沿用 §3/§4/§6：Schema 不变更行为契约；新增条目不得破坏 R1–R21 既有断言（默认策略保持 Throw 即与旧行为等价）。
