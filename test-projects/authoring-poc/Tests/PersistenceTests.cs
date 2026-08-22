// SPDX-License-Identifier: MIT
// PersistenceTests.cs —— 确定性持久化：Save → Load → Save 逐字节相同，hash 不变

using System;
using System.IO;
using System.Linq;
using Baize.Authoring;

namespace AuthoringPoc.Tests;

internal static class PersistenceTests
{
	public static void RunRoundTripIsByteStable(Action<bool, string> check)
	{
		var (world, ids) = TestSupport.BuildScene();

		// 加一些"会破坏确定性"的隐患数据：乱序创建的关系、浮点、覆盖
		var tx = new AuthoringTransaction();
		tx.AddRelation(ids.Enemy2, "Targets", ids.Player);
		tx.AddRelation(ids.Enemy1, "Targets", ids.Player);
		world.Apply(tx);

		string dir = ".tmp";
		Directory.CreateDirectory(dir);
		string pathA = Path.Combine(dir, "authoring-roundtrip-a.bscene");
		string pathB = Path.Combine(dir, "authoring-roundtrip-b.bscene");

		try
		{
			ulong hashBefore = world.ComputeArtifactHash();

			AuthoringSceneFile.Save(world, pathA);
			byte[] bytesA = File.ReadAllBytes(pathA);

			// 往返：Load 成新世界再保存
			AuthoringWorld loaded = AuthoringSceneFile.Load(pathA, world.Schema);
			check(loaded.ComputeArtifactHash() == hashBefore, "Load 后 Artifact hash 应与保存前一致");
			check(loaded.ObjectCount == world.ObjectCount, "Load 后对象数应一致");

			// 层级/关系/组件值逐一核对
			check(loaded.Require(ids.Enemy1).ParentId == ids.Group, "往返后层级父保持");
			check(loaded.Require(ids.Enemy2).Relations.Single().TargetId == ids.Player,
				"往返后关系目标保持");
			check(loaded.ChildrenOf(ids.Group).Count == 2, "往返后 children 索引重建正确");

			AuthoringSceneFile.Save(loaded, pathB);
			byte[] bytesB = File.ReadAllBytes(pathB);
			check(bytesA.SequenceEqual(bytesB), "Save→Load→Save 必须逐字节相同（Git 友好）");

			// nextId 恢复：加载后新分配的 Id 不与已有对象冲突
			StableId allocated = loaded.AllocateId();
			check(!loaded.Exists(allocated) && allocated.Value > ids.Enemy2.Value,
				"加载后分配的 Id 应大于场景内全部 Id");

			// 往返后的世界继续可用：事务 + undo 正常
			var postTx = new AuthoringTransaction();
			postTx.Rename(ids.Player, "RenamedAfterLoad");
			loaded.Apply(postTx);
			check(loaded.FindByName("RenamedAfterLoad") is not null, "往返后的世界可继续编辑");
			loaded.Undo();
			check(loaded.FindByName("Player") is not null, "往返后 undo 可用");
		}
		finally
		{
			File.Delete(pathA);
			File.Delete(pathB);
		}

		Console.WriteLine("authoring-poc: 确定性持久化验证通过（字节稳定 + hash 保持 + 继续可编辑）");
	}
}
