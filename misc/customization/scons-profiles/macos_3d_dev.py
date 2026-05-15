# macOS 3D development baseline.
# Usage:
#   scons profile=misc/customization/scons-profiles/macos_3d_dev.py -j8

platform = "macos"
dev_build = "yes"

# Keep optional dependencies off by default for easier setup.
accesskit = "no"
angle = "no"

# This baseline uses Metal directly. Vulkan on macOS needs the MoltenVK SDK.
vulkan = "no"
metal = "yes"
generate_bundle = "yes"
