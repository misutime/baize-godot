// SPDX-License-Identifier: MIT
// ActorComponents.cs —— 角色能力与阵营关系

using Friflo.Engine.ECS;

namespace Shooter.Gameplay;

// 能力特征：这个实体可以读取玩家输入。它不携带速度参数，也不是“玩家字段表”。
public struct PlayerInput : IComponent { }

// 能力特征：这个实体会寻找玩家阵营目标。目标位置每 Tick 直接查询，不复制进配置。
public struct SeekTarget : IComponent { }

// 标签关系：阵营回答“它和谁是一边”，不描述移动、武器或 AI。
public struct PlayerFaction : ITag { }
public struct EnemyFaction : ITag { }
