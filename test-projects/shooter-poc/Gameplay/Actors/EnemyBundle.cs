// SPDX-License-Identifier: MIT
// EnemyBundle.cs —— 敌人作者层配方：阵营、寻敌能力、速度参数彼此独立

using Baize.Ecs;

namespace ShooterPoc;

public readonly struct EnemyBundle : IEntityBundle
{
	private readonly Position _position;
	private readonly MoveSpeed _moveSpeed;
	private readonly Health _health;
	private readonly CollisionRadius _radius;

	public EnemyBundle(float x, float z, float moveSpeed = 3.5f,
		int health = 1, float radius = 0.5f)
	{
		_position = new Position { X = x, Z = z };
		_moveSpeed = new MoveSpeed { Value = moveSpeed };
		_health = new Health { Current = health, Max = health };
		_radius = new CollisionRadius { Value = radius };
	}

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
