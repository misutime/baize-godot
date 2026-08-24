// SPDX-License-Identifier: MIT
// CombatResources.cs —— 跨实体但属于全局输入解释的运行状态

namespace Shooter.Gameplay;

// FirePressed 是“本帧是否按住”；这里保存上一帧，系统才能计算按下边沿。
public sealed class FireInputState
{
	public bool WasPressed;
}
