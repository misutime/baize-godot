// SPDX-License-Identifier: MIT
// HierarchyNodeViewModel.cs —— Hierarchy 树节点（对象文档 record 的 UI 投影）

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using Sola3d.GameObject;

namespace Sola3d.EditorUi.ViewModels;

/// <summary>Hierarchy 树节点：绑定对象文档 record，Children 按 DFS 子级填充。</summary>
public partial class HierarchyNodeViewModel : ViewModelBase
{
	public HierarchyNodeViewModel(GameObjectRecord record)
	{
		Record = record;
		Name = record.Name;
	}

	/// <summary>对象文档 record（权威身份；Uid 稳定）。</summary>
	public GameObjectRecord Record { get; }

	/// <summary>显示名（对象 name）。</summary>
	[ObservableProperty]
	private string _name;

	/// <summary>展开状态（TreeViewItem 样式两路绑；重建树时按 Uid 恢复）。</summary>
	[ObservableProperty]
	private bool _isExpanded = true;
	public string UidText => "@" + Record.Uid.ToString("x16");

	/// <summary>子节点（DFS 序，由 VM 构建时填充）。</summary>
	public ObservableCollection<HierarchyNodeViewModel> Children { get; } = new();
}