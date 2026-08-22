// SPDX-License-Identifier: MIT
// TransactionTests.cs —— 门禁 1/2 + 原子性：事务、diff、Undo/Redo

using System;
using System.Collections.Generic;
using System.Linq;
using Baize.Authoring;
using Shooter.Gameplay;

namespace AuthoringPoc.Tests;

internal static class TransactionTests
{
	/// <summary>门禁 1：同一操作经 UI（强类型）和 MCP（JSON op）产生相同事务与 diff。</summary>
	public static void RunUiAndMcpProduceSameTransactionAndDiff(Action<bool, string> check)
	{
		var schema = TestSupport.BuildSchema();

		// —— UI 路径：编辑器面板用强类型便捷构造 ——

		var (uiWorld, uiIds) = TestSupport.BuildScene();
		var uiTx = new AuthoringTransaction();
		uiTx.Rename(uiIds.Enemy1, "EliteEnemy");
		uiTx.SetComponent(uiIds.Enemy1, new Health { Current = 50, Max = 100 }, schema);
		AuthoringDiff uiDiff = uiWorld.Apply(uiTx);

		// —— MCP 路径：工具层从 JSON 直接构造原始 op（组件值是同一段 JSON 语义） ——

		var (mcpWorld, mcpIds) = TestSupport.BuildScene();
		var mcpTx = new AuthoringTransaction();
		mcpTx.Add(new RenameObjectOp(mcpIds.Enemy1, "EliteEnemy"));
		mcpTx.Add(new SetComponentOp(
			mcpIds.Enemy1,
			"Shooter.Gameplay.Health",
			TestSupport.Json("{\"Current\":50,\"Max\":100}")));
		AuthoringDiff mcpDiff = mcpWorld.Apply(mcpTx);

		check(SameOps(uiTx.Ops, mcpTx.Ops), $"UI 与 MCP 构造的事务不同：{Describe(uiTx)} vs {Describe(mcpTx)}");
		check(Equals(uiDiff, mcpDiff), $"UI 与 MCP 的 diff 不同：{uiDiff} vs {mcpDiff}");
		check(uiWorld.ComputeArtifactHash() == mcpWorld.ComputeArtifactHash(),
			"UI 与 MCP 路径应用后的 Artifact hash 不同");

		Console.WriteLine($"authoring-poc: UI/MCP 同事务同 diff 验证通过（diff={uiDiff.Entries.Count} 条，hash 一致）");
	}

	/// <summary>事务原子性：中途失败时世界保持原状。</summary>
	public static void RunAtomicity(Action<bool, string> check)
	{
		var (world, ids) = TestSupport.BuildScene();
		ulong before = world.ComputeArtifactHash();
		ulong versionBefore = world.Version;

		var tx = new AuthoringTransaction();
		tx.Rename(ids.Player, "RenamedPlayer");   // 第一个 op 合法
		tx.Add(new SetComponentOp(ids.Enemy1, "Shooter.Gameplay.Health",
			TestSupport.Json("{\"Current\":1,\"Max\":1}")));   // 第二个 op 也合法
		tx.Add(new RenameObjectOp(new StableId(999), "Ghost"));   // 第三个 op 失败：对象不存在

		bool threw = false;
		try
		{
			world.Apply(tx);
		}
		catch (AuthoringTransactionException ex)
		{
			threw = true;
			check(ex.Message.Contains("3"), $"异常应指出失败位置（第 3 个操作）：{ex.Message}");
		}
		check(threw, "非法事务应抛 AuthoringTransactionException");
		check(world.ComputeArtifactHash() == before, "事务回滚后 Artifact hash 应与 Apply 前一致");
		check(world.Version == versionBefore, "事务回滚后版本号不应推进");
		check(world.Find(ids.Player)!.Name == "Player", "回滚后第一个 op 的效果也不应保留");
		Console.WriteLine("authoring-poc: 事务原子性验证通过");
	}

	/// <summary>门禁 2：Undo/Redo 后 Artifact hash 完全恢复（含级联删除的恢复）。</summary>
	public static void RunUndoRedoRestoresArtifactHash(Action<bool, string> check)
	{
		var (world, ids) = TestSupport.BuildScene();
		ulong h0 = world.ComputeArtifactHash();

		// 事务 A：改玩家数值 + 改敌人血量
		var txA = new AuthoringTransaction();
		txA.SetComponent(ids.Player, new Position { X = 2f, Z = 3f }, world.Schema);
		txA.SetComponent(ids.Enemy1, new Health { Current = 10, Max = 30 }, world.Schema);
		world.Apply(txA);
		ulong hA = world.ComputeArtifactHash();

		// 事务 B：级联删除 EnemyGroup（连 Enemy1/Enemy2 一起删）
		var txB = new AuthoringTransaction();
		txB.Delete(ids.Group);
		world.Apply(txB);
		ulong hB = world.ComputeArtifactHash();
		check(hB != hA && hA != h0, "事务应改变场景 hash");
		check(!world.Exists(ids.Enemy1), "级联删除后 Enemy1 应不存在");

		// Undo B → 恢复整棵子树
		world.Undo();
		check(world.ComputeArtifactHash() == hA, "Undo 级联删除后 hash 应恢复到事务 A 后状态");
		check(world.Exists(ids.Enemy1) && world.Exists(ids.Enemy2), "Undo 后子树对象应全部恢复");

		// Undo A → 回到初始
		world.Undo();
		check(world.ComputeArtifactHash() == h0, "Undo 全部后 hash 应恢复初始状态");
		var health = world.Require(ids.Enemy1).Components[typeof(Health)];
		check(((Health)health).Current == 30, "Undo 后组件值应为旧值（30）");

		// Redo ×2 → 重放到 hB
		world.Redo();
		check(world.ComputeArtifactHash() == hA, "Redo 事务 A 后 hash 应为 hA");
		world.Redo();
		check(world.ComputeArtifactHash() == hB, "Redo 事务 B 后 hash 应为 hB");
		check(!world.Exists(ids.Group), "Redo 级联删除再次生效");

		// 再 Undo → hA；新事务分支清空 redo
		world.Undo();
		check(world.ComputeArtifactHash() == hA, "再 Undo 后 hash 应回到 hA");
		check(world.CanRedo, "Undo 后应可 Redo");

		var txC = new AuthoringTransaction();
		txC.Rename(ids.Player, "BranchedPlayer");
		world.Apply(txC);
		check(!world.CanRedo, "新事务应清空 redo 栈");

		Console.WriteLine("authoring-poc: Undo/Redo hash 完全恢复验证通过（含级联删除恢复与 redo 分支清理）");
	}

	internal static bool SameOps(IReadOnlyList<AuthoringOp> left, IReadOnlyList<AuthoringOp> right)
	{
		if (left.Count != right.Count) return false;
		for (int index = 0; index < left.Count; index++)
		{
			if (Equals(left[index], right[index])) continue;

			if (left[index] is SetComponentOp lSet && right[index] is SetComponentOp rSet)
			{
				Console.WriteLine($"  [diag] op#{index} Id: {lSet.Id} vs {rSet.Id}; " +
					$"type: '{lSet.ComponentType}' vs '{rSet.ComponentType}'; " +
					$"value: {lSet.Value.GetRawText()} vs {rSet.Value.GetRawText()}");
			}
			else if (left[index] is RenameObjectOp lRename && right[index] is RenameObjectOp rRename)
			{
				Console.WriteLine($"  [diag] op#{index} rename: ({lRename.Id},'{lRename.NewName}') vs ({rRename.Id},'{rRename.NewName}')");
			}
			return false;
		}
		return true;
	}

	private static string Describe(AuthoringTransaction transaction) =>
		string.Join(", ", transaction.Ops.Select(op => op.GetType().Name));
}
