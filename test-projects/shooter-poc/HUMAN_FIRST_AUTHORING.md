<!-- SPDX-License-Identifier: MIT -->

# Human-first Authoring：从玩法句子到 ECS 代码

这份 PoC 是**教学样本**，不是性能样板。目标不是让开发者先记住 Friflo API，而是让人先看见游戏概念，再把概念直接写成数据与规则。

一句总纲：

> **Object + Components ≈ ECS。实体不是类，组件不是某种实体的字段表；实体只是若干事实在同一个 Id 上的组合。**

本项目沿用既定命名：

- `EcsWorld` 保存一局 ECS 世界（事实 + 规则）；**现阶段暂由它执行固定 Tick**；
- **将来由 `EcsHost` 接管引擎生命周期、输入采集与 Tick 驱动**（Bevy 的 App 跑循环、World 存数据的分工），`EcsWorld` 回归纯容器——所以 `Tick` 是作者层入口，但"谁来驱动 Tick"最终归 `EcsHost`；
- Friflo 是底层存储与查询实现，不在本示例中修改；
- `Resource`、`Bundle`、`EventWriter/EventReader` 是三点 Bevy 作者体验借鉴，但保持 C# 语义；`EcsState` 是框架层对世界状态生命周期的明确表达。

---

## 1. 开发者先看什么

不要从所有组件或所有系统开始读。按以下顺序读：

1. `Gameplay/ShooterGame.cs`：这一局装了什么全局事实、初始对象和玩法功能；
2. `Gameplay/Actors/PlayerBundle.cs`：玩家出生时有哪些事实；
3. `Gameplay/ShooterFeature.cs`：大 Feature 如何嵌套小 Feature，以及规则按什么因果顺序运行；
4. 再进入某个功能目录，例如 `Gameplay/Combat/` 看局部数据变换；
5. 最后才看 `Tests/ShooterPocTests.cs` 的测试安排和底层状态哈希。

唯一游戏装配入口是：

```csharp
var world = new EcsWorld(aot => EcsAotRegistration.RegisterAll(aot));
ShooterGame.Install(world);
```

`ShooterGame.Install(world)` 展开后仍只有三类作者动作：

```csharp
world.InsertState(...);        // 放入“这一局只有一份”的事实
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

### 2.3 WorldState：整个世界只有一份的事实（借鉴 Bevy Resource）

WorldState 不挂在虚构的“全局实体”上，由 `EcsWorld` 持有。

Shooter 示例：

- `MatchState`：本局互斥阶段（`EcsState`）、分数、存活敌人数；
- `SpawnConfig`：全局生成间隔、上限、生成半径；
- `SpawnState`：当前生成倒计时；
- `FireInputState`：上一 Tick 是否按住开火键。

**什么时候用 WorldState？**

当判断句是“这一局只有一份”，而不是“每个实体各有一份”时使用。
注意：配置和状态都可以是 WorldState，但必须用不同类型表达。`SpawnConfig` 不能再混入玩家位置或倒计时。

#### EcsState：有进入/退出生命周期的世界级 WorldState

普通计数仍用普通 WorldState；只有“世界同时只能处于一个阶段，而且切换有统一副作用”时才用 `EcsState<T>`：

```csharp
public sealed class MatchState : EcsState<GamePhase>
{
    public MatchState() : base(GamePhase.Playing) { }
    public int Score;

    protected override void OnExit(EcsWorld world, GamePhase state) { ... }
    protected override void OnEnter(EcsWorld world, GamePhase state) { ... }
}

State<MatchState>().TransitionTo(GamePhase.GameOver);
```

`InsertState(new MatchState())` 会进入初始状态；以后每次 `TransitionTo` 都严格执行 `OnExit(旧) → OnEnter(新)`。Shooter 在离开 `Playing` 时丢弃未落地命令，进入 `GameOver` 时统一冻结速度，进入 `Playing` 时重置输入边沿和生成节拍。副作用只写一次，不再让每个 System 各自猜测状态切换意味着什么。

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

### 2.6 System：依赖可见的数据变换规则

System 不拥有玩家、敌人或分数。它查询需要的事实，读取输入/资源/事件，然后改写明确的数据。Shooter 示例：

- `ApplyPlayerInputSystem`：`InputFrame + MoveSpeed → Velocity`；
- `SeekPlayerSystem`：玩家 `Position + MoveSpeed →` 敌人 `Velocity`；
- `MoveSystem`：`Position + Velocity + delta → Position`；
- `ResolveDamageSystem`：`DamageRequested + Health → Health/删除/Score`。

作者层可选用 `EcsSystem` 家族，让依赖集中在类头、构造器声明和 `Execute` 开头：

```csharp
public sealed class FireWeaponSystem
    : EcsSystem<Position, WeaponConfig, Cooldown>
{
    public FireWeaponSystem()
    {
        RunInState<MatchState>(GamePhase.Playing);
        ForTag<PlayerFaction>();
    }

    protected override void Execute()
    {
        FireInputState edge = Res<FireInputState>();
        bool pressed = Input.FirePressed;
        ForEach((ref Position position, ref WeaponConfig weapon,
            ref Cooldown cooldown, Entity player) => { /* 原地更新 */ });
    }
}
```

一眼可见的依赖是：查询组件泛型、`MatchState` 运行条件、Tag 过滤、`FireInputState` Resource 和当前输入。`World`、`Input`、`Res<T>()`、`State<T>()`、`ReadEvents<T>()`、`WriteEvents<T>()` 由基类提供，不再为每个系统保存 `_world`、写构造器注入和手工判空。

作者层查询按“是否改写组件”分成两条明确路径：

```csharp
ForTag<EnemyFaction>(); // 构造器：给本 System 的主查询加 Tag 条件

ForEach((ref Position position, ref Velocity velocity, Entity enemy) =>
{
    velocity.X = 0;     // 写路径：ref 原地更新，不复制组件
});

foreach (var (position, player) in Read<Position>().WithTag<PlayerFaction>())
{
    // 读路径：position 是按值快照，不能误改世界中的 Position
}
```

- `ForEach(...)` 只转发到 Friflo `Query.ForEachEntity(...)`；组件仍由 `ref` 原地访问。C# 对值类型原地改写必须显式写出 `ref`，封装不隐藏这一语义，只去掉重复的 `Query.ForEachEntity` 机制噪音。
- `ForTag<T>()` 只在构造器配置继承来的 `QueryFilter`，等价于原来的 `Filter.AllTags(Tags.Get<T>())`；查询创建时机、AOT 注册、Feature 生成器与诊断都不变。
- `Read<T...>().WithTag<T>()` 使用 Friflo `Chunks` 的结构体枚举器，逐项把组件复制为只读意图明确的值。它适合寻敌、接触检测等次级读取；大组件或需要写回的热循环继续用 `ForEach(ref ...)`。
- 一次 `Read<T...>()` 会创建一个 Friflo 查询对象；嵌套热循环应像命中系统一样先保存为局部变量再复用。该层不承诺查询对象零分配，但逐项枚举不走 `IEnumerable` 装箱，也不额外建立实体列表。

`EcsSystem<T...>` 底层仍继承 Friflo `QuerySystem<T...>`；原有 `Query.ForEachEntity`、`Filter`、`BaseSystem`、`QuerySystem<T...>` 与 `EcsWorld.AddSystem` 完全保留。新 API 是向后兼容的作者层捷径，不是对 Friflo 的替换。

#### System 纯函数约束

这里的“纯”不是说 System 不修改世界，而是说：**相同输入事实应产生相同输出事实，System 实例本身不暗藏跨 Tick 的玩法状态。**

- 会影响玩法、存档、回放、确定性哈希或 `Reset` 的值，必须放入 Component 或 Resource；
- 倒计时、输入边沿、随机种子、累计分数不能藏在 System 字段；
- 允许保存安装期不变配置，以及每次 `Execute` 开头清空的临时工作区；
- `ResolveDamageSystem` 的两个 `HashSet<Entity>` 只做同 Tick 去重，并在每次执行开头 `Clear()`，因此是可接受的 scratch state；
- 如果删掉 System 再重建会改变下一 Tick 玩法结果，说明它藏了状态，应把那份数据搬回 Resource/Component。

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
2. 长期事实还是瞬时发生？决定 Component/Resource 或 Event；若世界阶段互斥且切换有生命周期，再把该 Resource 表达为 `EcsState<T>`；

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

顺序不是性能细节，而是游戏语义。`ShooterFeature` 通过嵌套 `MatchFeature/CombatFeature/...` 集中展示这条因果链。

### 第六步：提取 Bundle 与 Composition Root

当创建一个对象需要重复添加组件时，提取 Bundle；当世界启动代码分散时，建立唯一 Composition Root。

最终作者应看到：

```csharp
public static void Install(EcsWorld world)
{
    world
        .InsertState(new SpawnConfig())
        .InsertState(new SpawnState())
        .InsertState(new FireInputState())
        .InsertState(new MatchState());

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
│  ├─ ShooterFeature.cs       # 大 Feature 嵌套小 Feature，集中表达系统因果顺序
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

**分层定位（日常只用 Baize.Ecs）**：

```text
Baize.Ecs        —— 游戏开发者日常入口（引用 using Baize.Ecs）
    EcsSystem / EcsState / EcsWorld / ForEach / ForTag / Read /
    InsertState / WorldState / EntityBundle / EventWriter-Reader / Phase
Friflo.Engine.ECS —— 底层高性能内核（无需主动 using 也会用到）
    IComponent / ITag / Entity / ref 组件 / 高级查询 / chunk 写入
```

- **日常写游戏**：`using Baize.Ecs`，用 `EcsSystem`/`ForEach`/`ForTag`/`InsertState`/`EcsState` 表达玩法。
- **组件声明**（`struct Position : IComponent`）：`IComponent` 是 Friflo 的（性能内核，无法改为 Baize 的），
  所以组件文件需 `using Friflo.Engine.ECS`——这是**有意**的（保留 Friflo 数据布局与泛型特化），不是缺隔离。
- **了解 Friflo 的用户**：可直接用 Friflo 高级 API（`Query`/`Filter`/`Tags.Get<T>()`/`Store.Query`）——
  Baize.Ecs 不封死底层，而是**默认走作者层**（更简洁），高级需求在局部显式下沉。

**核心原则**：**默认代码先表达"处理谁、读什么、写什么"；只有确有高级需求时，底层查询机制才在局部显式出现。**
不是把所有 Friflo 能力包一层藏起来，而是让日常写法最简、高级能力可及。
### 作者层优先使用

- `EcsWorld.InsertState`：装配全局事实；
- `EcsWorld.SpawnNow`：Composition Root、关卡装载、测试安排中的立即生成；
- `WorldCommandBuffer.Spawn`：System 查询期间的延迟生成；
- `EcsWorld.AddFeature`：按功能安装系统；`Install` 内可继续 `AddFeature` 组合子功能；
- `EcsSystem` / `EcsSystem<T...>`：显式获取世界依赖并声明 State 运行条件；
- `EcsState<T>`：集中执行世界阶段的 `OnExit/OnEnter`；
- Friflo `Entity`：唯一实体安全引用，自带 Store + Id + Revision；跨 Tick 直接用 `IsNull` 判断是否仍有效。

### 何时仍会看到 Friflo

系统的常见写路径优先使用 `ForEach(ref ...)`，常见次级只读查询优先使用 `Read<T...>().WithTag<T>()`。只有需要 Friflo 的高级过滤、索引、并行 Job 或直接 chunk 写入时，才下沉到 `Query`、`Filter`、`Tags.Get<T>()` 与 `world.Store.Query<...>()`。Human-first 不等于封死底层能力，而是：

> **默认代码先表达“处理谁、读什么、写什么”；只有确有高级需求时，底层查询机制才在局部显式出现。**

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
- [ ] System 能否写成“读取 A，更新 B”，且类字段没有藏跨 Tick 玩法状态？
- [ ] System 的组件、Resource、Event、输入与 State 条件是否一眼可见？
- [ ] 互斥世界阶段是否用 `EcsState<T>` 集中处理 `OnExit/OnEnter`？
- [ ] 瞬时跨系统因果是否应该使用 Event？
- [ ] 创建长链是否已经提取为 Bundle？大功能是否通过嵌套 Feature 组合小功能？
- [ ] 世界装配是否只从一个 Composition Root 进入？
- [ ] 测试安排是否和游戏装配分开？
- [ ] 新开发者能否先读 `ShooterGame.Install`，再按功能逐层深入？

如果这些问题都有明确答案，代码通常已经接近 Human-first Authoring：概念先于机制，事实先于类型，因果先于样板。


