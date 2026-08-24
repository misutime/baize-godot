// SPDX-License-Identifier: MIT
// RenderSnapshotTracker.cs —— O8-B 渲染快照差异跟踪（纯 .NET，零 Godot 依赖）
//
// Editor 预演刷新每次重建整帧投影命令；Gateway 端需要一个"相对上一帧"的差异，
// 才能把文档中被删除的对象从 RenderingServer 释放（否则旧 RID 永远显示）。
// 本类职责：维护上一帧活跃 Uid 集合，产出差异命令流（remove + upsert）。
// MeshPath 变更不在此判断——原命令原样下发，由 GodotRenderGateway 内部 rebase。

using System.Collections.Generic;
using Sola3d.MainLoop;

namespace Sola3d.Editor;

/// <summary>删除渲染对象命令（下行：对象从文档消失 → Gateway 释放实例）。</summary>
public sealed record PreviewRemoveCommand : GatewayCommand
{
	/// <summary>要移除的对象稳定 Uid。</summary>
	public ulong ObjectUid { get; init; }
}

/// <summary>
/// 渲染快照差异跟踪：输入一整帧投影命令，输出相对上一帧的差异命令流。
/// <list type="bullet">
/// <item>上帧存活、本帧消失的 Uid → <see cref="PreviewRemoveCommand"/>（删除对象后旧实例被释放）；</item>
/// <item>本帧存在的 Uid → 原 <see cref="PreviewRenderCommand"/>（含新建/更新/换 MeshPath；Gateway 端自行 rebase）；</item>
/// <item>同帧内重复 Uid 去重（最后一次为准）。</item>
/// </list>
/// 状态只有活跃集合与最近命令，无 Godot 依赖，可 headless 单测。
/// </summary>
public sealed class RenderSnapshotTracker
{
	private readonly HashSet<ulong> _alive = new();

	/// <summary>把整帧投影命令转为差异命令流，并推进内部快照。</summary>
	public List<GatewayCommand> Diff(IReadOnlyList<PreviewRenderCommand> frame)
	{
		var result = new List<GatewayCommand>();
		var frameUids = new HashSet<ulong>();
		var latest = new Dictionary<ulong, PreviewRenderCommand>();
		foreach (var command in frame)
		{
			frameUids.Add(command.ObjectUid);
			latest[command.ObjectUid] = command;
		}

		// 上帧存活、本帧消失 → 删除。
		foreach (var uid in _alive)
		{
			if (!frameUids.Contains(uid))
			{
				result.Add(new PreviewRemoveCommand { ObjectUid = uid });
			}
		}

		// 本帧对象 → 原命令（新建/更新/MeshPath 变更都由 Gateway 按需处理）。
		foreach (var pair in latest)
		{
			result.Add(pair.Value);
		}

		_alive.Clear();
		_alive.UnionWith(frameUids);
		return result;
	}
}