// SPDX-License-Identifier: MIT
// MainWindowViewModel.cs —— 编辑器主视图模型：Hierarchy（增量树）/ Inspector（Transform）/ Save-Load
//
// 分层：编辑核心在 src/libs/editor（EditorSession，纯 .NET）；本 VM 做 UI 状态与绑定源。
// 树策略（增量，对齐主流编辑器）：维护 对象Uid↔树节点 映射；仅在结构操作（新建/删除/加载）后
// 差量同步（节点实例复用 → 展开/选择/滚动保持，不闪）；属性编辑（Inspector）不触碰树。

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Sola3d.Editor;
using Sola3d.GameObject;

namespace Sola3d.EditorUi.ViewModels;

/// <summary>编辑器主视图模型：持有 EditorSession 文档，暴露 Hierarchy 树 / Inspector 字段 / 保存加载。</summary>
public partial class MainWindowViewModel : ViewModelBase
{
	private EditorSession _session;
	private bool _suppress; // 双向同步防重入（树选择 ↔ 文档选择）
	private readonly Dictionary<ulong, HierarchyNodeViewModel> _nodesByUid = new();
	private string? _currentPath;

	// 组件 TypeName = ComponentType.FullName（如 Sola3d.GameObject.TransformComponent），不能写短名。
	private static readonly string TransformTypeName = typeof(TransformComponent).FullName!;

	public MainWindowViewModel()
	{
		_session = new EditorSession();
		// 注册编辑器可用的组件 Schema（Hierarchy/Inspector 编辑用）。
		_session.Schemas.Register<TransformComponent>();
		_session.Schemas.Register<MeshComponent>();
		HookSession(_session);
		var root = _session.CreateGameObject("Root");
		_session.AddComponent(root, _session.Schemas.Get<TransformComponent>());
		var child = _session.CreateGameObject("Child");
		_session.AddComponent(child, _session.Schemas.Get<TransformComponent>());
		// Child 补一个可渲染 Mesh（预览立即可见）；Root 保持空组织根语义。
		_session.AddComponent(child, _session.Schemas.Get<MeshComponent>());
		_session.SetParent(child, root);
		SyncHierarchyTree();
		_session.SelectObject(root.Uid);
	}

	/// <summary>编辑会话（权威文档）。</summary>
	public EditorSession Session => _session;

	/// <summary>Hierarchy 树根集合。</summary>
	public ObservableCollection<HierarchyNodeViewModel> Hierarchy { get; } = new();

	/// <summary>当前选中树节点（TreeView SelectedItem 两路绑）。</summary>
	[ObservableProperty]
	private HierarchyNodeViewModel? _selectedNode;

	// ---------- Inspector：Transform（Position / Rotation(欧拉·度) / Scale）----------

	[ObservableProperty] private string _positionX = "0";
	[ObservableProperty] private string _positionY = "0";
	[ObservableProperty] private string _positionZ = "0";
	[ObservableProperty] private string _rotationX = "0";
	[ObservableProperty] private string _rotationY = "0";
	[ObservableProperty] private string _rotationZ = "0";
	[ObservableProperty] private string _scaleX = "1";
	[ObservableProperty] private string _scaleY = "1";
	[ObservableProperty] private string _scaleZ = "1";

	/// <summary>Inspector 提示（选中对象缺 Transform 组件时显示）。</summary>
	[ObservableProperty] private string _inspectorHint = "";

	partial void OnSelectedNodeChanged(HierarchyNodeViewModel? value)
	{
		if (_suppress || value?.Record == null)
		{
			return;
		}
		_session.SelectObject(value.Record.Uid);
	}

	partial void OnPositionXChanged(string value) => TryApplyTransform();
	partial void OnPositionYChanged(string value) => TryApplyTransform();
	partial void OnPositionZChanged(string value) => TryApplyTransform();
	partial void OnRotationXChanged(string value) => TryApplyTransform();
	partial void OnRotationYChanged(string value) => TryApplyTransform();
	partial void OnRotationZChanged(string value) => TryApplyTransform();
	partial void OnScaleXChanged(string value) => TryApplyTransform();
	partial void OnScaleYChanged(string value) => TryApplyTransform();
	partial void OnScaleZChanged(string value) => TryApplyTransform();

	private void HookSession(EditorSession session)
	{
		// 属性编辑（SetProperty）只触发标题/Inspector 刷新；树结构刷新由结构操作显式驱动（增量 SyncHierarchyTree）。
		session.Changed += (_, _) =>
		{
			UpdateTitle();
			SyncInspector();
		};
		session.SelectionChanged += (_, _) => SyncInspector();
	}

	private void UpdateTitle()
	{
		string sceneName = _currentPath == null ? "未命名.bscene" : Path.GetFileName(_currentPath);
		DocumentTitle = $"{sceneName}（{_session.Document.Objects.Count} 对象）" + (_session.IsDirty ? " *" : "");
	}

	// ---------- 增量 Hierarchy 树 ----------

	/// <summary>
	/// 差量同步树（结构操作后调用）：遍历 Document（DFS 序）get-or-create 节点（实例复用），
	/// 删除消失对象，按 Document 相对序对齐各父 Children。节点复用 → 展开/选中/滚动保持不闪。
	/// </summary>
	private void SyncHierarchyTree()
	{
		_suppress = true;
		try
		{
			var seen = new HashSet<ulong>();
			foreach (var record in _session.Document.Objects)
			{
				GetOrCreateNode(record);
				seen.Add(record.Uid);
			}

			// 删除：旧节点不再见于新文档 → 从树移除 + 从映射移除（含后代）。
			foreach (var uid in _nodesByUid.Keys.ToList())
			{
				if (!seen.Contains(uid))
				{
					RemoveNodeTree(_nodesByUid[uid]);
				}
			}

			// 顺序对齐：各父 Children / 根集合按 Document 相对序重排（差量移动）。
			foreach (var node in _nodesByUid.Values)
			{
				OrderChildren(node);
			}
			OrderRoots();

			// 选择保持（对象可能被删 → 回到 null）。
			SelectedNode = _session.SelectedObjectUid is ulong su && _nodesByUid.TryGetValue(su, out var sn) ? sn : null;
		}
		finally
		{
			_suppress = false;
		}
	}

	private HierarchyNodeViewModel GetOrCreateNode(GameObjectRecord record)
	{
		if (_nodesByUid.TryGetValue(record.Uid, out var existing))
		{
			if (existing.Name != record.Name)
			{
				existing.Name = record.Name; // 重命名同步（[ObservableProperty] 通知 UI）
			}
			return existing;
		}
		var node = new HierarchyNodeViewModel(record);
		_nodesByUid[record.Uid] = node;
		// 父节点必已创建（DFS 父先于子）。
		if (record.ParentIndex >= 0 && record.ParentIndex < _session.Document.Objects.Count)
		{
			var parentNode = _nodesByUid[_session.Document.Objects[record.ParentIndex].Uid];
			parentNode.Children.Add(node);
		}
		else
		{
			Hierarchy.Add(node);
		}
		return node;
	}

	private void RemoveNodeTree(HierarchyNodeViewModel node)
	{
		Hierarchy.Remove(node);
		foreach (var parent in _nodesByUid.Values)
		{
			parent.Children.Remove(node);
		}
		RemoveDescendantsFromMap(node);
	}

	private void RemoveDescendantsFromMap(HierarchyNodeViewModel node)
	{
		foreach (var child in node.Children.ToList())
		{
			RemoveDescendantsFromMap(child);
		}
		_nodesByUid.Remove(node.Record.Uid);
	}

	private void OrderChildren(HierarchyNodeViewModel node)
	{
		int parentIndex = _session.Document.Objects.IndexOf(node.Record);
		if (parentIndex < 0)
		{
			return;
		}
		var ordered = _session.Document.Objects
			.Where(r => r.ParentIndex == parentIndex)
			.Select(r => _nodesByUid[r.Uid])
			.ToList();
		ApplyOrder(node.Children, ordered);
	}

	private void OrderRoots()
	{
		var ordered = _session.Document.Objects
			.Where(r => r.ParentIndex == -1)
			.Select(r => _nodesByUid[r.Uid])
			.ToList();
		ApplyOrder(Hierarchy, ordered);
	}

	private static void ApplyOrder(ObservableCollection<HierarchyNodeViewModel> collection, List<HierarchyNodeViewModel> ordered)
	{
		for (int i = 0; i < ordered.Count; i++)
		{
			if (i < collection.Count && ReferenceEquals(collection[i], ordered[i]))
			{
				continue;
			}
			int index = collection.IndexOf(ordered[i]);
			if (index >= 0)
			{
				collection.RemoveAt(index);
			}
			collection.Insert(i, ordered[i]);
		}
		while (collection.Count > ordered.Count)
		{
			collection.RemoveAt(collection.Count - 1);
		}
	}

	// ---------- Inspector：Transform ----------

	/// <summary>Inspector 同步：把选中对象 Transform 值写入绑定字段（度）。</summary>
	private void SyncInspector()
	{
		_suppress = true;
		try
		{
			var obj = _session.SelectedObject;
			var tf = obj == null ? null : FindTransform(obj);
			if (tf == null)
			{
				InspectorHint = "该对象没有 Transform 组件";
				PositionX = PositionY = PositionZ = RotationX = RotationY = RotationZ = "0";
				ScaleX = ScaleY = ScaleZ = "1";
				return;
			}
			InspectorHint = "";
			var pos = ReadVector(tf, "Position") ?? Vector3.Zero;
			var rot = ReadQuaternion(tf, "Rotation") ?? Quaternion.Identity;
			var scale = ReadVector(tf, "Scale") ?? Vector3.One;
			PositionX = Fmt(pos.X); PositionY = Fmt(pos.Y); PositionZ = Fmt(pos.Z);
			var euler = ToEulerDegrees(rot);
			RotationX = Fmt(euler.X); RotationY = Fmt(euler.Y); RotationZ = Fmt(euler.Z);
			ScaleX = Fmt(scale.X); ScaleY = Fmt(scale.Y); ScaleZ = Fmt(scale.Z);
		}
		finally
		{
			_suppress = false;
		}
	}

	/// <summary>Inspector 编辑 → SetProperty（带 undo；属性编辑不触碰 Hierarchy 树）。</summary>
	private void TryApplyTransform()
	{
		if (_suppress)
		{
			return;
		}
		var obj = _session.SelectedObject;
		var tf = obj == null ? null : FindTransform(obj);
		if (tf == null)
		{
			return;
		}
		if (TryParse3(PositionX, PositionY, PositionZ, out var pos) &&
			TryParse3(RotationX, RotationY, RotationZ, out var euler) &&
			TryParse3(ScaleX, ScaleY, ScaleZ, out var scale))
		{
			_session.SetProperty(tf, "Position", pos);
			_session.SetProperty(tf, "Rotation", FromEulerDegrees(euler));
			_session.SetProperty(tf, "Scale", scale);
		}
	}

	// ---------- 对象操作（结构操作 → 增量同步树） ----------

	/// <summary>新建对象（默认带 Transform 组件；结构变化 → SyncHierarchyTree）。</summary>
	public void CreateObject(string name)
	{
		var record = _session.CreateGameObject(string.IsNullOrWhiteSpace(name) ? $"Obj{_session.Document.Objects.Count + 1}" : name);
		// 新建对象默认带 Transform 组件（Inspector 可编辑）。
		_session.AddComponent(record, _session.Schemas.Get<TransformComponent>());
		// 主流（Unity 基准）：选中对象时建为其子级；无选中 → 顶层。
		var parent = _session.SelectedObject;
		if (parent != null && parent != record)
		{
			_session.SetParent(record, parent);
		}
		// 主流：新建后自动选中新对象。
		_session.SelectObject(record.Uid);
		SyncHierarchyTree();
	}

	/// <summary>删除选中对象（子树 + 关系清理，可 undo；结构变化 → SyncHierarchyTree）。</summary>
	public void DeleteSelected()
	{
		var record = _session.SelectedObject;
		if (record != null)
		{
			_session.DeleteGameObject(record);
			SyncHierarchyTree();
		}
	}

	// ---------- 命令（供工具栏按钮与统一快捷键表（Window.InputBindings）绑定） ----------

	/// <summary>保存对话框请求（View 装配 StorageProvider 后调 SaveScene）。</summary>
	public Action? SaveRequested { get; set; }

	/// <summary>打开对话框请求（View 装配 StorageProvider 后调 LoadScene）。</summary>
	public Action? LoadRequested { get; set; }

	[RelayCommand] private void New() => CreateObject("Object");
	[RelayCommand] private void Delete() => DeleteSelected();
	[RelayCommand] private void UndoCommand() => Undo();
	[RelayCommand] private void RedoCommand() => Redo();
	[RelayCommand] private void Save() => SaveRequested?.Invoke();
	[RelayCommand] private void Load() => LoadRequested?.Invoke();

	// ---------- 保存 / 加载（纯方法：文件选择由 View 用 StorageProvider 装配）----------

	/// <summary>当前保存路径（首次保存选定后，后续 Ctrl+S 直接覆盖此文件）。</summary>
	public string? CurrentPath => _currentPath;

	public bool HasPath => _currentPath != null;

	public void SaveScene(string path)
	{
		File.WriteAllText(path, _session.SaveSceneText());
		_currentPath = path;
		UpdateTitle();
	}

	/// <summary>撤销（Ctrl+Z）。</summary>
	public void Undo() => _session.Undo();

	/// <summary>重做（Ctrl+Shift+Z / Ctrl+Y）。</summary>
	public void Redo() => _session.Redo();

	public void LoadScene(string path)
	{
		var text = File.ReadAllText(path);
		var loaded = EditorSession.LoadScene(text, new ComponentSchemaRegistry());
		_session = loaded;
		HookSession(loaded);
		_nodesByUid.Clear();
		Hierarchy.Clear();
		_currentPath = path;
		SyncHierarchyTree();
		UpdateTitle();
	}

	// ---------- 辅助 ----------

	private static GameComponentRecord? FindTransform(GameObjectRecord obj)
		=> obj.Components.FirstOrDefault(c => c.TypeName == TransformTypeName);

	private static Vector3? ReadVector(GameComponentRecord comp, string key)
	{
		foreach (var kv in comp.Properties)
		{
			if (kv.Key == key && kv.Value is Vector3 v)
			{
				return v;
			}
		}
		return null;
	}

	private static Quaternion? ReadQuaternion(GameComponentRecord comp, string key)
	{
		foreach (var kv in comp.Properties)
		{
			if (kv.Key == key && kv.Value is Quaternion q)
			{
				return q;
			}
		}
		return null;
	}

	private static bool TryParse3(string x, string y, string z, out Vector3 v)
	{
		if (float.TryParse(x, out var fx) && float.TryParse(y, out var fy) && float.TryParse(z, out var fz))
		{
			v = new Vector3(fx, fy, fz);
			return true;
		}
		v = default;
		return false;
	}

	private static string Fmt(float v) => v.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture);

	/// <summary>Quaternion → 欧拉角（度）。</summary>
	private static Vector3 ToEulerDegrees(Quaternion q)
	{
		var e = ToEulerRadians(q);
		return new Vector3(
			float.RadiansToDegrees(e.X),
			float.RadiansToDegrees(e.Y),
			float.RadiansToDegrees(e.Z));
	}

	private static Vector3 ToEulerRadians(Quaternion q)
	{
		var y = MathF.Atan2(2f * (q.Y * q.W - q.X * q.Z), 1f - 2f * (q.Y * q.Y + q.Z * q.Z));
		var x = MathF.Asin(Math.Clamp(2f * (q.X * q.W + q.Y * q.Z), -1f, 1f));
		var z = MathF.Atan2(2f * (q.X * q.Y + q.Z * q.W), 1f - 2f * (q.X * q.X + q.Z * q.Z));
		return new Vector3(x, y, z);
	}

	private static Quaternion FromEulerDegrees(Vector3 degrees)
	{
		return Quaternion.CreateFromYawPitchRoll(
			float.DegreesToRadians(degrees.Y),
			float.DegreesToRadians(degrees.X),
			float.DegreesToRadians(degrees.Z));
	}

	/// <summary>窗口标题（dirty 标记）。</summary>
	[ObservableProperty]
	private string _documentTitle = "Sola3d Editor";
}