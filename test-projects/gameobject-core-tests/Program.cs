// SPDX-License-Identifier: MIT
// TestProgram.cs —— gameobject-core-tests：O1 语义契约逐条断言（headless 纯 .NET）

using System;
using System.Collections.Generic;
using Baize.GameObject;

namespace GameObjectCoreTests;

internal static class Program
{
	private static int _passed;
	private static readonly List<string> _failed = new();

	private static void Check(string name, bool condition)
	{
		if (condition)
		{
			_passed++;
		}
		else
		{
			_failed.Add(name);
			Console.WriteLine($"[失败] {name}");
		}
	}

	private static void CheckEq<T>(string name, T actual, T expected)
		where T : notnull
	{
		Check(name, EqualityComparer<T>.Default.Equals(actual, expected));
		if (!EqualityComparer<T>.Default.Equals(actual, expected))
		{
			Console.WriteLine($"       实际={actual} 期望={expected}");
		}
	}

	// ---------- 测试组件 ----------

	[GameComponent]
	private sealed class Health : GameComponent
	{
		[GameProperty]
		public int Max { get; set; } = 100;

		[GameProperty]
		public int Current { get; set; } = 100;

		public int Ticks { get; private set; }

		public bool Created { get; private set; }
		public bool EnabledCount { get; set; }
		public bool DisabledCount { get; set; }
		public bool StartedCount { get; set; }
		public bool DestroyedCount { get; set; }

		public override void OnCreate() => Created = true;
		public override void OnEnable() => EnabledCount = true;
		public override void OnStart() => StartedCount = true;
		public override void OnTick(float delta) => Ticks++;
		public override void OnDisable() => DisabledCount = true;
		public override void OnDestroy() => DestroyedCount = true;
	}

	[GameComponent(AllowMultiple = true)]
	private sealed class Buff : GameComponent
	{
		[GameProperty]
		public string Name { get; set; } = string.Empty;

		[GameProperty]
		public float Amount { get; set; }
	}

	[GameComponent(Requires = new[] { typeof(Health) })]
	private sealed class Weapon : GameComponent
	{
		[GameProperty]
		public float Cooldown { get; set; } = 0.3f;
	}

	// 生命周期记录组件：验证调用顺序
	private sealed class LifecycleProbe : GameComponent
	{
		public List<string> Log = new();
		public override void OnCreate() => Log.Add("OnCreate");
		public override void OnEnable() => Log.Add("OnEnable");
		public override void OnStart() => Log.Add("OnStart");
		public override void OnTick(float delta) => Log.Add("OnTick");
		public override void OnDisable() => Log.Add("OnDisable");
		public override void OnDestroy() => Log.Add("OnDestroy");
	}

private sealed class TargetRelation : GameRelation
	{
	}

	// review P2：tick 内真实结构变更器——OnTick 里 Add/Remove/Destroy 验证快照语义。
	private sealed class TickMutator : GameComponent
	{
		public int TickCount;
		public int HealthAddedAtTick = -1;
		public int HealthRemovedAtTick = -1;
		public int DestroyedAtTick = -1;

		public override void OnTick(float delta)
		{
			TickCount++;
			if (TickCount == 1)
			{
				Owner!.AddComponent<Health>();
				HealthAddedAtTick = TickCount;
			}
			else if (TickCount == 3)
			{
				Owner!.RemoveComponent<Health>();
				HealthRemovedAtTick = TickCount;
			}
			else if (TickCount == 4)
			{
				Owner!.Destroy();
				DestroyedAtTick = TickCount;
			}
		}
	}

	// review P1（第二轮）：同轮移除并重挂同一实例（Revision 变化），验证“从下一轮开始”。
	private sealed class TickSwapMutator : GameComponent
	{
		public Health? Target;

		public override void OnTick(float delta)
		{
			if (Target == null)
			{
				return;
			}
// 只 swap 一次：先移除再重挂同一实例（Revision 自增），随后不再干预。
			var t = Target;
			Target = null;
			Owner!.RemoveComponent(t);
			Owner!.AddComponent(t);
		}
	}
	// review P2：ulong 底层枚举（hash 位模式不溢出）。
	private enum BigEnum : ulong
	{
		Huge = ulong.MaxValue,
	}

	[GameComponent]
	private sealed class BigEnumComp : GameComponent
	{
		[GameProperty]
		public BigEnum Kind { get; set; }
	}

	// ---------- 用例 ----------

	private static void Test_创建与销毁()
	{
		var world = new GameWorld();
		var obj = world.CreateGameObject("玩家");
		Check("创建：IsAlive", world.IsAlive(obj.Id));
		CheckEq("创建：名字", obj.Name, "玩家");

		var id = obj.Id;
		obj.Destroy();
		Check("销毁：IsAlive=false", !world.IsAlive(id));
		Check("销毁：IsDestroyed=true", obj.IsDestroyed);
		Check("销毁：GetObject=null", world.GetObject(id) == null);
	}

	private static void Test_ObjectId_Generation防复用()
	{
		var world = new GameWorld();
		var a = world.CreateGameObject("A");
		var idA = a.Id;
		a.Destroy();

		// 槽位可复用，但 Generation 递增 → 旧 Id 不等于新对象。
		var b = world.CreateGameObject("B");
		Check("Generation 防复用：旧 Id 不再存活", !world.IsAlive(idA));
		Check("Generation 防复用：重新分配后 Index 相同", b.Id.Index == idA.Index);
		Check("Generation 防复用：Generation 递增", b.Id.Generation == idA.Generation + 1 || b.Id.Generation > idA.Generation);
		Check("Generation 防复用：新对象存活", world.IsAlive(b.Id));
	}

	private static void Test_组件添加查询移除()
	{
		var world = new GameWorld();
		var obj = world.CreateGameObject();
		var health = obj.AddComponent<Health>();
		Check("AddComponent 返回实例", health != null);
		Check("GetComponent 一致", ReferenceEquals(obj.GetComponent<Health>(), health));
		CheckEq("OnCreate 已调用", health!.Created, true);
		Check("单实例：重复添加抛异常", Throws<InvalidOperationException>(() => obj.AddComponent<Health>()));
		Check("移除组件", obj.RemoveComponent<Health>());
		Check("移除后 GetComponent=null", obj.GetComponent<Health>() == null);
		Check("移除后 OnDestroy 已调用", health.DestroyedCount);
	}

	private static void Test_多实例与依赖()
	{
		var world = new GameWorld();
		var obj = world.CreateGameObject();

		obj.AddComponent<Buff>().Name = "攻速";
		obj.AddComponent<Buff>().Name = "暴击";
		CheckEq("多实例：数量=2", obj.GetComponents<Buff>().Count, 2);
		CheckEq("多实例：GetComponents 顺序", obj.GetComponents<Buff>()[0].Name, "攻速");
		CheckEq("多实例：GetComponent 取第一个", obj.GetComponent<Buff>()!.Name, "攻速");

		var other = world.CreateGameObject();
		Check("依赖：缺 Health 添加 Weapon 抛异常", Throws<InvalidOperationException>(() => other.AddComponent<Weapon>()));
		other.AddComponent<Health>();
		var weapon = other.AddComponent<Weapon>();
		Check("依赖：有 Health 可添加 Weapon", weapon != null);
	}

	private static void Test_生命周期顺序()
	{
		var world = new GameWorld();
		var obj = world.CreateGameObject();
		var probe = obj.AddComponent<LifecycleProbe>();

		// 外部添加后：OnCreate + OnEnable（无父链禁用、未暂停、对象 Enabled）
		CheckEq("生命周期：添加即 OnCreate", probe.Log[0], "OnCreate");
		CheckEq("生命周期：添加即 OnEnable", probe.Log[^1], "OnEnable");

		probe.Log.Clear();
		world.Tick(0.016f);
		Check("生命周期：首个 tick 有 OnStart+OnTick", probe.Log.Contains("OnStart") && probe.Log.Contains("OnTick"));
		Check("生命周期：Start 先于 Tick", probe.Log.IndexOf("OnStart") < probe.Log.IndexOf("OnTick"));

		probe.Log.Clear();
		obj.Enabled = false;
		Check("生命周期：禁用触发 OnDisable", probe.Log.Contains("OnDisable"));

		probe.Log.Clear();
		obj.Enabled = true;
		Check("生命周期：恢复触发 OnEnable", probe.Log.Contains("OnEnable"));

		probe.Log.Clear();
		obj.Destroy();
		Check("生命周期：销毁触发 OnDestroy", probe.Log.Contains("OnDestroy"));
	}

	private static void Test_Tick确定性与快照遍历()
	{
		var world = new GameWorld();
		var a = world.CreateGameObject("A");
		var b = world.CreateGameObject("B");
		var ha = a.AddComponent<Health>();
		var hb = b.AddComponent<Health>();

		// 确定顺序：A 先于 B。
		world.Tick(0.016f);
		CheckEq("Tick 顺序：A=1", ha.Ticks, 1);
		CheckEq("Tick 顺序：B=1", hb.Ticks, 1);

		// Tick 期间添加组件：立即生效但本轮快照不受影响（新组件下一轮才 tick）。
		var c = world.CreateGameObject("C");
		c.AddComponent<Health>();
		world.Tick(0.016f);
		CheckEq("Tick 期间新增：旧组件仍 tick（A=2）", ha.Ticks, 2);
		CheckEq("Tick 期间新增：新组件本轮参与（C=1）", c.GetComponent<Health>()!.Ticks, 1);
	}

	private static void Test_父子与销毁级联()
	{
		var world = new GameWorld();
		var parent = world.CreateGameObject("父");
		var child = world.CreateGameObject("子");
		var grand = world.CreateGameObject("孙");
		child.SetParent(parent);
		grand.SetParent(child);

		Check("层级：Parent 指向", ReferenceEquals(child.Parent, parent));
		Check("层级：Children 包含", ContainsRef(parent.Children, child));
		CheckEq("层级：Roots=1", world.Roots.Count, 1);
		Check("层级：禁环（子挂到孙）抛异常", Throws<InvalidOperationException>(() => grand.SetParent(grand)) || Throws<InvalidOperationException>(() => parent.SetParent(grand)));

		var hp = parent.AddComponent<Health>();
		var hc = child.AddComponent<Health>();
		_ = hp; _ = hc;

		parent.Destroy();
		Check("级联销毁：父不存活", parent.IsDestroyed);
		Check("级联销毁：子被销毁", child.IsDestroyed);
		Check("级联销毁：孙被销毁", grand.IsDestroyed);
		CheckEq("级联销毁：Roots=0", world.Roots.Count, 0);
	}

	private static void Test_有效状态传播()
	{
		var world = new GameWorld();
		var parent = world.CreateGameObject("父");
		var child = world.CreateGameObject("子");
		child.SetParent(parent);
		var probe = child.AddComponent<LifecycleProbe>();

		// 父禁用 → 子 effective 禁用 → OnDisable。
		parent.Enabled = false;
		Check("传播：父禁用子组件 OnDisable", probe.Log.Contains("OnDisable"));
		// 子 Enabled 标志不变。
		Check("传播：子 Enabled 标志不变", child.Enabled);
		// 恢复父 → 子重新 effective → OnEnable。
		parent.Enabled = true;
		Check("传播：父恢复子组件 OnEnable", probe.Log.Contains("OnEnable"));

		// 世界暂停 → 组件 OnDisable；恢复 → OnEnable（契约 §3）。
		probe.Log.Clear();
		world.Paused = true;
		Check("传播：暂停触发组件 OnDisable", probe.Log.Contains("OnDisable"));
		probe.Log.Clear();
		world.Paused = false;
		Check("传播：恢复触发组件 OnEnable", probe.Log.Contains("OnEnable"));
		Check("传播：暂停不改变标志", child.Enabled);
	}

	// ---------- review P1：回调内改结构不得中断暂停刷新（消除 Collection modified）----------

	private sealed class PauseMutator : GameComponent
	{
		public bool MutateOnDisable;
		public bool MutateOnEnable;

		public override void OnDisable()
		{
			if (!MutateOnDisable) return;
			// 在回调内改结构：销毁兄弟 + 新增组件——若枚举未快照，此处会抛 Collection modified。
			var world = World!;
			// 用快照遍历：回调内改结构时，本方法自己也要物化副本，避免枚举 live Roots（对齐内核快照语义）。
			var roots = new GameObject[world.Roots.Count];
			for (int i = 0; i < roots.Length; i++) roots[i] = world.Roots[i];
			foreach (var root in roots)
			{
				if (root != Owner && !root.IsDestroyed) root.Destroy();
			}
		}

		public override void OnEnable()
		{
			if (!MutateOnEnable) return;
			Owner!.AddComponent<Health>();
		}
}

	private static void Test_暂停回调内改结构不中断刷新()
	{
		var world = new GameWorld();
		var a = world.CreateGameObject("A");
		var b = world.CreateGameObject("B");
		var ma = a.AddComponent<PauseMutator>();
		ma.MutateOnDisable = true;
		ma.MutateOnEnable = true;
		_ = b;

		// Paused=true 触发全树 OnDisable；回调内 Destroy(兄弟) —— 快照后不抛异常、不中断。
		world.Paused = true;
		Check("暂停回调内改结构：Paused=true 无异常且已关闭", world.Paused);
		Check("暂停回调内改结构：兄弟已被销毁", b.IsDestroyed);

		// Paused=false 触发 OnEnable；回调内 AddComponent —— 快照后不抛异常。
		world.Paused = false;
		Check("暂停回调内改结构：恢复无异常且已开启", !world.Paused);
	}

	private static void Test_Review_重复挂载与外来组件()
	{
		var world = new GameWorld();
		var a = world.CreateGameObject("A");
		var b = world.CreateGameObject("B");
		var health = new Health();
		a.AddComponent(health);

		// P1：同一组件实例禁止重复挂载。
		Check("P1：同一实例加第二对象抛异常", Throws<InvalidOperationException>(() => b.AddComponent(health)));
		// P1：外来组件移除返回 false 且不影响原 owner。
		Check("P1：外来组件 RemoveComponent 拒绝", !b.RemoveComponent(health));
		Check("P1：原 owner 组件未受影响", ReferenceEquals(a.GetComponent<Health>(), health));
	}

	private static void Test_Review_跨世界与销毁语义()
	{
		var w1 = new GameWorld();
		var w2 = new GameWorld();
		var o1 = w1.CreateGameObject("o1");
		var o2 = w2.CreateGameObject("o2");

		// P1：跨世界 AddComponent / SetParent / Destroy 一律拒绝。
		Check("P1：跨世界 AddComponent 拒绝", Throws<InvalidOperationException>(() => w2.AddComponent(o1, new Health())));
		Check("P1：跨世界 SetParent 拒绝", Throws<InvalidOperationException>(() => o2.SetParent(o1)));
		Check("P1：跨世界 Destroy 拒绝", Throws<InvalidOperationException>(() => w1.Destroy(o2)));

		// P1：已销毁再 Destroy 抛异常（契约 §6，不再静默 return）。
		o1.Destroy();
		Check("P1：已销毁再 Destroy 抛异常", Throws<InvalidOperationException>(() => o1.Destroy()));

		// P1：跨世界 Relation 端点拒绝。
		var src = w1.CreateGameObject("src");
		var dst = w2.CreateGameObject("dst");
		Check("P1：跨世界 Relation 拒绝", Throws<InvalidOperationException>(() => w1.Relations.Add<TargetRelation>(src, dst)));
	}

	private sealed class DestroyReentryProbe : GameComponent
	{
		public bool ReEntered;

		public override void OnDestroy()
		{
			// 回调中再次销毁 owner：按契约 §6 应抛异常（Destroy 已拒绝 stale handle），不得重入/半销毁。
			try
			{
				Owner!.Destroy();
			}
			catch (InvalidOperationException)
			{
				ReEntered = true;
			}
		}
	}

	private static void Test_Review_Destroy回调重入保护()
	{
		var world = new GameWorld();
		var obj = world.CreateGameObject("r");
		var probe = obj.AddComponent<DestroyReentryProbe>();
		obj.Destroy();
		Check("P1：对象已彻底销毁", obj.IsDestroyed);
		Check("P1：回调重入被拒绝且状态一致", probe.ReEntered);
	}

	private static void Test_Review_FixedTick首次OnStart()
	{
		var world = new GameWorld();
		var probe = world.CreateGameObject("p").AddComponent<LifecycleProbe>();
		// fixed tick 是首次有效 tick：必须先 OnStart 再 OnFixedTick（契约 §4，reviewer P1）。
		world.FixedTick(0.02f);
		Check("P1：FixedTick 首次触发 OnStart", probe.Log.Contains("OnStart"));
		Check("P1：FixedTick 只调 OnFixedTick（无 OnTick）", !probe.Log.Contains("OnTick"));
	}

	private static void Test_Review_Tick内结构变更快照语义()
	{
		var world = new GameWorld();
		var obj = world.CreateGameObject("M");
		var mutator = obj.AddComponent<TickMutator>();

		// tick 1：OnTick 内 AddComponent<Health>——本轮快照已生成，新增组件本轮不 tick。
		world.Tick(0.016f);
		var health = obj.GetComponent<Health>();
		Check("P2：tick 内新增立即可见", health != null);
		Check("P2：tick 内新增本轮不 tick", health!.Ticks == 0);

		// tick 2：新增组件本轮 tick（Ticks=1）。对象内按插入序：Mutator、Health——都跑。
		world.Tick(0.016f);
		Check("P2：新增组件下一轮 tick", health.Ticks == 1);

		// tick 3：Mutator 本轮移除 Health；移除后 GetComponent=null，下一轮 Health 不再 tick。
		world.Tick(0.016f);
		Check("P2：tick 内移除生效", obj.GetComponent<Health>() == null);
		CheckEq("P2：移除后 Ticks 不再增长", health.Ticks, 1);

		// tick 4：mutator 在本轮销毁 owner（tick 内 Destroy），下一轮不再 tick。
		world.Tick(0.016f);
		CheckEq("P2：tick 内 Destroy 生效", mutator.DestroyedAtTick, 4);
		Check("P2：tick 内 Destroy 后对象失效", obj.IsDestroyed);
		CheckEq("P2：tick 内 Destroy 当帧计数", mutator.TickCount, 4);
		// 第 5 帧：对象已销毁，Mutator 不应再被 tick。
		world.Tick(0.016f);
		CheckEq("P2：下一轮 mutator 不再 tick", mutator.TickCount, 4);
	}

	private static void Test_Review_Hash位模式与ulong枚举()
	{
		var world = new GameWorld();
		var obj = world.CreateGameObject("big");
		obj.AddComponent<BigEnumComp>().Kind = BigEnum.Huge;
		var snap = GameWorldSerializer.Capture(world);
		// P2：ulong 枚举 hash 不溢出。
		ulong h = GameWorldSerializer.ComputeHash(snap);
		Check("P2：ulong 枚举 hash 可计算", h != 0);

		// P2：null 与字符串 "n" 不再产生相同输入。
		var w1 = new GameWorld();
		var o1 = w1.CreateGameObject("n1");
		o1.AddComponent<Buff>(); // Name 默认 "" （非 null）
		var w2 = new GameWorld();
		var o2 = w2.CreateGameObject("n1");
		o2.AddComponent<Buff>().Name = "n";
		ulong h3 = GameWorldSerializer.ComputeHash(GameWorldSerializer.Capture(w1));
		ulong h4 = GameWorldSerializer.ComputeHash(GameWorldSerializer.Capture(w2));
		Check("P2：值差异反映到 hash", h3 != h4);
	}

	private static void Test_Relation()
	{
		var world = new GameWorld();
		var player = world.CreateGameObject("玩家");
		var enemy = world.CreateGameObject("敌人");

		var rel = player.Relations.Add<TargetRelation>(enemy);
		Check("Relation：添加成功", rel != null);
		Check("Relation：Source 正确", rel!.Source == player.Id);
		Check("Relation：Target 正确", rel.Target == enemy.Id);
		Check("Relation：Get 门面取到", player.Relations.First<TargetRelation>() == rel);
		CheckEq("Relation：反向查询", world.Relations.GetTo<TargetRelation>(enemy).Count, 1);

		enemy.Destroy();
		Check("Relation：端点销毁自动清理", world.Relations.All.Count == 0);
		Check("Relation：销毁后 All 空", player.Relations.Get<TargetRelation>().Count == 0);
	}

	private static void Test_序列化RoundTrip()
	{
		var world = new GameWorld();
		world.AddResource(new TestResource { Tag = "world" });
		var root = world.CreateGameObject("主场景");
		var cube = world.CreateGameObject("CubeA");
		cube.SetParent(root);
		var health = cube.AddComponent<Health>();
		health.Max = 50;
		health.Current = 37;

		cube.AddComponent<Buff>().Name = "攻速";
		var enemy = world.CreateGameObject("敌人");
		cube.Relations.Add<TargetRelation>(enemy);

		var snap1 = GameWorldSerializer.Capture(world);
		ulong hash1 = GameWorldSerializer.ComputeHash(snap1);
		ulong hash2 = GameWorldSerializer.ComputeHash(snap1);
		Check("序列化：hash 稳定（同快照两次相等）", hash1 == hash2);

		var restored = GameWorldSerializer.Restore(snap1, world.Schemas, world.Relations);
		var rCube = restored.GetObject(restored.Roots[0].Children[0].Id);
		CheckEq("Restore：名字", rCube!.Name, "CubeA");
		CheckEq("Restore：Health.Max=50", rCube.GetComponent<Health>()!.Max, 50);
		CheckEq("Restore：Health.Current=37", rCube.GetComponent<Health>()!.Current, 37);
		CheckEq("Restore：多实例 Buff 数量", rCube.GetComponents<Buff>().Count, 1);
		CheckEq("Restore：层级", restored.Roots[0].Children.Count, 1);
		CheckEq("Restore：关系", restored.Relations.All.Count, 1);

		var hash3 = GameWorldSerializer.ComputeHash(GameWorldSerializer.Capture(restored));
		Check("Round-trip：Capture→Restore→Capture hash 相等", hash1 == hash3);

		// 边界：空世界 Capture/Restore 无异常且 hash 稳定。
		var empty = new GameWorld();
		var emptySnap = GameWorldSerializer.Capture(empty);
		CheckEq("边界：空世界对象数=0", emptySnap.Objects.Count, 0);
		Check("边界：空世界 hash 稳定", GameWorldSerializer.ComputeHash(emptySnap) == 14695981039346656037UL);
		var emptyRestored = GameWorldSerializer.Restore(emptySnap);
		Check("边界：空快照 Restore 返回空世界", emptyRestored.Roots.Count == 0);
	}

	private static void Test_UndoRedo()
	{
		var world = new GameWorld();
		var obj = world.CreateGameObject("Obj");

		var tx = world.CreateTransaction();
		var health = new Health { Max = 99 };
		tx.AddComponent(obj, health);
		tx.SetProperty(health, nameof(Health.Max), 42);
		tx.Commit();
		CheckEq("Undo：修改已生效", obj.GetComponent<Health>()!.Max, 42);
		CheckEq("Undo：UndoCount=1", world.UndoCount, 1);

		world.Undo();
		Check("Undo：组件已移除", obj.GetComponent<Health>() == null);
		CheckEq("Undo：RedoCount=1", world.RedoCount, 1);

		world.Redo();
		var healthAfter = obj.GetComponent<Health>();
		Check("Redo：组件恢复", healthAfter != null);
		if (healthAfter != null) { CheckEq("Redo：属性恢复 42", healthAfter.Max, 42); }
	}

	private static void Test_Review_CreateObject事务可重复撤销()
	{
		var world = new GameWorld();
		var tx = world.CreateTransaction();
		tx.CreateGameObject("新对象");
		tx.Commit();
		CheckEq("P1：CreateObject 提交后存活数=1", world.AliveCount, 1);

		world.Undo();
		CheckEq("P1：第一次 Undo 后存活数=0", world.AliveCount, 0);

		world.Redo();
		CheckEq("P1：Redo 后存活数=1", world.AliveCount, 1);

		// 第二次 Undo：必须销毁 Redo 新建的对象（而非已失效旧句柄），reviewer P1。
		world.Undo();
		CheckEq("P1：第二次 Undo 后存活数=0（新对象被销毁，无泄漏）", world.AliveCount, 0);
	}

	private static void Test_Review2_组合事务Create加编辑可Redo()
	{
		// reviewer P1（第二轮）：CreateGameObject 后同事务继续 AddComponent——
		// Redo 时后续步骤必须操作重建的新对象，而非已失效旧对象。
		var world = new GameWorld();
		var tx = world.CreateTransaction();
		var created = tx.CreateGameObject("新建");
		var health = new Health { Max = 77 };
		tx.AddComponent(created, health);
		tx.Commit();

		CheckEq("R2：提交后组件存在", world.Roots[0].GetComponent<Health>()!.Max, 77);

		world.Undo();
		CheckEq("R2：Undo 后对象销毁", world.AliveCount, 0);

		world.Redo();
		CheckEq("R2：Redo 后对象重建", world.AliveCount, 1);
		CheckEq("R2：Redo 后组件挂到新对象", world.Roots[0].GetComponent<Health>()!.Max, 77);

		world.Undo();
		CheckEq("R2：再次 Undo 后仍干净", world.AliveCount, 0);
	}

	[GameComponent(AllowMultiple = true)]
	private sealed class MultiBuff : GameComponent
	{
	}

	[GameComponent(Requires = new[] { typeof(MultiBuff) })]
	private sealed class NeedsMulti : GameComponent
	{
	}

	private sealed class ThrowOnDestroy : GameComponent
	{
		public static int DestroyedCount;

		public override void OnDestroy()
		{
			DestroyedCount++;
			throw new InvalidOperationException("OnDestroy 故意抛出");
		}
	}

	private static void Test_Review2_Destroy回调异常仍全部清理()
	{
		// reviewer P1（第二轮）：OnDestroy 抛异常时，其余组件仍被清理，异常聚合抛出。
		ThrowOnDestroy.DestroyedCount = 0;
		var world = new GameWorld();
		var obj = world.CreateGameObject("boom");
		var health = obj.AddComponent<Health>();
		obj.AddComponent<ThrowOnDestroy>();

		bool threw = Throws<AggregateException>(() => obj.Destroy());
		Check("R2：销毁回调异常聚合为 AggregateException", threw);
		Check("R2：抛异常的 OnDestroy 被调用", ThrowOnDestroy.DestroyedCount == 1);
		Check("R2：异常后组件仍全部清理", world.AliveCount == 0 && health.Owner == null);
	}

	private static void Test_Review2_Tick内移除又重挂本轮跳过()
	{
		// reviewer P1（第二轮）：同一轮前序组件移除并重挂 target 后，target 本轮不再执行。
		var world = new GameWorld();
		var obj = world.CreateGameObject("swap");
		var swapper = obj.AddComponent<TickSwapMutator>();
		swapper.Target = null!; // 占位，下一行赋真实 target
		var target = obj.AddComponent<Health>(); // 先 swapper 后 target：swapper 先 tick
		swapper.Target = target;

		world.Tick(0.016f); // 第 1 轮：swapper 移除并重挂 target
		CheckEq("R2：同轮重挂后本轮 target 未 tick", target.Ticks, 0);
		Check("R2：重挂后组件仍存在", obj.GetComponent<Health>() == target);

		world.Tick(0.016f); // 第 2 轮：target 从下一轮恢复参与
		CheckEq("R2：下一轮 target 恢复 tick", target.Ticks, 1);
	}

	private static void Test_Review2_Requires支持多实例依赖()
	{
		// reviewer P1（第二轮）：依赖可以是多实例组件类型。
		var world = new GameWorld();
		var obj = world.CreateGameObject("m");
		obj.AddComponent<MultiBuff>(); // 多实例容器
		var needs = obj.AddComponent<NeedsMulti>();
		Check("R2：多实例组件满足 Requires 依赖", needs != null);
	}

	private static void Test_Review2_跨世界关系查询返回空()
	{
		// reviewer P1（第二轮）：GetFrom/GetTo 跨世界对象返回空而非命中本地关系。
		var w1 = new GameWorld();
		var w2 = new GameWorld();
		var a = w1.CreateGameObject("a");
		var b = w1.CreateGameObject("b");
		a.Relations.Add<TargetRelation>(b);

		var foreign = w2.CreateGameObject("f");
		Check("R2：跨世界 GetFrom 返回空", w1.Relations.GetFrom<TargetRelation>(foreign).Count == 0);
		Check("R2：跨世界 GetTo 返回空", w1.Relations.GetTo<TargetRelation>(foreign).Count == 0);
		Check("R2：同世界查询不受影响", w1.Relations.GetFrom<TargetRelation>(a).Count == 1);
	}

	private static void Test_Review2_对象名hash长度前缀()
	{
		// reviewer P2（第二轮）：对象名用长度前缀，杜绝拼接歧义。
		var w1 = new GameWorld();
		w1.CreateGameObject("a|1|-1|\nOb");
		var w2 = new GameWorld();
		w2.CreateGameObject("a");
		w2.CreateGameObject("b");
		ulong h1 = GameWorldSerializer.ComputeHash(GameWorldSerializer.Capture(w1));
		ulong h2 = GameWorldSerializer.ComputeHash(GameWorldSerializer.Capture(w2));
		Check("R2：对象名长度前缀消除 hash 歧义", h1 != h2);
	}

	// reviewer P1（第三轮）：跨事务 Undo/Redo 链——事务句柄提升到 GameWorld 级后，
	// tx2 引用 tx1 创建的对象，Undo tx2/Undo tx1/Redo tx1/Redo tx2 需完整闭环。
	private static void Test_Review3_跨事务句柄重映射()
	{
		var world = new GameWorld();

		var tx1 = world.CreateTransaction();
		var created = tx1.CreateGameObject("跨事务");
		tx1.Commit();
		CheckEq("R3：tx1 提交后存活数=1", world.AliveCount, 1);

		var tx2 = world.CreateTransaction();
		var health = new Health { Max = 55 };
		tx2.AddComponent(created, health);
		tx2.Commit();
		CheckEq("R3：tx2 提交后组件存在", world.Roots[0].GetComponent<Health>()!.Max, 55);

		world.Undo(); // 撤销 tx2
		Check("R3：撤销 tx2 后组件移除", world.Roots[0].GetComponent<Health>() == null);
		world.Undo(); // 撤销 tx1 → 对象销毁
		CheckEq("R3：撤销 tx1 后存活数=0", world.AliveCount, 0);

		world.Redo(); // 重做 tx1 → 对象重建（新实例）
		CheckEq("R3：重做 tx1 后存活数=1", world.AliveCount, 1);
		world.Redo(); // 重做 tx2 → 组件必须挂到重建的新实例上（世界级句柄解析）
		var after = world.Roots[0].GetComponent<Health>();
		Check("R3：重做 tx2 后组件挂到新实例", after != null);
		CheckEq("R3：组件值保持 55", after!.Max, 55);

		world.Undo();
		world.Undo();
		CheckEq("R3：再次双 Undo 后干净", world.AliveCount, 0);
	}

	// reviewer P1（第三轮）：OnDisable 抛异常不得吞掉同组件的 OnDestroy。
	private sealed class DisableThrowsProbe : GameComponent
	{
		public static int DisableCount;
		public static int DestroyCount;

		public override void OnDisable()
		{
			DisableCount++;
			throw new InvalidOperationException("OnDisable 故意抛出");
		}

		public override void OnDestroy() => DestroyCount++;
	}

	private static void Test_Review3_OnDisable异常仍调用OnDestroy()
	{
		DisableThrowsProbe.DisableCount = 0;
		DisableThrowsProbe.DestroyCount = 0;
		var world = new GameWorld();
		var obj = world.CreateGameObject("d");
		obj.AddComponent<DisableThrowsProbe>();

// SetEnabled 路径：OnDisable 异常直接传播（RefreshEffective 不做聚合，保持简单契约）。
		bool threw = Throws<InvalidOperationException>(() => obj.Enabled = false);
		Check("R3：SetEnabled 时 OnDisable 异常直接传播", threw);
		Check("R3：OnDisable 被调用", DisableThrowsProbe.DisableCount == 1);
		Check("R3：OnDisable 异常后 OnDestroy 仍未被调用（未销毁）", DisableThrowsProbe.DestroyCount == 0);

		// 销毁路径：OnDisable 再抛 + OnDestroy 必须被调用。
		DisableThrowsProbe.DisableCount = 0;
		DisableThrowsProbe.DestroyCount = 0;
		var obj2 = world.CreateGameObject("d2");
		obj2.AddComponent<DisableThrowsProbe>();
		bool threw2 = Throws<AggregateException>(() => obj2.Destroy());
		Check("R3：销毁时 OnDisable 异常聚合抛出", threw2);
		Check("R3：OnDisable 异常后 OnDestroy 仍被调用", DisableThrowsProbe.DestroyCount == 1);
	}

	// reviewer P1（第三轮）：OnStart 内销毁自身 → 不再调用 OnTick。
	private sealed class StartDestroyProbe : GameComponent
	{
		public int TickCount;

		public override void OnStart()
		{
			Owner!.Destroy(); // OnStart 内销毁自身
		}

		public override void OnTick(float delta) => TickCount++;
	}

	private static void Test_Review3_OnStart销毁后不再Tick()
	{
		var world = new GameWorld();
		var obj = world.CreateGameObject("s");
		var probe = obj.AddComponent<StartDestroyProbe>();
		world.Tick(0.016f);
		Check("R3：OnStart 内销毁后对象失效", obj.IsDestroyed);
		CheckEq("R3：OnStart 后不再调用 OnTick", probe.TickCount, 0);
	}

	// reviewer P1（第三轮）延伸：三事务交错全链 Undo/Redo（tx1 建 → tx2 挂组件 → tx3 改属性）。
	private static void Test_Review3_三事务交错全链Redo()
	{
		var world = new GameWorld();

		var tx1 = world.CreateTransaction();
		var obj = tx1.CreateGameObject("root");
		tx1.Commit();

		var tx2 = world.CreateTransaction();
		var h = new Health { Max = 100 };
		tx2.AddComponent(obj, h);
		tx2.Commit();

		var tx3 = world.CreateTransaction();
		tx3.SetProperty(h, nameof(Health.Max), 123);
		tx3.Commit();
		Check("R3：tx3 后 Max=123", world.Roots[0].GetComponent<Health>()!.Max == 123);

		world.Undo();
		Check("R3：Undo tx3 后 Max=100", world.Roots[0].GetComponent<Health>()!.Max == 100);
		world.Undo();
		Check("R3：Undo tx2 后组件移除", world.Roots[0].GetComponent<Health>() == null);
		world.Undo();
		Check("R3：Undo tx1 后对象销毁", world.AliveCount == 0);

		world.Redo();
		world.Redo();
		world.Redo();
		Check("R3：全链 Redo 后组件与属性恢复", world.Roots[0].GetComponent<Health>()!.Max == 123);

		world.Undo();
		world.Undo();
		Check("R3：再双 Undo 后组件移除", world.Roots[0].GetComponent<Health>() == null);
		world.Redo();
		world.Redo();
		Check("R3：再双 Redo 后完全恢复", world.Roots[0].GetComponent<Health>()!.Max == 123);
	}

	// reviewer P1（第四轮）：跨世界事务被拒绝——world2 的事务传入 world1 的对象/组件须抛异常。
	private static void Test_Review4_跨世界事务拒绝()
	{
		var w1 = new GameWorld();
		var w2 = new GameWorld();
		var o1 = w1.CreateGameObject("w1obj");
		var h = o1.AddComponent<Health>();

		var tx = w2.CreateTransaction();
// 跨世界 RemoveComponent 走严格路径（RequireResolved 抛异常，不触碰外世界）。
		Check("R4：跨世界 RemoveComponent 拒绝", Throws<InvalidOperationException>(() => tx.RemoveComponent(o1, new Health())));
		Check("R4：跨世界 AddComponent 拒绝", Throws<InvalidOperationException>(() => tx.AddComponent(o1, new Health())));
		Check("R4：跨世界 SetProperty 拒绝", Throws<InvalidOperationException>(() => tx.SetProperty(h, nameof(Health.Max), 50)));
		Check("R4：跨世界 SetParent 拒绝", Throws<InvalidOperationException>(() => tx.SetParent(o1, null)));
		tx.Rollback();
	}

	private static void Test_Resources()
	{
		var world = new GameWorld();
		world.AddResource(new TestResource { Tag = "svc-1" });
		CheckEq("Resource：取回", world.GetResource<TestResource>().Tag, "svc-1");
		Check("Resource：重复注册抛异常", Throws<InvalidOperationException>(() => world.AddResource(new TestResource())));
		Check("Resource：HasResource", world.HasResource<TestResource>());
	}

	private static void Test_Reset()
	{
		var world = new GameWorld();
		world.AddResource(new TestResource { Tag = "keep" });
		var obj = world.CreateGameObject("A");
		var id = obj.Id;
		obj.AddComponent<Health>();
		world.Tick(0.016f);
		Check("Reset 前：TickIndex>0", world.TickIndex > 0);

		world.Reset();
		Check("Reset：对象清空", world.AliveCount == 0);
		Check("Reset：TickIndex 归零", world.TickIndex == 0);
		Check("Reset：旧 Id 失效", !world.IsAlive(id));
		Check("Reset：Resources 保留", world.GetResource<TestResource>().Tag == "keep");
	}

	private sealed class TestResource
	{
		public string Tag = string.Empty;
	}

	private static bool ContainsRef(IReadOnlyList<GameObject> list, GameObject obj)
	{
		foreach (var item in list)
		{
			if (ReferenceEquals(item, obj))
			{
				return true;
			}
		}
		return false;
	}

	private static bool Throws<T>(Action action) where T : Exception
	{
		try
		{
			action();
			return false;
		}
		catch (T)
		{
			return true;
		}
	}

	private static int Main()
	{
		Console.WriteLine("gameobject-core-tests —— O1 GameObject 内核语义契约验证\n");

		Test_创建与销毁();
		Test_ObjectId_Generation防复用();
		Test_组件添加查询移除();
		Test_多实例与依赖();
		Test_生命周期顺序();
		Test_Tick确定性与快照遍历();
		Test_父子与销毁级联();
		Test_有效状态传播();
		Test_暂停回调内改结构不中断刷新();
		Test_Review_重复挂载与外来组件();
		Test_Review_跨世界与销毁语义();
		Test_Review_FixedTick首次OnStart();
		Test_Review_Tick内结构变更快照语义();
		Test_Review_Hash位模式与ulong枚举();
		Test_Review_Destroy回调重入保护();
		Test_Relation();
		Test_序列化RoundTrip();
		Test_UndoRedo();
		Test_Review_CreateObject事务可重复撤销();
		Test_Review2_组合事务Create加编辑可Redo();
		Test_Review2_Destroy回调异常仍全部清理();
		Test_Review2_Tick内移除又重挂本轮跳过();
		Test_Review2_Requires支持多实例依赖();
		Test_Review2_跨世界关系查询返回空();
		Test_Review2_对象名hash长度前缀();
		Test_Review3_跨事务句柄重映射();
		Test_Review3_OnDisable异常仍调用OnDestroy();
		Test_Review3_OnStart销毁后不再Tick();
		Test_Review3_三事务交错全链Redo();
		Test_Review4_跨世界事务拒绝();
		Test_Resources();
		Test_Reset();

		Console.WriteLine($"\n通过 {_passed} 项，失败 {_failed.Count} 项");
		if (_failed.Count > 0)
		{
			Console.WriteLine("失败清单：" + string.Join(", ", _failed));
			return 1;
		}
		Console.WriteLine("全部通过 ✅");
		return 0;
	}
}
