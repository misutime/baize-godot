// SPDX-License-Identifier: MIT
// EcsFeature.cs —— 面向作者的功能安装边界

namespace Baize.Ecs;

/// <summary>
/// 一个可安装的玩法功能切片：负责注册本功能系统，也可通过 world.AddFeature 安装子功能。
/// 组件仍是事实，Bundle 仍是实体配方，Feature 只表达“这个世界启用了哪些规则”。
/// </summary>
public interface IEcsFeature
{
	/// <summary>安装功能；实现内可 AddFeature 形成嵌套组合。</summary>
	void Install(EcsWorld world);
}
