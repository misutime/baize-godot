// SPDX-License-Identifier: MIT
// EcsFeature.cs —— 面向作者的功能安装边界

namespace Baize.Ecs;

/// <summary>
/// 一个可安装的玩法功能切片：只负责把本功能需要的系统注册进世界。
/// 组件仍是事实，Bundle 仍是实体配方，Feature 只表达“这个世界启用了哪些规则”。
/// </summary>
public interface IEcsFeature
{
	/// <summary>把功能所需系统安装到世界。</summary>
	void Install(EcsWorld world);
}
