// SPDX-License-Identifier: MIT
// ShooterGame.cs —— Shooter PoC 的唯一 Composition Root

using Sola3d.Ecs;

namespace Shooter.Gameplay;

public static class ShooterGame
{
	/// <summary>
	/// 安装一局可运行的 Shooter：先放全局事实，再生成初始对象，最后启用玩法规则。
	/// 读游戏结构时先看这里，不需要先理解 Friflo 查询细节。
	/// </summary>
	public static void Install(EcsWorld world)
	{
		world
			.InsertState(new SpawnConfig())
			.InsertState(new SpawnState())
			.InsertState(new FireInputState())
			.InsertState(new MatchState())
			.InsertState(new ShooterSnapshotState());

		world.SpawnNow(PlayerBundle.Default);
		world.GetState<ShooterSnapshotState>().Current = new ShooterSnapshotExtractor().Extract(world);
		world.AddFeature(new ShooterFeature());
	}
}
