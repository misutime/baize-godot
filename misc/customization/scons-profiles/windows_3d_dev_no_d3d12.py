# Windows 3D development baseline with D3D12 disabled.
# Usage:
#   scons profile=misc/customization/scons-profiles/windows_3d_dev_no_d3d12.py -j8

platform = "windows"
dev_build = "yes"

d3d12 = "no"
accesskit = "no"
angle = "no"
