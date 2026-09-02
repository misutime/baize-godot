// Copyright (c) Ullrich Praetz - https://github.com/friflo. All rights reserved.
// See LICENSE file in the project root for full license information.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using Friflo.Engine.ECS.Collections;
// ReSharper disable ConvertToConstant.Local
// ReSharper disable HeuristicUnreachableCode
// ReSharper disable ReturnTypeCanBeEnumerable.Global
namespace Friflo.Engine.ECS.Utils;

public static class TreeUtils
{
#region Duplicate Entity's
    /// <returns> the indexes of the duplicated entities within the parent of the original entities</returns>
    public static int[] DuplicateEntities(List<Entity> entities)
    {
        var indexes = new int [entities.Count];
        var store   = entities[0].Store;
        int pos     = 0;
        foreach (var entity in entities) {
            var parent  = entity.Parent;
            if (parent.IsNull) {
                indexes[pos++] = -1;
                continue;
            }
            var clone       = entity.CloneEntity();
            var index       = parent.AddChild(clone);
            indexes[pos++]  = index;
            DuplicateChildren(entity, clone, store);
        }
        return indexes;
    }
    
    private static void DuplicateChildren(Entity entity, Entity clone, EntityStore store)
    {
        foreach (var childId in entity.ChildIds) {
            var child       = store.GetEntityById(childId);
            var childClone  = child.CloneEntity();
            clone.AddChild(childClone);
            
            DuplicateChildren(child, childClone, store);
        }
    }
    
    #endregion
    
    
    
#region Remove ExplorerItem's
    private static readonly bool Log = false;
    
    [ExcludeFromCodeCoverage]
    private static void LogRemove(Entity parent, Entity entity) {
        if (!Log) return;
        var msg = $"parent id: {parent.Id} - Remove child id: {entity.Id}";
        Console.WriteLine(msg);
    }
    
    public static void RemoveExplorerItems(ExplorerItem[] items)
    {
        foreach (var item in items) {
            var entity = item.Entity; 
            if (entity.TreeMembership != TreeMembership.treeNode) {
                // case: entity is not a tree member => cannot remove from tree
                continue;
            }
            var parent = entity.Parent;
            if (parent.IsNull) {
                // case: entity is root item => cannot remove root item
                continue;
            }
            LogRemove(parent, entity);
            parent.RemoveChild(entity);
        }
    }
    #endregion
    
#region Move ExplorerItem's
    [ExcludeFromCodeCoverage]
    private static void LogMove(Entity parent, int newIndex, Entity entity) {
        if (!Log) return;
        var msg = $"parent id: {parent.Id} - Move child: Child[{newIndex}] = {entity.Id}";
        Console.WriteLine(msg);
    }

    public static int[] MoveExplorerItemsUp(ExplorerItem[] items, int shift)
    {
        var parent  = items[0].Entity.Parent;
        if (parent.IsNull) {
            return null;
        }
        var indexes = new int[items.Length];
        var pos     = 0;
        foreach (var item in items)
        {
            var entity      = item.Entity;
            int index       = parent.GetChildIndex(entity);
            int newIndex    = index - shift;
            if (newIndex < pos) {
                indexes[pos] = index;
            } else {
                indexes[pos] = newIndex;
                LogMove(parent, newIndex, entity);
                parent.InsertChild(newIndex, entity);
            }
            pos++;
        }
        return indexes;
    }
    
    public static int[] MoveExplorerItemsDown(ExplorerItem[] items, int shift)
    {
        var parent      = items[0].Entity.Parent;
        if (parent.IsNull) {
            return null;
        }
        var indexes     = new int[items.Length];
        var childCount  = parent.ChildCount;
        var pos         = 0;
        for (int n = items.Length - 1; n >= 0; n--)
        {
            var entity      = items[n].Entity;
            int index       = parent.GetChildIndex(entity);
            int newIndex    = index + shift;
            if (newIndex >= childCount - pos++) {
                indexes[n] = index;
                continue;
            }
            indexes[n] = newIndex;
            LogMove(parent, newIndex, entity);
            parent.InsertChild(newIndex, entity);
        }
        return indexes;
    }
    #endregion
}
