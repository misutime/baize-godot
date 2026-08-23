<!-- SPDX-License-Identifier: MIT -->
# O4 实施：.bscene / .bprefab 最小格式和实例化计划

> 阶段：O4（§14.10 修订路线：O4 = `.bscene/.bprefab` 最小格式和实例化计划）。
> 本文是 **O4 阶段权威**：格式编码规则引用 `O3-可读格式契约草案.md`（R26，不改动其确定性/校验语义）；
> 本阶段在此基础上定义**文件层扩展**：StableId 双身份、prefab 引用与实例化、override 记录、loader 链路。
> 决策权威：`D:\MisuNotes\3D游戏开发\Godot_ALL_IN_C#\Godot_Fork_GameObject-Components替换Node_源码级落地方案.md`（§6.1/6.2/6.3/§8、§14.10）。
> 实现：`modules/gameobject/`（`GameWorldTextSerializer` @id 扩展 + `BSceneLoader`）；验证：`test-projects/gameobject-core-tests/`。

## 1. 目标与非目标

- **目标**：
  1. `.bscene`（场景）/ `.bprefab`（模板）文件格式的最小可用集；
  2. 场景内 prefab 实例化（`SourceTemplate` 引用 + 复制模板树）；
  3. 实例级 override 记录（对齐 B3：Unity per-instance modifications 借用，不自造格式）；
  4. loader 链路：文件 → 快照 → 世界（复用 O3 `Deserialize`/`Restore`）；
  5. 以上全部保持 O3 确定性契约（hash 相等、幂等、strict 校验）。
- **非目标**：
  - document model 完整实现（未知数据保留、编辑器加载-保存保真）——本阶段只规划，O7 编辑器阶段落地；
  - Resource references 完整体系（属性级 resource() token、依赖表）——规划于 §6；
  - Schema 迁移版本、解析性能限额——沿用 O3 草案 §6.4 指引，本阶段不新增字段；
  - 完整编辑器——O7。

## 2. 与 O3 草案的分工

| 层 | 载体 | 身份 | 引用 |
|---|---|---|---|
| O3 编码层 | `GameWorldSnapshot ↔ 文本` | 无 StableId（可空；Serialize 自动分配临时 uid） | `@uid`（唯一引用形态；无 # 序号） |
| O4 文件层 | `.bscene` / `.bprefab` | **StableId（`@<hex>`，稳定作者身份）** | `@id`（持久，唯一形态） |

- O3 层语义**不动**：DFS 前序校验、头部阶段化、strict token、幂等、hash 口径全部沿用。
- O4 层 = O3 语法 + **对象级 StableId** + **prefab 引用行** + **override 区**。
- StableId **不进入 `ComputeHash`**（运行时 hash 口径不变）；只服务作者文件的稳定引用与 diff。

## 3. StableId 双身份（文件层扩展）

### 3.1 对象行（uid-only；无序号）

```text
object @01a3c5e7 "Player" parent = @00b0b1          # 作者格式：@id 稳定引用
object "匿名对象"                                     # 无 @uid：不可被引用
object @5e "Cube" parent = @01a3c5e7                # 物理顺序即索引（自上而下）
```

- `@<hex16>`：`StableObjectId`（ulong 的 16 位 hex，`Identity.cs` 既有语义）；`@0`/缺省 = 无身份（匿名对象）。
- **文件内 @id 必须唯一**（作者态身份）；重复 → 报错（带行号与重复值）。
- `parent` 引用唯一形态：`parent = @<id>`（按 id 查映射）；**无 # 序号引用**（uid-only，用户裁定）。
  **DFS 前序校验对引用一致**（解析成 ParentIndex 后共用祖先栈校验）。
- `StableId` 写入 `GameObject.StableId`（O1 预留字段）；`StableObjectId` 参与
  **快照记录**（`GameObjectRecord.StableId`，可空 0 = 无），但**不参与 hash**。

- `@<hex16>`：`StableObjectId`（ulong 的 16 位 hex，`Identity.cs` 既有语义）；`@0`/缺省 = 无作者身份（运行时对象）。
- **文件内 @id 必须唯一**（作者态身份）；重复 → 报错（带行号与重复值）。
- `parent` 引用两种形态：`parent = @<id>`（按 id 查映射）或 `parent = #<序号>`（O3 原样）。
  **DFS 前序校验对两种形态一致**（解析成 ParentIndex 后共用祖先栈校验）。
- `StableId` 写入 `GameObject.StableId`（O1 预留字段）；`StableObjectId` 参与
  **快照记录**（`GameObjectRecord.StableId` 新增，可空 0 = 无），但**不参与 hash**。

### 3.2 关系行（@uid 端点）

```text
relation MyGame.TargetRelation @01a3c5e7 -> @00b0b1
```

- 端点解析同 3.1 映射；未注册 @id → 报错。
### 3.3 编辑/合并语义（明确意图）

- 重排对象 = 换行顺序（物理顺序改变），@id 引用**不受影响**（身份稳定）；**没有 # 序号概念**。
- 多人 merge 冲突按 @id 对齐。

## 4. `.bprefab` 与实例化

### 4.1 prefab 文件

- `kind = "prefab"`：单 root 子树模板（root 可为 0..n 子对象）。语法 = 场景语法的子集
  （无跨文件引用、无 override 区；可嵌套其他 prefab——本阶段允许，递归解析）。
- 例 `Enemy.bprefab`：

```text
format = "sola3d.v1"
kind = "prefab"

object @a1 "EnemyRoot"
    [component MyGame.Health]
        Max = 100
        Current = 100
    [component MyGame.EnemyAI]
        Speed = 3.0
object @b2 "Mesh" parent = @a1
    [component MyGame.Health] ...              # 模板内多组件
```

### 4.2 场景内实例化声明

```text
object @f1 "敌人实例" prefab = "res://Enemy.bprefab" parent = @01a3c5e7
```

- `prefab = "<路径>"`（对象级字段，映射 `SourceTemplate`）：loader 解析 → 复制模板树到该位置。
- **实例化语义**（v1 最小）：
  - 模板树**整棵复制**（深拷贝快照记录 + 属性值），模板内 @id 需**重影射为唯一 id**（防与场景内其它对象撞 id）；
  - root 对象：名字/启用状态用场景行覆盖；`StableId` 用场景行声明（模板 root 的 @id 不保留）；
  - 非 root：保留模板相对层级，StableId 由 loader 生成或复用模板（重映射后唯一）；
  - 实例 root 记录 `SourceTemplate = prefab 路径`（运行时只读元数据，序列化时保留）。

### 4.3 override 区（B3 借鉴）

```text
[override]
    @f1 MyGame.Health.Max = 50
    @f1 MyGame.EnemyAI.Speed = 5.0
	@c3 MyGame.Health.Current = 10
```

- 位置：文件尾部（`[override]` 标记行起），每条 = `<对象引用> <组件稳定名> <属性名> = <值 token>`。
	- 引用对象：场景内任意带 @uid 的对象。
	- **override 一律用 @id 定位**（uid-only，O3 草案 §3.3/§6.2）；无 # 序号。
- 未知组件/属性 → 走 R24（Throw/Skip，与 Deserialize/Restore 同链）。

## 5. Loader 链路

### 5.1 类与签名

```csharp
public static class BSceneLoader
{
    // 场景：文本 → 快照（含 prefab 实例化 + override 应用）
    public static GameWorldSnapshot LoadScene(string text, Func<string, string?>? prefabResolver, out List<string>? warnings);
    // 世界：快照 → GameWorld（复用 Restore，R24 容错）
    public static GameWorld LoadSceneToWorld(string text, ComponentSchemaRegistry schemas, RelationGraph? relations,
        Func<string, string?>? prefabResolver, RestoreOptions? options = null);
    // prefab 模板：文本 → 深拷贝模板快照
    public static GameWorldSnapshot ParsePrefab(string text);
}
```

- `prefabResolver`：路径 → prefab 文本（测试注入内存字典；O5 文件系统接入时换成资源加载器）。
  返回 null / 解析失败 → 报错（带 prefab 路径上下文）。
- 解析顺序：`Deserialize`（场景语法）→ 展开 prefab 实例（含 override）→ 构造完整快照 → `Restore`。
- 展开期间确定性与 O3 一致：DFS 前序 + 序号连续由 Deserialize/展开共同保证。

### 5.2 document model（规划，O7 落地）

- O4 最小实现只做「文件 → 运行时快照 → 世界」单向投影；
- document model（未知组件/属性原文保留、源顺序、编辑器保存保真）按 O3 草案 §5.2/§6.3
  规划，O7 编辑器阶段实现 `AuthoringDocument ↔ 快照` 双向层。

## 6. 本阶段明确不做（O4 边界）

| 项 | 归属 | 说明 |
|---|---|---|
| Resource references 属性 token | O5/O6 | `resource()`/`prefab()` 值语法 + 依赖表，需资源系统 |
| document model 实现 | O7 | 编辑器加载-保存保真 |
| Schema 迁移版本 | O4 后 | type alias + revision + migrator，O3 §6.4 指引 |
| 文件系统/资源加载器 | O5 | `prefabResolver` 留接口，测试内存注入 |
| `.bprefab` 嵌套重启实例化 | 本阶段最小 | 允许解析嵌套但 v1 实例化展开递归可简化为非嵌套场景测试先行 |

## 7. 验证清单（O4 验收）

- [x]（目标）`.bscene` 完整 round-trip（含 @id/混合引用/禁用/层级/关系）hash 相等；
- [x] prefab 实例化：场景含实例对象 → 展开后对象树/组件/属性正确、@id 无冲突；
- [x] override 应用：实例组件属性被覆盖（模板值 → 场景 override 值）；
- [x] @id 唯一性校验、未知 @ref / 越界报错（带上下文）；
- [x] 未知组件的 override → R24 Throw/Skip 生效；
- [x] `GameObject.StableId` 经 Restore 写回、Capture 再导出（round-trip 保真）；
- [x] 全部既有 O1–O3 测试不回归（223 项基线）。