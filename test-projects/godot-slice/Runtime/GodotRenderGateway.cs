// SPDX-License-Identifier: MIT
// GodotRenderGateway.cs —— RenderingServer 真桥（O6/O7.5，O6-GameWorldGateway与垂直切片.md）
//
// 状态 C（Server-backed）实体桥：不经过 Node3D/StaticMesh3D——直接
// GameWorld 语义 → PreviewRenderCommand → 本 Gateway → RenderingServer RID。
// O7.5：支持 cube 几何 surface + 按对象 Uid 缓存实例（同命令只 SetTransform，不重复建实例）。
// §14.6 方向唯一：只读投影到 Server，永不反写 Gameplay。

using Godot;
using Sola3d.Editor;
using Sola3d.MainLoop;

namespace Sola3d.GodotSlice;

/// <summary>
/// RenderingServer 真桥：把投影出的渲染命令落到 RID 上。
/// scenario 由外部注入（真实 Viewport 环境调用 Initialize；headless 测试不调用）。
/// </summary>
public sealed partial class GodotRenderGateway : IRenderGateway
{
	private Rid _scenario;
	private readonly System.Collections.Generic.Dictionary<string, Rid> _meshCache = new();
	private readonly System.Collections.Generic.Dictionary<ulong, Rid> _instanceCache = new();
	private readonly System.Collections.Generic.List<Rid> _meshRids = new();
	private readonly System.Collections.Generic.List<Rid> _ownedRids = new();
	private bool _surfaceAdded;

	/// <summary>注入渲染 scenario（真实 Viewport 环境调用；headless 测试不调）。</summary>
	public void Initialize(Rid scenario)
	{
		_scenario = scenario;
	}

	public void BeginFrame(float nowSeconds) { }

	public void EndFrame(float nowSeconds) { }

	/// <summary>消费投影命令：建实例 + 设 Transform（Godot 侧最小实现）。</summary>
	public void Consume(System.Collections.Generic.IReadOnlyList<GatewayCommand> commands)
	{
		foreach (var c in commands)
		{
			if (c is PreviewRenderCommand rc)
			{
				Apply(rc);
			}
		}
	}

	private void Apply(PreviewRenderCommand rc)
	{
		// Mesh RID（首个对象加载 cube 几何 surface——O7.5 最小切片只有 cube）。
		if (!_meshCache.TryGetValue(rc.MeshPath, out Rid meshRid))
		{
			meshRid = RenderingServer.MeshCreate();
			if (!_surfaceAdded)
			{
				AddCubeSurface(meshRid);
				_surfaceAdded = true;
			}
			_meshCache[rc.MeshPath] = meshRid;
			_meshRids.Add(meshRid);
		}
		// 按对象 Uid 缓存实例（同一对象重复推送只 SetTransform）。
		if (!_instanceCache.TryGetValue(rc.ObjectUid, out Rid instance))
		{
			instance = RenderingServer.InstanceCreate2(meshRid, _scenario);
			_instanceCache[rc.ObjectUid] = instance;
		}
		var basis = new Basis(new Quaternion(rc.Rotation.X, rc.Rotation.Y, rc.Rotation.Z, rc.Rotation.W));
		var t = new Transform3D(basis, new Vector3(rc.Position.X, rc.Position.Y, rc.Position.Z))
			.Scaled(new Vector3(rc.Scale.X, rc.Scale.Y, rc.Scale.Z)); // reviewer P1-4：应用 Scale（无则单位缩放）
		RenderingServer.InstanceSetTransform(instance, t);
	}

	public void Dispose()
	{
		foreach (Rid instance in _instanceCache.Values)
		{
			RenderingServer.FreeRid(instance);
		}
		foreach (Rid mesh in _meshRids)
		{
			RenderingServer.FreeRid(mesh);
		}
		foreach (Rid rid in _ownedRids)
		{
			RenderingServer.FreeRid(rid);
		}
		_instanceCache.Clear();
		_meshCache.Clear();
		_meshRids.Clear();
		_ownedRids.Clear();
	}
	/// <summary>诊断：mesh/实例缓存计数（O7.5 排障用）。</summary>
	public string DebugInfo => $"mesh={_meshCache.Count} instance={_instanceCache.Count} surface={(_surfaceAdded ? 1 : 0)} scenario={(_scenario.IsValid ? "valid" : "INVALID")}";

	/// <summary>给 mesh 加立方体 surface（O7.5：24 顶点/36 索引）。</summary>
	private void AddCubeSurface(Rid mesh)
	{
		// Godot 4 数组下标约定（RenderingServer.ArrayType）：0=Vertex, 1=Normal, 12=Index。
		// Vertex/Normal 用 Vector3[]（Marshaling 自动转 PackedVector3Array），Index 用 int[]。
		var verts = new Vector3[Sola3d.Host.CubeGeometry.Vertices.Length];
		for (int i = 0; i < verts.Length; i++)
		{
			var v = Sola3d.Host.CubeGeometry.Vertices[i];
			verts[i] = new Vector3(v.X, v.Y, v.Z);
		}
		var norms = new Vector3[Sola3d.Host.CubeGeometry.Normals.Length];
		for (int i = 0; i < norms.Length; i++)
		{
			var n = Sola3d.Host.CubeGeometry.Normals[i];
			norms[i] = new Vector3(n.X, n.Y, n.Z);
		}
		var arrays = new Godot.Collections.Array();
		arrays.Resize((int)RenderingServer.ArrayType.Max);
		arrays[(int)RenderingServer.ArrayType.Vertex] = verts;
		arrays[(int)RenderingServer.ArrayType.Normal] = norms;
		arrays[(int)RenderingServer.ArrayType.Index] = Sola3d.Host.CubeGeometry.Indices; // int[]
		RenderingServer.MeshAddSurfaceFromArrays(mesh, RenderingServer.PrimitiveType.Triangles, arrays); // 关键：必须调用，否则 surface=0
																										 // RenderingServer.MaterialCreate() 只创建空材质；必须绑定 spatial shader。
		var shader = RenderingServer.ShaderCreate();
		RenderingServer.ShaderSetCode(shader, "shader_type spatial; render_mode unshaded; void fragment() { ALBEDO = vec3(0.0, 1.0, 1.0); }");
		var material = RenderingServer.MaterialCreate();
		RenderingServer.MaterialSetShader(material, shader);
		RenderingServer.MeshSurfaceSetMaterial(mesh, 0, material);
		_ownedRids.Add(shader);
		_ownedRids.Add(material);
		GD.Print($"preview: AddCubeSurface — surfaces={RenderingServer.MeshGetSurfaceCount(mesh)}");
	}
}
