# Windows 3D daily-use editor build.
# Usage:
#   scons profile=misc/customization/scons-profiles/windows_3d_pro.py -j8

platform = "windows"

# Keep optional dependencies off by default for easier setup.
accesskit = "no"
angle = "no"
