// SPDX-License-Identifier: MIT
// RenderAdapter.cs —— 双快照插值后写入程序 Mesh 与 MultiMesh；绝不回写 Gameplay

using Godot;
using Shooter.Gameplay;

namespace Sola3d.GodotSlice;

public partial class RenderAdapter : Node3D
{
	[Export] public NodePath HostPath { get; set; } = new("../../EcsHost");
	[Export] public NodePath PlayerPath { get; set; } = new("Player");
	[Export] public NodePath EnemiesPath { get; set; } = new("Enemies");
	[Export] public NodePath ProjectilesPath { get; set; } = new("Projectiles");

	private EcsHost? _host;
	private MeshInstance3D? _player;
	private MultiMesh? _enemies;
	private MultiMesh? _projectiles;

	public override void _Ready()
	{
		_host = GetNodeOrNull<EcsHost>(HostPath);
		_player = GetNode<MeshInstance3D>(PlayerPath);
		MultiMeshInstance3D enemyInstances = GetNode<MultiMeshInstance3D>(EnemiesPath);
		MultiMeshInstance3D projectileInstances = GetNode<MultiMeshInstance3D>(ProjectilesPath);

		_player.Mesh = new CapsuleMesh { Radius = 0.45f, Height = 1.4f };
		_player.MaterialOverride = CreateMaterial(new Color("55d6be"));

		var enemyMesh = new CylinderMesh
		{
			TopRadius = 0.5f,
			BottomRadius = 0.5f,
			Height = 1.0f,
		};
		enemyInstances.MaterialOverride = CreateMaterial(new Color("ef476f"));
		_enemies = CreateMultiMesh(enemyMesh);
		enemyInstances.Multimesh = _enemies;

		var projectileMesh = new SphereMesh { Radius = 0.16f, Height = 0.32f };
		projectileInstances.MaterialOverride = CreateMaterial(new Color("ffd166"), emission: true);
		_projectiles = CreateMultiMesh(projectileMesh);
		projectileInstances.Multimesh = _projectiles;
	}

	public override void _Process(double delta)
	{
		if (_host is null || _player is null || _enemies is null || _projectiles is null) return;

		float alpha = Mathf.Clamp((float)Engine.GetPhysicsInterpolationFraction(), 0, 1);
		RenderSnapshot previous = _host.PreviousSnapshot.Render;
		RenderSnapshot current = _host.CurrentSnapshot.Render;

		PresentPlayer(previous, current, alpha);
		PresentInstances(_enemies, current.Enemies, previous, alpha, 0.5f);
		PresentInstances(_projectiles, current.Projectiles, previous, alpha, 0.2f);
	}

	private void PresentPlayer(RenderSnapshot previous, RenderSnapshot current, float alpha)
	{
		if (_player is null) return;
		if (current.Players.Length == 0)
		{
			_player.Visible = false;
			return;
		}

		_player.Visible = true;
		RenderEntitySnapshot item = current.Players[0];
		_player.Position = InterpolatePosition(previous, item, alpha, 0.7f);
	}

	private static void PresentInstances(
		MultiMesh multiMesh,
		RenderEntitySnapshot[] items,
		RenderSnapshot previous,
		float alpha,
		float height)
	{
		if (multiMesh.InstanceCount != items.Length) multiMesh.InstanceCount = items.Length;
		multiMesh.VisibleInstanceCount = items.Length;

		for (int index = 0; index < items.Length; index++)
		{
			Vector3 position = InterpolatePosition(previous, items[index], alpha, height);
			multiMesh.SetInstanceTransform(index, new Transform3D(Basis.Identity, position));
		}
	}

	private static Vector3 InterpolatePosition(
		RenderSnapshot previous,
		RenderEntitySnapshot current,
		float alpha,
		float height)
	{
		previous.TryFind(current, out RenderEntitySnapshot from);
		return new Vector3(
			Mathf.Lerp(from.X, current.X, alpha),
			height,
			Mathf.Lerp(from.Z, current.Z, alpha));
	}

	private static MultiMesh CreateMultiMesh(Mesh mesh) => new()
	{
		TransformFormat = MultiMesh.TransformFormatEnum.Transform3D,
		Mesh = mesh,
		InstanceCount = 0,
		VisibleInstanceCount = 0,
	};

	private static StandardMaterial3D CreateMaterial(Color color, bool emission = false)
	{
		var material = new StandardMaterial3D
		{
			AlbedoColor = color,
			Roughness = 0.65f,
		};
		if (emission)
		{
			material.EmissionEnabled = true;
			material.Emission = color;
		}
		return material;
	}
}
