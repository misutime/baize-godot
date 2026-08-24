// SPDX-License-Identifier: MIT
// Program.cs —— 编辑器壳启动（hosts/editor-shell，M1 ④ 最小编辑器壳骨架）
//
// 分层：本壳 = Avalonia 窗口 UI；数据/编辑逻辑全在 modules/editor（EditorSession，纯 .NET）。
// 本骨架：启动一个最小窗口并在标题回显 EditorSession 文档对象数（0 UI 面板，证明宿主可跑）；
// Hierarchy/Inspector/3D view 为下一步增量。

using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Themes.Fluent;

namespace Sola3d.EditorUi;

public static class Program
{
	public static void Main(string[] args)
	{
		BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
	}

	private static AppBuilder BuildAvaloniaApp()
		=> AppBuilder.Configure<App>().UsePlatformDetect();
}

/// <summary>Avalonia 应用根。</summary>
public class App : Application
{
	public override void Initialize()
	{
		Styles.Add(new FluentTheme());
	}

	public override void OnFrameworkInitializationCompleted()
	{
		if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
		{
			desktop.MainWindow = new EditorUiWindow();
		}
		base.OnFrameworkInitializationCompleted();
	}
}

/// <summary>主窗口（骨架）：复用 EditorSession 冒烟，UI 面板下一步。</summary>
public class EditorUiWindow : Window
{
	public EditorUiWindow()
	{
		Title = "Sola3d Editor";
		Width = 1280;
		Height = 720;

		// 冒烟：EditorSession（O8-A 核心）建对象 → 标题回显，证明宿主↔核心联通。
		var session = new Sola3d.Editor.EditorSession();
		var root = session.CreateGameObject("Root");
		var child = session.CreateGameObject("Child");
		session.SetParent(child, root);
		Title = $"Sola3d Editor — Document({session.Document.Objects.Count} objects, dirty={session.IsDirty})";
	}
}