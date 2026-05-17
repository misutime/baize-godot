# macOS 3D 开发基线。
# 用法：
#   scons profile=misc/customization/scons-profiles/macos_3d_dev.py -j8

platform = "macos"
dev_build = "yes"

# 默认关闭可选依赖，让新机器更容易先跑起来。
accesskit = "no"
angle = "no"

# macOS 基线直接使用 Metal；Vulkan 需要额外准备 MoltenVK SDK。
vulkan = "no"
metal = "yes"
generate_bundle = "yes"
