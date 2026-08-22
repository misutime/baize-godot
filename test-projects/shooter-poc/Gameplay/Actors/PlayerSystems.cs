// SPDX-License-Identifier: MIT
// PlayerSystems.cs —— 把输入事实翻译成玩家可控实体的速度

using Baize.Ecs;
using Friflo.Engine.ECS;
using Friflo.Engine.ECS.Systems;

namespace ShooterPoc;

public sealed class ApplyPlayerInputSystem : QuerySystem<Velocity, MoveSpeed, PlayerInput>
{
	private readonly EcsWorld _world;

	public ApplyPlayerInputSystem(EcsWorld world) => _world = world;

	protected override void OnUpdate()
	{
		var match = _world.GetResource<MatchState>();
		var input = _world.CurrentInput;

		Query.ForEachEntity((ref Velocity velocity, ref MoveSpeed speed, ref PlayerInput _, Entity entity) =>
		{
			if (match.Phase != GamePhase.Playing)
			{
				velocity.X = 0;
				velocity.Z = 0;
				return;
			}

			velocity.X = input.MoveX * speed.Value;
			velocity.Z = input.MoveZ * speed.Value;
		});
	}
}
