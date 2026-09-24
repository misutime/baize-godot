// SPDX-License-Identifier: MIT
// HudPresenter.cs —— HudSnapshot 到 Control 的无状态投影

using Godot;
using Shooter.Gameplay;

namespace Baize.GodotSlice;

public partial class HudPresenter : Control
{
	[Export] public NodePath HostPath { get; set; } = new("../../../EcsHost");
	[Export] public NodePath ScoreLabelPath { get; set; } = new("Panel/Margin/Rows/Score");
	[Export] public NodePath StatusLabelPath { get; set; } = new("Panel/Margin/Rows/Status");

	private EcsHost? _host;
	private Label? _score;
	private Label? _status;
	private ulong _presentedTick = ulong.MaxValue;

	public override void _Ready()
	{
		_host = GetNodeOrNull<EcsHost>(HostPath);
		_score = GetNode<Label>(ScoreLabelPath);
		_status = GetNode<Label>(StatusLabelPath);
	}

	public override void _Process(double delta)
	{
		if (_host is null || _score is null || _status is null) return;
		HudSnapshot snapshot = _host.CurrentSnapshot.Hud;
		if (_presentedTick == snapshot.TickIndex) return;
		_presentedTick = snapshot.TickIndex;

		_score.Text = $"分数：{snapshot.Score}　敌人：{snapshot.AliveEnemies}";
		_status.Text = snapshot.Phase == GamePhase.Playing
			? "状态：游戏中"
			: "状态：游戏结束，按 R 重新开始";
	}
}
