// SPDX-License-Identifier: MIT
// RelationGraph.cs —— 对象关系图（O1，方案 §4.5/§14.1 / 契约 §9）
//
// Relation 是非父子语义关系（Target/Owner/TeamMember/Equipped/...）。
// - Source/Target 均存 ObjectId（身份安全，随销毁同步清理）。
// - 双向索引（outgoing/incoming）；任一端点销毁自动移除该对象全部关系。
// - 不做同级单例限制（同类型多关系允许）；查询返回插入序。
// - 类型注册表：Add&lt;T&gt; 自动注册；Restore 依赖注册表（确定性，不反射扫描）。

using System;
using System.Collections.Generic;

namespace Sola3d.GameObject;
/// <summary>关系（Relation）—— 非父子语义关系，一等数据（方案 §4.5/契约 §9）。</summary>
public abstract class GameRelation
{
	/// <summary>源对象身份。</summary>
	public ObjectId Source { get; internal set; }
	/// <summary>目标对象身份。</summary>
	public ObjectId Target { get; internal set; }

	/// <summary>关系类型（人类可读，调试用）。</summary>
	public virtual string RelationName => GetType().Name;
}

/// <summary>关系图：source/target 双向索引。世界内单例，绑定所属 GameWorld（拒跨世界端点）。</summary>
public sealed class RelationGraph
{
	private readonly GameWorld _world;

	internal RelationGraph(GameWorld world) => _world = world;
	private readonly Dictionary<ObjectId, List<GameRelation>> _outgoing = new();
	private readonly Dictionary<ObjectId, List<GameRelation>> _incoming = new();
	private readonly List<GameRelation> _order = new();
	private readonly Dictionary<string, Func<GameRelation>> _typeFactories = new();

	/// <summary>全部关系（插入序，确定性）。</summary>
	public IReadOnlyList<GameRelation> All => _order;

	/// <summary>已注册关系类型数（序列化 Restore 依赖注册表）。</summary>
	public int RegisteredTypeCount => _typeFactories.Count;

	/// <summary>注册关系类型（序列化 Restore 用；Add&lt;T&gt; 会自动注册）。</summary>
	public RelationGraph Register<TRelation>() where TRelation : GameRelation, new()
	{
		var key = StableTypeKey(typeof(TRelation));
		if (!_typeFactories.ContainsKey(key))
		{
			_typeFactories.Add(key, () => new TRelation());
		}
		return this;
	}

	/// <summary>复制另一关系图的类型注册表（Restore 重建世界用）。</summary>
	internal void CopyFactoriesFrom(RelationGraph other)
	{
		foreach (var kv in other._typeFactories)
		{
			if (!_typeFactories.ContainsKey(kv.Key))
			{
				_typeFactories.Add(kv.Key, kv.Value);
			}
		}
	}

	private Func<GameRelation> FactoryOf<T>() where T : GameRelation, new()
	{
		var key = StableTypeKey(typeof(T));
		if (!_typeFactories.TryGetValue(key, out var factory))
		{
			factory = () => new T();
			_typeFactories.Add(key, factory);
		}
		return factory;
	}

	/// <summary>稳定类型键：全限定名（防跨命名空间同名冲突，reviewer P1）。</summary>
	internal static string StableTypeKey(Type type) => type.FullName ?? type.Name;

	/// <summary>按类型名创建关系并登记（Restore 用；未注册抛异常）。</summary>
	internal GameRelation RestoreTyped(string typeName, ObjectId sourceId, ObjectId targetId)
	{
		if (!_typeFactories.TryGetValue(typeName, out var factory))
		{
			throw new InvalidOperationException($"关系类型 {typeName} 未注册（Restore 前需 Register&lt;T&gt;() 或 Add&lt;T&gt; 过）。");
		}
		var relation = factory();
		relation.Source = sourceId;
		relation.Target = targetId;
		AddCore(relation, sourceId, targetId);
		return relation;
	}

	/// <summary>添加关系：source → target。</summary>
	public TRelation Add<TRelation>(GameObject source, GameObject target) where TRelation : GameRelation, new()
	{
		ArgumentNullException.ThrowIfNull(source);
		ArgumentNullException.ThrowIfNull(target);
		EnsureOwned(source);
		EnsureOwned(target);

		var relation = new TRelation
		{
			Source = source.Id,
			Target = target.Id,
		};
		FactoryOf<TRelation>(); // 自动注册，确保 Restore 可恢复
		AddCore(relation, source.Id, target.Id);
		return relation;
	}

	internal void AddCore(GameRelation relation, ObjectId sourceId, ObjectId targetId)
	{
		if (!_outgoing.TryGetValue(sourceId, out var outList))
		{
			outList = new List<GameRelation>();
			_outgoing.Add(sourceId, outList);
		}
		outList.Add(relation);

		if (!_incoming.TryGetValue(targetId, out var inList))
		{
			inList = new List<GameRelation>();
			_incoming.Add(targetId, inList);
		}
		inList.Add(relation);

		_order.Add(relation);
	}

	/// <summary>查询：本对象作为 Source 的所有关系（按类型过滤，插入序）。跨世界对象返回空（reviewer P1）。</summary>
	public IReadOnlyList<TRelation> GetFrom<TRelation>(GameObject source) where TRelation : GameRelation
	{
		ArgumentNullException.ThrowIfNull(source);
		if (!IsOwned(source))
		{
			return Array.Empty<TRelation>();
		}
		if (!_outgoing.TryGetValue(source.Id, out var list))
		{
			return Array.Empty<TRelation>();
		}
		List<TRelation>? result = null;
		foreach (var r in list)
		{
			if (r is TRelation typed)
			{
				(result ??= new List<TRelation>()).Add(typed);
			}
		}
		return result ?? (IReadOnlyList<TRelation>)Array.Empty<TRelation>();
	}

	/// <summary>查询：本对象作为 Target 的所有关系（按类型过滤，插入序）。跨世界对象返回空（reviewer P1）。</summary>
	public IReadOnlyList<TRelation> GetTo<TRelation>(GameObject target) where TRelation : GameRelation
	{
		ArgumentNullException.ThrowIfNull(target);
		if (!IsOwned(target))
		{
			return Array.Empty<TRelation>();
		}
		if (!_incoming.TryGetValue(target.Id, out var list))
		{
			return Array.Empty<TRelation>();
		}
		List<TRelation>? result = null;
		foreach (var r in list)
		{
			if (r is TRelation typed)
			{
				(result ??= new List<TRelation>()).Add(typed);
			}
		}
		return result ?? (IReadOnlyList<TRelation>)Array.Empty<TRelation>();
	}

	/// <summary>移除指定关系实例。返回是否移除。</summary>
	public bool Remove(GameRelation relation)
	{
		if (!_order.Remove(relation))
		{
			return false;
		}
		if (_outgoing.TryGetValue(relation.Source, out var outList))
		{
			outList.Remove(relation);
			if (outList.Count == 0)
			{
				_outgoing.Remove(relation.Source);
			}
		}
		if (_incoming.TryGetValue(relation.Target, out var inList))
		{
			inList.Remove(relation);
			if (inList.Count == 0)
			{
				_incoming.Remove(relation.Target);
			}
		}
		return true;
	}

	/// <summary>移除对象全部进出关系（销毁时同步调用，契约 §9）。</summary>
	internal void RemoveAll(ObjectId id)
	{
		if (_outgoing.TryGetValue(id, out var outList))
		{
			foreach (var r in outList)
			{
				_order.Remove(r);
				if (_incoming.TryGetValue(r.Target, out var inList))
				{
					inList.Remove(r);
					if (inList.Count == 0)
					{
						_incoming.Remove(r.Target);
					}
				}
			}
			_outgoing.Remove(id);
		}
		if (_incoming.TryGetValue(id, out var inList2))
		{
			foreach (var r in inList2)
			{
				_order.Remove(r);
				if (_outgoing.TryGetValue(r.Source, out var outList2))
				{
					outList2.Remove(r);
					if (outList2.Count == 0)
					{
						_outgoing.Remove(r.Source);
					}
				}
			}
			_incoming.Remove(id);
		}
	}

	/// <summary>端点是否属于本关系图所属世界且存活（查询用：跨世界返回空，不抛，读安全，reviewer P1）。</summary>
	private bool IsOwned(GameObject obj) => ReferenceEquals(obj.World, _world) && !obj.IsDestroyed;

	/// <summary>端点必须属于本关系图所属世界且存活（reviewer P1：拒跨世界端点）。</summary>
	private void EnsureOwned(GameObject obj)
	{
		if (obj.World != _world)
		{
			throw new InvalidOperationException($"关系端点 {obj} 不属于当前 GameWorld（跨世界关系被拒绝）。");
		}
		if (obj.IsDestroyed)
		{
			throw new InvalidOperationException($"关系操作的目标对象已销毁（{obj}）。");
		}
	}
}

/// <summary>
/// 对象关系门面：GameObject.Relations 便捷访问（以该对象为 Source，契约 §9）。
/// </summary>
public readonly struct RelationAccess
{
	private readonly GameWorld _world;
	private readonly GameObject _owner;

	internal RelationAccess(GameWorld world, GameObject owner)
	{
		_world = world;
		_owner = owner;
	}

	/// <summary>添加关系：本对象 → target。</summary>
	public TRelation Add<TRelation>(GameObject target) where TRelation : GameRelation, new()
	{
		return _world.Relations.Add<TRelation>(_owner, target);
	}

	/// <summary>查询本对象作为 Source 的所有该类型关系。</summary>
	public IReadOnlyList<TRelation> Get<TRelation>() where TRelation : GameRelation
	{
		return _world.Relations.GetFrom<TRelation>(_owner);
	}

	/// <summary>查询本对象作为 Source 的该类型第一个关系（方便 Get&lt;TargetRelation&gt;() 直取）。</summary>
	public TRelation? First<TRelation>() where TRelation : GameRelation
	{
		var all = Get<TRelation>();
		return all.Count > 0 ? all[0] : null;
	}
}
