using Godot;

[Tool]
public partial class plugin : EditorPlugin
{
	public override void _EnterTree()
	{
		GD.Print("p15-plugin: EditorPlugin 加载成功 (P1.5)");
	}

	public override void _ExitTree()
	{
		GD.Print("p15-plugin: EditorPlugin 卸载");
	}
}
