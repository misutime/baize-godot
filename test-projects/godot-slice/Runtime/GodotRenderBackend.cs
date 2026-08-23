// SPDX-License-Identifier: MIT
// GodotRenderBackend.cs —— RenderingServer 真桥（O6，O6-GameWorldBackend与垂直切片.md §5）
//
// 状态 C（Server-backed）第一块实体砖：不经过 Node3D/StaticMesh3D——直接
// GameWorld 语义 → RenderCommand → 本 backend → RenderingServer RID。
// §14.6 方向唯一：只读投影到 Server，永不反写 Gameplay。

using Godot;
using Sola3d.MainLoop;

namespace Sola3d.GodotSlice;

/// <summary>
/// RenderingServer 真桥：把投影出的渲染命令落到 RID 上。
/// 最小切片：实例建立 + Transform 投影（Mesh RID 建立走 MeshCreate/InstanceCreate2）。
/// RenderingServer 为静态 API；scenario 由外部注入（真实 Viewport 环境调用 Initialize，
/// headless 测试不调用——保持 O6 纯 .NET 验证与 Godot 桥编译验证两层分离）。
/// </summary>
public sealed partial class GodotRenderBackend : IRenderBackend
{
	private Rid _scenario;
	private readonly System.Collections.Generic.Dictionary<System.Numerics.Vector3, Rid> _meshCache = new();

	/// <summary>注入渲染 scenario（真实 Viewport 环境调用；headless 测试不调）。</summary>
	public void Initialize(Rid scenario)
	{
		_scenario = scenario;
	}

	public void BeginFrame(float nowSeconds) { }

	public void EndFrame(float nowSeconds) { }

	/// <summary>消费投影命令：建实例 + 设 Transform（Godot 侧最小实现）。</summary>
	public void Consume(System.Collections.Generic.IReadOnlyList<BackendCommand> commands)
	{
		foreach (var c in commands)
		{
			if (c is VerticalSliceRenderCommand rc)
			{
				Apply(rc);
			}
		}
	}

	private void Apply(VerticalSliceRenderCommand rc)
	{
		// 建/复用 Mesh RID（按 MeshPath 缓存，最小实现；真实 surface 数据由 O6 后续 resource 层补）。
		if (!_meshCache.TryGetValue(rc.MeshPathKey, out Rid meshRid))
		{
			meshRid = RenderingServer.MeshCreate();
			_meshCache[rc.MeshPathKey] = meshRid;
		}
		Rid instance = RenderingServer.InstanceCreate2(meshRid, _scenario);
		var t = new Transform3D(
			new Basis(new Quaternion(rc.Rotation.X, rc.Rotation.Y, rc.Rotation.Z, rc.Rotation.W)),
			new Vector3(rc.Position.X, rc.Position.Y, rc.Position.Z));
		RenderingServer.InstanceSetTransform(instance, t);
	}
}

/// <summary>Godot 侧接收的渲染命令形态（跨模块最小共用；O6 后续随投影契约定型）。</summary>
public sealed record VerticalSliceRenderCommand : BackendCommand
{
	public System.Numerics.Vector3 MeshPathKey { get; init; }
	public System.Numerics.Vector3 Position { get; init; }
	public System.Numerics.Quaternion Rotation { get; init; }
}
