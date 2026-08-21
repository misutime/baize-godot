# Windows 3D development baseline (baize-godot All-in C#).
# Usage:
#   scons profile=misc/customization/scons-profiles/windows_3d_dev.py -j8

platform = "windows"
dev_build = "yes"

# All-in C# 路线：默认启用 C# (mono)，禁用 GDScript（源码保留，先禁用后裁剪）。
module_mono_enabled = "yes"
module_gdscript_enabled = "no"

# Keep optional dependencies off by default for easier setup.
accesskit = "no"
angle = "no"
