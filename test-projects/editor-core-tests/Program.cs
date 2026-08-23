// SPDX-License-Identifier: MIT
// Program.cs —— editor-core-tests：O7 编辑器第一切片验证（纯 .NET headless）
//
// 断言：① Create→Add 组件→SetProperty→Save→Load 往返保真（§14.9 闭环）
//       ② Undo 属性/对象 ③ 层级编辑 SetParent ④ 保存文本含预期结构。

using System;
using System.Collections.Generic;
using System.Numerics;
using Sola3d.Editor;
using Sola3d.GameObject;

namespace EditorCoreTests;

internal static class Program
{
	private static int _passed;
	private static readonly List<string> _failed = new();

	private static void Check(string name, bool condition)
	{
		if (condition)
		{
			_passed++;
		}
		else
		{
			_failed.Add(name);
			Console.WriteLine($"[失败] {name}");
		}
	}

	private static void Test_编辑保存重载闭环()  // §14.9：Create → Add → Save → Reopen → 断言
	{
		var reg = new ComponentSchemaRegistry();
		reg.Register<TransformComponent>();
		reg.Register<MeshComponent>();

		var session = new EditorSession(schemas: reg);
		var cube = session.CreateGameObject("Cube");
		var tf = session.AddComponent(cube, reg.Get<TransformComponent>());
		session.SetProperty(tf, "Position", new Vector3(1, 2, 3));
		session.SetProperty(tf, "Scale", new Vector3(2, 2, 2));
		var mesh = session.AddComponent(cube, reg.Get<MeshComponent>());
		session.SetProperty(mesh, "MeshPath", "res://models/cube.mesh");

		string text = session.SaveSceneText();
		Check("闭环：保存文本含对象行", text.Contains("object @") && text.Contains("\"Cube\""));
		Check("闭环：保存文本含组件块", text.Contains("[component ") && text.Contains("Position = \"(1.0,2.0,3.0)\""));

		// Close/Reopen：新会话载入（Deserialize = 语法层，未知/已知属性都保留 token 原文——Design 保真）
		var reopened = EditorSession.LoadScene(text, reg);
		Check("闭环：重载对象数一致", reopened.Document.Objects.Count == 1);
		var rc = reopened.Document.Objects[0];
		Check("闭环：重载名字一致", rc.Name == "Cube");
		Check("闭环：重载 Uid 一致（Design 稳定身份）", rc.Uid == cube.Uid);
		Check("闭环：重载组件数一致", rc.Components.Count == 2);
		// Design 级保真：再序列化 == 原文本（幂等，token 原文保留）
		Check("闭环：文本幂等（重载后 Serialize == 原文本）", reopened.SaveSceneText() == text);
		// 语义值验证走 Restore（语法层 token → 运行时值）：快照成长为世界后读组件
		var world = GameWorldSerializer.Restore(reopened.Document, reg);
		var rt = world.Roots[0].GetComponent<TransformComponent>()!;
		Check("闭环：Restore 后 Position 语义值正确", rt.Position == new Vector3(1, 2, 3));
	}

	private static void Test_Undo()
	{
		var reg = new ComponentSchemaRegistry();
		reg.Register<TransformComponent>();
		var session = new EditorSession(schemas: reg);
		var cube = session.CreateGameObject("Cube");
		var pos = session.Document.Objects[0];
		var tf = session.AddComponent(pos, reg.Get<TransformComponent>());
		session.SetProperty(tf, "Position", new Vector3(9, 9, 9));

		// Undo 属性：回到无属性
		session.Undo();
		Check("Undo：属性被移除（无旧值）", tf.Properties.Find(kv => kv.Key == "Position").Key == null);

		// 改第二遍再 Undo：回到第一遍值（栈 LIFO）
		session.SetProperty(tf, "Position", new Vector3(1, 1, 1));
		session.SetProperty(tf, "Position", new Vector3(2, 2, 2));
		session.Undo();
		Check("Undo：回到上一个值", tf.Properties.Find(kv => kv.Key == "Position").Value is Vector3 vv && vv == new Vector3(1, 1, 1));

		// Undo AddComponent：组件消失
		session.Undo(); // 撤 SetProperty(1,1,1)
		session.Undo(); // 撤 SetProperty(9,9,9)——但它在 AddComponent 之后，先撤属性
						// 再撤一次撤 AddComponent
		session.Undo();
		Check("Undo：组件被移除", pos.Components.Count == 0);

		// Undo CreateGameObject：对象消失
		session.Undo();
		Check("Undo：对象被移除", session.Document.Objects.Count == 0);
	}

	private static void Test_层级编辑()
	{
		var session = new EditorSession();
		var root = session.CreateGameObject("Root");
		var child = session.CreateGameObject("Child");
		session.SetParent(child, root);
		Check("层级：子对象 ParentIndex 指向父", child.ParentIndex == 0);
		session.Undo();
		Check("层级：Undo 回顶层（-1）", child.ParentIndex == -1);
	}

	private static int Main()
	{
		Console.WriteLine("editor-core-tests —— O7 编辑器第一切片验证（Design World 编辑闭环）\n");

		Test_编辑保存重载闭环();
		Test_Undo();
		Test_层级编辑();

		Console.WriteLine($"\n通过 {_passed} 项，失败 {_failed.Count} 项");
		if (_failed.Count > 0)
		{
			Console.WriteLine("失败清单：" + string.Join(", ", _failed));
			return 1;
		}
		Console.WriteLine("全部通过 ✅");
		return 0;
	}
}
