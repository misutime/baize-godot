// SPDX-License-Identifier: MIT
// InputFrame.cs —— baize-godot EcsWorld 输入帧（P2.1）
//
// 不可变输入帧：Godot Input → InputAdapter → InputFrame → EcsWorld.Step。
// 回放/测试直接注入 InputFrame（不依赖 Godot.Input）。

namespace Baize.Ecs;

/// <summary>
/// 一帧的输入（不可变，回放/测试可注入）。
/// </summary>
public readonly struct InputFrame
{
    /// <summary>移动向量（水平面，X/Z）。</summary>
    public readonly float MoveX;
    public readonly float MoveZ;

    /// <summary>射击：本帧按下（边沿触发，不重复）。</summary>
    public readonly bool FirePressed;

    /// <summary>瞄准方向（世界空间，若适用）。</summary>
    public readonly float AimX;
    public readonly float AimZ;

    public InputFrame(float moveX, float moveZ, bool firePressed, float aimX = 0, float aimZ = 0)
    {
        MoveX = moveX;
        MoveZ = moveZ;
        FirePressed = firePressed;
        AimX = aimX;
        AimZ = aimZ;
    }

    /// <summary>空输入帧（用于回放/测试初始）。</summary>
    public static InputFrame Empty => new(0, 0, false);
}
