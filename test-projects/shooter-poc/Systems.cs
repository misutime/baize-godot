// SPDX-License-Identifier: MIT
// Systems.cs —— P2.2 Shooter PoC 系统（批处理逻辑）
//
// 系统清单：ApplyInput / Fire / EnemySteering / Move / SweptBulletHit /
// EnemyContact / DamageResolve / Score / Lifetime / Cleanup。
// 结构变更（创建/删除）统一走 CommandBuffer（查询循环内禁止直接改）。

using System;
using Baize.Ecs;
using Friflo.Engine.ECS;
using Friflo.Engine.ECS.Systems;

namespace ShooterPoc;

/// <summary>输入应用：InputFrame → 玩家速度（Phase.Input）。</summary>
public class ApplyInputSystem : QuerySystem<Velocity, PlayerControl>
{
    private readonly EcsWorld _world;

    public ApplyInputSystem(EcsWorld world) { _world = world; Filter.AllTags(Tags.Get<PlayerTag>()); }

    protected override void OnUpdate()
    {
        var input = _world.CurrentInput;
        Query.ForEachEntity((ref Velocity vel, ref PlayerControl ctrl, Entity e) =>
        {
            vel.X = input.MoveX * ctrl.MoveSpeed;
            vel.Z = input.MoveZ * ctrl.MoveSpeed;
        });
    }
}

/// <summary>射击：Fire 边沿（FirePressed 本帧为真）→ 生成子弹（Phase.Spawn）。</summary>
public class FireSystem : QuerySystem<Position, Weapon>
{
    private readonly EcsWorld _world;
    private bool _prevFire;
    public int FireCount;   // 测试计数器

    public FireSystem(EcsWorld world) { _world = world; Filter.AllTags(Tags.Get<PlayerTag>()); }

    protected override void OnUpdate()
    {
        float dt = Tick.deltaTime;
        var cb = _world.CommandBuffer;
        bool firePressed = _world.CurrentInput.FirePressed;   // 循环外（边沿判断）
        Query.ForEachEntity((ref Position pos, ref Weapon weapon, Entity player) =>
        {
            weapon.Timer -= dt;

            // Fire 边沿：本帧按下 && 上一帧未按（不重复/不丢失）
            bool fireEdge = firePressed && !_prevFire;

            if (fireEdge && weapon.Timer <= 0)
            {
                weapon.Timer = weapon.Cooldown;
                FireCount++;
                cb.CreateEntity()
                  .Add(new Position { X = pos.X, Z = pos.Z })
                  .Add(new Velocity { X = 0, Z = -weapon.BulletSpeed })   // 向前（-Z）
                  .Add(new Bullet { Damage = 1, Range = 50, Travelled = 0 })
                  .Add(new Radius { Value = 0.2f })
                  .AddTag<BulletTag>();
            }
        });
        _prevFire = firePressed;   // 存本帧（下帧判断边沿）
    }
}

/// <summary>敌人生成：定时从四边生成（Phase.Spawn，用 IResettableSystem 重置计时器）。</summary>
public class SpawnSystem : BaseSystem, IResettableSystem
{
    private readonly EcsWorld _world;
    private float _timer;

    public SpawnSystem(EcsWorld world) { _world = world; }

    protected override void OnUpdateGroup()
    {
        var config = _world.Resources.Get<SpawnConfig>();
        var state = _world.Resources.Get<GameState>();
        if (config == null || state == null || state.Phase != GamePhase.Playing) return;

        _timer -= Tick.deltaTime;
        if (_timer > 0) return;
        _timer = config.Interval;

        if (state.AliveEnemies >= config.MaxAlive) return;

        // 从四边随机生成
        var rand = new Random(TickIndexSeed());
        float x = 0, z = 0;
        switch (rand.Next(4))
        {
            case 0: x = -config.SpawnRadius; z = rand.Next(-20, 20); break;
            case 1: x = config.SpawnRadius; z = rand.Next(-20, 20); break;
            case 2: x = rand.Next(-20, 20); z = -config.SpawnRadius; break;
            case 3: x = rand.Next(-20, 20); z = config.SpawnRadius; break;
        }

        _world.CommandBuffer.CreateEntity()
            .Add(new Position { X = x, Z = z })
            .Add(new Velocity { X = 0, Z = 0 })
            .Add(new Health { Current = 1, Max = 1 })
            .Add(new EnemyAI { Speed = 3.5f })
            .Add(new Radius { Value = 0.5f })
            .AddTag<EnemyTag>();
        state.AliveEnemies++;
    }

    private int TickIndexSeed() => (int)(_world.TickIndex % int.MaxValue);

    public void ResetState() => _timer = 0;
}

/// <summary>敌人 AI：朝玩家方向移动（Phase.Simulation）。</summary>
public class EnemySteeringSystem : QuerySystem<Position, EnemyAI>
{
    private readonly EcsWorld _world;

    public EnemySteeringSystem(EcsWorld world) { _world = world; Filter.AllTags(Tags.Get<EnemyTag>()); }

    protected override void OnUpdate()
    {
        var config = _world.Resources.Get<SpawnConfig>();
        if (config == null) return;
        float dt = Tick.deltaTime;

        Query.ForEachEntity((ref Position pos, ref EnemyAI ai, Entity e) =>
        {
            float dx = config.PlayerX - pos.X;
            float dz = config.PlayerZ - pos.Z;
            float len = MathF.Sqrt(dx * dx + dz * dz);
            if (len > 0.01f)
            {
                pos.X += dx / len * ai.Speed * dt;
                pos.Z += dz / len * ai.Speed * dt;
            }
        });
    }
}

/// <summary>移动：Position += Velocity × dt（Phase.Simulation）。</summary>
public class MoveSystem : QuerySystem<Position, Velocity>
{
    protected override void OnUpdate()
    {
        float dt = Tick.deltaTime;
        Query.ForEachEntity((ref Position pos, ref Velocity vel, Entity e) =>
        {
            pos.X += vel.X * dt;
            pos.Z += vel.Z * dt;
        });
    }
}

/// <summary>子弹命中：swept 检测（子弹本帧轨迹 vs 敌人圆），命中发 DamageRequest（Phase.Collision）。</summary>
public class SweptBulletHitSystem : QuerySystem<Position, Bullet>
{
    private readonly EcsWorld _world;

    public SweptBulletHitSystem(EcsWorld world) { _world = world; Filter.AllTags(Tags.Get<BulletTag>()); }

    protected override void OnUpdate()
    {
        float dt = Tick.deltaTime;
        var writer = _world.Events.Writer<DamageRequest>();
        var store = _world.Store;

        Query.ForEachEntity((ref Position pos, ref Bullet bullet, Entity e) =>
        {
            // 子弹本帧位移（swept 线段：pos → pos + vel*dt）
            var vel = e.GetComponent<Velocity>();
            float endX = pos.X + vel.X * dt;
            float endZ = pos.Z + vel.Z * dt;

            // 遍历敌人（Tag 过滤：Query<Position> + AllTags + Entities）
            var enemies = store.Query<Position>().AllTags(Tags.Get<EnemyTag>()).Entities;
            foreach (var enemy in enemies)
            {
                ref var ePos = ref enemy.GetComponent<Position>();
                float r = 0.5f;  // 敌人半径（扫地机器人）
                // swept：点到线段距离 < r
                if (SegmentPointDistance(pos.X, pos.Z, endX, endZ, ePos.X, ePos.Z) < r)
                {
                    writer.Send(new DamageRequest(e.Id, enemy.Id, (int)bullet.Damage));  // 子弹→敌人
                    break;
                }
            }
        });
    }

    private static float SegmentPointDistance(float x1, float z1, float x2, float z2, float px, float pz)
    {
        float dx = x2 - x1, dz = z2 - z1;
        float len2 = dx * dx + dz * dz;
        if (len2 < 0.0001f) return MathF.Sqrt((px - x1) * (px - x1) + (pz - z1) * (pz - z1));
        float t = ((px - x1) * dx + (pz - z1) * dz) / len2;
        t = MathF.Max(0, MathF.Min(1, t));
        float cx = x1 + t * dx, cz = z1 + t * dz;
        return MathF.Sqrt((px - cx) * (px - cx) + (pz - cz) * (pz - cz));
    }
}

/// <summary>敌人接触玩家：距离 < 阈值 → GameOver 事件（Phase.Collision）。</summary>
public class EnemyContactSystem : QuerySystem<Position, EnemyAI>
{
    private readonly EcsWorld _world;

    public EnemyContactSystem(EcsWorld world) { _world = world; Filter.AllTags(Tags.Get<EnemyTag>()); }

    protected override void OnUpdate()
    {
        var config = _world.Resources.Get<SpawnConfig>();
        if (config == null) return;

        Query.ForEachEntity((ref Position pos, ref EnemyAI ai, Entity e) =>
        {
            float dx = config.PlayerX - pos.X;
            float dz = config.PlayerZ - pos.Z;
            if (MathF.Sqrt(dx * dx + dz * dz) < 1.0f)   // 敌人碰到主角
            {
                _world.Events.Writer<GameOverEvent>().Send(default);
            }
        });
    }
}

/// <summary>伤害结算：消费 DamageRequest → 消灭敌人 + 计分（Phase.Resolve）。</summary>
public class DamageResolveSystem : BaseSystem
{
    private readonly EcsWorld _world;
    private readonly System.Collections.Generic.HashSet<int> _hitTargets = new();

    public DamageResolveSystem(EcsWorld world) { _world = world; }

    protected override void OnUpdateGroup()
    {
        var cb = _world.CommandBuffer;
        var state = _world.Resources.Get<GameState>();
        if (state == null) return;

        var reader = _world.Events.Reader<DamageRequest>();
        _hitTargets.Clear();   // 每 Tick 重置（同 Tick 多次命中同一敌人只记一次死亡）
        foreach (DamageRequest req in reader.Read())
        {
            if (_hitTargets.Add(req.TargetId))
            {
                cb.DeleteEntity(req.TargetId);   // 消灭敌人（延迟）
                cb.DeleteEntity(req.SourceId);   // 子弹消失（延迟）
                state.Score += 1;
                if (state.AliveEnemies > 0) state.AliveEnemies--;
            }
        }
    }
}

/// <summary>计分系统（Phase.Resolve 后）：维护 GameState.Score。</summary>
public class ScoreSystem : BaseSystem
{
    protected override void OnUpdateGroup() { /* 分数已在 DamageResolve 更新 */ }
}


/// <summary>清理：超射程子弹删除（Phase.Cleanup）。</summary>
public class CleanupSystem : QuerySystem<Position, Bullet>
{
    private readonly EcsWorld _world;

    public CleanupSystem(EcsWorld world) { _world = world; Filter.AllTags(Tags.Get<BulletTag>()); }

    protected override void OnUpdate()
    {
        float dt = Tick.deltaTime;
        var cb = _world.CommandBuffer;

        Query.ForEachEntity((ref Position pos, ref Bullet bullet, Entity e) =>
        {
            var vel = e.GetComponent<Velocity>();
            bullet.Travelled += MathF.Sqrt(vel.X * vel.X + vel.Z * vel.Z) * dt;
            if (bullet.Travelled > bullet.Range)
            {
                cb.DeleteEntity(e.Id);   // 子弹超射程（延迟）
            }
        });
    }
}








