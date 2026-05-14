# Windows 3D editor soft-prune experimental profile.
# Usage:
#   scons profile=misc/customization/scons-profiles/windows_3d_soft_prune_experimental.py -j8
#
# This profile is for testing whether candidate features can be safely disabled.
# Do not treat these options as permanent deletion decisions.
# Stable decisions must be recorded in removal-ledger.md.

platform = "windows"
dev_build = "yes"

accesskit = "no"
angle = "no"

# VR/XR prune candidates. Web platform itself is kept; this only tests WebXR.
disable_xr = "yes"
module_openxr_enabled = "no"
module_mobile_vr_enabled = "no"
module_webxr_enabled = "no"
