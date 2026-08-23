// SPDX-License-Identifier: MIT
// EditTransaction.cs —— 事务化修改 + Undo/Redo（O1，方案 §7.4/§10 O1 验证；契约 §12）
//
// 所有编辑动作统一形成 Object 事务（AddComponent/RemoveComponent/SetProperty/Reparent/CreateObject）：
// - 操作立即同步生效（契约 §5），同时记录反操作（undo 闭包）；
// - Commit() 将 undo 序列压入世界 undo 栈；Undo()/Redo() 由世界执行；
// - Rollback() 逆序执行已记录的反操作并丢弃，不留栈。
// - reviewer P1（第二轮）：CreateGameObject 与后续步骤通过**对象句柄重映射**协作——
//   Redo 重建对象后，同事务内对该对象的后续步骤操作的是当前存活实例，而非已失效旧对象。

using System;
using System.Collections.Generic;
using System.Reflection;

namespace Baize.GameObject;

/// <summary>一条编辑步骤（apply = 重做动作，undo = 撤销动作）。内部使用。</summary>
internal sealed record EditStep(Action Apply, Action Undo);

/// <summary>对象编辑事务（命令模式：记录反操作，执行立即生效）。</summary>
public sealed class EditTransaction
{
	private readonly GameWorld _world;
	private readonly List<EditStep> _steps = new();
	private bool _disposed;

	internal EditTransaction(GameWorld world) => _world = world;

	/// <summary>解析事务对象：先验证对象属于本世界（防跨世界 TransactionId 冲突，reviewer P1 第四轮），
	/// 再经 GameObject.TransactionId + GameWorld 级映射取当前存活实例；普通对象（TransactionId=0）原样返回。</summary>
	private GameObject RequireResolved(GameObject obj)
	{
		if (!ReferenceEquals(obj.World, _world))
		{
			throw new InvalidOperationException($"事务步骤引用了不属于当前 GameWorld 的对象（{obj}），跨世界事务被拒绝。");
		}
		if (obj.TransactionId > 0)
		{
			var resolved = _world.GetTransactionObject(obj.TransactionId);
			if (resolved == null)
			{
				throw new InvalidOperationException($"事务步骤引用的对象已被销毁且无法重映射（事务句柄 {obj.TransactionId}），请检查 Undo/Redo 顺序。");
			}
			return resolved;
		}
		return obj;
	}

	/// <summary>解析事务对象（撤销路径宽容版）：已销毁返回 null（跳过该步骤，reviewer P1）。</summary>
	private GameObject? TryResolved(GameObject obj)
	{
		if (!ReferenceEquals(obj.World, _world))
		{
			return null; // 跨世界对象：撤销路径宽容返回 null，不触碰
		}
		if (obj.TransactionId > 0)
		{
			return _world.GetTransactionObject(obj.TransactionId);
		}
		return obj.IsDestroyed ? null : obj;
	}

	/// <summary>设置组件属性（记录旧值，可 Undo/Redo）。</summary>
	public EditTransaction SetProperty(GameComponent component, string propertyName, object? newValue)
	{
		ThrowIfDisposed();
		ArgumentNullException.ThrowIfNull(component);
		if (component.Owner == null)
		{
			throw new InvalidOperationException($"SetProperty 的组件未挂载对象（{component.GetType().Name}.{propertyName}）。");
		}
		// reviewer P1（第四轮）：组件所属世界必须与本事务一致（防跨世界组件编辑）。
		if (!ReferenceEquals(component.Owner.World, _world))
		{
			throw new InvalidOperationException("SetProperty 的组件属于其他 GameWorld（跨世界属性编辑被拒绝）。");
		}
		var prop = FindWritableProperty(component.GetType(), propertyName);
		object? oldValue = prop.GetValue(component);

		prop.SetValue(component, newValue);
		_steps.Add(new EditStep(
			Apply: () => prop.SetValue(component, newValue),
			Undo: () => prop.SetValue(component, oldValue)));
		return this;
	}

	/// <summary>添加组件（undo = 移除该实例；redo = 重新挂载同一实例到当前存活对象）。</summary>
	public EditTransaction AddComponent(GameObject obj, GameComponent component)
	{
		ThrowIfDisposed();
		_world.AddComponent(RequireResolved(obj), component);
		_steps.Add(new EditStep(
			Apply: () =>
			{
				var target = RequireResolved(obj);
				_world.AddComponent(target, component);
			},
			Undo: () =>
			{
				var target = TryResolved(obj);
				if (target != null)
				{
					_world.RemoveComponent(target, component);
				}
			}));
		return this;
	}

	/// <summary>移除组件（记录被移除实例，undo = 重新挂载；RemoveComponent&lt;T&gt; 按引用恢复）。</summary>
	public EditTransaction RemoveComponent(GameObject obj, GameComponent component)
	{
		ThrowIfDisposed();
		var target = RequireResolved(obj);
		if (_world.RemoveComponent(target, component))
		{
			_steps.Add(new EditStep(
				Apply: () =>
				{
					var t = RequireResolved(obj);
					_world.RemoveComponent(t, component);
				},
				Undo: () =>
				{
					var t = TryResolved(obj);
					if (t != null)
					{
						_world.AddComponent(t, component);
					}
				}));
		}
		return this;
	}

	/// <summary>重新挂父对象（undo = 恢复旧父；父对象同样经句柄重映射解析）。</summary>
	public EditTransaction SetParent(GameObject obj, GameObject? newParent)
	{
		ThrowIfDisposed();
// reviewer P1（第四轮）：先解析当前实例，再从解析实例读旧父（stale 句柄的 Parent 不可信）。
		var resolvedTarget = RequireResolved(obj);
		GameObject? oldParent = resolvedTarget.Parent;
		GameObject? resolvedNew = newParent != null ? RequireResolved(newParent) : null;
		_world.SetParent(resolvedTarget, resolvedNew);
		_steps.Add(new EditStep(
			Apply: () =>
			{
				var t = RequireResolved(obj);
				GameObject? np = newParent != null ? RequireResolved(newParent) : null;
				_world.SetParent(t, np);
			},
			Undo: () =>
			{
				var t = TryResolved(obj);
				if (t != null)
				{
					GameObject? op = oldParent != null ? TryResolved(oldParent) : null;
					_world.SetParent(t, op);
				}
			}));
		return this;
	}

	/// <summary>创建事务对象：分配世界级逻辑句柄（TransactionId），undo 销毁当前实例、redo 重建并**重映射同一句柄**——跨事务可解析（reviewer P1 第三轮）。</summary>
	public GameObject CreateGameObject(string name = "")
	{
		ThrowIfDisposed();
		var obj = _world.CreateGameObject(name);
		long id = _world.RegisterTransactionObject(obj);
		_steps.Add(new EditStep(
			Apply: () => _world.RemapTransactionObject(id, _world.CreateGameObject(name)),
			Undo: () =>
			{
				var current = _world.GetTransactionObject(id);
				if (current != null)
				{
					_world.Destroy(current);
				}
			}));
		return obj;
	}

	/// <summary>提交事务：undo 序列压入世界 undo 栈（可 Undo）；redo 栈清空。</summary>
	public void Commit()
	{
		ThrowIfDisposed();
		_world.PushUndoSteps(_steps);
		_steps.Clear();
		_disposed = true; // 一次事务只能 Commit/Rollback 一次
	}

	/// <summary>回滚：逆序执行已记录反操作并丢弃（不留栈）。</summary>
	public void Rollback()
	{
		ThrowIfDisposed();
		for (int i = _steps.Count - 1; i >= 0; i--)
		{
			_steps[i].Undo();
		}
		_steps.Clear();
		_disposed = true;
	}

	private void ThrowIfDisposed()
	{
		if (_disposed)
		{
			throw new InvalidOperationException("事务已提交或回滚，不能继续使用。");
		}
	}

	private static PropertyInfo FindWritableProperty(Type type, string propertyName)
	{
		var prop = type.GetProperty(propertyName, BindingFlags.Public | BindingFlags.Instance);
		if (prop == null || !prop.CanWrite)
		{
			throw new InvalidOperationException($"组件 {type.Name} 不存在可写属性 {propertyName}。");
		}
		return prop;
	}
}
