# Windows 3D profile for testing VR/XR pruning only.
# Usage:
#   scons profile=misc/customization/scons-profiles/windows_3d_prune_vr_xr.py -j8
#
# Android, iOS, and Web platform export chains are kept.
# This only disables VR/XR features, including WebXR.

platform = "windows"
dev_build = "yes"

accesskit = "no"
angle = "no"

disable_xr = "yes"
module_openxr_enabled = "no"
module_mobile_vr_enabled = "no"
module_webxr_enabled = "no"

