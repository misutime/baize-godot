// SPDX-License-Identifier: MIT
// PlayerSystems.cs —— 把输入事实翻译成玩家可控实体的速度

using Baize.Ecs;
using Friflo.Engine.ECS;

namespace Shooter.Gameplay;

public sealed class ApplyPlayerInputSystem : EcsSystem<Velocity, MoveSpeed, PlayerInput>
{
	public ApplyPlayerInputSystem() => RunInState<MatchState>(GamePhase.Playing);

	protected override void Execute()
	{
		InputFrame input = Input;
		ForEach((ref Velocity velocity, ref MoveSpeed speed, ref PlayerInput _, Entity entity) =>
		{
			velocity.X = input.MoveX * speed.Value;
			velocity.Z = input.MoveZ * speed.Value;
		});
	}
}
