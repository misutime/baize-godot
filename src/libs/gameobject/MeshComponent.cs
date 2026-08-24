// SPDX-License-Identifier: MIT
// MeshComponent.cs —— 网格引用数据组件（O6，O6-GameWorldGateway与垂直切片.md §2）
//
// 纯数据：只持有资源路径（O4 资源引用 token 前置），真实 RID 建立由 RenderGateway 完成。
// Godot 上游 mesh 的 AABB/材质/层级缓存等"表现聚合"不进本组件——那些是 gateway 投影域。

namespace Sola3d.GameObject;

/// <summary>网格引用：MeshPath/MaterialPath 为资源路径（.mesh/.material），加载与 RID 由 RenderGateway 负责。</summary>
[GameComponent]
public sealed class MeshComponent : GameComponent
{
	[GameProperty]
	public string MeshPath { get; set; } = string.Empty;

	[GameProperty]
	public string MaterialPath { get; set; } = string.Empty;

	/// <summary>是否可见（gateway 投影的可见性开关；语义权威在 Gameplay）。</summary>
	[GameProperty]
	public bool Visible { get; set; } = true;
}
