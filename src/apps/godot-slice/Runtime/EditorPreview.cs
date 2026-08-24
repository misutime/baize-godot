// SPDX-License-Identifier: MIT
// EditorPreview.cs —— 编辑器 3D 预览宿主（godot-slice，--editor-preview <path>）
//
// 跨进程 MVP 连接：editor-ui（Avalonia）编辑并保存 .bscene → 启动本宿主真渲染该文档。
// 链路：.bscene 文本 → EditorSession.LoadScene → DesignPreviewHost.BuildPreviewWorld（预演世界）
//      → SceneProjector 投影 → RenderSnapshotTracker 帧差异 → GodotRenderGateway → RenderingServer。
// 刷新：文件修改时间变化时自动重载重投影（编辑器保存后这里即时更新）。
// 相机：MVP 固定俯视（CameraComponent 未做）；退出由外部 --quit-after 控制。

using Godot;
using Sola3d.Editor;
using Sola3d.GameObject;

namespace Sola3d.GodotSlice;

/// <summary>编辑器文档 3D 预览：Design 文档 → 预演世界 → RenderingServer 真渲染。</summary>
public sealed partial class EditorPreview : Node3D
{
	private readonly string _path;
	private DesignPreviewHost? _preview;
	private SceneProjector? _projector;
	private RenderSnapshotTracker? _tracker;
	private GodotRenderGateway? _gateway;
	private System.DateTime _lastWriteUtc;

	public EditorPreview(string path)
	{
		_path = path;
	}

	public override void _Ready()
	{
		_lastWriteUtc = System.IO.File.GetLastWriteTimeUtc(_path);

		// 注册预演已知组件 Schema（.bscene 含 Transform/Mesh 才可 Restore）。
		var schemas = new ComponentSchemaRegistry();
		schemas.Register<TransformComponent>();
		schemas.Register<MeshComponent>();
		_preview = new DesignPreviewHost(schemas);
		_projector = new SceneProjector();
		_tracker = new RenderSnapshotTracker();
		_gateway = new GodotRenderGateway();
		_gateway.Initialize(GetViewport().GetWorld3D().Scenario);

		// 相机（MVP 固定斜视；文档含 CameraComponent 前的过渡）。
		var cam = new Camera3D { Fov = 60.0f, Near = 0.05f, Far = 200.0f };
		cam.Position = new Vector3(3, 8, 8);
		AddChild(cam);
		cam.LookAt(new Vector3(0, 0, 0), Vector3.Up);
		cam.MakeCurrent();

		RefreshProjection(first: true);
	}

	public override void _Process(double delta)
	{
		// 编辑器保存后文件 mtime 变化 → 增量重投影。
		var writeTime = System.IO.File.GetLastWriteTimeUtc(_path);
		if (writeTime != _lastWriteUtc)
		{
			_lastWriteUtc = writeTime;
			RefreshProjection(first: false);
		}
	}

	private void RefreshProjection(bool first)
	{
		if (_gateway is null || _preview is null || _projector is null || _tracker is null)
		{
			return;
		}
		try
		{
			var snapshot = GameWorldTextSerializer.Deserialize(System.IO.File.ReadAllText(_path));
			var world = _preview.BuildPreviewWorld(snapshot);
			var commands = _projector.Project(world);
			_gateway.Consume(_tracker.Diff(commands));
			if (first)
			{
				GD.Print($"editor-preview: 加载 {_path} — objects={snapshot.Objects.Count} render_commands={commands.Count} gateway={_gateway.DebugInfo}");
			}
			else
			{
				GD.Print($"editor-preview: 文档已更新（mtime）→ 重投影 {commands.Count} 命令");
			}
		}
		catch (System.Exception ex)
		{
			GD.PushError($"editor-preview: 加载失败 {_path} — {ex.Message}");
		}
	}

	public override void _ExitTree()
	{
		_gateway?.Dispose();
	}
}