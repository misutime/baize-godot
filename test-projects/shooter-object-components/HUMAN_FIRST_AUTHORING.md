<!-- SPDX-License-Identifier: MIT -->

# Human-first Authoring：从玩法句子到 GameObject + Components 代码

这份 O2 Shooter 是**概念教学样本**，不是性能样板。目标不是让你先记住「组件基类、Requires 依赖、对象工厂」这些机制，而是让人先看见游戏概念，再把概念直接写成「一组组件 + 一套规则」。

一句总纲：

> **Object + Components ≈ ECS。`Object`（对象）不是「某种类的实例」，它只是若干独立组件挂在同一个 Id 上；规则围绕组件复用，而不是围绕实体类型树写 if/else。**

先锚定名称：**`Object`/对象 = `GameObject`（≈ ECS 的 Entity），组件 = `GameComponent`（≈ ECS 的 Component）**——对象承载 Id，组件是挂在 Id 上的那块数据。

本项目沿用既定命名：

- `GameObject`（`Baize.GameObject`）保存一个对象（一组组件的 Id + 生命周期）；`GameWorld` 保存一局世界（组件 + 规则 + 服务）；
- `GameComponent` 是挂在对象上的小块能力/数据；`GameWorld.Tick` 每帧驱动所有组件的 `OnTick`；
- `ComponentSchema` 用 `[GameComponent]` 与 `[GameComponent(Requires = ...)]` 声明「这个组件需要哪些其它组件」，创建时校验；
- `[GameProperty]` 标记会被确定性序列化的字段；
- `GameWorld.AddService<T>()` / `GetService<T>()` 承载「这一局只有一份」的全局状态（对局控制器/输入/生成配置）；
- `MotionPlan` 是这个例子里的关键：**控制器先在 tick 前提交本帧唯一的运动计划，移动与碰撞都只消费这一条**，从而让「先全局移动，再碰撞」的顺序无关语义成立。

---

## 0. 这份设计站在哪

`shooter-object-components`（共享库）与 `shooter-object-components-poc`（验收程序）都只引用 `modules/gameobject/`（O1 纯 .NET 内核）。**零 Godot、零 Node、零 Friflo/Baize.Ecs**——玩法层不依赖引擎，也不依赖任何 ECS 框架。这是 O1 的硬门禁：Gameplay 侧调不到引擎 API。

游戏概念是否成立，全部由 `test-projects/shooter-object-components-poc/Program.cs` 的 15 项断言把关。

---

## 1. 开发者先看什么

不要从所有组件或所有行为开始读。按以下顺序读：

1. `ShooterGame.cs`：这一局装了什么全局服务、初始对象和宿主；
2. `ShooterFactory.cs`：玩家/敌人/投射物出生时分别有哪些组件（B1 工厂，一行创建带全套组件）；
3. `ShooterComponents.cs`：所有组件的类型——数据组件、参数、状态、标记、`MotionPlan`；
4. `ShooterActions.cs`：规则按什么顺序运行（先是各控制器的 `PlanMotion`，再是 `OnTick` 消费计划）；
5. `ShooterServices.cs`：全局状态持有者（对局控制器/输入/生成配置）；
6. 最后才看 `shooter-object-components-poc/Program.cs` 的 15 项断言。

唯一游戏装配入口是：

```csharp
var world = ShooterGame.CreateWorld();  // new GameWorld(fixedDelta=0.01) + Install(world, withPlayer)
```

`Install` 展开后仍只有三类动作：

```csharp
var match = new MatchController();
match.Bind(world);                       // 通知它"世界是谁"，供 Paused 冻结用
world.AddService(match);                 // 放进「这一局只有一份」的全局状态
world.AddService(new InputService());
world.AddService(new SpawnConfig());
world.AddService(new SpawnState());
world.AddService(new CollisionResolver());
SetupScene(world, withPlayer);           // 建宿主 Game + 初始玩家
```

一步推进一帧：

```csharp
ShooterGame.Step(world);   // 1) 所有控制器先提交本帧运动计划  2) world.Tick 统一执行移动+碰撞
```

这就是 Human-first Authoring 的入口：**先读「世界里有什么」「世界怎么步进」，再读底层查询与被操作的组件。**

---

## 2. GameObject + Components 六个概念分别是什么

### 2.1 GameObject：组件组合的 Id

对象不是 `Player`/`Enemy` 继承树，也不承载大段行为。在 Shooter 里：

- 玩家对象 = `Position + PreviousPosition + MotionPlan + Velocity + MoveSpeed + CollisionRadius + PlayerFaction + PlayerInputMarker + WeaponConfig + Cooldown + PlayerControllerAction + WeaponAction + MoveAction`；
- 敌人对象 = `Position + PreviousPosition + MotionPlan + Velocity + MoveSpeed + CollisionRadius + EnemyFaction + SeekTargetMarker + Health + EnemyControllerAction`；
- 投射物对象 = `Position + PreviousPosition + MotionPlan + Velocity + ProjectileConfig + TravelDistance + CollisionRadius + ProjectileTag + BulletAction`。

「玩家/敌人/投射物」是人对组件组合的称呼，不是必须存在的基类。

**什么时候用 GameObject？** 当一个东西需要独立存在、被查询、被销毁，或被稳定句柄引用时，给它一个对象。

### 2.2 GameComponent：关于某个对象的一小块数据

组件是挂在对象上的小块数据。行为组件（Action）是「这个组件对应的一小段规则」——它们读/改自己的 `Owner` 组件，组件间直接调用（如子弹命中直接调 `enemy.Health.ApplyDamage`）。

Shooter 示例：

- `Position`：这个对象现在在哪；
- `PlayerInputMarker`：这个对象能接收玩家输入（能力标记）；
- `MoveSpeed`：这个对象自己的移动速度参数；
- `EnemyFaction`：这个对象属于敌方阵营（标签）。

**什么时候用 GameComponent？** 当这份数据属于某个对象，而且规则需要按「同时拥有这些组件的对象」批量处理时使用。

错误问法：「敌人类还缺什么字段？」
正确问法：「哪条规则需要哪一块独立组件？」

### 2.3 组件的四分类：能力特征 / 运行状态 / 每实体参数 / 标签关系

四类可以共存于同一对象。分类的目的不是建立四个继承体系，而是阻止一个组件同时回答多个问题。

| 类别 | 判断句 | Shooter 示例 | 变化频率 |
|---|---|---|---|
| 能力特征 | 「它**能不能/会不会**做这件事？」 | `PlayerInputMarker`、`SeekTargetMarker` | 很少变化 |
| 运行状态 | 「它**现在**是什么值？」 | `Position`、`Velocity`、`Health.Current`、`Cooldown.Remaining`、`TravelDistance` | 经常变化 |
| 每实体参数 | 「这个实例的**规则数值**是多少？」 | `MoveSpeed`、`WeaponConfig`、`ProjectileConfig`、`CollisionRadius` | 通常由作者设置 |
| 标签关系 | 「它**属于谁/扮演什么关系**？」 | `PlayerFaction`、`EnemyFaction`、`ProjectileTag` | 很少变化 |

#### 能力特征
> 如果删掉数值后，「有/没有这项能力」仍然有意义，它适合成为能力组件。

`SeekTargetMarker` 只表示会寻敌，速度由 `MoveSpeed` 单独表达——之后友方无人机也可以拥有 `SeekTargetMarker`，而不把它伪装成 Enemy。

#### 运行状态
> 如果保存游戏、回放或状态哈希必须记录「它现在到哪一步」，它是运行状态。

所以武器倒计时是 `Cooldown.Remaining`，不是 `WeaponConfig` 的一部分；投射物已飞距离是 `TravelDistance`，不是射程参数。

#### 每实体参数
> 如果设计者会问「这个实例应该设成多少」，但它不会自然每帧累加/递减，它是参数。

`WeaponConfig.CooldownSeconds` 是参数，`Cooldown.Remaining` 是状态。名字故意都带「冷却」，但类型绝不混用，以消除二义。

#### 标签关系
> 如果只需要回答分类、阵营或参与关系，而没有连续数值，就用标签。

`EnemyFaction` 只表达敌方关系；「会追踪目标」由 `SeekTargetMarker` 表达，「跑多快」由 `MoveSpeed` 表达。三者可独立替换。

#### 拆分前后对照

| 拆分前 | 问题 | 拆分后 |
|---|---|---|
| `EnemyAI.Speed` + `EnemyTag` | 身份、策略、速度混在一起 | `EnemyFaction` + `SeekTargetMarker` + `MoveSpeed` |
| `Weapon.Cooldown/BulletSpeed/Timer` | 参数与状态混用 | `WeaponConfig` + `Cooldown`、`ProjectileConfig` |
| `Bullet.Damage/Range/Travelled` + `BulletTag` | 参数、状态、类别重复 | `ProjectileConfig` + `TravelDistance` + `ProjectileTag` |
| `PlayerControl.MoveSpeed` + `PlayerTag` | 控制能力、参数、身份重复 | `PlayerInputMarker` + `MoveSpeed` + `PlayerFaction` |

### 2.4 GameWorld Service：整个世界只有一份的全局状态

服务不挂在虚构的「全局实体」上，由 `GameWorld` 通过 `AddService<T>()`/`GetService<T>()` 持有（内部一局一份）。Shooter 示例：

- `MatchController`：本局互斥阶段（`GamePhase`）、分数、存活敌人数；`RequestGameOver` 会设 `GameWorld.Paused = true` 冻结全局；
- `InputService`：本帧移动输入 + 射击边沿（`FirePressed`/`WasPressed`）；
- `SpawnConfig`：全局生成间隔、上限、生成半径；
- `SpawnState`：当前生成倒计时；
- `CollisionResolver`：共享的扫掠碰撞几何（保持组件自包含，避免重复代码）。

**什么时候用 Service？** 当判断句是「这一局只有一份」，而不是「每个对象各有一份」时使用。配置和状态都可能是 Service，但必须用不同类型表达——`SpawnConfig` 不能再混入当前位置或倒计时。

#### 前端 store/state 心智模型（借前端 Redux/Pinia）

| 前端（Redux/Pinia） | 我们 | 含义 |
|---|---|---|
| **store**（全局容器） | `GameWorld` 的服务注册表（`AddService`/`GetService`） | 装所有「这一局唯一」状态的容器 |
| **state**（容器里的一块） | `MatchController`/`InputService`/`SpawnConfig` | 具体的某个全局状态 |
| **set/getState** | `AddService` / `GetService<T>()` | 读/写某个全局状态 |
| **action / 纯函数更新** | 各 Action 的 `OnTick` / `OnEnemyKilled` | 修改状态的方式（规则） |

**注意**：对象存储（`GameWorld` 的对象注册表）与 `Services`（全局状态）是两件事——一个管对象+组件，一个管全局状态。

#### 阶段控制：`GameWorld.Paused`（而非逐帧 IsPlaying）

`GameOver` 时 `MatchController.RequestGameOver()` 把 `Phase` 切到 `GameOver`，并设 `world.Paused = true`。O1 的 `Paused` 语义是「所有组件 `OnTick` 停」，因此组件**无需再逐帧自查 `IsPlaying`**——这是等效于 ECS `RunInState(Playing)` 门禁的全局冻结。`GameWorld.Reset()` 会把 `Paused` 归零。

### 2.5 MotionPlan：一个 tick 的唯一运动计划

这是本设计最关键、也最容易误解的一点。

在大多数游戏里，「移动」是每帧读速度、改位置；但这样会遇到顺序问题：**如果子弹先执行、它读的是敌人上一帧的位置；如果敌人先执行、它读的是敌人本帧的位置——结果依赖执行顺序。**

为了做到**顺序无关**（也即 ECS「先全局移动，再碰撞」的语义），本设计把「一个 tick 的运动」先统一规划出来：

```
Step(world)：
  1) 若未 Paused：tickIndex = world.TickIndex + 1
     按 PlanPhase 声明序遍历（PlayerInput → Enemy → Projectile）：
        ShooterWorld.PlanMotion(world, delta, tickIndex, phase)
        - PlayerInput → 每个 PlayerControllerAction.PlanMotion   // 玩家先规划
        - Enemy       → 每个 EnemyControllerAction.PlanMotion    // 敌人读玩家本帧终点
        - Projectile  → 每个 BulletAction.PlanMotion             // 子弹提交自身线段
  2) world.Tick(delta)                                              // 统一执行所有 OnTick
```

`MotionPlan` 组件保存本 tick 的 `(StartX, StartZ, EndX, EndZ)` 与 `TickIndex`。移动（`MoveAction`/子弹/敌人的 `OnTick`）只把 `Position` 设成 `plan.End`，把 `PreviousPosition` 设成 `plan.Start`。

**为什么玩家先规划？** 因为敌人要「寻玩家」。如果敌人读玩家**实时**位置，而玩家在同一个 tick 里移动了，两者会分叉。玩家先在 tick 前提交「本帧终点」，敌人的 `PlanMotion` 就直接把玩家的 `plan.End` 当作寻向目标——等价于「玩家先移动」，但不依赖实际 tick 顺序。

**为什么 tickIndex 门禁？** `BulletAction.OnTick` 命中时检查 `enemyPlan.TickIndex == world.TickIndex` 才参与；本 tick 内新建的对象（例如刚生成的敌人）其计划属于下一 tick，按 O1「tick 内新建对象下一轮参与」快照语义自然地从下一帧开始参与。

**一句话记忆**：控制器负责「决定本帧去哪」，`MotionPlan` 是「决定好了的组件」，执行与碰撞只消费这个组件——所以顺序无关、且无「快照重算 vs 实际移动」分叉。

### 2.6 对象工厂：出生时的组件配方（B1）

`ShooterFactory` 就相当于 ECS 的 Bundle——它不是新运行时类型，而是把「创建时通常一起出现的组件」集中到一个可读配方里。B1 落地：

```csharp
public static GameObject SpawnPlayer(GameWorld world, float x, float z, ...)
{
    var obj = world.CreateGameObject("Player");
    obj.AddComponent<PlayerFaction>();
    obj.AddComponent<PlayerInputMarker>();
    AddMoveStack(obj, x, z, moveSpeed, radius);   // Position + Previous + MotionPlan + Velocity + MoveSpeed + CollisionRadius
    obj.AddComponent(new WeaponConfig { CooldownSeconds = fireCooldown, ProjectileSpeed = projectileSpeed });
    obj.AddComponent<PlayerControllerAction>();  // 行为组件
    ...
    return obj;
}
```

**什么时候用工厂？** 当同一组创建代码开始重复，或装配根被 `.AddComponent(...).AddComponent(...)` 长链淹没时使用。

工厂允许重复：玩家和敌人都有 `Position`、`Velocity`、`MoveSpeed`、`CollisionRadius`、`MotionPlan`——这种重复恰好说明组件是可组合数据，不是实体类型字段表。

### 2.7 Action：依赖可见的数据变换规则

Action（行为组件）不拥有玩家、敌人或分数。它读取所属对象（`Owner`）上的组件、读服务/输入，然后改写明确的数据。Shooter 示例：

- `PlayerControllerAction.PlanMotion`：`InputService + MoveSpeed → Velocity → MotionPlan`；
- `EnemyControllerAction.PlanMotion`：`玩家 MotionPlan.End + MoveSpeed → Velocity → MotionPlan`；
- `MoveAction.OnTick`：`MotionPlan → Position/PreviousPosition`；
- `BulletAction.OnTick`：`自身+敌方 MotionPlan → 扫掠命中 → enemy.Health.ApplyDamage → 计分；越界销毁`；
- `WeaponAction.OnTick`：`Fire 边沿 + Cooldown → SpawnProjectile`；
- `EnemySpawnerAction.OnTick`：`TickIndex 确定 HashTick → 生成敌人`。

一眼可见的依赖是：`Requires` 里声明的组件、`World!.GetService<T>()` 取的服务、以及它读写的 `Owner` 组件。`OnStart`（或 `OnCreate`）只缓存这些引用，`OnTick` 只做变换。

#### Action 纯函数约束

这里的「纯」不是说 Action 不修改世界，而是：**相同输入数据产生相同输出数据，Action 实例本身不暗藏跨帧的玩法状态。**

- 会影响玩法、存档、回放、确定性哈希或 `Reset` 的值，必须放入组件或服务；
- 倒计时、输入边沿、随机种子、累计分数不能藏在 Action 字段；
- 允许保存安装期不变配置，以及每次 `OnTick` 开头清空的临时工作区；
- 如果删掉 Action 再重建会改变下一帧玩法结果，说明它藏了状态，应把那份数据搬回组件/服务。

---

## 3. 做一个游戏怎么下手：六步心智模型

下面六步按顺序做。不要先创建 `Components.cs`、`Actions.cs` 两个大文件。

### 第一步：写玩法闭环

先用自然语言写输入到反馈：

> 玩家移动并按下开火键 → 生成投射物 → 投射物移动并命中敌人 → 敌人失去生命并死亡 → 分数增加；敌人持续生成并追向玩家 → 接触玩家后本局结束。

如果一句话还不能读通，不要急着写代码。

### 第二步：列组件

只列名词和可观察数据，不分类型：

- 玩家位置、移动速度、输入能力、武器间隔、武器剩余冷却；
- 敌人位置、寻敌能力、移动速度、生命、阵营；
- 投射物位置、速度、伤害、最大射程、已飞距离；
- 本局阶段、分数、敌人数、生成间隔、生成剩余时间、本帧运动计划。

### 第三步：分类

逐条问：

1. 每对象还是全世界一份？决定 `GameComponent` 或 Service；
2. 长期数据还是瞬时发生？决定组件/服务或由某个 Action 瞬时触发；
3. 是「有没有这项能力」「现在的值」「作者设置的参数」还是「属于谁」？决定能力/状态/参数/标签四类之一。

例如：

- 「敌人速度」不是 EnemyAI，而是每实体参数 `MoveSpeed`；
- 「敌人追玩家」不是身份，而是能力 `SeekTargetMarker`；
- 「玩家现在位置」不是 `SpawnConfig.PlayerX/Z`，而是玩家对象的 `Position`；
- 「本帧它到哪」不是临时算出来的，而是 `MotionPlan`。

### 第四步：写数据变换

每个 Action 先写成公式，再写 C#：

```text
InputService + MoveSpeed -> Velocity -> MotionPlan
玩家 MotionPlan.End + MoveSpeed -> Velocity -> MotionPlan（敌人）
MotionPlan -> Position/PreviousPosition
Fire 边沿 + Cooldown -> SpawnProjectile
自身+敌方 MotionPlan -> 扫掠命中 -> Health.ApplyDamage -> 计分/销毁
TickIndex -> HashTick -> SpawnEnemy
```

如果公式里出现「某 Manager 里面那个字段」，说明组件还没有归位。

### 第五步：排因果

本设计的因果顺序由 `Step` 的**规划阶段**显式固定：

```text
PlayerController.PlanMotion   // 玩家先规划
  -> EnemyController.PlanMotion   // 敌人据玩家本帧终点规划
  -> BulletAction.PlanMotion    // 子弹提交自身线段
  -> world.Tick                    // 统一执行移动 + 命中 + 生成 + 接触
```

顺序不是性能细节，而是游戏语义。正是这个顺序，让「先全局移动，再碰撞」的扫掠命中不用再关心谁先谁后。

### 第六步：提取工厂与装配根

当创建一个对象需要重复添加组件时，提取 `ShooterFactory`；当世界启动代码分散时，建立唯一装配根 `ShooterGame.Install`。

最终作者看到：

```csharp
public static void Install(GameWorld world, bool withPlayer = true)
{
    var match = new MatchController();
    match.Bind(world);                 // 通知它世界是谁（供 Paused 冻结用）
    world.AddService(match);
    world.AddService(new InputService());
    world.AddService(new SpawnConfig());
    world.AddService(new SpawnState());
    world.AddService(new CollisionResolver());
    SetupScene(world, withPlayer);
}
```

这段代码先表达游戏概念，底层对象注册表与查询细节退到各 Action 内部。

---

## 4. 文件为什么这样组织

```text
shooter-object-components/
├─ Shooter.Objects.csproj         # 纯 .NET 共享类库，只引用 modules/gameobject（O1）
├─ ShooterComponents.cs           # 组件类型：数据组件 / 状态 / 参数 / 标签 / MotionPlan / Health
├─ ShooterActions.cs            # 规则：控制器 PlanMotion + OnTick 消费 + 命中/生成/接触；含 CollisionResolver 扫掠几何
├─ ShooterServices.cs             # 全局状态持有者：MatchController / InputService / SpawnConfig / SpawnState
├─ ShooterFactory.cs              # 出生配方：SpawnPlayer / SpawnEnemy / SpawnProjectile
├─ ShooterGame.cs                 # 唯一装配根 + Step（规划阶段 + world.Tick）
├─ ShooterWorld.cs                # 查询辅助（AllObjects / QueryObjects / CanTick）+ PlanPhase 枚举 / PlanMotion 编排
└─ HUMAN_FIRST_AUTHORING.md

shooter-object-components-poc/
├─ Program.cs                     # 15 项验收断言（纯 .NET，零引擎）
└─ HUMAN_FIRST_AUTHORING.md       # 本文件在共享库侧
```

不要回到以下结构（按技术类型分文件会迫使读者同时扫描整个游戏）：

```text
Components.cs
Actions.cs
Services.cs
```

按「组合」组织后，改「伤害」主要停留在 `Health` 组件 + `BulletAction`+`MatchController`；跨功能的依赖通过组件、服务或直接调用明说。

---

## 5. 与 ECS 心智的对照

如果你熟悉 Bevy/Friflo 那种 ECS，以下对照能帮你迁移：

| ECS | GameObject + Components |
|---|---|
| `Entity` | `GameObject` |
| `IComponent` / `ITag` | `GameComponent`（`[GameComponent]` 数据组件 / 标记组件） |
| `Resource` | `GameWorld` 服务（`AddService` / `GetService<T>()`） |
| `System` | Action 组件（`OnTick` 规则；`PlanMotion` 控制器） |
| `Bundle` | `ShooterFactory`（`SpawnXxx` 一行创建带全套组件） |
| `CommandBuffer` | **无**（创建/命中/死亡都即时、直接；同步销毁） |
| `RunInState(Playing)` | `GameWorld.Paused`（全局冻结，组件不再自查） |
| `Event` | **无**（组件间直接调用，如 `enemy.Health.ApplyDamage`） |
| 阶段/Phase | `Step` 的规划阶段 + `world.Tick` |
| （无直接对应物） | `MotionPlan`（本设计为「先全局移动，再碰撞」引入的唯一运动计划） |

两点**有意不同**：

1. **没有 CommandBuffer**。本设计不做帧末缓冲：命中/死亡/创建都即时落地、直接调用。同步销毁配合「对象创建序在 tick 中天然去重」，避免了帧末缓冲那套双缓冲复杂度。
2. **没有 Event**。`bullet → enemy.Health.ApplyDamage → MatchController.OnEnemyKilled` 是直接调用，因果链对读者是一行一行可见的，不用再追踪事件生产者/消费者。

---

## 6. 用 Shooter 检查自己的设计

新增「精英敌人」时，不要先建 `EliteEnemy` 对象。先问组件：

- 仍属 `EnemyFaction`；
- 仍有 `SeekTargetMarker`；
- `MoveSpeed` 更高；
- `Health` 更高；
- 如果会远程攻击，再组合 `WeaponConfig + Cooldown`。

它可能只需要新的 `ShooterFactory.SpawnEliteEnemy`，现有移动、寻敌、命中、生成逻辑全部复用。

新增「友方无人机」时：

- 可以有 `SeekTargetMarker`，但标签换成友方关系；
- 可以有 `WeaponConfig`，但命中规则选择敌方目标；
- 不需要继承 Enemy，也不需要复制寻敌逻辑。

新增「减速状态」时：

- 先判断它是临时运行状态，不应改写作者参数 `MoveSpeed`；
- 可以增加 `MoveSpeedModifier` 状态，再由移动规划组合基础参数与修正值。

这就是组合优于实体类型树：**规则围绕组件复用，工厂只负责让常见组合好写。**

---

## 7. 最终检查清单

写新玩法前逐项回答：

- [ ] 我能用一句「输入 → 行为 → 反馈」说清玩法闭环吗？
- [ ] 每份数据的拥有者是对象还是世界（Service）？
- [ ] 每个组件只属于能力、状态、参数、标签关系中的一个主要类别吗？
- [ ] 配置里是否偷偷混入倒计时、当前位置、累计值？
- [ ] Action 能否写成「读取 A，更新 B」，且类字段没有藏跨帧玩法状态？
- [ ] Action 的 `Requires`（依赖组件）、取的服务、读写的 Owner 组件是否一眼可见？
- [ ] 互斥世界阶段是否用 `GameWorld.Paused` 集中冻结（而非逐帧自查）？
- [ ] 一个 tick 的运动是否统一先规划成 `MotionPlan`，移动与碰撞能否消费同一条线段？
- [ ] 是否仍依赖「谁先执行」？测试里是否覆盖了「子弹先建/敌人后建」的顺序无关场景？
- [ ] 创建长链是否已经提取为 `ShooterFactory`？世界装配是否只从一个 `ShooterGame.Install` 进入？
- [ ] 测试安排是否和游戏装配分开？
- [ ] 新开发者能否先读 `ShooterGame.Install` + `ShooterGame.Step`，再按文件逐层深入？
- [ ] 是否零 Godot/Node/Friflo/Baize.Ecs 引用（只 `using Baize.GameObject`）？

如果这些问题都有明确答案，代码通常已经接近 Human-first Authoring：概念先于机制，组件先于类型，因果先于样板。
