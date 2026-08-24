// SPDX-License-Identifier: MIT
// ViewLocator.cs —— 视图定位器（Avalonia 12 官方模板自带约定：VM 类名 "ViewModel" → "View"）

using System;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Sola3d.EditorUi.ViewModels;

namespace Sola3d.EditorUi;

/// <summary>按命名约定（ViewModel → View）解析视图的数据模板；注册在 App.axaml DataTemplates。</summary>
public class ViewLocator : IDataTemplate
{
	public Control? Build(object? data)
	{
		if (data is null)
		{
			return null;
		}
		string name = data.GetType().FullName!.Replace("ViewModel", "View", StringComparison.Ordinal);
		Type? viewType = Type.GetType(name);
		if (viewType != null)
		{
			return (Control)Activator.CreateInstance(viewType)!;
		}
		return new TextBlock { Text = $"未找到视图：{name}" };
	}

	public bool Match(object? data) => data is ViewModelBase;
}