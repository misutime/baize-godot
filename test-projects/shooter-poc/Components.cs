// SPDX-License-Identifier: MIT
// Components.cs —— P2.2 Shooter PoC 组件（纯数据，生成器自动注册）
//
// 示例游戏（胶囊体主角 × 扫地机器人敌人）的 ECS 组件。
// 所有状态是纯数据（可序列化/回放/确定性）。

using Friflo.Engine.ECS;

namespace ShooterPoc;

// --- 基础组件 ---
public struct Position : IComponent { public float X, Z; }
public struct Velocity : IComponent { public float X, Z; }
public struct Health   : IComponent { public int Current, Max; }
public struct Radius   : IComponent { public float Value; }          // 碰撞半径（swept 检测用）

// --- 玩家 ---
public struct PlayerControl : IComponent { public float MoveSpeed; }

// --- 射击 ---
public struct Weapon : IComponent { public float Cooldown; public float BulletSpeed; public float Timer; }

// --- 敌人 ---
public struct EnemyAI : IComponent { public float Speed; }

// --- 子弹 ---
public struct Bullet : IComponent { public float Damage; public float Range; public float Travelled; }

// --- 生成器（Resource 单例，非实体组件）---
public class SpawnConfig
{
    public float Interval;      // 生成间隔（秒）
    public int MaxAlive;        // 最大存活敌人
    public float SpawnRadius;   // 生成半径（从四边）
    public float PlayerX, PlayerZ;  // 玩家位置（AI 目标）
}

// --- 游戏状态（Resource 单例）---
public class GameState
{
    public GamePhase Phase;
    public int Score;
    public int AliveEnemies;
}
public enum GamePhase { Playing, GameOver }

// --- 标签 ---
public struct PlayerTag : ITag { }
public struct EnemyTag  : ITag { }
public struct BulletTag : ITag { }
