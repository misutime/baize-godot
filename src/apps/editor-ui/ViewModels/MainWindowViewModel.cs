// SPDX-License-Identifier: MIT
// MainWindowViewModel.cs —— 主窗口视图模型（骨架：接 EditorSession 冒烟；下一步演化 Hierarchy/Inspector 模型）
//
// 分层：编辑核心在 src/libs/editor（EditorSession，纯 .NET）；本 VM 只做 UI 状态与绑定源。
// 说明：EditorSession.Document 是编辑器权威文档；骨架用一条文档摘要冒烟，未来扩展为
//   Hierarchy（对象树）+ Inspector（选中对象 Transform 编辑）+ Save/Load 命令。

using CommunityToolkit.Mvvm.ComponentModel;
using Sola3d.Editor;

namespace Sola3d.EditorUi.ViewModels;

/// <summary>编辑器主视图模型：持有 EditorSession 文档，暴露 UI 标题/摘要。</summary>
public partial class MainWindowViewModel : ViewModelBase
{
	private readonly EditorSession _session;

	public MainWindowViewModel()
	{
		// 骨架冒烟：建一个含层级的小文档（O8-A 编辑核心）。
		_session = new EditorSession();
		var root = _session.CreateGameObject("Root");
		var child = _session.CreateGameObject("Child");
		_session.SetParent(child, root);
		_session.SelectObject(root.Uid);
		DocumentTitle = $"Sola3d Editor — 未命名.bscene（{_session.Document.Objects.Count} 对象）";
	}

	/// <summary>编辑会话（未来 Hierarchy/Inspector 的绑定源）。</summary>
	public EditorSession Session => _session;

	/// <summary>窗口标题（示例：[ObservableProperty] 源生字段 → 编译绑定）。</summary>
	[ObservableProperty]
	private string _documentTitle = "Sola3d Editor";
}