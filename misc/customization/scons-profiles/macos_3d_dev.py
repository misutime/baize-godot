# macOS 3D 开发基线 (baize-godot All-in C#)。
# 用法：
#   scons profile=misc/customization/scons-profiles/macos_3d_dev.py -j8

platform = "macos"
dev_build = "yes"

# All-in C# 路线：默认启用 C# (mono)，禁用 GDScript（源码保留，先禁用后裁剪）。
module_mono_enabled = "yes"
module_gdscript_enabled = "no"

# 默认关闭可选依赖，让新机器更容易先跑起来。
accesskit = "no"
angle = "no"

# macOS 基线直接使用 Metal；Vulkan 需要额外准备 MoltenVK SDK。
vulkan = "no"
metal = "yes"
generate_bundle = "yes"
