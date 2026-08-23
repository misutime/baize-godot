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
		obj.AddComponent<WeaponConfig>().CooldownSeconds = fireCooldown;
		obj.GetComponent<WeaponConfig>()!.ProjectileSpeed = projectileSpeed;
		obj.AddComponent<Cooldown>();
		obj.AddComponent<PlayerControllerBehavior>();
		obj.AddComponent<WeaponBehavior>();
		obj.AddComponent<MoveBehavior>();
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
		obj.AddComponent<Health>()!.Current = health;
		obj.GetComponent<Health>()!.Max = health;
		obj.AddComponent<EnemyControllerBehavior>();
		return obj;
	}

	/// <summary>创建投射物对象（移动 + 扫掠命中 + 射程清理）。速度方向 (vx, vz)；固定朝 +Z 发射时传 (0, speed)。</summary>
	public static GameObject SpawnProjectile(GameWorld world, float x, float z,
		float vx, float vz, int damage = 1, float maxRange = 50.0f, float radius = 0.2f)
	{
		var obj = world.CreateGameObject("Projectile");
		obj.AddComponent<ProjectileTag>();
		obj.AddComponent<Position>()!.X = x;
		obj.GetComponent<Position>()!.Z = z;
		obj.AddComponent<PreviousPosition>()!.X = x;
		obj.GetComponent<PreviousPosition>()!.Z = z;
		obj.AddComponent<MotionPlan>();
		obj.AddComponent<Velocity>()!.X = vx;
		obj.GetComponent<Velocity>()!.Z = vz;
		obj.AddComponent<ProjectileConfig>()!.Damage = damage;
		obj.GetComponent<ProjectileConfig>()!.MaxRange = maxRange;
		obj.AddComponent<TravelDistance>();
		obj.AddComponent<CollisionRadius>()!.Value = radius;
		obj.AddComponent<BulletBehavior>();
		return obj;
	}

	/// <summary>移动组件栈（Position/Previous/Velocity/MoveSpeed/CollisionRadius + 初始位置）。</summary>
	private static void AddMoveStack(GameObject obj, float x, float z, float moveSpeed, float radius)
	{
		obj.AddComponent<Position>()!.X = x;
		obj.GetComponent<Position>()!.Z = z;
		obj.AddComponent<PreviousPosition>()!.X = x;
		obj.GetComponent<PreviousPosition>()!.Z = z;
		obj.AddComponent<MotionPlan>();
		obj.AddComponent<Velocity>();
		obj.AddComponent<MoveSpeed>()!.Value = moveSpeed;
		obj.AddComponent<CollisionRadius>()!.Value = radius;
	}
}
