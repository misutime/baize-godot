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
	private readonly System.Collections.Generic.Dictionary<ulong, string> _instanceMesh = new();
	private readonly System.Collections.Generic.List<Rid> _meshRids = new();
	private readonly System.Collections.Generic.List<Rid> _ownedRids = new();
	private bool _disposed;

	/// <summary>注入渲染 scenario（真实 Viewport 环境调用；headless 测试不调）。</summary>
	public void Initialize(Rid scenario)
	{
		_scenario = scenario;
	}

	public void BeginFrame(float nowSeconds) { }

	public void EndFrame(float nowSeconds) { }

	/// <summary>消费投影命令：建实例 + 设 Transform（Godot 侧最小实现）。</summary>
	/// <summary>消费差异命令流：upsert（PreviewRenderCommand）+ remove（PreviewRemoveCommand）。</summary>
	public void Consume(System.Collections.Generic.IReadOnlyList<GatewayCommand> commands)
	{
		foreach (var c in commands)
		{
			switch (c)
			{
				case PreviewRenderCommand rc: Apply(rc); break;
				case PreviewRemoveCommand rm: Remove(rm.ObjectUid); break;
			}
		}
	}

	private void Apply(PreviewRenderCommand rc)
	{
		// Mesh RID：每个 MeshPath 独立加 surface（reviewer P2：去掉全局 _surfaceAdded，
		// 否则第二种 MeshPath 只有 MeshCreate 无 surface → 不可见）。
		if (!_meshCache.TryGetValue(rc.MeshPath, out Rid meshRid))
		{
			meshRid = RenderingServer.MeshCreate();
			AddCubeSurface(meshRid);
			_meshCache[rc.MeshPath] = meshRid;
			_meshRids.Add(meshRid);
		}
		// 按对象 Uid 缓存实例：新对象建实例；同 Uid MeshPath 变更 → rebase（换 base，不重建实例）。
		if (!_instanceCache.TryGetValue(rc.ObjectUid, out Rid instance))
		{
			instance = RenderingServer.InstanceCreate2(meshRid, _scenario);
			_instanceCache[rc.ObjectUid] = instance;
			_instanceMesh[rc.ObjectUid] = rc.MeshPath;
		}
		else if (_instanceMesh.TryGetValue(rc.ObjectUid, out string? current) && current != rc.MeshPath)
		{
			RenderingServer.InstanceSetBase(instance, meshRid);
			_instanceMesh[rc.ObjectUid] = rc.MeshPath;
		}
		var t = BuildTransform(rc.Rotation, rc.Position, rc.Scale); // reviewer P1-3：局部轴缩放 + 保持 origin
		RenderingServer.InstanceSetTransform(instance, t);
	}

	/// <summary>释放对象实例（对象从文档删除 → 本帧 remove 命令）。</summary>
	private void Remove(ulong objectUid)
	{
		if (_instanceCache.Remove(objectUid, out Rid instance))
		{
			RenderingServer.FreeRid(instance);
			_instanceMesh.Remove(objectUid);
		}
	}

	/// <summary>构造实例变换：局部轴缩放（ScaledLocal）+ 独立 position（reviewer P1-3 语义）。</summary>
	public static Transform3D BuildTransform(System.Numerics.Quaternion rotation, System.Numerics.Vector3 position, System.Numerics.Vector3 scale)
	{
		var basis = new Basis(new Quaternion(rotation.X, rotation.Y, rotation.Z, rotation.W));
		// ScaledLocal：按对象局部轴缩放各列（连同旋转后的轴），不改 origin。
		var scaledBasis = basis.ScaledLocal(new Vector3(scale.X, scale.Y, scale.Z));
		return new Transform3D(scaledBasis, new Vector3(position.X, position.Y, position.Z));
	}

	public void Dispose()
	{
		if (_disposed)
		{
			return; // 幂等：重复 Dispose 不重复 FreeRid
		}
		_disposed = true;
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
		_instanceMesh.Clear();
		_meshCache.Clear();
		_meshRids.Clear();
		_ownedRids.Clear();
	}
	/// <summary>诊断：mesh/实例缓存计数（O7.5 排障用）。</summary>
	/// <summary>诊断：mesh/实例缓存计数（O7.5 排障用；surface 现为每 mesh 独立）。</summary>
	public string DebugInfo => $"mesh={_meshCache.Count} instance={_instanceCache.Count} disposed={_disposed} scenario={(_scenario.IsValid ? "valid" : "INVALID")}";

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
