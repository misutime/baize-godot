// SPDX-License-Identifier: MIT
// Identity.cs —— 运行时/作者身份（O1，方案 §14.7）
//
// 两层身份，禁止混用：
// - ObjectId                运行时身份 = Index + Generation；防删除后 ID 复用（Generation 递增）。
// - AuthoringObjectId       静态作者 ID（.bscene/.bprefab 内稳定，O4 启用）；O1 仅占位。
// RuntimeGameObjectHandle == ObjectId（含 Generation 的运行时句柄）。

namespace Baize.GameObject;

/// <summary>
/// 运行时对象身份：Index（槽位）+ Generation（防复用）。
/// 旧 ObjectId 永不等于新对象：Destroy 后槽位可复用，但 Generation 递增。
/// </summary>
public readonly record struct ObjectId(int Index, uint Generation)
{
	/// <summary>无效身份（默认值）。</summary>
	public static readonly ObjectId Invalid = new(-1, 0);

	/// <summary>是否有效（Index 非负）。</summary>
	public bool IsValid => Index >= 0;

	/// <summary>对当前身份取"下一 Generation"（销毁时使用，其余场景勿用）。</summary>
	internal ObjectId NextGeneration() => new(Index, Generation + 1);
}

/// <summary>
/// 作者/静态场景稳定 ID（方案 §14.7 —— .bscene/.bprefab 内稳定 ID）。
/// O1 仅占位并参与确定性序列化；O4 起由 .bscene/.bprefab 解析器生成。
/// </summary>
public readonly record struct AuthoringObjectId(ulong Value)
{
	/// <summary>无效作者 ID。</summary>
	public static readonly AuthoringObjectId Invalid = new(0);

	public bool IsValid => Value != 0;

	public override string ToString() => Value.ToString("x16");
}
