// SPDX-License-Identifier: MIT
// DesignPreviewHost.cs —— 预演世界投影（O7，O7-编辑器第一切片.md §4）
//
// 3D view 显示路径的第一步（纯 .NET）：把 Design 文档（GameWorldSnapshot）成长为
// 预演 Runtime 世界（Restore），再用 SceneProjector 读出 Transform/Mesh → 渲染命令。
// 单向：Design 是权威，预演只是投影显示的副本——改文档不会自动改预演，需重新 Project。
// 命令负载：Sola3d.MainLoop.GatewayCommand（下行）。

using System.Collections.Generic;
using System.Numerics;
using Sola3d.GameObject;
using Sola3d.MainLoop;

namespace Sola3d.Editor;

/// <summary>预演渲染命令（Design → RenderGateway 下行负载）。</summary>
public sealed record PreviewRenderCommand : GatewayCommand
{
	public ulong ObjectUid { get; init; }
	public string MeshPath { get; init; } = "";
	public Vector3 Position { get; init; }
	public Vector3 Scale { get; init; }
	public Quaternion Rotation { get; init; }
}

/// <summary>
/// 遍历预演世界（Restore 所得），把带 Transform+Mesh 的对象投影为渲染命令。
/// 与编辑器文档单向：输入快照 → 命令流，不改任何文档/世界状态。
/// </summary>
public sealed class SceneProjector
{
	/// <summary>投影整棵对象树（DFS），产出渲染命令列表。</summary>
	public List<PreviewRenderCommand> Project(GameWorld world)
	{
		var commands = new List<PreviewRenderCommand>();
		foreach (var root in world.Roots)
		{
			Walk(root, world, commands);
		}
		return commands;
	}

	private void Walk(Sola3d.GameObject.GameObject obj, GameWorld world, List<PreviewRenderCommand> commands)
	{
		var tf = obj.GetComponent<TransformComponent>();
		var mesh = obj.GetComponent<MeshComponent>();
		if (tf != null && mesh != null)
		{
			commands.Add(new PreviewRenderCommand
			{
				ObjectUid = obj.Uid.Value,
				MeshPath = mesh.MeshPath,
				Position = tf.Position,
				Scale = tf.Scale,
				Rotation = tf.Rotation,
			});
		}
		foreach (var child in obj.Children)
		{
			Walk(child, world, commands);
		}
	}
}

/// <summary>
/// 面向编辑器的预演宿主（纯 .NET）：Document（快照）→ Restore（预演世界）→ SceneProjector → 命令流。
/// RenderGateway 消费命令（Godot 侧真桥在 godot-slice）。
/// </summary>
public sealed class DesignPreviewHost
{
	private readonly ComponentSchemaRegistry _schemas;
	private readonly SceneProjector _projector = new();

	public DesignPreviewHost(ComponentSchemaRegistry? schemas = null)
	{
		_schemas = schemas ?? new ComponentSchemaRegistry();
	}

	/// <summary>把 Design 文档变成预演世界（每次调用重建——文档是权威，预演随编辑刷新）。</summary>
	public GameWorld BuildPreviewWorld(GameWorldSnapshot document)
	{
		return GameWorldSerializer.Restore(document, _schemas, null);
	}

	/// <summary>预演世界 → 渲染命令流（喂给 RenderGateway）。</summary>
	public IReadOnlyList<PreviewRenderCommand> ProjectToCommands(GameWorld previewWorld)
	{
		return _projector.Project(previewWorld);
	}
}
