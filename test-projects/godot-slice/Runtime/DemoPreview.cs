// SPDX-License-Identifier: MIT
// DemoPreview.cs —— O7.5 最小可展示切片（真实窗口，用户可见）
//
// 目标：证明 Design World → Runtime World → Godot Core 全链路肉眼可见。
// 流程：构造 EditorSession（Design 文档：Cube）→ DesignPreviewHost 预演世界 →
//       SceneProjector 命令 → GodotRenderGateway（建 cube surface + 实例）→
//       自建 camera（RenderingServer）看向 cube → 每帧旋转 Refresh。
// 触发：Main.cs 检测 "--demo" 参数创建本节点（避开 ECS slice 场景）。

using Godot;
using Sola3d.Editor;
using Sola3d.GameObject;

namespace Sola3d.GodotSlice;

/// <summary>真实窗口演示：一个由 GameObject+Component 驱动、经 Gateway 画出的旋转立方体。</summary>
public sealed partial class DemoPreview : Node3D
{
	private EditorSession? _session;
	private DesignPreviewHost? _preview;
	private GodotRenderGateway? _gateway;
	private Sola3d.GameObject.TransformComponent? _runtimeTf;
	private float _angle;
	private int _frameCount;

	public override void _Ready()
	{
		var reg = new ComponentSchemaRegistry();
		reg.Register<TransformComponent>();
		reg.Register<MeshComponent>();

		// ① Design 文档：一个 Cube（Edit in Design World）
		_session = new EditorSession(schemas: reg);
		var cube = _session.CreateGameObject("Cube");
		var tf = _session.AddComponent(cube, reg.Get<TransformComponent>());
		_session.SetProperty(tf, "Position", new System.Numerics.Vector3(0, 0, 0));
		_session.SetProperty(tf, "Scale", new System.Numerics.Vector3(1, 1, 1));
		_session.AddComponent(cube, reg.Get<MeshComponent>());

		// ② 预演世界（Design → Runtime 单向投影）
		_preview = new DesignPreviewHost(reg);
		var world = _preview.BuildPreviewWorld(_session.Document);
		_runtimeTf = world.Roots[0].GetComponent<TransformComponent>();

		// ③ Gateway：建 cube 几何 + 实例（Godot Core 渲染）
		_gateway = new GodotRenderGateway();
		_gateway.Initialize(GetViewport().GetWorld3D().Scenario);
		var commands = _preview.ProjectToCommands(world);
		var cmdList = new System.Collections.Generic.List<Sola3d.MainLoop.GatewayCommand>(commands);
		_gateway.Consume(cmdList);

		var world3d = GetWorld3D();
		GD.Print($"preview: scenario={world3d.Scenario} gateway={_gateway!.DebugInfo} screen={GetViewport().GetVisibleRect().Size} cam={GetViewport().GetCamera3D()?.Name ?? "null"}");
		// ④ camera：用 Camera3D 节点（Godot 内部处理 Viewport attach；我们的渲染仍经 RenderingServer 的
		//    scenario——camera 只是"视图窗口的投射器"，不是"GameObject 语义"）。
		var cam = new Camera3D();
		cam.Fov = 60.0f;
		cam.Near = 0.05f;
		cam.Far = 100.0f;
		cam.Position = new Vector3(2.4f, 2.0f, 2.4f);
		AddChild(cam); // 先入树，LookAt 才有效（Node3D::look_at 要求已在树内）
		cam.LookAt(new Vector3(0, 0, 0), Vector3.Up);
		cam.MakeCurrent(); // 显式成为当前相机（Current=true 在动态挂载时可能不足）
		GD.Print($"preview: camera current? {GetViewport().GetCamera3D() == cam}");
		// 对照 cube：ArrayMesh + 我们的 CubeGeometry 数据（场景节点路径）——验证数据是否有效。
		var control = new MeshInstance3D();
		var arrMesh = new ArrayMesh();
		var surface = new Godot.Collections.Array();
		surface.Resize((int)Godot.Mesh.ArrayType.Max);
		var bv = new Vector3[Sola3d.Host.CubeGeometry.Vertices.Length];
		for (int i = 0; i < bv.Length; i++) { var v = Sola3d.Host.CubeGeometry.Vertices[i]; bv[i] = new Vector3(v.X, v.Y, v.Z); }
		var bn = new Vector3[Sola3d.Host.CubeGeometry.Normals.Length];
		for (int i = 0; i < bn.Length; i++) { var n = Sola3d.Host.CubeGeometry.Normals[i]; bn[i] = new Vector3(n.X, n.Y, n.Z); }
		surface[(int)Godot.Mesh.ArrayType.Vertex] = bv;
		surface[(int)Godot.Mesh.ArrayType.Normal] = bn;
		surface[(int)Godot.Mesh.ArrayType.Index] = Sola3d.Host.CubeGeometry.Indices;
		arrMesh.AddSurfaceFromArrays((Godot.Mesh.PrimitiveType)3, surface);
		arrMesh.SurfaceSetMaterial(0, new StandardMaterial3D { ShadingMode = BaseMaterial3D.ShadingModeEnum.Unshaded, AlbedoColor = new Color(1, 0, 0) });
		control.Mesh = arrMesh;
		control.Position = new Vector3(3.0f, 0, 0);
		AddChild(control);
		GD.Print($"preview: ArrayMesh control (我们的数据,红色) surface={arrMesh.GetSurfaceCount()}");
		GD.Print("preview: cube instance created — Design→Runtime→GodotCore 链路通电");
	}

	public override void _Process(double delta)
	{
		_frameCount++;
		if (_runtimeTf != null)
		{
			// 每帧旋转预演世界的 Transform（语义权威在 GameWorld；Gateway 只投影）。
			_angle += (float)delta * 60.0f;
			_runtimeTf.Rotation = System.Numerics.Quaternion.CreateFromYawPitchRoll(
				Mathf.DegToRad(_angle), Mathf.DegToRad(_angle * 0.5f), 0);
		}
		// 每帧重建预演世界并投影（Design 权威，简单刷新）。
		if (_preview != null && _session != null && _gateway != null)
		{
			var world = _preview.BuildPreviewWorld(_session.Document);
			var updated = _preview.ProjectToCommands(world);
			var list = new System.Collections.Generic.List<Sola3d.MainLoop.GatewayCommand>(updated);
			_gateway.Consume(list);
		}
		// O7.5 验证：渲染 300 帧（约 5 秒）后截 viewport 存 PNG，然后退出。
		if (_frameCount == 300)
		{
			var img = GetViewport().GetTexture().GetImage();
			string path = ProjectSettings.GlobalizePath("user://demo_cube.png"); // user:// = %APPDATA%/Godot/app_userdata/godot-slice
			img.SavePng(path);
			GD.Print("preview: screenshot saved → user://demo_cube.png");
			GetTree().Quit();
		}
	}

	public override void _ExitTree()
	{
		_gateway?.Dispose();
	}
}
