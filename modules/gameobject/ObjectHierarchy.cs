// SPDX-License-Identifier: MIT
// ObjectHierarchy.cs —— 对象层级（O1，契约 §7）
//
// Parent/Children 承担：场景组织、所有权、生命周期归属（销毁级联）、遍历；
// 不承担空间继承（Transform 由 O6 TransformComponent/Backend 承担）。

using System;
using System.Collections.Generic;

namespace Baize.GameObject;

/// <summary>对象层级：parent/children 双向映射 + 环检测 + 遍历。世界内单例。</summary>
public sealed class ObjectHierarchy
{
	private readonly Dictionary<GameObject, GameObject?> _parent = new();
	private readonly Dictionary<GameObject, List<GameObject>> _children = new();
	private readonly List<GameObject> _roots = new();

	/// <summary>顶层对象列表（插入序）。</summary>
	public IReadOnlyList<GameObject> Roots => _roots;

	/// <summary>取父对象（顶层返回 null）。</summary>
	public GameObject? GetParent(GameObject obj) => _parent.TryGetValue(obj, out var p) ? p : null;

	/// <summary>取子对象列表（插入序；无子返回空列表）。</summary>
	public IReadOnlyList<GameObject> GetChildren(GameObject obj)
	{
		return _children.TryGetValue(obj, out var list) ? list : Array.Empty<GameObject>();
	}

/// <summary>是否拥有该对象（已登记）。</summary>
	public bool Contains(GameObject obj) => _parent.ContainsKey(obj);

	/// <summary>是否 ancestor 是 obj 的祖先（沿内部 parent 链向上；环检测用）。</summary>
	internal bool IsAncestorOf(GameObject? ancestor, GameObject obj)
	{
		for (var cur = obj; cur != null;)
		{
			_parent.TryGetValue(cur, out var p);
			if (ReferenceEquals(p, ancestor))
			{
				return true;
			}
			cur = p;
		}
		return false;
	}

	/// <summary>登记新对象（顶层；由世界在创建时调用）。</summary>
	internal void Register(GameObject obj)
	{
		_parent.Add(obj, null);
		_roots.Add(obj);
	}

/// <summary>移除对象登记（由世界在销毁时调用；整棵子树随销毁一并摘除，不重新挂 root）。</summary>
	internal void Unregister(GameObject obj)
	{
		// 销毁场景：子对象随父一并销毁，直接递归摘除子树，不再 Unparent 回 root。
		if (_children.Remove(obj, out var children))
		{
			foreach (var child in children)
			{
				Unregister(child);
			}
		}

		GameObject? parent = GetParent(obj);
		if (parent == null)
		{
			_roots.Remove(obj);
		}
		else if (_children.TryGetValue(parent, out var parentList))
		{
			parentList.Remove(obj);
			if (parentList.Count == 0)
			{
				_children.Remove(parent);
			}
		}
		_parent.Remove(obj);
	}

	/// <summary>挂到新父对象（可为 null → 顶层）。禁止环。notify 由世界统一处理。</summary>
	internal void SetParentInternal(GameObject obj, GameObject? newParent)
	{
		GameObject? oldParent = GetParent(obj);

		if (oldParent == null)
		{
			_roots.Remove(obj);
		}
		else if (_children.TryGetValue(oldParent, out var oldList))
		{
			oldList.Remove(obj);
			if (oldList.Count == 0)
			{
				_children.Remove(oldParent);
			}
		}

		_parent[obj] = newParent;

		if (newParent == null)
		{
			_roots.Add(obj);
		}
		else
		{
			if (!_children.TryGetValue(newParent, out var list))
			{
				list = new List<GameObject>();
				_children.Add(newParent, list);
			}
			list.Add(obj);
		}
	}

	private void Unparent(GameObject obj)
	{
		GameObject? parent = GetParent(obj);
		if (parent == null)
		{
			_roots.Remove(obj);
		}
		else if (_children.TryGetValue(parent, out var list))
		{
			list.Remove(obj);
			if (list.Count == 0)
			{
				_children.Remove(parent);
			}
		}
		_parent[obj] = null;
		_roots.Add(obj);
	}
}

/// <summary>关系（Relation）—— 非父子语义关系，一等数据（方案 §1.1/§4.5/契约 §9）。</summary>
public abstract class GameRelation
{
	/// <summary>源对象身份。</summary>
	public EntityId Source { get; internal set; }

	/// <summary>目标对象身份。</summary>
	public EntityId Target { get; internal set; }

	/// <summary>关系类型（人类可读，调试用）。</summary>
	public virtual string RelationName => GetType().Name;
}
