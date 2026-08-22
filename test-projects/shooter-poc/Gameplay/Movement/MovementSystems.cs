// SPDX-License-Identifier: MIT
// MovementSystems.cs —— 对所有具备位置与速度事实的实体执行同一数据变换

using Baize.Ecs;
using Friflo.Engine.ECS;
using Friflo.Engine.ECS.Systems;

namespace ShooterPoc;

public sealed class MoveSystem : QuerySystem<Position, PreviousPosition, Velocity>
{
	private readonly EcsWorld _world;

	public MoveSystem(EcsWorld world) => _world = world;

	protected override void OnUpdate()
	{
		if (_world.GetResource<MatchState>().Phase != GamePhase.Playing) return;

		float delta = Tick.deltaTime;
		Query.ForEachEntity((ref Position position, ref PreviousPosition previous,
			ref Velocity velocity, Entity entity) =>
		{
			previous.X = position.X;
			previous.Z = position.Z;
			position.X += velocity.X * delta;
			position.Z += velocity.Z * delta;
		});
	}
}
