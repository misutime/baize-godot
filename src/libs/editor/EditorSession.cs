// SPDX-License-Identifier: MIT
// EditorSession.cs —— Design World 编辑会话（O7，O7-编辑器第一切片.md §3）
//
// 编辑器核心：持有 GameWorldSnapshot 作为 Design 文档模型（O3 §6.3 兑现），
// 提供对象/组件/属性编辑操作，带轻量 undo 栈（Design 级命令——与 O1 EditTransaction
// 的运行时事务区分：此处编辑的是快照 record，不是运行时 GameObject；O1 事务供运行时/预演用）。
// 保存 = Serialize(document)；载入 = Deserialize → 文档。

using System;
using System.Collections.Generic;
using Sola3d.GameObject;

namespace Sola3d.Editor;

/// <summary>Design World 编辑会话：内存文档（GameWorldSnapshot） + 编辑操作 + undo 栈。</summary>
public sealed class EditorSession
{
	private sealed record EditCommand(Action Undo, Action Redo);
	private sealed class LayoutState
	{
		public required List<GameObjectRecord> Order { get; init; }
		public required Dictionary<GameObjectRecord, GameObjectRecord?> Parents { get; init; }
		public required List<(RelationRecord Record, GameObjectRecord? Source, GameObjectRecord? Target)> Relations { get; init; }
	}
	/// <summary>Design 文档模型（.bscene 数据；对象序 = 物理顺序，Uid 稳定身份）。</summary>
	public GameWorldSnapshot Document { get; }

	private readonly ComponentSchemaRegistry _schemas;
	private readonly Stack<EditCommand> _undoStack = new();
	private readonly Stack<EditCommand> _redoStack = new();
	private ulong _nextUid = 1;
	private ulong? _selectedObjectUid;

	/// <summary>文档是否有未保存的 Design 修改。</summary>
	public bool IsDirty { get; private set; }

	/// <summary>当前选中对象的稳定 Uid；选择不依赖列表索引。</summary>
	public ulong? SelectedObjectUid => _selectedObjectUid;

	/// <summary>当前选中对象；对象被删除后自动清除。</summary>
	public GameObjectRecord? SelectedObject => _selectedObjectUid.HasValue ? FindObject(_selectedObjectUid.Value) : null;

	/// <summary>文档编辑变化通知。</summary>
	public event EventHandler? Changed;

	/// <summary>选择变化通知；选择不改变 dirty 状态。</summary>
	public event EventHandler? SelectionChanged;
	/// <summary>编辑器可以使用的组件 Schema 注册表（O3 元数据）。</summary>
	public ComponentSchemaRegistry Schemas => _schemas;

	public EditorSession(GameWorldSnapshot? document = null, ComponentSchemaRegistry? schemas = null)
	{
		Document = document ?? new GameWorldSnapshot();
		_schemas = schemas ?? new ComponentSchemaRegistry();
		// 从现有文档恢复 uid 分配器（避免与已用 Uid 冲突）。
		foreach (var obj in Document.Objects)
		{
			if (obj.Uid >= _nextUid)
			{
				_nextUid = obj.Uid + 1;
			}
		}
	}

	// ---------- 编辑操作（全部经 undo 栈） ----------

	/// <summary>创建顶层对象（Design：加进文档对象列表；Uid 自动分配）。</summary>
	public GameObjectRecord CreateGameObject(string name = "")
	{
		var record = new GameObjectRecord { Name = name, Uid = _nextUid++, ParentIndex = -1 };
		Document.Objects.Add(record);
		PushEdit(() => Document.Objects.Remove(record), () => Document.Objects.Add(record));
		return record;
	}

	/// <summary>设置父对象（层级编辑；null = 顶层）。父必须在文档中且非自身。</summary>
	public void SetParent(GameObjectRecord obj, GameObjectRecord? parent)
	{
		EnsureObject(obj);
		if (obj == parent)
		{
			throw new InvalidOperationException("对象不能成为自己的父。");
		}
		if (parent != null)
		{
			EnsureObject(parent);
		}
		int objIdx = Document.Objects.IndexOf(obj);
		if (parent != null && IsDescendant(parent, objIdx))
		{
			throw new InvalidOperationException("父对象不能是子对象的后代（环）。");
		}
		int parentIndex = parent == null ? -1 : Document.Objects.IndexOf(parent);
		if (obj.ParentIndex == parentIndex)
		{
			return;
		}
		LayoutState before = CaptureLayout();
		MoveParentCore(obj, parent);
		LayoutState after = CaptureLayout();
		PushEdit(() => RestoreLayout(before), () => RestoreLayout(after));
	}
	private LayoutState CaptureLayout()
	{
		var order = new List<GameObjectRecord>(Document.Objects);
		var parents = new Dictionary<GameObjectRecord, GameObjectRecord?>();
		foreach (var item in order)
		{
			int parentIndex = item.ParentIndex;
			parents[item] = parentIndex >= 0 && parentIndex < order.Count ? order[parentIndex] : null;
		}
		var relations = new List<(RelationRecord, GameObjectRecord?, GameObjectRecord?)>();
		foreach (var relation in Document.Relations)
		{
			relations.Add((relation,
				relation.SourceIndex >= 0 && relation.SourceIndex < order.Count ? order[relation.SourceIndex] : null,
				relation.TargetIndex >= 0 && relation.TargetIndex < order.Count ? order[relation.TargetIndex] : null));
		}
		return new LayoutState { Order = order, Parents = parents, Relations = relations };
	}

	private void RestoreLayout(LayoutState state)
	{
		Document.Objects.Clear();
		Document.Objects.AddRange(state.Order);
		foreach (var item in state.Order)
		{
			GameObjectRecord? parent = state.Parents[item];
			item.ParentIndex = parent == null ? -1 : Document.Objects.IndexOf(parent);
		}
		Document.Relations.Clear();
		foreach (var (record, source, target) in state.Relations)
		{
			record.SourceIndex = source == null ? -1 : Document.Objects.IndexOf(source);
			record.TargetIndex = target == null ? -1 : Document.Objects.IndexOf(target);
			Document.Relations.Add(record);
		}
	}

	private void MoveParentCore(GameObjectRecord obj, GameObjectRecord? parent)
	{
		LayoutState before = CaptureLayout();
		int start = Document.Objects.IndexOf(obj);
		int span = SubtreeSpan(start);
		var subtree = Document.Objects.GetRange(start, span);
		Document.Objects.RemoveRange(start, span);
		int insertAt = parent == null ? Document.Objects.Count : Document.Objects.IndexOf(parent) + SubtreeSpan(Document.Objects.IndexOf(parent));
		Document.Objects.InsertRange(insertAt, subtree);
		foreach (var item in Document.Objects)
		{
			GameObjectRecord? oldParent = before.Parents[item];
			item.ParentIndex = ReferenceEquals(item, obj)
				? (parent == null ? -1 : Document.Objects.IndexOf(parent))
				: (oldParent == null ? -1 : Document.Objects.IndexOf(oldParent));
		}
		ApplyRelationSnapshot(before.Relations.ConvertAll(r => (r.Source, r.Target)));
	}

	private List<GameObjectRecord> CollectSubtree(GameObjectRecord obj)
	{
		int start = Document.Objects.IndexOf(obj);
		return Document.Objects.GetRange(start, SubtreeSpan(start));
	}


	/// <summary>按对象引用重写 Document.Relations 的端点索引（引用稳定，index 随重排变）。</summary>
	private void ApplyRelationSnapshot(List<(GameObjectRecord? Src, GameObjectRecord? Dst)> relSnapshot)
	{
		for (int i = 0; i < Document.Relations.Count && i < relSnapshot.Count; i++)
		{
			var (src, dst) = relSnapshot[i];
			Document.Relations[i].SourceIndex = src == null ? -1 : Document.Objects.IndexOf(src);
			Document.Relations[i].TargetIndex = dst == null ? -1 : Document.Objects.IndexOf(dst);
		}
	}

	/// <summary>判断 candidate 是否在 obj 的 DFS 子树内（Objects 序即 DFS 序）。</summary>
	private bool IsDescendant(GameObjectRecord candidate, int objIdx)
	{
		int span = SubtreeSpan(objIdx);
		for (int i = objIdx + 1; i < objIdx + span && i < Document.Objects.Count; i++)
		{
			if (ReferenceEquals(Document.Objects[i], candidate))
			{
				return true;
			}
		}
		return false;
	}

	/// <summary>obj 起 DFS 连续子树跨度（含自身）：Objects 序即 DFS 序，子树为连续块。</summary>
	private int SubtreeSpan(int idx)
	{
		if (idx < 0 || idx >= Document.Objects.Count)
		{
			return 0;
		}
		int span = 1;
		while (idx + span < Document.Objects.Count)
		{
			int pi = Document.Objects[idx + span].ParentIndex;
			// 后续节点若父在 [idx, idx+span) 内 → 属于当前子树，继续扩展。
			if (pi >= idx && pi < idx + span)
			{
				span++;
			}
			else
			{
				break;
			}
		}
		return span;
	}

	/// <summary>添加组件（Design：给对象 record 加组件 record；TypeName = Schema 稳定名）。</summary>
	private static void SetPropertyCore(GameComponentRecord comp, string propertyName, object? value)
	{
		int index = comp.Properties.FindIndex(kv => kv.Key == propertyName);
		var pair = new KeyValuePair<string, object?>(propertyName, value);
		if (index >= 0)
		{
			comp.Properties[index] = pair;
		}
		else
		{
			comp.Properties.Add(pair);
		}
	}

	/// <summary>添加组件（Design：给对象 record 加组件；TypeName = Schema 稳定名）。</summary>
	public GameComponentRecord AddComponent(GameObjectRecord obj, ComponentSchema schema)
	{
		var rec = new GameComponentRecord { TypeName = schema.TypeName };
		obj.Components.Add(rec);
		PushEdit(() => obj.Components.Remove(rec), () => obj.Components.Add(rec));
		return rec;
	}

	/// <summary>写组件属性（Design：Properties 键值对，值可序列化；undo 恢复旧值）。</summary>
	public void SetProperty(GameComponentRecord comp, string propertyName, object? value)
	{
		ArgumentNullException.ThrowIfNull(comp);
		ArgumentNullException.ThrowIfNull(propertyName);
		int index = comp.Properties.FindIndex(kv => kv.Key == propertyName);
		bool existed = index >= 0;
		object? old = existed ? comp.Properties[index].Value : null;
		SetPropertyCore(comp, propertyName, value);
		PushEdit(
			() =>
			{
				if (existed)
				{
					comp.Properties[index] = new KeyValuePair<string, object?>(propertyName, old);
				}
				else
				{
					int current = comp.Properties.FindIndex(kv => kv.Key == propertyName);
					if (current >= 0) comp.Properties.RemoveAt(current);
				}
			},
			() => SetPropertyCore(comp, propertyName, value));
	}

	// ---------- Undo ----------
	// ---------- Undo / Redo ----------

	private void PushEdit(Action undo, Action redo)
	{
		_undoStack.Push(new EditCommand(undo, redo));
		_redoStack.Clear();
		MarkChanged();
	}

	private void MarkChanged()
	{
		IsDirty = true;
		Changed?.Invoke(this, EventArgs.Empty);
	}

	private void EnsureObject(GameObjectRecord obj)
	{
		ArgumentNullException.ThrowIfNull(obj);
		if (Document.Objects.IndexOf(obj) < 0)
		{
			throw new InvalidOperationException("对象不在文档中。");
		}
	}

	/// <summary>按稳定 Uid 查找对象。</summary>
	public GameObjectRecord? FindObject(ulong uid)
	{
		foreach (var obj in Document.Objects)
		{
			if (obj.Uid == uid)
			{
				return obj;
			}
		}
		return null;
	}

	/// <summary>选择对象。null 清除选择；选择不进入 Undo 栈。</summary>
	public void SelectObject(ulong? uid)
	{
		if (uid.HasValue && FindObject(uid.Value) == null)
		{
			throw new InvalidOperationException("不能选择不在文档中的对象。");
		}
		if (_selectedObjectUid == uid)
		{
			return;
		}
		_selectedObjectUid = uid;
		SelectionChanged?.Invoke(this, EventArgs.Empty);
	}

	public void ClearSelection() => SelectObject(null);

	/// <summary>重命名对象。</summary>
	public void RenameGameObject(GameObjectRecord obj, string name)
	{
		EnsureObject(obj);
		ArgumentNullException.ThrowIfNull(name);
		if (obj.Name == name)
		{
			return;
		}
		string oldName = obj.Name;
		obj.Name = name;
		PushEdit(() => obj.Name = oldName, () => obj.Name = name);
	}

	/// <summary>删除对象及其连续 DFS 子树，并清理相关关系。</summary>
	public void DeleteGameObject(GameObjectRecord obj)
	{
		EnsureObject(obj);
		LayoutState before = CaptureLayout();
		var removed = CollectSubtree(obj);
		var removedSet = new HashSet<GameObjectRecord>(removed);
		Document.Objects.RemoveAll(removedSet.Contains);
		var relationEndpoints = new Dictionary<RelationRecord, (GameObjectRecord? Source, GameObjectRecord? Target)>();
		foreach (var relation in Document.Relations)
		{
			relationEndpoints[relation] = (
				relation.SourceIndex >= 0 && relation.SourceIndex < before.Order.Count ? before.Order[relation.SourceIndex] : null,
				relation.TargetIndex >= 0 && relation.TargetIndex < before.Order.Count ? before.Order[relation.TargetIndex] : null);
		}
		for (int i = Document.Relations.Count - 1; i >= 0; i--)
		{
			var relation = Document.Relations[i];
			var endpoints = relationEndpoints[relation];
			if (endpoints.Source == null || endpoints.Target == null || removedSet.Contains(endpoints.Source) || removedSet.Contains(endpoints.Target))
			{
				Document.Relations.RemoveAt(i);
			}
		}
		foreach (var relation in Document.Relations)
		{
			var endpoints = relationEndpoints[relation];
			relation.SourceIndex = endpoints.Source == null ? -1 : Document.Objects.IndexOf(endpoints.Source);
			relation.TargetIndex = endpoints.Target == null ? -1 : Document.Objects.IndexOf(endpoints.Target);
		}
		LayoutState after = CaptureLayout();
		bool selectedRemoved = false;
		if (_selectedObjectUid.HasValue)
		{
			foreach (var item in removed)
			{
				if (item.Uid == _selectedObjectUid.Value)
				{
					selectedRemoved = true;
					break;
				}
			}
		}
		if (selectedRemoved)
		{
			SelectObject(null);
		}
		PushEdit(
			() =>
			{
				RestoreLayout(before);
				if (selectedRemoved) SelectObject(obj.Uid);
			},
			() =>
			{
				RestoreLayout(after);
				if (selectedRemoved) SelectObject(null);
			});
	}

	/// <summary>删除对象上的组件。</summary>
	public void RemoveComponent(GameObjectRecord obj, GameComponentRecord component)
	{
		EnsureObject(obj);
		ArgumentNullException.ThrowIfNull(component);
		int index = obj.Components.IndexOf(component);
		if (index < 0)
		{
			throw new InvalidOperationException("组件不属于该对象。");
		}
		obj.Components.RemoveAt(index);
		PushEdit(() => obj.Components.Insert(index, component), () => obj.Components.Remove(component));
	}
	/// <summary>回滚最近一次编辑操作（栈内）。</summary>
	public void Undo()
	{
		if (_undoStack.Count == 0)
		{
			return;
		}
		var command = _undoStack.Pop();
		command.Undo();
		_redoStack.Push(command);
		MarkChanged();
	}

	/// <summary>重做最近一次被撤销的编辑操作。</summary>
	public void Redo()
	{
		if (_redoStack.Count == 0)
		{
			return;
		}
		var command = _redoStack.Pop();
		command.Redo();
		_undoStack.Push(command);
		MarkChanged();
	}

	// ---------- 保存 / 载入（Design ↔ .bscene 文本闭环） ----------
	/// <summary>保存：文档 → .bscene 文本（Serialize，确定性），并清除 dirty。</summary>
	public string SaveSceneText()
	{
		string text = GameWorldTextSerializer.Serialize(Document);
		IsDirty = false;
		return text;
	}

	/// <summary>序列化（不清 dirty）：保存前取文本，落盘成功后再 <see cref="MarkSaved"/>（reviewer P1：写入失败不得误清 dirty）。</summary>
	public string SerializeSceneForSave() => GameWorldTextSerializer.Serialize(Document);

	/// <summary>落盘成功后标记已保存（清 dirty）。</summary>
	public void MarkSaved() => IsDirty = false;

	/// <summary>载入：.bscene 文本 → 新 EditorSession（未注册组件按 R24 缺省策略保留 token）。</summary>
	public static EditorSession LoadScene(string text, ComponentSchemaRegistry? schemas = null)
	{
		var document = GameWorldTextSerializer.Deserialize(text);
		return new EditorSession(document, schemas);
	}

	/// <summary>
	/// 语义化载入（reviewer P1）：反序列化（token）→ 按已注册 schema Restore（Skip 容错）→ Capture
	/// 得 typed 属性的文档——Inspector 可直接读 Vector3/Quaternion，编辑单轴不覆盖其余未显示值。
	/// </summary>
	public static EditorSession LoadSceneWithSchemas(string text, ComponentSchemaRegistry schemas)
	{
		var parsed = GameWorldTextSerializer.Deserialize(text);
		var restoreOptions = new RestoreOptions
		{
			UnknownComponentPolicy = UnknownMemberPolicy.Skip,
			UnknownPropertyPolicy = UnknownMemberPolicy.Skip,
		};
		var world = GameWorldSerializer.Restore(parsed, restoreOptions, schemas, null);
		var typed = GameWorldSerializer.Capture(world);
		return new EditorSession(typed, schemas);
	}
}
