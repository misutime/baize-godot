// SPDX-License-Identifier: MIT
// CubeGeometry.cs —— 单位立方体几何数据（O7.5，O7.5-最小可展示切片.md §2.1）
//
// 纯数据：24 顶点（每面 4 顶点、独立法线）+ 36 索引（12 三角形）。
// 供 RenderingServer.MeshAddSurfaceFromArrays 使用（Godot 4 数组下标约定）。
// headless 可断言：顶点数/索引数/索引范围合法。

using System.Numerics;

namespace Sola3d.Host;  // 放 mainloop 旁（纯数据；未来 Gateway 共用）

/// <summary>单位立方体（边长 1，中心在原点）几何：vertices/normals/indices 三数组。</summary>
public static class CubeGeometry
{
	/// <summary>24 顶点（每面 4 顶点独立，保证硬边法线正确）。</summary>
	public static Vector3[] Vertices { get; } = FillVertices();

	/// <summary>24 法线（逐顶点、每面相同）。</summary>
	public static Vector3[] Normals { get; } = FillNormals();

	/// <summary>36 索引（12 个三角形，逆时针迎外）。</summary>
	public static int[] Indices { get; } = FillIndices();

	private static Vector3[] FillVertices()
	{
		const float h = 0.5f;
		return new[]
		{
			// +X
			new Vector3(h, -h, -h), new Vector3(h, h, -h), new Vector3(h, h, h), new Vector3(h, -h, h),
			// -X
			new Vector3(-h, -h, h), new Vector3(-h, h, h), new Vector3(-h, h, -h), new Vector3(-h, -h, -h),
			// +Y
			new Vector3(-h, h, -h), new Vector3(-h, h, h), new Vector3(h, h, h), new Vector3(h, h, -h),
			// -Y
			new Vector3(-h, -h, h), new Vector3(-h, -h, -h), new Vector3(h, -h, -h), new Vector3(h, -h, h),
			// +Z
			new Vector3(-h, -h, h), new Vector3(h, -h, h), new Vector3(h, h, h), new Vector3(-h, h, h),
			// -Z
			new Vector3(h, -h, -h), new Vector3(-h, -h, -h), new Vector3(-h, h, -h), new Vector3(h, h, -h),
		};
	}

	private static Vector3[] FillNormals()
	{
		var n = new Vector3[24];
		for (int i = 0; i < 24; i++)
		{
			n[i] = (i / 4) switch
			{
				0 => Vector3.UnitX,
				1 => -Vector3.UnitX,
				2 => Vector3.UnitY,
				3 => -Vector3.UnitY,
				4 => Vector3.UnitZ,
				_ => -Vector3.UnitZ,
			};
		}
		return n;
	}

	private static int[] FillIndices()
	{
		var idx = new int[36];
		int t = 0;
		for (int f = 0; f < 6; f++)
		{
			int b = f * 4;
			// 两个三角形（逆时针迎外）：(b, b+1, b+2)(b, b+2, b+3)
			idx[t++] = b + 0; idx[t++] = b + 1; idx[t++] = b + 2;
			idx[t++] = b + 0; idx[t++] = b + 2; idx[t++] = b + 3;
		}
		return idx;
	}
}
