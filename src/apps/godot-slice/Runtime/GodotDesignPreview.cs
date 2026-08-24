// SPDX-License-Identifier: MIT
// GodotDesignPreview.cs —— 编辑器 3D view 显示路径真桥（O7，O7-编辑器第一切片.md §4）
//
// Design 文档 → 预演世界 → SceneProjector（Sola3d.Editor）→ GodotRenderGateway → RenderingServer。
// 本层只做"接上、可编译"：真窗口渲染（O7.5 编辑器壳）时 Initialize(scenario) 后每帧调用 Refresh。
// §14.6 方向唯一：Design 是权威，预演只投影显示，不反写文档。

using Godot;
using Sola3d.Editor;
using Sola3d.GameObject;

namespace Sola3d.GodotSlice;

/// <summary>编辑器 3D 预览桥：把 Design 文档渲染到 RenderingServer（状态 C，无 Node3D）。</summary>
public sealed partial class GodotDesignPreview
{
	private readonly DesignPreviewHost _host;
	private readonly GodotRenderGateway _renderGateway;
	private readonly RenderSnapshotTracker _tracker = new();

	public GodotDesignPreview(ComponentSchemaRegistry? schemas = null)
	{
		_host = new DesignPreviewHost(schemas);
		_renderGateway = new GodotRenderGateway();
	}

	/// <summary>注入渲染 scenario（真实 Viewport 环境调用；headless 编译验证不调）。</summary>
	public void Initialize(Rid scenario) => _renderGateway.Initialize(scenario);

	/// <summary>把 Design 文档投影到 RenderingServer（每次调用重建预演世界——文档是权威）。</summary>
	/// <summary>把 Design 文档投影到 RenderingServer（每次调用重建预演世界——文档是权威；tracker 产出差异）。</summary>
	public int Refresh(GameWorldSnapshot document)
	{
		var world = _host.BuildPreviewWorld(document);
		var commands = _host.ProjectToCommands(world);
		_renderGateway.Consume(_tracker.Diff(commands));
		return commands.Count;
	}
}
