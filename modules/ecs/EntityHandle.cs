// SPDX-License-Identifier: MIT
// EntityHandle.cs —— baize-godot EcsWorld 实体句柄（P2.1）
//
// 实体安全句柄：Id + Revision，防止 ID 复用后旧句柄错指新实体。

using System;

namespace Baize.Ecs;

/// <summary>
/// 实体句柄（Id + Revision）。跨 Tick 安全引用实体。
/// </summary>
public readonly struct EntityHandle
{
    /// <summary>实体 Id。</summary>
    public readonly int Id;

    /// <summary>实体 Revision（删除重建后递增，防错指）。</summary>
    public readonly short Revision;

    public EntityHandle(int id, short revision)
    {
        Id = id;
        Revision = revision;
    }

    public bool Equals(EntityHandle other) => Id == other.Id && Revision == other.Revision;

    public override bool Equals(object? obj) => obj is EntityHandle other && Equals(other);

    public override int GetHashCode() => HashCode.Combine(Id, Revision);

    public static bool operator ==(EntityHandle a, EntityHandle b) => a.Equals(b);
    public static bool operator !=(EntityHandle a, EntityHandle b) => !a.Equals(b);

    public override string ToString() => $"EntityHandle({Id}:{Revision})";
}

