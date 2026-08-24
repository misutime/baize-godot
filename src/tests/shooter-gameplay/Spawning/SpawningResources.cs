// SPDX-License-Identifier: MIT
// SpawningResources.cs —— 全局生成配置与运行状态分开

namespace Shooter.Gameplay;

// Resource 配置：只描述生成规则，不缓存玩家位置，也不保存倒计时。
public sealed class SpawnConfig
{
	public float Interval = 1.0f;
	public int MaxAlive = 10;
	public float SpawnRadius = 20.0f;
}

// Resource 运行状态：全世界只有一个生成节拍。
public sealed class SpawnState
{
	public float Remaining;
}
