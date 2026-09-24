// SPDX-License-Identifier: MIT
// ProjectileBundle.cs —— 投射物作者层配方

using Baize.Ecs;

namespace Shooter.Gameplay;

public readonly struct ProjectileBundle : IEntityBundle
{
	private readonly Position _position;
	private readonly Velocity _velocity;
	private readonly ProjectileConfig _config;
	private readonly CollisionRadius _radius;

	public ProjectileBundle(float x, float z, float velocityX, float velocityZ,
		int damage = 1, float maxRange = 50.0f, float radius = 0.2f)
	{
		_position = new Position { X = x, Z = z };
		_velocity = new Velocity { X = velocityX, Z = velocityZ };
		_config = new ProjectileConfig { Damage = damage, MaxRange = maxRange };
		_radius = new CollisionRadius { Value = radius };
	}

	public void Apply(in EntityCommand entity)
	{
		entity
			.Add(_position)
			.Add(new PreviousPosition { X = _position.X, Z = _position.Z })
			.Add(_velocity)
			.Add(_config)
			.Add(new TravelDistance())
			.Add(_radius)
			.AddTag<ProjectileTag>();
	}
}
