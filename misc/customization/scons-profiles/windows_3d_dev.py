# Windows 3D development baseline.
# Usage:
#   scons profile=misc/customization/scons-profiles/windows_3d_dev.py -j8

platform = "windows"
dev_build = "yes"

# Keep optional dependencies off by default for easier setup.
accesskit = "no"
angle = "no"
