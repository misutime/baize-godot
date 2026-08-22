// SPDX-License-Identifier: MIT
// SpawningSystems.cs —— 生成规则只读配置，节拍写入独立运行状态

using Baize.Ecs;

namespace Shooter.Gameplay;

public sealed class SpawnEnemiesSystem : EcsSystem
{
	public SpawnEnemiesSystem() => RunInState<MatchState>(GamePhase.Playing);

	protected override void Execute()
	{
		MatchState match = State<MatchState>();
		SpawnConfig config = Res<SpawnConfig>();
		SpawnState state = Res<SpawnState>();
		state.Remaining -= Tick.deltaTime;
		if (state.Remaining > 0) return;
		state.Remaining = config.Interval;

		if (match.AliveEnemies >= config.MaxAlive) return;

		// TickIndex 是回放状态的一部分；同 Tick 会得到同一条边与同一偏移，不依赖进程随机源。
		ulong random = HashTick(World.TickIndex);
		float edgeOffset = (((random >> 8) & 0x00FF_FFFFUL) / 16_777_215.0f * 2.0f - 1.0f)
			* config.SpawnRadius;
		(float x, float z) = (random & 3UL) switch
		{
			0 => (config.SpawnRadius, edgeOffset),
			1 => (-config.SpawnRadius, edgeOffset),
			2 => (edgeOffset, config.SpawnRadius),
			_ => (edgeOffset, -config.SpawnRadius),
		};
		World.CommandBuffer.Spawn(new EnemyBundle(x, z));
		match.AliveEnemies++;
	}

	private static ulong HashTick(ulong tickIndex)
	{
		unchecked
		{
			ulong value = tickIndex + 0x9E37_79B9_7F4A_7C15UL;
			value = (value ^ (value >> 30)) * 0xBF58_476D_1CE4_E5B9UL;
			value = (value ^ (value >> 27)) * 0x94D0_49BB_1331_11EBUL;
			return value ^ (value >> 31);
		}
	}
}
