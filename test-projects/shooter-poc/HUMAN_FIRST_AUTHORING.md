<!-- SPDX-License-Identifier: MIT -->

# Human-first Authoring：从玩法句子到 ECS 代码

这份 PoC 是**教学样本**，不是性能样板。目标不是让开发者先记住 Friflo API，而是让人先看见游戏概念，再把概念直接写成数据与规则。

一句总纲：

> **Object + Components ≈ ECS。实体不是类，组件不是某种实体的字段表；实体只是若干事实在同一个 Id 上的组合。**

本项目沿用既定命名：

- `EcsWorld` 保存一局 ECS 世界并执行固定 Tick；
- 将来由 `EcsHost` 负责引擎生命周期、输入与一个或多个 `EcsWorld` 的驱动；
- Friflo 是底层存储与查询实现，不在本示例中修改；
- `Resource`、`Bundle`、`EventWriter/EventReader` 的作者体验借鉴 Bevy，但保持 C# 语义。

---

## 1. 开发者先看什么

不要从所有组件或所有系统开始读。按以下顺序读：

1. `Gameplay/ShooterGame.cs`：这一局装了什么全局事实、初始对象和玩法功能；
2. `Gameplay/Actors/PlayerBundle.cs`：玩家出生时有哪些事实；
3. `Gameplay/ShooterFeature.cs`：规则按什么因果顺序运行；
4. 再进入某个功能目录，例如 `Gameplay/Combat/` 看局部数据变换；
5. 最后才看 `Tests/ShooterPocTests.cs` 的测试安排和底层状态哈希。

唯一游戏装配入口是：

```csharp
var world = new EcsWorld(aot => EcsAotRegistration.RegisterAll(aot));
ShooterGame.Install(world);
```

`ShooterGame.Install(world)` 展开后仍只有三类作者动作：

```csharp
world.InsertResource(...);        // 放入“这一局只有一份”的事实
world.SpawnNow(PlayerBundle.Default); // 按配方生成初始对象
world.AddFeature(new ShooterFeature()); // 启用会持续运行的规则
```

这就是 Human-first Authoring 的入口：**先读“世界里有什么”和“世界启用了什么”，再读底层查询怎么写。**

---

## 2. ECS 六个概念分别是什么

### 2.1 Entity：事实组合的 Id

Entity 不是 `Player`、`Enemy` 这样的继承对象，也不应该承载大段行为。

在 Shooter 中：

- 玩家实体 = `Position + Velocity + PlayerInput + MoveSpeed + WeaponConfig + Cooldown + PlayerFaction + ...`；
- 敌人实体 = `Position + Velocity + SeekTarget + MoveSpeed + Health + EnemyFaction + ...`；
- 投射物实体 = `Position + Velocity + ProjectileConfig + TravelDistance + ProjectileTag + ...`。

“玩家/敌人/投射物”是人对组件组合的称呼，不是必须存在的实体基类。

**什么时候用 Entity？**

当一个东西需要独立存在、被查询、被销毁，或被稳定句柄引用时，就给它一个 Entity。

### 2.2 Component：关于某个实体的一条事实

Component 是挂在实体上的小块数据。系统通过组件组合找到要处理的实体。

Shooter 示例：

- `Position`：这个实体现在在哪里；
- `PlayerInput`：这个实体能接收玩家输入；
- `MoveSpeed`：这个实体自己的移动速度参数；
- `EnemyFaction`：这个实体属于敌方阵营。

**什么时候用 Component？**

当这份数据属于每个实体，而且系统需要按“具有这些事实的所有实体”批量查询时使用。

错误问法：“敌人类还缺什么字段？”

正确问法：“哪条规则需要哪一条独立事实？”

### 2.3 Resource：整个世界只有一份的事实

Resource 不挂在虚构的“全局实体”上，由 `EcsWorld` 持有。

Shooter 示例：

- `MatchState`：本局阶段、分数、存活敌人数；
- `SpawnConfig`：全局生成间隔、上限、生成半径；
- `SpawnState`：当前生成倒计时；
- `FireInputState`：上一 Tick 是否按住开火键。

**什么时候用 Resource？**

当判断句是“这一局只有一份”，而不是“每个实体各有一份”时使用。

注意：配置和状态都可以是 Resource，但必须用不同类型表达。`SpawnConfig` 不能再混入玩家位置或倒计时。

### 2.4 Event：已经发生、等待其他规则处理的瞬时事实

Event 不是长期状态，也不是直接回调。发送方只声明“发生了什么”，接收方决定后果。

Shooter 示例：

- `DamageRequested`：某投射物请求伤害某目标；
- `GameOverRequested`：敌人与玩家发生接触，请求结束本局。

**什么时候用 Event？**

当事实是瞬时的、可能有零到多个接收者，而且发送者不应直接修改接收者拥有的数据时使用。

本框架的事件是 Tick 双缓冲：本 Tick 写入，下一 Tick 读取。这个因果延迟是明确契约，不是偶然实现。

### 2.5 Bundle：实体出生时的组件配方

Bundle 不代表一种新的运行时对象类型。它只是把“创建时通常一起出现的组件”集中到一个可读配方里。

Shooter 示例：

- `PlayerBundle`；
- `EnemyBundle`；
- `ProjectileBundle`。

**什么时候用 Bundle？**

当同一组创建代码开始重复，或 Composition Root 被 `.Add(...).Add(...)` 长链淹没时使用。

Bundle 允许重复：玩家和敌人都可以添加 `Position`、`Velocity`、`CollisionRadius`。这种重复恰好说明组件是可组合事实，不是实体类型字段表。

### 2.6 System：把一组数据变成另一组数据的规则

System 不拥有玩家、敌人或分数。它查询需要的事实，读取输入/资源/事件，然后改写明确的数据。

Shooter 示例：

- `ApplyPlayerInputSystem`：`InputFrame + MoveSpeed → Velocity`；
- `SeekPlayerSystem`：玩家 `Position + MoveSpeed →` 敌人 `Velocity`；
- `MoveSystem`：`Position + Velocity + delta → Position`；
- `ResolveDamageSystem`：`DamageRequested + Health → Health/删除/Score`。

**什么时候用 System？**

当你能把规则写成“读取 A，更新 B”，并且它需要每 Tick 或某个阶段批量运行时使用。

System 可以保留临时工作集合，例如同 Tick 去重用的 `HashSet`；但会影响玩法、回放或重置的长期状态，应优先放进 Component 或 Resource。

---

## 3. 组件四分类：能力、状态、参数、标签关系

四类可以共存于同一个实体。分类的目的不是建立四个继承体系，而是阻止一个组件同时回答多个问题。

| 类别 | 判断句 | Shooter 示例 | 典型变化频率 |
|---|---|---|---|
| 能力特征 | “它**能不能/会不会**做这件事？” | `PlayerInput`、`SeekTarget` | 很少变化 |
| 运行状态 | “它**现在**是什么值？” | `Position`、`Velocity`、`Health`、`Cooldown`、`TravelDistance` | 经常变化 |
| 每实体参数 | “这个实例的**规则数值**是多少？” | `MoveSpeed`、`WeaponConfig`、`ProjectileConfig`、`CollisionRadius` | 通常由作者设置 |
| 标签关系 | “它**属于谁/扮演什么关系**？” | `PlayerFaction`、`EnemyFaction`、`ProjectileTag` | 很少变化 |

### 3.1 能力特征

判断句：

> 如果删掉数值后，“有/没有这项能力”仍然有意义，它适合成为能力组件。

`SeekTarget` 只表示会寻敌。速度不再塞进 `EnemyAI.Speed`，而由 `MoveSpeed` 单独表达。以后可以让友方无人机也拥有 `SeekTarget`，而不把它伪装成 Enemy。

### 3.2 运行状态

判断句：

> 如果保存游戏、回放或状态哈希必须记录“它现在到哪一步”，它是运行状态。

所以武器倒计时是 `Cooldown.Remaining`，不是 `WeaponConfig` 的一部分；投射物已飞距离是 `TravelDistance`，不是射程参数。

### 3.3 每实体参数

判断句：

> 如果设计者会问“这个实例应该设成多少”，但它不会自然地每 Tick 累加/递减，它是参数。

`WeaponConfig.CooldownSeconds` 是参数，`Cooldown.Remaining` 是状态。两者名字故意重叠“冷却”概念，但类型绝不混用，以消除二义。

### 3.4 标签关系

判断句：

> 如果只需要回答分类、阵营或参与关系，而没有连续数值，就用 Tag。

`EnemyFaction` 只表达敌方关系；“会追踪目标”由 `SeekTarget` 表达，“跑多快”由 `MoveSpeed` 表达。三条事实可以独立替换。

### 3.5 拆分前后对照

| 拆分前 | 问题 | 拆分后 |
|---|---|---|
| `EnemyAI.Speed` + `EnemyTag` | 身份、策略、速度混在一起 | `EnemyFaction` + `SeekTarget` + `MoveSpeed` |
| `Weapon.Cooldown/BulletSpeed/Timer` | 参数与状态混用 | `WeaponConfig` + `Cooldown` |
| `SpawnConfig.Interval/PlayerX/PlayerZ` | 配置缓存运行状态 | 纯 `SpawnConfig`；AI 直接查询玩家 `Position` |
| `Bullet.Damage/Range/Travelled` + `BulletTag` | 参数、状态、类别重复 | `ProjectileConfig` + `TravelDistance` + `ProjectileTag` |
| `PlayerControl.MoveSpeed` + `PlayerTag` | 控制能力、参数、身份重复 | `PlayerInput` + `MoveSpeed` + `PlayerFaction` |

---

## 4. 做一个游戏怎么下手：六步心智模型

下面六步按顺序做。不要先创建 `Components.cs`、`Systems.cs` 两个大文件。

### 第一步：写玩法闭环

先用自然语言写输入到反馈：

> 玩家移动并按下开火键 → 生成投射物 → 投射物移动并命中敌人 → 敌人失去生命并死亡 → 分数增加；敌人持续生成并追向玩家 → 接触玩家后本局结束。

如果一句话还不能读通，不要急着写 ECS。

### 第二步：列事实

只列名词和可观察事实，不分类型：

- 玩家位置、移动速度、输入能力、武器间隔、武器剩余冷却；
- 敌人位置、寻敌能力、移动速度、生命、阵营；
- 投射物位置、速度、伤害、最大射程、已飞距离；
- 本局阶段、分数、敌人数、生成间隔、生成剩余时间；
- 伤害发生、游戏结束请求。

### 第三步：分类

逐条问：

1. 每实体还是全世界一份？决定 Component 或 Resource；
2. 长期事实还是瞬时发生？决定 State 或 Event；
3. 对每实体事实再问：能力、运行状态、参数、标签关系中的哪一种？

例如：

- “敌人速度”不是 EnemyAI，而是每实体参数 `MoveSpeed`；
- “敌人追玩家”不是身份，而是能力 `SeekTarget`；
- “玩家现在位置”不是 `SpawnConfig.PlayerX/Z`，而是玩家实体的 `Position`。

### 第四步：写数据变换

每个 System 先写成公式，再写 C#：

```text
InputFrame + PlayerInput + MoveSpeed -> Velocity
SeekTarget + 玩家 Position + MoveSpeed -> Velocity
Position + Velocity + delta -> Position
按下边沿 + WeaponConfig + Cooldown -> ProjectileBundle
DamageRequested + Health -> Health / 删除实体 / MatchState.Score
```

如果公式里出现“某 Manager 里面那个字段”，说明事实还没有归位。

### 第五步：排因果

把变换放入 `Phase`：

```text
Input
  -> Spawn
  -> Simulation
  -> Collision
  -> Resolve
  -> Cleanup
  -> RenderExtract
```

Shooter 的明确顺序：

1. `ApplyPlayerInputSystem`；
2. 开火和敌人生成；
3. 寻敌，再统一移动；
4. 投射物 swept 命中和敌人接触；
5. 先结束对局，再结算伤害；
6. 清理超射程投射物。

顺序不是性能细节，而是游戏语义。`ShooterFeature` 集中展示这条因果链。

### 第六步：提取 Bundle 与 Composition Root

当创建一个对象需要重复添加组件时，提取 Bundle；当世界启动代码分散时，建立唯一 Composition Root。

最终作者应看到：

```csharp
public static void Install(EcsWorld world)
{
    world
        .InsertResource(new MatchState())
        .InsertResource(new SpawnConfig())
        .InsertResource(new SpawnState())
        .InsertResource(new FireInputState());

    world.SpawnNow(PlayerBundle.Default);
    world.AddFeature(new ShooterFeature());
}
```

这段代码先表达游戏概念，底层 `EntityStore` 与查询细节退到各系统内部。

---

## 5. 文件为什么按功能组织

当前结构：

```text
shooter-poc/
├─ Gameplay/
│  ├─ ShooterGame.cs          # 唯一 Composition Root
│  ├─ ShooterFeature.cs       # 系统因果顺序
│  ├─ Actors/                 # 玩家、敌人的能力/关系/配方/规则
│  ├─ Combat/                 # 武器、投射物、伤害事件与规则
│  ├─ Match/                  # 对局状态、结束事件与规则
│  ├─ Movement/               # 通用移动事实与规则
│  └─ Spawning/               # 生成配置、状态与规则
├─ Tests/
│  └─ ShooterPocTests.cs      # 测试安排、断言、稳定哈希
├─ Program.cs                 # 仅启动测试
└─ HUMAN_FIRST_AUTHORING.md
```

不要回到以下结构：

```text
Components.cs
Systems.cs
Events.cs
```

按技术类型分文件会迫使读者同时扫描整个游戏。按功能组织后，修改“伤害”时主要停留在 `Combat/`；跨功能的依赖通过组件、Resource 或 Event 明说。

---

## 6. 作者层与底层实现层的边界

### 作者层优先使用

- `EcsWorld.InsertResource`：装配全局事实；
- `EcsWorld.SpawnNow`：Composition Root、关卡装载、测试安排中的立即生成；
- `WorldCommandBuffer.Spawn`：System 查询期间的延迟生成；
- `EcsWorld.AddFeature`：按功能安装系统；
- `EntityHandle`：跨 Tick 引用实体，校验 Id + Revision。

### 何时仍会看到 Friflo

系统实现需要高效类型化查询，因此 `QuerySystem<...>`、`Entity`、`Tags.Get<T>()` 和 `world.Store.Query<...>()` 仍属于实现层工具。Human-first 不等于隐藏所有底层能力，而是：

> **装配游戏不需要底层 API；实现一条批处理规则时，底层查询在局部、直接、可见。**

测试中的 `Store` 查询只用于白盒断言和稳定状态哈希，不是游戏 Composition Root 的示范写法。

`SpawnNow` 只用于系统更新之外。系统遍历查询时创建/删除实体必须走 `CommandBuffer`，避免结构变更破坏迭代。

---

## 7. 用 Shooter 检查自己的设计

新增“精英敌人”时，不要先建 `EliteEnemy` 类。先问事实：

- 仍属 `EnemyFaction`；
- 仍有 `SeekTarget`；
- `MoveSpeed` 更高；
- `Health` 更高；
- 如果会远程攻击，再组合 `WeaponConfig + Cooldown`。

它可能只需要新的 `EliteEnemyBundle`，现有移动、寻敌、伤害系统全部复用。

新增“友方无人机”时：

- 可以有 `SeekTarget`，但标签换成友方关系；
- 可以有 `WeaponConfig`，但命中规则选择敌方目标；
- 不需要继承 Enemy，也不需要复制 EnemyAI。

新增“减速状态”时：

- 先判断它是临时运行状态，不应改写作者参数 `MoveSpeed`；
- 可以增加 `MoveSpeedModifier` 状态，再由速度计算系统组合基础参数与修正值。

这就是组合优于实体类型树：**规则围绕事实复用，Bundle 只负责让常见组合好写。**

---

## 8. 最终检查清单

写新玩法前逐项回答：

- [ ] 我能用一句“输入 → 行为 → 反馈”说清玩法闭环吗？
- [ ] 每份数据的拥有者是实体还是世界？
- [ ] 每个组件只属于能力、状态、参数、标签关系中的一个主要类别吗？
- [ ] 配置里是否偷偷混入倒计时、当前位置、累计值？
- [ ] System 能否写成“读取 A，更新 B”？
- [ ] 瞬时跨系统因果是否应该使用 Event？
- [ ] 创建长链是否已经提取为 Bundle？
- [ ] 世界装配是否只从一个 Composition Root 进入？
- [ ] 测试安排是否和游戏装配分开？
- [ ] 新开发者能否先读 `ShooterGame.Install`，再按功能逐层深入？

如果这些问题都有明确答案，代码通常已经接近 Human-first Authoring：概念先于机制，事实先于类型，因果先于样板。
