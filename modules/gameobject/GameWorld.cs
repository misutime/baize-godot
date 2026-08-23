// SPDX-License-Identifier: MIT
// GameWorld.cs —— 纯运行时世界（O1，方案 §4.6/§14.5 / 契约全篇）
//
// 纯 .NET、可测试、可服务器复用；不连接 Node、不连接编辑器（O5 才做 BaizeMainLoop/Host）。
// 职责：对象 registry（EntityId 槽位 + Generation）、层级、关系、组件生命周期调度、
//       variable/fixed tick、删除（同步，契约 §5）、services 端口。

using System;
using System.Collections.Generic;

namespace Baize.GameObject;

/// <summary>纯运行时游戏世界（Headless GameObject Kernel）。</summary>
	/// 线程亲和（reviewer P3）：单线程模型——Tick/Paused/结构操作均无锁访问内部集合，
	/// 服务器并发复用前需单线程命令队列或统一同步边界。
public sealed class GameWorld
{
	private readonly List<GameObject?> _slots = new();          // index → 存活对象（null = 槽位空闲）
	private readonly List<uint> _generations = new();            // index → 当前 Generation
	private readonly Stack<int> _freeIndices = new();            // 可复用槽位
	private readonly ObjectHierarchy _hierarchy = new();
	private readonly RelationGraph _relations;
	// 注：_relations 在构造函数中 = new RelationGraph(this)（注入所属世界，reviewer P1）
	private readonly ComponentSchemaRegistry _schemas = new();
	private readonly List<GameComponent> _tickOrder = new();     // 全局组件注册序（对象创建序 → 组件插入序）
	private readonly Dictionary<GameObject, bool> _enabled = new();
	private readonly Dictionary<GameObject, ComponentStore> _stores = new();
	private readonly Dictionary<Type, object> _services = new();
	private uint _revisionCounter;
	private uint _creationCounter; // 对象创建序号（tick 顺序 = 对象创建序 → 组件插入序，契约 §4）
	private long _nextTransactionId = 1;
	private readonly Dictionary<long, GameObject?> _transactionObjects = new(); // 事务逻辑句柄 → 当前实例（跨事务重映射，reviewer P1 第三轮）
	/// <summary>固定步长（秒）。</summary>
	public float FixedDelta { get; }
	private bool _paused;

	/// <summary>是否暂停（暂停时所有组件停 tick；OnEnable/OnDisable 随 effective 翻转，契约 §3）。</summary>
	public bool Paused
	{
		get => _paused;
		set
		{
			if (_paused == value)
			{
				return;
			}
			_paused = value;
			// 暂停/恢复会翻转全树 effective 状态 → 触发 OnDisable/OnEnable（契约 §3）。
			foreach (var root in _hierarchy.Roots)
			{
				RefreshEffective(root);
			}
		}
	}

	/// <summary>当前 variable tick 序号。</summary>
	public ulong TickIndex { get; private set; }

	/// <summary>当前 fixed tick 序号。</summary>
	public ulong FixedTickIndex { get; private set; }

	/// <summary>存活对象数。</summary>
	public int AliveCount
	{
		get
		{
			int count = 0;
			foreach (var slot in _slots)
			{
				if (slot != null)
				{
					count++;
				}
			}
			return count;
		}
	}

	/// <summary>对象层级（parent/children；契约 §7）。</summary>
	public ObjectHierarchy Hierarchy => _hierarchy;

	/// <summary>关系图（契约 §9）。</summary>
	public RelationGraph Relations => _relations;

	/// <summary>组件 Schema 注册表（契约 §1/§2/§10）。</summary>
	public ComponentSchemaRegistry Schemas => _schemas;

	/// <summary>顶层对象（插入序）。</summary>
	public IReadOnlyList<GameObject> Roots => _hierarchy.Roots;

	public GameWorld(float fixedDelta = 0.02f)
	{
		FixedDelta = fixedDelta;
		_relations = new RelationGraph(this); // 绑定所属世界（reviewer P1）
	}

	/// <summary>创建空 GameObject（顶层）。返回对象；开发者可再 SetParent。</summary>
	public GameObject CreateGameObject(string name = "")
	{
		int index = AllocateIndex();
		var id = new EntityId(index, _generations[index]);
		var obj = new GameObject(this, id, name)
		{
			CreationIndex = ++_creationCounter,
		};
		_slots[index] = obj;
		_enabled.Add(obj, true);
		_store(obj);
		_hierarchy.Register(obj);
		return obj;
	}

	/// <summary>注册事务创建对象：分配稳定逻辑句柄（跨事务重映射用，reviewer P1 第三轮）。</summary>
	internal long RegisterTransactionObject(GameObject obj)
	{
		long id = _nextTransactionId++;
		obj.TransactionId = id;
		_transactionObjects[id] = obj;
		return id;
	}

	/// <summary>按稳定句柄取当前存活实例（Redo 重建后返回新实例；未注册/已销毁返回 null）。</summary>
	internal GameObject? GetTransactionObject(long id)
	{
		if (!_transactionObjects.TryGetValue(id, out var obj))
		{
			return null;
		}
		return obj != null && !obj.IsDestroyed ? obj : null;
	}

	/// <summary>Redo 重建：把同句柄映射到新实例（覆盖旧值，支撑跨事务解析）。</summary>
	internal void RemapTransactionObject(long id, GameObject newInstance)
	{
		newInstance.TransactionId = id;
		_transactionObjects[id] = newInstance;
	}

	/// <summary>对象销毁时清理句柄映射（防陈旧引用）。</summary>
	private void UnregisterTransactionObject(GameObject obj)
	{
		if (obj.TransactionId > 0)
		{
			_transactionObjects.Remove(obj.TransactionId);
		}
	}
	private int AllocateIndex()
	{
		if (_freeIndices.Count > 0)
		{
			return _freeIndices.Pop();
		}
		int index = _slots.Count;
		_slots.Add(null);
		_generations.Add(0);
		return index;
	}

	private ComponentStore _store(GameObject obj)
	{
		if (!_stores.TryGetValue(obj, out var store))
		{
			store = new ComponentStore();
			_stores.Add(obj, store);
		}
		return store;
	}

	/// <summary>身份是否存活（Index 槽位有效且 Generation 匹配，契约 §6）。</summary>
	public bool IsAlive(EntityId id)
	{
		if (!id.IsValid || id.Index >= _slots.Count)
		{
			return false;
		}
		var slot = _slots[id.Index];
		return slot != null && slot.Id == id;
	}

	/// <summary>按身份取对象（已销毁/无效返回 null，读安全）。</summary>
	public GameObject? GetObject(EntityId id)
	{
		if (!IsAlive(id))
		{
			return null;
		}
		return _slots[id.Index];
	}

	/// <summary>取对象启用标志（读安全：已销毁返回 false）。</summary>
	internal bool GetEnabled(GameObject obj) => !obj.IsDestroyed && _enabled.TryGetValue(obj, out bool e) && e;

	/// <summary>设置对象启用标志；触发子树 effective 刷新（OnEnable/OnDisable 翻转，契约 §3）。</summary>
	internal void SetEnabled(GameObject obj, bool value)
	{
		EnsureOwnedAndAlive(obj); // 跨世界/已销毁一律拒绝（reviewer P1 统一入口）
		if (_enabled.TryGetValue(obj, out bool current) && current == value)
		{
			return;
		}
		_enabled[obj] = value;
		RefreshEffective(obj);
	}

	/// <summary>是否有效启用（对象 Enabled && 父链全 Enabled && !Paused，契约 §3）。</summary>
	internal bool IsEffectivelyEnabled(GameObject obj)
	{
		if (Paused || obj.IsDestroyed || !_enabled.TryGetValue(obj, out bool e) || !e)
		{
			return false;
		}
		var parent = _hierarchy.GetParent(obj);
		return parent == null || IsEffectivelyEnabled(parent);
	}

	/// <summary>刷新对象 effective 状态（含子树）：仅翻转时调用 OnEnable/OnDisable（契约 §3/§4）。</summary>
	internal void RefreshEffective(GameObject obj)
	{
		bool effective = IsEffectivelyEnabled(obj);
		if (obj.IsDestroyed)
		{
			return;
		}
		if (_stores.TryGetValue(obj, out var store))
		{
			foreach (var comp in store.All)
			{
				bool compEffective = effective && comp.Enabled;
				if (compEffective && !comp.EffectiveActive)
				{
					comp.EffectiveActive = true;
					comp.OnEnable();
				}
				else if (!compEffective && comp.EffectiveActive)
				{
					comp.EffectiveActive = false;
					comp.OnDisable();
				}
			}
		}
		foreach (var child in _hierarchy.GetChildren(obj))
		{
			RefreshEffective(child);
		}
	}

	// ---------- 组件操作 ----------

	/// <summary>给对象添加组件（同步生效；OnCreate 立即、OnEnable 跟随 effective、OnStart 首个有效 tick）。</summary>
	public T AddComponent<T>(GameObject obj, T component) where T : GameComponent
	{
		EnsureOwnedAndAlive(obj);
		ArgumentNullException.ThrowIfNull(component);
		if (component.Owner != null)
		{
			throw new InvalidOperationException($"组件实例 {component.GetType().Name} 已挂载到对象 {component.Owner}，禁止重复挂载（reviewer P1）。");
		}

		// 以运行时类型取 Schema（T 可能是基类），保证与存储/序列化一致（reviewer P1）。
		var schema = _schemas.Get(component.GetType());
		var store = _store(obj);

		// 契约 §1：单实例校验（按运行时类型）。
		if (!schema.AllowMultiple && store.TryGetSingle(component.GetType(), out _))
		{
			throw new InvalidOperationException($"对象 {obj} 已存在单实例组件 {schema.TypeName}（重复添加被拒绝）。");
		}
		// 契约 §2：必需依赖校验。
		foreach (var requireType in schema.Requires)
		{
			// reviewer P1（第二轮）：依赖校验需同时覆盖单实例与多实例容器（ContainsType）。
			if (!store.ContainsType(requireType))
			{
				throw new InvalidOperationException($"添加 {schema.TypeName} 缺少必需依赖组件 {requireType.Name}（契约 §2）。");
			}
		}

		component.AttachTo(obj);
		component.Revision = ++_revisionCounter;
		store.Add(component, schema.AllowMultiple);
		InsertTickOrdered(component); // 保持（对象创建序 → 组件插入序），reviewer P1

		// 生命周期（契约 §4）：OnCreate 立即；OnEnable 按 effective 翻转。
		component.OnCreate();
		bool effective = IsEffectivelyEnabled(obj);
		if (effective && component.Enabled && !component.EffectiveActive)
		{
			component.EffectiveActive = true;
			component.OnEnable();
		}
		return component;
	}

	/// <summary>
	/// 按（对象创建序 → 组件插入序）有序插入全局 tick 表（reviewer P1 确定性：调用时序不影响顺序）。
	/// 依赖：obj.CreationIndex（世界内单调递增）与 component.Revision（AddComponent 内已分配单调递增）。
	/// </summary>
	private void InsertTickOrdered(GameComponent component)
	{
		uint objOrder = component.Owner!.CreationIndex;
		uint compRevision = component.Revision;
		// 从尾部向前找到第一个（对象序更小，或对象序相同且组件序更小）的位置之后插入 —— 保持稳定排序。
		int index = _tickOrder.Count;
		while (index > 0)
		{
			var prev = _tickOrder[index - 1];
			bool prevBefore = prev.Owner == null ||
				prev.Owner.CreationIndex < objOrder ||
				(prev.Owner.CreationIndex == objOrder && prev.Revision <= compRevision);
			if (prevBefore)
			{
				break;
			}
			index--;
		}
		_tickOrder.Insert(index, component);
	}

	// ----------
	/// <summary>移除组件（单实例：按类型；多实例：第一个）。返回是否移除。</summary>
	public bool RemoveComponent<T>(GameObject obj) where T : GameComponent
	{
		return RemoveComponent(obj, typeof(T));
	}

	/// <summary>移除组件（单实例：按类型；多实例：第一个该类型）。返回是否移除。</summary>
	public bool RemoveComponent(GameObject obj, Type type)
	{
		EnsureOwnedAndAlive(obj);
		var schema = _schemas.Get(type);
		var store = _store(obj);

		GameComponent? target;
		if (!schema.AllowMultiple)
		{
			if (!store.TryGetSingle(type, out target) || target == null)
			{
				return false;
			}
		}
		else
		{
			var all = store.GetAll(type);
			if (all.Count == 0)
			{
				return false;
			}
			target = all[0];
		}

		return RemoveComponent(obj, target);
	}

	/// <summary>移除指定组件实例（多实例精确移除）。返回是否移除。</summary>
	public bool RemoveComponent(GameObject obj, GameComponent component)
	{
		EnsureOwnedAndAlive(obj);
		var schema = _schemas.Get(component.GetType());
		var store = _store(obj);
		// review P1：先验证组件确属该对象（按引用），防外来组件破坏原 owner。
		if (!ReferenceEquals(component.Owner, obj) || !store.Contains(component))
		{
			return false;
		}

		if (component.EffectiveActive)
		{
			component.EffectiveActive = false;
			component.OnDisable();
		}
		component.OnDestroy();
		store.Remove(component, schema.AllowMultiple);
		_tickOrder.Remove(component);
		component.Detach();
		return true;
	}

	/// <summary>取单实例组件（不存在返回 null，读安全）。</summary>
	public T? GetComponent<T>(GameObject obj) where T : GameComponent
	{
		if (obj.IsDestroyed || !ReferenceEquals(obj.World, this))
		{
			return null;
		}
		var store = _store(obj);
		if (store.TryGetSingle(typeof(T), out var comp) && comp != null)
		{
			return (T)comp;
		}
		// 单实例类型但此前以多实例注册？规范路径不会发生；兜底查多实例首项。
		var all = store.GetAll(typeof(T));
		return all.Count > 0 ? (T)all[0] : null;
	}

	/// <summary>取全部组件（多实例；单实例最多一个）。</summary>
	public IReadOnlyList<T> GetComponents<T>(GameObject obj) where T : GameComponent
	{
		if (obj.IsDestroyed || !ReferenceEquals(obj.World, this))
		{
			return Array.Empty<T>();
		}
		var store = _store(obj);
		if (store.TryGetSingle(typeof(T), out var single) && single != null)
		{
			return new T[] { (T)single };
		}
		var all = store.GetAll(typeof(T));
		if (all.Count == 0)
		{
			return Array.Empty<T>();
		}
		var result = new List<T>(all.Count);
		foreach (var c in all)
		{
			result.Add((T)c);
		}
		return result;
	}

	/// <summary>取对象全部组件（插入序只读视图）。</summary>
	internal IReadOnlyList<GameComponent> GetComponentList(GameObject obj)
	{
		if (obj.IsDestroyed || !ReferenceEquals(obj.World, this))
		{
			return Array.Empty<GameComponent>();
		}
		return _store(obj).All;
	}

	// ---------- 层级 ----------

	/// <summary>取父对象（顶层返回 null）。</summary>
	internal GameObject? GetParent(GameObject obj) => obj.IsDestroyed ? null : _hierarchy.GetParent(obj);

	/// <summary>取子对象列表（插入序）。</summary>
	internal IReadOnlyList<GameObject> GetChildren(GameObject obj) => obj.IsDestroyed ? Array.Empty<GameObject>() : _hierarchy.GetChildren(obj);

	/// <summary>重新挂父对象（null = 顶层；禁止环，契约 §7）。</summary>
	internal void SetParent(GameObject obj, GameObject? newParent)
	{
		EnsureOwnedAndAlive(obj);
		if (newParent != null)
		{
			EnsureOwnedAndAlive(newParent);
			if (ReferenceEquals(newParent, obj) || _hierarchy.IsAncestorOf(obj, newParent))
			{
				throw new InvalidOperationException($"SetParent 形成环引用（{obj} → {newParent}），契约 §7 禁止。");
			}
		}
		_hierarchy.SetParentInternal(obj, newParent);
		RefreshEffective(obj);
	}

	// ---------- 销毁 ----------

	/// <summary>销毁对象（同步；级联销毁整棵子树；句柄立即失效，契约 §5/§6）。</summary>
	/// review P1：两阶段——先全子树句柄失效（防回调重入/半销毁），再基于组件快照执行 OnDisable/OnDestroy。
	public void Destroy(GameObject obj)
	{
		// 已销毁再 Destroy：按契约 §6 抛异常（不再静默 return，reviewer P1）。
		EnsureOwnedAndAlive(obj);

		// 收集子树（深度优先，先子后父保证 OnDestroy 顺序）。
		var toDestroy = new List<GameObject>();
		CollectSubtree(obj, toDestroy);

		// 阶段 1：整棵子树同步失效——registry 槽位置空、Generation++、层级/关系摘除、enabled 移除。
		// 目的：回调发生在句柄失效之后，destroying 期间任何重入/结构操作都会被契约 §6 拒绝，杜绝半销毁。
		foreach (var doomed in toDestroy)
		{
			_relations.RemoveAll(doomed.Id);
			_hierarchy.Unregister(doomed);
			_slots[doomed.Id.Index] = null;
			_generations[doomed.Id.Index]++;
			_freeIndices.Push(doomed.Id.Index);
			_enabled.Remove(doomed);
			UnregisterTransactionObject(doomed); // 清理句柄映射（reviewer P1 第三轮）
		}

		// 阶段 2：基于组件快照执行销毁回调（快照防回调修改 Store 导致枚举异常）。
		// reviewer P1（第二轮）：逐组件 try/finally 保证清理（Detach/移除 tick 表）不因回调异常中断；
		// 全部清理完成后统一抛 AggregateException——不残留半清理组件。
		var destroyErrors = new List<Exception>();
		foreach (var doomed in toDestroy)
		{
			if (_stores.TryGetValue(doomed, out var store))
			{
				var snapshot = new GameComponent[store.All.Count];
				for (int ci = 0; ci < snapshot.Length; ci++)
				{
					snapshot[ci] = store.All[ci]; // 避免 Linq，保持零依赖
				}
				foreach (var comp in snapshot)
				{
					try
					{
						if (comp.EffectiveActive)
						{
							comp.EffectiveActive = false;
							comp.OnDisable();
						}
					}
					catch (Exception ex)
					{
						destroyErrors.Add(ex);
					}
					try
					{
						// reviewer P1（第三轮）：OnDisable 异常不得吞掉同组件的 OnDestroy——回调分离捕获。
						comp.OnDestroy();
					}
					catch (Exception ex)
					{
						destroyErrors.Add(ex);
					}
					finally
					{
						comp.Detach();
						// 注（reviewer P3）：逐组件 _tickOrder.Remove 为 O(C²)；大世界后续改为索引/墓碑或批量线性过滤。
						_tickOrder.Remove(comp);
					}
				}
				_stores.Remove(doomed);
			}
		}
		if (destroyErrors.Count > 0)
		{
			throw new AggregateException("对象销毁回调中发生异常（组件已全部清理）", destroyErrors);
		}
	}

	private void CollectSubtree(GameObject root, List<GameObject> result)
	{
		foreach (var child in _hierarchy.GetChildren(root))
		{
			CollectSubtree(child, result);
		}
		result.Add(root);
	}

	// ---------- Tick ----------

	/// <summary>推进一帧 variable tick（OnStart 一次 + OnTick 每帧，快照遍历，契约 §4/§5）。</summary>
	public void Tick(float delta)
	{
		TickIndex++;
		// 快照遍历：本轮结构变更（Add/Remove/Destroy）不影响本轮。
		// reviewer P1（第二轮）：快照同时记录 Revision——同一轮内被移除又重挂的组件
		// （Revision 已变更）在本轮跳过，符合“tick 内 Add 从下一轮开始”契约。
		foreach (var entry in TickSnapshot())
		{
			if (entry.Comp.Revision != entry.RevisionAtSnapshot)
			{
				continue; // 本轮回移除又重挂：从下一轮开始参与
			}
			if (!IsTickable(entry.Comp))
			{
				continue;
			}
			EnsureStarted(entry.Comp);
			// reviewer P1（第三轮）：OnStart 内可能禁用/销毁/重挂自身——重验 Revision 与有效性，通过才回调。
			if (entry.Comp.Revision != entry.RevisionAtSnapshot || !IsTickable(entry.Comp))
			{
				continue;
			}
			entry.Comp.OnTick(delta);
		}
	}

	/// <summary>取本轮 tick 快照（组件 + 快照时 Revision）。</summary>
	private (GameComponent Comp, uint RevisionAtSnapshot)[] TickSnapshot()
	{
		var snapshot = new (GameComponent, uint)[_tickOrder.Count];
		for (int i = 0; i < snapshot.Length; i++)
		{
			snapshot[i] = (_tickOrder[i], _tickOrder[i].Revision);
		}
		return snapshot;
	}

	/// <summary>推进一帧 fixed tick（OnFixedTick 每帧，快照遍历；delta 固定为 world.FixedDelta，契约 §4）。</summary>
	/// review P1：与 Tick 共用 OnStart 首次门禁（FixedTick 先到也算首次有效 tick）；
	/// 忽略调用方传入的 delta，按契约使用 world.FixedDelta。
	public void FixedTick(float delta)
	{
		FixedTickIndex++;
		foreach (var entry in TickSnapshot())
		{
			if (entry.Comp.Revision != entry.RevisionAtSnapshot)
			{
				continue; // 同轮移除又重挂：从下一轮开始
			}
			if (!IsTickable(entry.Comp))
			{
				continue;
			}
			EnsureStarted(entry.Comp);
			// reviewer P1（第三轮）：OnStart 后重验，防失效/重挂组件继续回调。
			if (entry.Comp.Revision != entry.RevisionAtSnapshot || !IsTickable(entry.Comp))
			{
				continue;
			}
			entry.Comp.OnFixedTick(FixedDelta);
		}
	}

	/// <summary>组件当前是否应参与 tick（存活、effective enabled，契约 §3/§4）。</summary>
	private bool IsTickable(GameComponent comp)
	{
		if (comp.Owner == null || comp.Owner.IsDestroyed)
		{
			return false;
		}
		return IsEffectivelyEnabled(comp.Owner) && comp.Enabled;
	}

	/// <summary>首次有效 tick 前调用一次 OnStart（Tick/FixedTick 共用门禁，契约 §4，reviewer P1）。</summary>
	private static void EnsureStarted(GameComponent comp)
	{
		if (!comp.Started)
		{
			comp.Started = true;
			comp.OnStart();
		}
	}

	// ---------- Services（契约 §11）----------

	/// <summary>注册服务单例（同一类型重复注册抛异常）。</summary>
	public T AddService<T>(T service) where T : class
	{
		ArgumentNullException.ThrowIfNull(service);
		if (_services.ContainsKey(typeof(T)))
		{
			throw new InvalidOperationException($"服务类型 {typeof(T).Name} 已注册。");
		}
		_services.Add(typeof(T), service);
		return service;
	}

	/// <summary>取服务单例（未注册抛异常）。</summary>
	public T GetService<T>() where T : class
	{
		if (_services.TryGetValue(typeof(T), out var service))
		{
			return (T)service;
		}
		throw new InvalidOperationException($"服务类型 {typeof(T).Name} 未注册。");
	}

	/// <summary>是否存在该服务。</summary>
	public bool HasService<T>() where T : class => _services.ContainsKey(typeof(T));

	/// <summary>清理全部对象（等价逐个 Destroy；处理过程中 registry 与层级同步失效）。</summary>
/// <summary>清理全部对象（等价逐个 Destroy；已随级联销毁的对象自动跳过，契约 §6 语义）。</summary>
	public void Clear()
	{
		var roots = new GameObject[_hierarchy.Roots.Count];
		for (int i = 0; i < roots.Length; i++)
		{
			roots[i] = _hierarchy.Roots[i]; // 避免 Linq，保持零依赖
		}
		foreach (var obj in roots)
		{
			if (!obj.IsDestroyed)
			{
				Destroy(obj);
			}
		}
	}

	// ---------- Undo/Redo（事务栈，O1 验证清单项）----------

	private readonly Stack<List<EditStep>> _undoStack = new();
	private readonly Stack<List<EditStep>> _redoStack = new();

	/// <summary>可撤销事务数。</summary>
	public int UndoCount => _undoStack.Count;

	/// <summary>可重做事务数。</summary>
	public int RedoCount => _redoStack.Count;

	/// <summary>开启一个新事务（所有编辑动作走事务记录，Commit 后可 Undo/Redo）。</summary>
	public EditTransaction CreateTransaction() => new(this);

	/// <summary>提交事务步骤入栈（由 EditTransaction.Commit 调用）。</summary>
	internal void PushUndoSteps(List<EditStep> steps)
	{
		if (steps.Count == 0)
		{
			return;
		}
		_undoStack.Push(new List<EditStep>(steps));
		_redoStack.Clear(); // 新编辑使 redo 历史失效
	}

	/// <summary>撤销最近一次已提交事务。无可撤销时返回 false。</summary>
	public bool Undo()
	{
		if (_undoStack.Count == 0)
		{
			return false;
		}
		var steps = _undoStack.Pop();
		for (int i = steps.Count - 1; i >= 0; i--)
		{
			steps[i].Undo();
		}
		_redoStack.Push(steps);
		return true;
	}

	/// <summary>重做最近一次被撤销的事务。无可重做时返回 false。</summary>
	public bool Redo()
	{
		if (_redoStack.Count == 0)
		{
			return false;
		}
		var steps = _redoStack.Pop();
		foreach (var step in steps)
		{
			step.Apply();
		}
		_undoStack.Push(steps);
		return true;
	}

	/// <summary>结构操作前置：对象必须属于本世界且存活（reviewer P1：拒跨世界对象）。</summary>
	private void EnsureOwnedAndAlive(GameObject obj)
	{
		if (!ReferenceEquals(obj.World, this))
		{
			throw new InvalidOperationException($"对象 {obj} 不属于当前 GameWorld（跨世界结构操作被拒绝）。");
		}
		if (obj.IsDestroyed)
		{
			throw new InvalidOperationException($"对已销毁对象执行结构操作（{obj}），契约 §6 禁止。");
		}
	}
}
