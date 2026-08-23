// SPDX-License-Identifier: MIT
// TransformComponent.cs —— 空间变换数据组件（O6，O6-GameWorldBackend与垂直切片.md §2）
//
// 方案 §2.3/§4.7：Transform 不属于层级内核（契约 §7 明确由 TransformComponent+TransformBackend 承担）。
// 纯数据组件（无行为）：GameWorld 只存语义状态，投影到 RenderingServer 由 backend 完成（状态 C）。
// 序列化：System.Numerics.Vector3 已进白名单（R27）。

using System.Numerics;

namespace Sola3d.GameObject;

/// <summary>空间变换（局部语义；由于当前无层级空间继承，语义即全局方位——O6 最小切片）。</summary>
[GameComponent]
public sealed class TransformComponent : GameComponent
{
	[GameProperty]
	public Vector3 Position { get; set; }

	[GameProperty]
	public Quaternion Rotation { get; set; } = Quaternion.Identity;

	[GameProperty]
	public Vector3 Scale { get; set; } = Vector3.One;
}
