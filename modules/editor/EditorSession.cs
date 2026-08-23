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
	/// <summary>Design 文档模型（.bscene 数据；对象序 = 物理顺序，Uid 稳定身份）。</summary>
	public GameWorldSnapshot Document { get; }

	private readonly ComponentSchemaRegistry _schemas;
	private readonly Stack<Action> _undoStack = new();
	private ulong _nextUid = 1;

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
		_undoStack.Push(() => Document.Objects.Remove(record));
		return record;
	}

	/// <summary>设置父对象（层级编辑；null = 顶层）。父必须在文档中且非自身。</summary>
	public void SetParent(GameObjectRecord obj, GameObjectRecord? parent)
	{
		if (obj == parent)
		{
			throw new InvalidOperationException("对象不能成为自己的父。");
		}
		int objIdx = Document.Objects.IndexOf(obj);
		if (objIdx < 0)
		{
			throw new InvalidOperationException("对象不在文档中。");
		}
		if (parent != null && Document.Objects.IndexOf(parent) < 0)
		{
			throw new InvalidOperationException("父对象不在文档中。");
		}
		// 环检测：parent 不能是 obj 的后代（子树内）。
		if (parent != null && IsDescendant(parent, objIdx))
		{
			throw new InvalidOperationException("父对象不能是子对象的后代（环）。");
		}
		if (obj.ParentIndex == (parent == null ? -1 : Document.Objects.IndexOf(parent)))
		{
			return; // 无变化
		}

		// 快照：原列表顺序、每对象原父、原关系端点（引用身份）——undo 用。
		var beforeOrder = new List<GameObjectRecord>(Document.Objects);
		var beforeParent = new Dictionary<GameObjectRecord, GameObjectRecord?>();
		foreach (var o in beforeOrder)
		{
			int pi = o.ParentIndex;
			beforeParent[o] = pi >= 0 && pi < beforeOrder.Count ? beforeOrder[pi] : null;
		}
		var relSnapshot = new List<(GameObjectRecord? Src, GameObjectRecord? Dst)>();
		foreach (var r in Document.Relations)
		{
			relSnapshot.Add((
				r.SourceIndex >= 0 && r.SourceIndex < beforeOrder.Count ? beforeOrder[r.SourceIndex] : null,
				r.TargetIndex >= 0 && r.TargetIndex < beforeOrder.Count ? beforeOrder[r.TargetIndex] : null));
		}

		// 1) obj 的 DFS 连续子树区间。
		int subStart = objIdx;
		int subEnd = subStart + SubtreeSpan(subStart);
		var subtree = Document.Objects.GetRange(subStart, subEnd - subStart);

		// 2) 先移除子树。
		Document.Objects.RemoveRange(subStart, subEnd - subStart);

		// 3) 在"移除后"的列表上定位新插入点：父 DFS 子树末尾；顶层 = 末尾。
		int insertAt;
		if (parent == null)
		{
			insertAt = Document.Objects.Count;
		}
		else
		{
			int pIdx = Document.Objects.IndexOf(parent);
			insertAt = pIdx + SubtreeSpan(pIdx);
		}
		Document.Objects.InsertRange(insertAt, subtree);

		// 4) 全表按身份重建父引用：obj → parent，其余保持原父（beforeParent）。
		foreach (var o in Document.Objects)
		{
			var p = ReferenceEquals(o, obj) ? parent : beforeParent[o];
			o.ParentIndex = p == null ? -1 : Document.Objects.IndexOf(p);
		}

		// 5) 关系端点按引用重算（reviewer P1-2：重排对象必须同步关系索引）。
		ApplyRelationSnapshot(relSnapshot);

		// undo：恢复原顺序、原父、原关系端点。
		_undoStack.Push(() =>
		{
			var current = new List<GameObjectRecord>(Document.Objects);
			Document.Objects.Clear();
			foreach (var o in beforeOrder)
			{
				Document.Objects.Add(current[current.IndexOf(o)]);
			}
			for (int i = 0; i < Document.Objects.Count; i++)
			{
				var o = Document.Objects[i];
				var p = beforeParent[o];
				o.ParentIndex = p == null ? -1 : Document.Objects.IndexOf(p);
			}
			ApplyRelationSnapshot(relSnapshot);
		});
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
	public GameComponentRecord AddComponent(GameObjectRecord obj, ComponentSchema schema)
	{
		var rec = new GameComponentRecord { TypeName = schema.TypeName };
		obj.Components.Add(rec);
		_undoStack.Push(() => obj.Components.Remove(rec));
		return rec;
	}

	/// <summary>写组件属性（Design：Properties 键值对，值可序列化；undo 恢复旧值）。</summary>
	public void SetProperty(GameComponentRecord comp, string propertyName, object? value)
	{
		int i = comp.Properties.FindIndex(kv => kv.Key == propertyName);
		object? old = i >= 0 ? comp.Properties[i].Value : null;
		if (i >= 0)
		{
			comp.Properties[i] = new KeyValuePair<string, object?>(propertyName, value);
		}
		else
		{
			comp.Properties.Add(new KeyValuePair<string, object?>(propertyName, value));
		}
		// undo：删到旧状态（无旧值 → 移除；有旧值 → 还原）。
		_undoStack.Push(() =>
		{
			int j = comp.Properties.FindIndex(kv => kv.Key == propertyName);
			if (i >= 0)
			{
				comp.Properties[j] = new KeyValuePair<string, object?>(propertyName, old);
			}
			else if (j >= 0)
			{
				comp.Properties.RemoveAt(j);
			}
		});
	}

	// ---------- Undo ----------

	/// <summary>回滚最近一次编辑操作（栈内）。</summary>
	public void Undo()
	{
		if (_undoStack.Count == 0)
		{
			return;
		}
		_undoStack.Pop()();
	}

	// ---------- 保存 / 载入（Design ↔ .bscene 文本闭环） ----------

	/// <summary>保存：文档 → .bscene 文本（Serialize，确定性）。</summary>
	public string SaveSceneText() => GameWorldTextSerializer.Serialize(Document);

	/// <summary>载入：.bscene 文本 → 新 EditorSession（未注册组件按 R24 缺省策略保留 token）。</summary>
	public static EditorSession LoadScene(string text, ComponentSchemaRegistry? schemas = null)
	{
		var document = GameWorldTextSerializer.Deserialize(text);
		return new EditorSession(document, schemas);
	}
}
