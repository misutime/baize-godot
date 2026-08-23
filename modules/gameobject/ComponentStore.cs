// SPDX-License-Identifier: MIT
// ComponentStore.cs —— GameObject 的组件容器（O1）
//
// 单实例（默认）与多实例（[GameComponent(AllowMultiple=true)]）混合存储；
// 保持插入序遍历（对象创建序 → 组件插入序，契约 §4 确定性）。

using System;
using System.Collections.Generic;

namespace Baize.GameObject;

/// <summary>组件容器：单实例快速路径 + 多实例列表 + 插入序遍历。内部使用。</summary>
public sealed class ComponentStore
{
	private readonly Dictionary<Type, GameComponent> _single = new();
	private readonly Dictionary<Type, List<GameComponent>> _multi = new();
	private readonly List<GameComponent> _order = new();

	/// <summary>已注册组件数。</summary>
	public int Count => _order.Count;

	/// <summary>按插入序遍历全部组件（确定性：对象创建序 → 组件插入序）。</summary>
	public IReadOnlyList<GameComponent> All => _order;

	/// <summary>尝试取单实例组件。</summary>
	public bool TryGetSingle(Type type, out GameComponent? component) => _single.TryGetValue(type, out component);

	/// <summary>是否持有该组件实例（按引用；移除前需确认归属，产地 §1/§5）。</summary>
	public bool Contains(GameComponent component) => _order.Contains(component);

	/// <summary>是否持有该类型组件（单实例或已有多实例；依赖校验用，reviewer P1）。</summary>
	public bool ContainsType(Type type) => _single.ContainsKey(type) || (_multi.TryGetValue(type, out var list) && list.Count > 0);

	/// <summary>取多实例组件列表（不存在返回空列表，不分配）。</summary>
	public IReadOnlyList<GameComponent> GetAll(Type type)
	{
		return _multi.TryGetValue(type, out var list) ? list : Array.Empty<GameComponent>();
	}

	/// <summary>添加组件（allowMultiple 由外部 Schema 判定，契约 §1 一致）。</summary>
	public void Add(GameComponent component, bool allowMultiple)
	{
		Type type = component.GetType();
		if (!allowMultiple)
		{
			_single.Add(type, component);
		}
		else
		{
			if (!_multi.TryGetValue(type, out var list))
			{
				list = new List<GameComponent>();
				_multi.Add(type, list);
			}
			list.Add(component);
		}
		_order.Add(component);
	}

	/// <summary>移除组件（按引用；多实例只移除该实例）。返回是否移除成功。</summary>
	public bool Remove(GameComponent component, bool allowMultiple)
	{
		Type type = component.GetType();
		bool removed = _order.Remove(component);
		if (allowMultiple)
		{
			if (_multi.TryGetValue(type, out var list))
			{
				removed |= list.Remove(component);
				if (list.Count == 0)
				{
					_multi.Remove(type);
				}
			}
		}
		else
		{
			removed |= _single.Remove(type);
		}
		return removed;
	}
}
