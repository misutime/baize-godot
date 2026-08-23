// SPDX-License-Identifier: MIT
// EnemyBundle.cs —— 敌人作者层配方：阵营、寻敌能力、速度参数彼此独立

using Sola3d.Ecs;

namespace Shooter.Gameplay;

public readonly struct EnemyBundle(
	float x, float z, float moveSpeed = 3.5f, int health = 1, float radius = 0.5f) : IEntityBundle
{
	private readonly Position _position = new() { X = x, Z = z };
	private readonly MoveSpeed _moveSpeed = new() { Value = moveSpeed };
	private readonly Health _health = new() { Current = health, Max = health };
	private readonly CollisionRadius _radius = new() { Value = radius };

	public void Apply(in EntityCommand entity)
	{
		entity
			.Add(_position)
			.Add(new PreviousPosition { X = _position.X, Z = _position.Z })
			.Add(new Velocity())
			.Add(new SeekTarget())
			.Add(_moveSpeed)
			.Add(_health)
			.Add(_radius)
			.AddTag<EnemyFaction>();
	}
}
