# site_scons/site_init.py -- baize-godot global SCons config
#
# FORK-CUSTOM (Chinese-first constitution): force full-chain UTF-8.
# On Windows, Python locale defaults to GBK; SCons reads profile/config files
# with the locale encoding, so UTF-8 files containing Chinese fail to decode.
# This file is loaded by SCons at startup, pinning the default encoding to UTF-8
# regardless of the calling environment (build.py/task/env).
# NOTE: This file MUST stay pure ASCII -- SCons loads it before UTF-8 takes
# effect, so any non-ASCII byte here fails to decode (GBK) on Windows.

import locale
import sys

# Python 3.7+: enable UTF-8 mode (equivalent to PYTHONUTF8=1).
try:
    sys.flags.utf8_mode = True
except Exception:
    pass

# Pin locale to a UTF-8 locale so SCons uses UTF-8 for file reads.
for loc in ("C.UTF-8", "en_US.UTF-8"):
    try:
        locale.setlocale(locale.LC_ALL, loc)
        break
    except Exception:
        continue

# Fallback: force open() default encoding to UTF-8 on Python < 3.15.
if sys.version_info < (3, 15):
    try:
        import builtins

        _orig_open = builtins.open

        def _utf8_open(file, mode="r", buffering=-1, encoding=None, errors=None, newline=None, closefd=True, opener=None):
            if "b" not in mode and encoding is None:
                encoding = "utf-8"
            return _orig_open(file, mode, buffering, encoding, errors, newline, closefd, opener)

        builtins.open = _utf8_open
    except Exception:
        pass
