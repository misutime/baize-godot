// SPDX-License-Identifier: MIT
// ShooterGame.cs —— Shooter PoC 的唯一 Composition Root

using Baize.Ecs;

namespace ShooterPoc;

public static class ShooterGame
{
	/// <summary>
	/// 安装一局可运行的 Shooter：先放全局事实，再生成初始对象，最后启用玩法规则。
	/// 读游戏结构时先看这里，不需要先理解 Friflo 查询细节。
	/// </summary>
	public static void Install(EcsWorld world)
	{
		world
			.InsertResource(new MatchState())
			.InsertResource(new SpawnConfig())
			.InsertResource(new SpawnState())
			.InsertResource(new FireInputState());

		world.SpawnNow(PlayerBundle.Default);
		world.AddFeature(new ShooterFeature());
	}
}
