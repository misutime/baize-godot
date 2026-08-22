// SPDX-License-Identifier: MIT
// SchemaTests.cs —— Schema 注册与按名读写（生成器产物验证）

using System;
using System.Linq;
using Baize.Authoring;
using Shooter.Gameplay;

namespace AuthoringPoc.Tests;

internal static class SchemaTests
{
	public static void Run(Action<bool, string> check)
	{
		var schema = TestSupport.BuildSchema();

		check(schema.All.Count >= 12, $"应注册全部 shooter 组件（≥12），实际 {schema.All.Count}");

		var healthSchema = schema.GetByName("Shooter.Gameplay.Health");
		check(healthSchema.ComponentType == typeof(Health), "按类型名取回应是 Health");
		check(healthSchema.Fields.Select(f => f.Name).SequenceEqual(["Current", "Max"]),
			"Health 字段应按声明顺序 [Current, Max]");

		// 按名读写：模拟 MCP 侧"改字段"路径
		object boxed = healthSchema.CreateDefault();
		check(((Health)boxed).Current == 0 && ((Health)boxed).Max == 0, "默认值应为 0/0");
		healthSchema.SetFieldRaw(ref boxed, 0, 42);
		healthSchema.SetFieldRaw(ref boxed, 1, 100);
		check(((Health)boxed).Current == 42 && ((Health)boxed).Max == 100, "SetFieldRaw 应写入装箱实例");
		check((int)healthSchema.GetFieldRaw(boxed, 0) == 42, "GetFieldRaw 应回读字段值");

		// JSON 往返（持久化/MCP 的统一中间表示）
		var json = healthSchema.ToJson(boxed);
		check(json.GetProperty("Current").GetInt32() == 42, "ToJson 应含 Current=42");
		object reparsed = healthSchema.ReadJson(json);
		check(healthSchema.ValueEquals(boxed, reparsed), "JSON 往返后值应相等");
		check(!healthSchema.ValueEquals(boxed, healthSchema.CreateDefault()), "不同值不应误判相等");

		// 标签组件（无字段）也可注册与构造
		var factionSchema = schema.Get(typeof(PlayerFaction));
		check(factionSchema.Fields.Count == 0, "标签组件应无字段");
		check(factionSchema.ComponentType == typeof(PlayerFaction), "标签组件按 CLR 类型可查");

		Console.WriteLine($"authoring-poc: Schema 注册/按名读写验证通过（{schema.All.Count} 个组件）");
	}
}
