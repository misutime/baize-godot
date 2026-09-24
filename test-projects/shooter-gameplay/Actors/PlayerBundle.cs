// SPDX-License-Identifier: MIT
// PlayerBundle.cs —— “玩家”只是这一组事实的作者层配方

using Baize.Ecs;

namespace Shooter.Gameplay;

public readonly struct PlayerBundle : IEntityBundle
{
	private readonly Position _position;
	private readonly MoveSpeed _moveSpeed;
	private readonly WeaponConfig _weapon;
	private readonly CollisionRadius _radius;

	public PlayerBundle(float x, float z, float moveSpeed, float fireCooldown,
		float projectileSpeed, float radius)
	{
		_position = new Position { X = x, Z = z };
		_moveSpeed = new MoveSpeed { Value = moveSpeed };
		_weapon = new WeaponConfig
		{
			CooldownSeconds = fireCooldown,
			ProjectileSpeed = projectileSpeed,
		};
		_radius = new CollisionRadius { Value = radius };
	}

	public static PlayerBundle Default => new(0, 0, 8.0f, 0.3f, 30.0f, 0.5f);

	public void Apply(in EntityCommand entity)
	{
		entity
			.Add(_position)
			.Add(new PreviousPosition { X = _position.X, Z = _position.Z })
			.Add(new Velocity())
			.Add(new PlayerInput())
			.Add(_moveSpeed)
			.Add(_weapon)
			.Add(new Cooldown())
			.Add(_radius)
			.AddTag<PlayerFaction>();
	}
}
