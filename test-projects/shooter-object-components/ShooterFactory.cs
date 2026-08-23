// SPDX-License-Identifier: MIT
// ShooterFactory.cs —— O2 对象工厂（B1 Required Components：创建即带全套组件，零样板）

using Baize.GameObject;

namespace Shooter.Objects;

/// <summary>对象工厂：一行创建即带全套组件与行为（借鉴 Bevy Bundle / B1 决策）。</summary>
public static class ShooterFactory
{
	/// <summary>创建玩家对象（移动 / 输入 / 武器 / 阵营 / 行为）。</summary>
	public static GameObject SpawnPlayer(GameWorld world, float x, float z, float moveSpeed = 8.0f,
		float fireCooldown = 0.3f, float projectileSpeed = 30.0f, float radius = 0.5f)
	{
		var obj = world.CreateGameObject("Player");
		obj.AddComponent<PlayerFaction>();
		obj.AddComponent<PlayerInputMarker>();
		AddMoveStack(obj, x, z, moveSpeed, radius);
		obj.AddComponent(new WeaponConfig { CooldownSeconds = fireCooldown, ProjectileSpeed = projectileSpeed });
		obj.AddComponent<Cooldown>();
		obj.AddComponent<PlayerControllerAction>();
		obj.AddComponent<WeaponAction>();
		obj.AddComponent<MoveAction>();
		return obj;
	}

	/// <summary>创建敌人对象（寻敌 AI 内联移动 + 接触触发 GameOver）。</summary>
	public static GameObject SpawnEnemy(GameWorld world, float x, float z,
		float moveSpeed = 3.5f, int health = 1, float radius = 0.5f)
	{
		var obj = world.CreateGameObject("Enemy");
		obj.AddComponent<EnemyFaction>();
		obj.AddComponent<SeekTargetMarker>();
		AddMoveStack(obj, x, z, moveSpeed, radius);
		obj.AddComponent(new Health { Current = health, Max = health });
		obj.AddComponent<EnemyControllerAction>();
		return obj;
	}

	/// <summary>创建投射物对象（移动 + 扫掠命中 + 射程清理）。速度方向 (vx, vz)；固定朝 +Z 发射时传 (0, speed)。</summary>
	public static GameObject SpawnProjectile(GameWorld world, float x, float z,
		float vx, float vz, int damage = 1, float maxRange = 50.0f, float radius = 0.2f)
	{
		var obj = world.CreateGameObject("Projectile");
		obj.AddComponent<ProjectileTag>();
		obj.AddComponent(new Position { X = x, Z = z });
		obj.AddComponent(new PreviousPosition { X = x, Z = z });
		obj.AddComponent<MotionPlan>();
		obj.AddComponent(new Velocity { X = vx, Z = vz });
		obj.AddComponent(new ProjectileConfig { Damage = damage, MaxRange = maxRange });
		obj.AddComponent<TravelDistance>();
		obj.AddComponent(new CollisionRadius { Value = radius });
		obj.AddComponent<BulletAction>();
		return obj;
	}

	/// <summary>移动组件栈（Position/Previous/Velocity/MoveSpeed/CollisionRadius + 初始位置）。</summary>
	private static void AddMoveStack(GameObject obj, float x, float z, float moveSpeed, float radius)
	{
		obj.AddComponent(new Position { X = x, Z = z });
		obj.AddComponent(new PreviousPosition { X = x, Z = z });
		obj.AddComponent<MotionPlan>();
		obj.AddComponent<Velocity>();
		obj.AddComponent(new MoveSpeed { Value = moveSpeed });
		obj.AddComponent(new CollisionRadius { Value = radius });
	}
}
