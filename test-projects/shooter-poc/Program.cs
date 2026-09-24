// SPDX-License-Identifier: MIT
// Program.cs —— 只负责启动可执行验收；游戏装配在 ShooterGame，断言在 Tests

namespace ShooterPoc;

internal static class Program
{
	private static int Main() => ShooterPocTests.RunAll();
}
