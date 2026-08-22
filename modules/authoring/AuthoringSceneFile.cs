// SPDX-License-Identifier: MIT
// AuthoringSceneFile.cs —— W1 场景的确定性持久化（P2.4）
//
// Git 友好的稳定 JSON：
// - 对象按 StableId 升序、组件按类型全名序、关系按 (类型,目标) 序、覆盖集按字节序；
// - 字段顺序由 Schema 声明序决定、枚举写名字、浮点 round-trip——
//   同一数据永远得到同一文件字节；Save → Load → Save 逐字节相同。
// - nextId 随文件保存：加载后继续分配不会撞已有 Id。
//
// 事务历史不持久化（编辑会话内有效）；场景文件即 Artifact。

using System;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Baize.Authoring;

public static class AuthoringSceneFile
{
	public const string FormatName = "baize-scene";
	public const int FormatVersion = 1;

	/// <summary>保存 W1 世界为稳定 JSON 文件。</summary>
	public static void Save(AuthoringWorld world, string path)
	{
		if (world is null) throw new ArgumentNullException(nameof(world));
		using var stream = File.Create(path);
		Save(world, stream);
	}

	public static void Save(AuthoringWorld world, Stream stream)
	{
		var options = new JsonWriterOptions { Indented = true };
		using var writer = new Utf8JsonWriter(stream, options);

		writer.WriteStartObject();
		writer.WriteString("format", FormatName);
		writer.WriteNumber("version", FormatVersion);
		writer.WriteNumber("nextId", world.CurrentNextId);

		writer.WriteStartArray("objects");
		foreach (var id in SortedIds(world))
		{
			var obj = world.Require(id);
			writer.WriteStartObject();
			writer.WriteNumber("id", id.Value);
			writer.WriteString("name", obj.Name);
			writer.WriteNumber("parent", obj.ParentId.Value);
			if (obj.PrototypeId is { } prototype)
			{
				writer.WriteNumber("prototype", prototype.Value);
			}

			writer.WriteStartObject("components");
			foreach (var pair in world.SortedComponents(obj))
			{
				var schema = world.Schema.Get(pair.Key);
				writer.WritePropertyName(schema.TypeName);
				schema.WriteJson(writer, pair.Value);
			}
			writer.WriteEndObject();

			writer.WriteStartArray("relations");
			foreach (var relation in world.SortRelations(obj))
			{
				writer.WriteStartObject();
				writer.WriteString("type", relation.RelationType);
				writer.WriteNumber("target", relation.TargetId.Value);
				writer.WriteEndObject();
			}
			writer.WriteEndArray();

			writer.WriteStartArray("overrides");
			foreach (string typeName in world.SortedOverrides(obj))
			{
				writer.WriteStringValue(typeName);
			}
			writer.WriteEndArray();

			writer.WriteEndObject();
		}
		writer.WriteEndArray();
		writer.WriteEndObject();
		writer.Flush();
	}

	/// <summary>从稳定 JSON 文件加载为新世界（undo/redo 历史不恢复）。</summary>
	public static AuthoringWorld Load(string path, AuthoringSchema schema)
	{
		if (schema is null) throw new ArgumentNullException(nameof(schema));
		using var document = JsonDocument.Parse(File.ReadAllBytes(path));
		return FromJson(document.RootElement, schema);
	}

	public static AuthoringWorld Load(Stream stream, AuthoringSchema schema)
	{
		if (schema is null) throw new ArgumentNullException(nameof(schema));
		using var document = JsonDocument.Parse(stream);
		return FromJson(document.RootElement, schema);
	}

	internal static AuthoringWorld FromJson(JsonElement root, AuthoringSchema schema)
	{
		string? format = root.TryGetProperty("format", out var formatElement) ? formatElement.GetString() : null;
		if (!string.Equals(format, FormatName, StringComparison.Ordinal))
		{
			throw new InvalidDataException($"不是 {FormatName} 场景文件：format='{format}'");
		}
		int version = root.TryGetProperty("version", out var versionElement) ? versionElement.GetInt32() : 0;
		if (version > FormatVersion)
		{
			throw new InvalidDataException($"场景格式版本过新：文件 v{version}，本程序支持到 v{FormatVersion}");
		}

		var world = new AuthoringWorld(schema);

		foreach (var objectElement in root.GetProperty("objects").EnumerateArray())
		{
			var id = new StableId(objectElement.GetProperty("id").GetUInt64());
			string name = objectElement.GetProperty("name").GetString()
				?? throw new InvalidDataException($"对象 {id} 的 name 缺失");
			var parent = new StableId(objectElement.GetProperty("parent").GetUInt64());
			StableId? prototype = objectElement.TryGetProperty("prototype", out var protoElement)
				? new StableId(protoElement.GetUInt64())
				: null;

			if (parent != default && !world.Exists(parent))
			{
				throw new InvalidDataException($"对象 {id} 的父 {parent} 在文件中不存在或尚未定义（父必须先于子出现）");
			}

			var transaction = new AuthoringTransaction();
			transaction.Add(new CreateObjectOp(id, name, parent));
			if (prototype is { } prototypeValue)
			{
				transaction.Add(new SetPrototypeOp(id, prototypeValue));
			}

			foreach (var componentProperty in objectElement.GetProperty("components").EnumerateObject())
			{
				transaction.Add(new AddComponentOp(id, componentProperty.Name, componentProperty.Value.Clone()));
			}

			foreach (var relationElement in objectElement.GetProperty("relations").EnumerateArray())
			{
				transaction.Add(new AddRelationOp(
					id,
					relationElement.GetProperty("type").GetString()
						?? throw new InvalidDataException($"对象 {id} 有缺 type 的关系"),
					new StableId(relationElement.GetProperty("target").GetUInt64())));
			}

			world.Apply(transaction);

			// overrides 不走事务（是原型语义的派生记录）：直接落盘数据原样恢复
			if (prototype is not null && objectElement.TryGetProperty("overrides", out var overridesElement))
			{
				var restored = world.Require(id);
				foreach (var overrideElement in overridesElement.EnumerateArray())
				{
					restored._overrides.Add(overrideElement.GetString()
						?? throw new InvalidDataException($"对象 {id} 有空 override 记录"));
				}
			}
		}

		if (root.TryGetProperty("nextId", out var nextIdElement))
		{
			ulong savedNextId = nextIdElement.GetUInt64();
			while (world.CurrentNextId < savedNextId)
			{
				world.AllocateId();   // 推进计数器到保存值（空洞无害）
			}
		}
		return world;
	}

	private static System.Collections.Generic.List<StableId> SortedIds(AuthoringWorld world)
	{
		var ids = new System.Collections.Generic.List<StableId>(world.ObjectCount);
		foreach (var obj in world.Objects)
		{
			ids.Add(obj.Id);
		}
		ids.Sort();
		return ids;
	}
}

internal static class AuthoringWorldSerializationExtensions
{
	/// <summary>组件按类型全名排序（序列化与 hash 共用同一确定性顺序）。</summary>
	public static System.Collections.Generic.IEnumerable<System.Collections.Generic.KeyValuePair<Type, object>>
		SortedComponents(this AuthoringWorld world, AuthoringObject obj)
	{
		var pairs = new System.Collections.Generic.List<System.Collections.Generic.KeyValuePair<Type, object>>(obj.Components);
		pairs.Sort((a, b) => string.CompareOrdinal(a.Key.FullName, b.Key.FullName));
		return pairs;
	}

	public static System.Collections.Generic.IEnumerable<AuthoringRelation> SortRelations(this AuthoringWorld world, AuthoringObject obj) =>
		obj.Relations
			.OrderBy(r => r.RelationType, StringComparer.Ordinal)
			.ThenBy(r => r.TargetId);

	public static System.Collections.Generic.IEnumerable<string> SortedOverrides(this AuthoringWorld world, AuthoringObject obj) =>
		obj.OverriddenComponents.OrderBy(n => n, StringComparer.Ordinal);
}
