// SPDX-License-Identifier: MIT
// ViewModelBase.cs —— 编辑器 UI 视图模型基类（Avalonia 12 推荐：CommunityToolkit.Mvvm ObservableObject）

using CommunityToolkit.Mvvm.ComponentModel;

namespace Sola3d.EditorUi.ViewModels;

/// <summary>视图模型基类；ViewLocator 以它做数据模板匹配。</summary>
public abstract class ViewModelBase : ObservableObject
{
}