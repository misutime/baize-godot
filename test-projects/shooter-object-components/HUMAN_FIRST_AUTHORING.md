<!-- SPDX-License-Identifier: MIT -->

# Human-first Authoring：从玩法句子到 GameObject + Components 代码

这份 O2 Shooter 是**概念教学样本**，不是性能样板。目标不是让你先记住「组件基类、Requires 依赖、对象工厂」这些机制，而是让人先看见游戏概念，再把概念直接写成「一组组件 + 一套规则」。

一句总纲：

> **Object + Components ≈ ECS。`Object`（对象）不是「某种类的实例」，它只是若干独立组件挂在同一个 Id 上；规则围绕组件复用，而不是围绕实体类型树写 if/else。**

先锚定名称：**`Object`/对象 = `GameObject`（≈ ECS 的 Entity），组件 = `GameComponent`（≈ ECS 的 Component）**——对象承载 Id，组件是挂在 Id 上的那块数据。

本项目沿用既定命名：

- `GameObject`（`Sola3d.GameObject`）保存一个对象（一组组件的 Id + 生命周期）；`GameWorld` 保存一局世界（组件 + 规则 + 资源）；
- `GameComponent` 是挂在对象上的小块能力/数据；`GameWorld.Tick` 每帧驱动所有组件的 `OnTick`；
- `ComponentSchema` 用 `[GameComponent]` 与 `[GameComponent(Requires = ...)]` 声明「这个组件需要哪些其它组件」，创建时校验；
- `[GameProperty]` 标记会被确定性序列化的字段；
- `GameWorld.AddResource<T>()` / `GetResource<T>()` 承载「这一局只有一份」的全局状态（对局控制器/输入/生成配置）；
- `GameWorld.Tick` 每帧驱动所有组件的 `OnTick`；`ShooterGame.RunFrame` 用**显式阶段顺序**（先 Move 全部对象，再 Collide 扫掠命中）保证「先全局移动，再碰撞」的顺序无关语义；

---

## 0. 这份设计站在哪

`shooter-object-components`（共享库）与 `shooter-object-components-poc`（验收程序）都只引用 `modules/gameobject/`（O1 纯 .NET 内核）。**零 Godot、零 Node、零 Friflo/Sola3d.Ecs**——玩法层不依赖引擎，也不依赖任何 ECS 框架。这是 O1 的硬门禁：Gameplay 侧调不到引擎 API。

游戏概念是否成立，全部由 `test-projects/shooter-object-components-poc/Program.cs` 的 17 项断言把关。

#### World 的粒度：一局 = 一个 GameWorld

`GameWorld` 不是"整个游戏的全局世界"，而是**一场对局 / 一个关卡的可运行模拟容器**。它持有这一局的对象注册表、层级、关系、组件生命周期调度、全局 tick（`TickIndex`/`FixedTickIndex`）、资源（计分/阶段/输入/生成配置）与 `Paused` 冻结。

所以：

```text
关卡 A ──> 自己的 GameWorld（加载关卡 A 的场景/预置体快照）
关卡 B ──> 自己的 GameWorld（加载关卡 B 的场景/预置体快照）
```

- **多关卡游戏 = 多个 GameWorld 实例**，不是把多个关卡塞进一个世界。
- **切关卡** = 销毁旧 `GameWorld`、新建一个，再用该关卡的内容快照（`GameWorldSerializer` 导出的 `GameWorldSnapshot`）填充。
- **`Reset()`** 只用于**重开同一局**（清对象 + tick 归零），不用于切换不同的关卡内容。
- `var world = ShooterGame.CreateWorld()` 每次都会生成一个**全新的、独立的**世界（新 `MatchController`/`InputService`/玩家）。



## 1. 开发者先看什么

不要从所有组件或所有行为开始读。按以下顺序读：

1. `ShooterGame.cs`：这一局装了什么全局资源、初始对象和宿主；
2. `ShooterFactory.cs`：玩家/敌人/投射物出生时分别有哪些组件（B1 工厂，一行创建带全套组件）；
3. `ShooterComponents.cs`：所有组件的类型——数据组件、参数、状态、标记；
4. `ShooterActions.cs`：规则按什么顺序运行（先是各控制器的 `Move`，再是命中/接触）；
5. `ShooterResources.cs`：全局状态持有者（对局控制器/输入/生成配置/多来源暂停）；
6. 最后才看 `shooter-object-components-poc/Program.cs` 的 17 项断言。

唯一游戏装配入口是：

```csharp
var world = ShooterGame.CreateWorld();  // new GameWorld(fixedDelta=0.01) + Install(world, withPlayer)
```

`Install` 展开后仍只有三类动作：

```csharp
world.AddResource(new MatchController()); // 放进「这一局只有一份」的全局状态；纯状态，不碰世界
world.AddResource(new PauseManager());    // 多来源暂停计数（终局/菜单等各自 Pause/Unpause）
world.AddResource(new InputService());
world.AddResource(new SpawnConfig());
world.AddResource(new SpawnState());
world.AddResource(new CollisionResolver());
SetupScene(world, withPlayer);           // 建宿主 Game + 初始玩家
```

一步推进一帧：

```csharp
ShooterGame.RunFrame(world);   // 1) Move 阶段全部对象移动到本帧终点  2) Collide 阶段子弹扫掠命中  3) world.Tick 执行开火/生成等杂项 OnTick
```

这就是 Human-first Authoring 的入口：**先读「世界里有什么」「世界怎么步进」，再读底层查询与被操作的组件。**

---

## 2. GameObject + Components 六个概念分别是什么

### 2.1 GameObject：组件组合的 Id

对象不是 `Player`/`Enemy` 继承树，也不承载大段行为。在 Shooter 里：

- 玩家对象 = `Position + PreviousPosition + Velocity + MoveSpeed + CollisionRadius + PlayerFaction + PlayerInputMarker + WeaponConfig + Cooldown + PlayerControllerAction + WeaponAction`；
- 敌人对象 = `Position + PreviousPosition + Velocity + MoveSpeed + CollisionRadius + EnemyFaction + SeekTargetMarker + Health + EnemyControllerAction`；
- 投射物对象 = `Position + PreviousPosition + Velocity + ProjectileConfig + TravelDistance + CollisionRadius + ProjectileTag + BulletAction`。

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

### 2.4 GameWorld Resource：整个世界只有一份的全局状态

资源不挂在虚构的「全局实体」上，由 `GameWorld` 通过 `AddResource<T>()`/`GetResource<T>()` 持有（内部一局一份）。Shooter 示例：

- `MatchController`：本局互斥阶段（`GamePhase`）、分数、存活敌人数；`RequestGameOver` 只切 `Phase`（纯状态，不碰世界）；
- `PauseManager`：多来源暂停计数（终局/菜单等各自 `Pause`/`Unpause`；任一来源 active 即暂停）；
- `InputService`：本帧移动输入 + 射击边沿（`FirePressed`/`WasPressed`）；
- `SpawnConfig`：全局生成间隔、上限、生成半径；
- `SpawnState`：当前生成倒计时；
- `CollisionResolver`：共享的扫掠碰撞几何（保持组件自包含，避免重复代码）。

**装配约定**：资源用 `world.AddResource(new Xxx(...))` 一行装配（纯状态资源无需回指世界）；若将来需要给资源传配置，一律走**构造函数参数**（如 `new SpawnConfig(fixedDelta)`），不用 `Bind`/后门方法——资源保持「纯状态 + 组合根读状态得出门禁」的单向因果。

**什么时候用 Resource？** 当判断句是「这一局只有一份」，而不是「每个对象各有一份」时使用。配置和状态都可能是 Resource，但必须用不同类型表达——`SpawnConfig` 不能再混入当前位置或倒计时。

#### 前端 store/state 心智模型（借前端 Redux/Pinia）

| 前端（Redux/Pinia） | 我们 | 含义 |
|---|---|---|
| **store**（全局容器） | `GameWorld` 的资源注册表（`AddResource`/`GetResource`） | 装所有「这一局唯一」状态的容器 |
| **state**（容器里的一块） | `MatchController`/`InputService`/`SpawnConfig` | 具体的某个全局状态 |
| **set/getState** | `AddResource` / `GetResource<T>()` | 读/写某个全局状态 |
| **action / 纯函数更新** | 各 Action 的 `OnTick` / `OnEnemyKilled` | 修改状态的方式（规则） |

**注意**：对象存储（`GameWorld` 的对象注册表）与 `Resources`（全局状态）是两件事——一个管对象+组件，一个管全局状态。

#### 阶段控制：`GameWorld.Paused`（而非逐帧 IsPlaying）

`GameOver` 时 `MatchController.RequestGameOver()` 只把 `Phase` 切到 `GameOver`（纯状态，不碰世界）。冻结由组合根唯一聚合：`ShooterGame.RunFrame` 的 `ApplyPause` 读各来源（`PauseManager` 菜单/暂停表 + 终局 `Phase`）→ 写 `GameWorld.Paused`。O1 的 `Paused` 语义是「所有组件 `OnTick` 停」，因此组件**无需再逐帧自查 `IsPlaying`**——等效于 ECS `RunInState(Playing)` 门禁的全局冻结。`GameWorld.Reset()` 会把 `Paused` 归零；多来源暂停（菜单 + 终局）用 `PauseManager` 计数，任一来源 active 即冻结、来源互不误伤。

### 2.5 阶段顺序：先全局移动，再碰撞

这是本设计最关键的一点。

在大多数游戏里，「移动」是每帧读速度、改位置；但这样会遇到顺序问题：**如果子弹先执行、它读的是敌人上一帧的位置；如果敌人先执行、它读的是敌人本帧的位置——结果依赖执行顺序。**

为了做到**顺序无关**（即 ECS「先全局移动，再碰撞」的语义），本设计用**显式阶段顺序**，而不做每对象的运动计划：

```
RunFrame(world)：
  1) 若未 Paused：
     ShooterWorldHelper.MoveAll(world, delta)     // 阶段1 Move：玩家→敌人→子弹，全部移动到本帧终点
     ShooterWorldHelper.CollideAll(world, delta)  // 阶段2 Collide：子弹扫掠命中（读各方本帧 prev→pos）
  2) world.Tick(delta)                              // 执行开火/生成等杂项 OnTick
```

`Move` 里每个会动组件的 `Move(delta)` 只做「设 `PreviousPosition` 为旧位置、`Position` 累加速度」。`Collide` 里每个子弹的 `Collide(delta)` 用 `CollisionResolver.SegmentSegmentDistance` 对 子弹本帧线段 vs 敌人本帧线段 做同步扫掠最短距离，命中则 `enemy.Health.ApplyDamage`、`Owner.Destroy()`。

**为什么玩家在 Move 里先动？** 因为敌人要「寻玩家」。Move 阶段顺序固定为 玩家→敌人→子弹：玩家先把 `Position` 移到本帧终点，敌方 `Move` 读取玩家当前位置就拿到「本帧终点」作为寻向目标——等价于「玩家先移动」，但按阶段固定，不依赖实际 tick 顺序。

**为什么阶段顺序能保证顺序无关？** 因为所有 `Move` 先于所有 `Collide` 完成；`Collide` 读的是各方**已经更新好的** `PreviousPosition→Position` 本帧线段，与对象创建顺序/执行顺序无关。本 tick 内新建的对象（例如刚生成的敌人/子弹）在下一次 `RunFrame` 的 Move/Collide 阶段才参与——按 O1「tick 内新建对象下一轮参与」快照语义自然从下一帧开始。

**一句话记忆**：控制器在 Move 阶段先移动，子弹在 Collide 阶段按本帧线段扫掠命中——移动全部完成后再碰撞，所以顺序无关、且没有「快照重算 vs 实际移动」分叉。

### 2.6 对象工厂：出生时的组件配方（B1）

`ShooterFactory` 就相当于 ECS 的 Bundle——它不是新运行时类型，而是把「创建时通常一起出现的组件」集中到一个可读配方里。B1 落地：

```csharp
public static GameObject SpawnPlayer(GameWorld world, float x, float z, ...)
{
    var obj = world.CreateGameObject("Player");
    obj.AddComponent<PlayerFaction>();
    obj.AddComponent<PlayerInputMarker>();
    AddMoveStack(obj, x, z, moveSpeed, radius);   // Position + Previous + Velocity + MoveSpeed + CollisionRadius
    obj.AddComponent(new WeaponConfig { CooldownSeconds = fireCooldown, ProjectileSpeed = projectileSpeed });
    obj.AddComponent<PlayerControllerAction>();  // 行为组件
    ...
    return obj;
}
```

**什么时候用工厂？** 当同一组创建代码开始重复，或装配根被 `.AddComponent(...).AddComponent(...)` 长链淹没时使用。

工厂允许重复：玩家和敌人都有 `Position`、`Velocity`、`MoveSpeed`、`CollisionRadius`——这种重复恰好说明组件是可组合数据，不是实体类型字段表。

### 2.7 Action：依赖可见的数据变换规则

Action（行为组件）不拥有玩家、敌人或分数。它读取所属对象（`Owner`）上的组件、读资源/输入，然后改写明确的数据。Shooter 示例：

- `PlayerControllerAction.Move`：`InputService + MoveSpeed → Velocity → Position/PreviousPosition`；
- `EnemyControllerAction.Move`：`玩家 Position + MoveSpeed → Velocity + 接触判定 → Position/PreviousPosition`；
- `BulletAction.Move`：`Velocity → Position/PreviousPosition`；
- `BulletAction.Collide`：`自身+敌方 Position→PreviousPosition → 扫掠命中 → enemy.Health.ApplyDamage → 计分；越界销毁`；
- `WeaponAction.OnTick`：`Fire 边沿 + Cooldown → SpawnProjectile`；
- `EnemySpawnerAction.OnTick`：`TickIndex 确定 HashTick → 生成敌人`。

一眼可见的依赖是：`Requires` 里声明的组件、`World!.GetResource<T>()` 取的资源、以及它读写的 `Owner` 组件。`OnCreate`（或 `OnStart`）只缓存这些引用，`Move`/`Collide`/`OnTick` 只做变换。

#### Action 纯函数约束

这里的「纯」不是说 Action 不修改世界，而是：**相同输入数据产生相同输出数据，Action 实例本身不暗藏跨帧的玩法状态。**

- 会影响玩法、存档、回放、确定性哈希或 `Reset` 的值，必须放入组件或资源；
- 倒计时、输入边沿、随机种子、累计分数不能藏在 Action 字段；
- 允许保存安装期不变配置，以及每次 `OnTick` 开头清空的临时工作区；
- 如果删掉 Action 再重建会改变下一帧玩法结果，说明它藏了状态，应把那份数据搬回组件/资源。

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
- 本局阶段、分数、敌人数、生成间隔、生成剩余时间。

### 第三步：分类

逐条问：

1. 每对象还是全世界一份？决定 `GameComponent` 或 Resource；
2. 长期数据还是瞬时发生？决定组件/资源或由某个 Action 瞬时触发；
3. 是「有没有这项能力」「现在的值」「作者设置的参数」还是「属于谁」？决定能力/状态/参数/标签四类之一。

例如：

- 「敌人速度」不是 EnemyAI，而是每实体参数 `MoveSpeed`；
- 「敌人追玩家」不是身份，而是能力 `SeekTargetMarker`；
- 「玩家现在位置」不是 `SpawnConfig.PlayerX/Z`，而是玩家对象的 `Position`；
- 「本帧它到哪」不是临时算出来的，而是 `Position`（以及它的 `PreviousPosition` 本帧线段）。

### 第四步：写数据变换

每个 Action 先写成公式，再写 C#：

```text
InputService + MoveSpeed -> Velocity -> Position/PreviousPosition
玩家 Position + MoveSpeed -> Velocity + 接触判定 -> Position/PreviousPosition（敌人）
Velocity -> Position/PreviousPosition
Fire 边沿 + Cooldown -> SpawnProjectile
自身+敌方 Position->PreviousPosition -> 扫掠命中 -> Health.ApplyDamage -> 计分/销毁
TickIndex -> HashTick -> SpawnEnemy
```

如果公式里出现「某 Manager 里面那个字段」，说明组件还没有归位。

### 第五步：排因果

本设计的因果顺序由 `RunFrame` 的**阶段顺序**显式固定：

```text
ShooterWorldHelper.MoveAll(world, delta)      // 阶段1：玩家→敌人→子弹，全部移动到本帧终点
  -> 玩家先 Move（玩家 Position 先到本帧终点）
  -> 敌人 Move（据玩家本帧终点寻向 + 接触判定）
ShooterWorldHelper.CollideAll(world, delta)   // 阶段2：子弹扫掠命中
  -> world.Tick                       // 开火/生成等杂项 OnTick
```

顺序不是性能细节，而是游戏语义。正是这个顺序，让「先全局移动，再碰撞」的扫掠命中不用再关心谁先谁后。

### 第六步：提取工厂与装配根

当创建一个对象需要重复添加组件时，提取 `ShooterFactory`；当世界启动代码分散时，建立唯一装配根 `ShooterGame.Install`。

最终作者看到：

```csharp
public static void Install(GameWorld world, bool withPlayer = true)
{
    world.AddResource(new MatchController());   // 纯状态，不碰世界
    world.AddResource(new PauseManager());
    world.AddResource(new InputService());
    world.AddResource(new SpawnConfig());
    world.AddResource(new SpawnState());
    world.AddResource(new CollisionResolver());
    SetupScene(world, withPlayer);
}
```

这段代码先表达游戏概念，底层对象注册表与查询细节退到各 Action 内部。

---

## 4. 文件为什么这样组织

```text
shooter-object-components/
├─ Shooter.Objects.csproj         # 纯 .NET 共享类库，只引用 modules/gameobject（O1）
├─ ShooterComponents.cs           # 组件类型：数据组件 / 状态 / 参数 / 标签 / Health
├─ ShooterActions.cs            # 规则：控制器 Move + 子弹 Collide/命中 + 开火/生成/接触；含 CollisionResolver 扫掠几何
├─ ShooterResources.cs             # 全局状态持有者：MatchController / InputService / SpawnConfig / SpawnState
├─ ShooterFactory.cs              # 出生配方：SpawnPlayer / SpawnEnemy / SpawnProjectile
├─ ShooterGame.cs                 # 唯一装配根 + RunFrame（Move 阶段 + Collide 阶段 + world.Tick）
├─ ShooterWorldHelper.cs                # 查询辅助（AllObjects / QueryObjects / CanTick）+ MoveAll / CollideAll 编排
└─ HUMAN_FIRST_AUTHORING.md

shooter-object-components-poc/
├─ Program.cs                     # 17 项验收断言（纯 .NET，零引擎）
└─ HUMAN_FIRST_AUTHORING.md       # 本文件在共享库侧
```

不要回到以下结构（按技术类型分文件会迫使读者同时扫描整个游戏）：

```text
Components.cs
Actions.cs
Resources.cs
```

按「组合」组织后，改「伤害」主要停留在 `Health` 组件 + `BulletAction`+`MatchController`；跨功能的依赖通过组件、资源或直接调用明说。

---

## 5. 与 ECS 心智的对照

如果你熟悉 Bevy/Friflo 那种 ECS，以下对照能帮你迁移：

| ECS | GameObject + Components |
|---|---|
| `Entity` | `GameObject` |
| `IComponent` / `ITag` | `GameComponent`（**Unity 式**：数据组件 / 行为组件（Action）/ 标记组件三种，见下方说明） |
| `Resource` | `GameWorld` 资源（`AddResource` / `GetResource<T>()`） |
| `System` | Action 组件（`OnTick` 规则；`Move`/`Collide` 阶段动作） |
| `Bundle` | `ShooterFactory`（`SpawnXxx` 一行创建带全套组件） |
| `CommandBuffer` | **无**（创建/命中/死亡都即时、直接；同步销毁） |
| `RunInState(Playing)` | `GameWorld.Paused`（全局冻结，组件不再自查） |
| `Event` | **无**（组件间直接调用，如 `enemy.Health.ApplyDamage`） |
| 阶段/Phase | `RunFrame` 的 Move 阶段 + Collide 阶段 + `world.Tick` |

**⚠️ 一个关键区别：我们的 `GameComponent` 是 Unity 式（数据 + 行为共存），不是 Bevy 的纯数据 `Component`。**

Bevy/Friflo 里 `Component` 是**纯数据 struct**（位置/血量），行为在**独立 `System`** 里。而我们 `GameComponent` 是**带生命周期的 C# 类**，可以同时承担：
- **数据组件**（如 `Position`/`Health`/`MoveSpeed`，只存数据）；
- **行为组件 / Action**（如 `BulletAction`，既有数据又有 `Move`/`Collide`/`OnTick` 行为）；
- **标记组件**（如 `PlayerFaction`/`EnemyFaction`，自动标，无数据）。

这正对齐 **Unity 的 `MonoBehaviour` 模型**（数据 + 行为混合挂在对象上），而非 ECS 的"纯数据 Component + 独立 System"。作者创建时用 `[GameComponent]` 作数据/标记/行为标签，用 `AddComponent<T>()` 挂载；同一种 `GameComponent` 基类承载多种形态。如果你熟悉 Bevy，请把这里的 `Component` 理解成"对象上的一块"（数据或行为），而不是"纯数据字段"。

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
- 可以增加 `MoveSpeedModifier` 状态，再由移动（`Move` 阶段）组合基础参数与修正值。

这就是组合优于实体类型树：**规则围绕组件复用，工厂只负责让常见组合好写。**

---

## 7. 最终检查清单

写新玩法前逐项回答：

- [ ] 我能用一句「输入 → 行为 → 反馈」说清玩法闭环吗？
- [ ] 每份数据的拥有者是对象还是世界（Resource）？
- [ ] 每个组件只属于能力、状态、参数、标签关系中的一个主要类别吗？
- [ ] 配置里是否偷偷混入倒计时、当前位置、累计值？
- [ ] Action 能否写成「读取 A，更新 B」，且类字段没有藏跨帧玩法状态？
- [ ] Action 的 `Requires`（依赖组件）、取的资源、读写的 Owner 组件是否一眼可见？
- [ ] 互斥世界阶段是否用 `GameWorld.Paused` 集中冻结（而非逐帧自查）？
- [ ] 一个 tick 的运动是否由 `RunFrame` 的 Move 阶段（全部移动到本帧终点）→ Collide 阶段（子弹扫掠命中）显式排定，碰撞读的是各方本帧 `PreviousPosition→Position` 线段？
- [ ] 是否仍依赖「谁先执行」？测试里是否覆盖了「子弹先建/敌人后建」的顺序无关场景？
- [ ] 创建长链是否已经提取为 `ShooterFactory`？世界装配是否只从一个 `ShooterGame.Install` 进入？
- [ ] 测试安排是否和游戏装配分开？
- [ ] 新开发者能否先读 `ShooterGame.Install` + `ShooterGame.RunFrame`，再按文件逐层深入？
- [ ] 是否零 Godot/Node/Friflo/Sola3d.Ecs 引用（只 `using Sola3d.GameObject`）？

如果这些问题都有明确答案，代码通常已经接近 Human-first Authoring：概念先于机制，组件先于类型，因果先于样板。
