// SPDX-License-Identifier: MIT
// MainWindow.axaml.cs —— 主窗口代码后置：文件对话框（Avalonia 12 StorageProvider）装配 → VM 纯方法
//
// 分层：VM 只做业务与命令（New/Delete/Save/Load/Undo/Redo，绑定统一快捷键表 Window.InputBindings）；
// 本层只处理文件选择 I/O：订阅 VM.SaveRequested/LoadRequested → StorageProvider 对话框。
// 12 推荐：IStorageProvider.OpenFilePickerAsync / SaveFilePickerAsync（旧 FileDialog 已移除）。

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