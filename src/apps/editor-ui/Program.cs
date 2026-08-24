// SPDX-License-Identifier: MIT
// Program.cs —— 编辑器 UI 入口（src/apps/editor-ui，Avalonia 12 MVVM 模板结构）
//
// 12 官方推荐：BuildAvaloniaApp().UsePlatformDetect().StartWithClassicDesktopLifetime(args)。
// 应用资源/主题在 App.axaml；窗口与视图在 Views/；数据与命令在 ViewModels/（CommunityToolkit.Mvvm）。

using System;
using Avalonia;

namespace Sola3d.EditorUi;

public static class Program
{
	[STAThread]
	public static void Main(string[] args)
	{
		BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
	}

	private static AppBuilder BuildAvaloniaApp()
		=> AppBuilder.Configure<App>().UsePlatformDetect();
}