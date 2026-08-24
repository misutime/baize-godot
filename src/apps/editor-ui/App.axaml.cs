// SPDX-License-Identifier: MIT
// App.axaml.cs —— 应用代码后置：加载 axaml 资源 + 装配主窗口（12 官方模板方式）

using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Sola3d.EditorUi.ViewModels;
using Sola3d.EditorUi.Views;

namespace Sola3d.EditorUi;

public class App : Application
{
	public override void Initialize()
	{
		AvaloniaXamlLoader.Load(this);
	}

	public override void OnFrameworkInitializationCompleted()
	{
		if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
		{
			desktop.MainWindow = new MainWindow
			{
				DataContext = new MainWindowViewModel(),
			};
		}

		base.OnFrameworkInitializationCompleted();
	}
}