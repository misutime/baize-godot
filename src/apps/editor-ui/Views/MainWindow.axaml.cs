// SPDX-License-Identifier: MIT
// MainWindow.axaml.cs —— 主窗口代码后置：文件对话框（Avalonia 12 StorageProvider）装配 → VM 纯方法
//
// 分层：VM 只做业务与命令（New/Delete/Save/Load/Undo/Redo，绑定统一快捷键表 Window.InputBindings）；
// 本层只处理文件选择 I/O：订阅 VM.SaveRequested/LoadRequested → StorageProvider 对话框。
// 12 推荐：IStorageProvider.OpenFilePickerAsync / SaveFilePickerAsync（旧 FileDialog 已移除）。

using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Sola3d.EditorUi.ViewModels;

namespace Sola3d.EditorUi.Views;

public partial class MainWindow : Window
{
	private static readonly FilePickerFileType BSceneType = new("Sola3d scene") { Patterns = new[] { "*.bscene" } };
	private Process? _previewProcess; // 预览宿主子进程（godot-slice --editor-preview）

	/// <summary>统一快捷键表（集中一处，构造函数内初始化；后续做“设置→快捷键”重绑定即改此表/配置源）。</summary>
	private (KeyGesture Gesture, System.Action<KeyEventArgs> Handler)[] _shortcuts = null!;

	/// <summary>快捷键分发：遍历统一表匹配即执行（替代硬编码 switch）。</summary>
	protected override void OnKeyDown(KeyEventArgs e)
	{
		foreach (var shortcut in _shortcuts)
		{
			if (shortcut.Gesture.Matches(e))
			{
				e.Handled = true;
				shortcut.Handler(e);
				return;
			}
		}
		base.OnKeyDown(e);
	}

	public MainWindow()
	{
		InitializeComponent();
		_shortcuts = new (KeyGesture, System.Action<KeyEventArgs>)[]
		{
			(new KeyGesture(Key.S, KeyModifiers.Control), e => { _ = SaveOrPickAsync(); }),
			(new KeyGesture(Key.O, KeyModifiers.Control), e => { _ = LoadPickAsync(); }),
			(new KeyGesture(Key.Z, KeyModifiers.Control), e => { if (e.KeyModifiers.HasFlag(KeyModifiers.Shift)) { Vm?.Redo(); } else { Vm?.Undo(); } }),
			(new KeyGesture(Key.Y, KeyModifiers.Control), _ => Vm?.Redo()),
			(new KeyGesture(Key.Delete, KeyModifiers.None), _ => Vm?.DeleteSelected()),
		};
		DataContextChanged += (_, _) => BindVmEvents();
		// reviewer P2：关闭时终止本窗口启动的预览宿主进程。
		Closed += (_, _) => DisposePreviewProcess();
	}

	private MainWindowViewModel? Vm => DataContext as MainWindowViewModel;

	/// <summary>把 VM 的命令请求（对话框）接到本窗口（仅在 DataContext 变化时装配一次系统）。</summary>
	private void BindVmEvents()
	{
		if (Vm is null)
		{
			return;
		}
		Vm.SaveRequested = () => _ = SaveOrPickAsync();
		Vm.LoadRequested = () => _ = LoadPickAsync();
	}

	/// <summary>保存：首次弹出选择路径；已有路径直接覆盖当前文件（主流 Ctrl+S 行为）。</summary>
	private async Task SaveOrPickAsync()
	{
		if (Vm is null)
		{
			return;
		}
		if (Vm.HasPath)
		{
			Vm.SaveScene(Vm.CurrentPath!); // 后续直接覆盖
			return;
		}
		var file = await StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
		{
			SuggestedFileName = "未命名.bscene",
			DefaultExtension = "bscene",
			FileTypeChoices = new[] { BSceneType },
		});
		if (file != null)
		{
			Vm.SaveScene(file.Path.LocalPath);
		}
	}

	/// <summary>3D 预览：保存当前文档 → 若预览宿主未运行则启动 godot-slice --editor-preview（跨进程）。
	/// 保存会使 mtime 变化，已运行的宿主自动重投影（无需重启）。</summary>
	private async void OnPreviewClick(object? sender, RoutedEventArgs e)
	{
		if (Vm is null)
		{
			return;
		}
		if (!Vm.HasPath)
		{
			await SaveOrPickAsync();
		}
		if (!Vm.HasPath)
		{
			return;
		}
		Vm.SaveScene(Vm.CurrentPath!);

		if (_previewProcess == null || _previewProcess.HasExited)
		{
			DisposePreviewProcess(); // reviewer P2：覆盖前释放已退出旧句柄

			string exe = Path.Combine(Environment.CurrentDirectory, "bin", "godot.windows.editor.dev.x86_64.mono.console.exe");
			if (!File.Exists(exe))
			{
				await ShowMessageAsync($"未找到引擎：{exe}");
				return;
			}
			_previewProcess = Process.Start(new ProcessStartInfo(exe)
			{
				WorkingDirectory = Environment.CurrentDirectory,
				UseShellExecute = false,
				Arguments = $"--path src\\apps\\godot-slice -- --editor-preview \"{Vm.CurrentPath}\"",
			});
		}
	}

	/// <summary>终止并释放预览宿主（Kill 仅活进程；Dispose/null 无条件执行——reviewer P2 句柄不泄漏）。</summary>
	private void DisposePreviewProcess()
	{
		if (_previewProcess == null)
		{
			return;
		}
		if (!_previewProcess.HasExited)
		{
			try { _previewProcess.Kill(); } catch { /* 已退出 */ }
		}
		_previewProcess.Dispose();
		_previewProcess = null;
	}

	private async Task ShowMessageAsync(string text)
	{
		var dialog = new Window { Title = "Sola3d Editor", Content = text, Width = 360, Height = 140 };
		await dialog.ShowDialog(this);
	}


	private async Task LoadPickAsync()
	{
		if (Vm is null)
		{
			return;
		}
		var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
		{
			AllowMultiple = false,
			FileTypeFilter = new[] { BSceneType },
		});
		var file = files.FirstOrDefault();
		if (file != null)
		{
			Vm.LoadScene(file.Path.LocalPath);
		}
	}
}