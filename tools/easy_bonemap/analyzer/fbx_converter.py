"""FBX -> GLB conversion via a headless Blender subprocess.

The converted GLB is produced inside a fresh temporary directory, so the input
FBX is never modified. This module only knows how to turn an FBX into a
pygltflib GLTF2 document (or a temporary GLB path); skeleton extraction and
semantic mapping stay out of scope.

Blender executable resolution order:
  1. ``EASY_BONEMAP_BLENDER`` environment variable (must point at an existing file)
  2. ``blender`` found on PATH
  3. ``/Applications/Blender.app/Contents/MacOS/Blender`` on macOS
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator

from pygltflib import GLTF2

BLENDER_APP_MACOS = Path("/Applications/Blender.app/Contents/MacOS/Blender")

FBX_CONVERT_TIMEOUT = 300.0

# Marker printed by the Blender script after a successful export. Blender does
# not reliably propagate ``--python`` script exceptions into its exit code, so
# the parent process treats a missing marker as a failure even on exit code 0.
FBX_CONVERT_OK = "FBX_CONVERT_OK"

# Blender-side conversion script. Paths are read from sys.argv after the ``--``
# separator, so they are passed as plain subprocess arguments (never through a
# shell) and survive spaces, quotes, and non-ASCII characters.
FBX_CONVERT_SCRIPT = r"""
import bpy
import sys
import traceback


def _arguments() -> list[str]:
    argv = sys.argv
    if "--" in argv:
        return argv[argv.index("--") + 1:]
    return []


def _convert() -> None:
    arguments = _arguments()
    if len(arguments) != 2:
        raise RuntimeError(f"expected <input.fbx> <output.glb>, got {arguments!r}")
    input_path, output_path = arguments

    bpy.ops.wm.read_factory_settings(use_empty=True)
    result = bpy.ops.import_scene.fbx(filepath=input_path)
    if "FINISHED" not in result:
        raise RuntimeError(f"FBX import did not finish: {result!r}")
    bpy.ops.export_scene.gltf(
        filepath=output_path,
        export_format="GLB",
        export_skins=True,
        export_animations=True,
    )
    print("__FBX_CONVERT_OK__")


if __name__ == "__main__":
    try:
        _convert()
    except Exception:
        traceback.print_exc()
        sys.exit(1)
""".replace("__FBX_CONVERT_OK__", FBX_CONVERT_OK)


class FbxConversionError(RuntimeError):
    """A diagnosable FBX conversion failure.

    ``stage`` names the failing pipeline phase; the message always carries the
    Blender executable, the input FBX path, and the stage.
    """

    def __init__(self, stage: str, message: str) -> None:
        super().__init__(message)
        self.stage = stage


class BlenderNotFoundError(FbxConversionError):
    """No usable Blender executable could be located."""


def find_blender_executable() -> str:
    """Locate a Blender executable, raising BlenderNotFoundError when absent."""
    configured = os.environ.get("EASY_BONEMAP_BLENDER")
    if configured:
        candidate = Path(configured).expanduser()
        if candidate.is_file():
            return str(candidate)
        raise BlenderNotFoundError(
            "blender_resolution",
            f"EASY_BONEMAP_BLENDER points at a missing Blender: {configured}",
        )
    from_path = shutil.which("blender")
    if from_path:
        return from_path
    if sys.platform == "darwin" and BLENDER_APP_MACOS.is_file():
        return str(BLENDER_APP_MACOS)
    raise BlenderNotFoundError(
        "blender_resolution",
        "No Blender executable found: set EASY_BONEMAP_BLENDER, install 'blender' "
        "on PATH, or use /Applications/Blender.app/Contents/MacOS/Blender",
    )


def build_blender_command(
    blender: str,
    script_path: Path,
    input_path: Path,
    output_path: Path,
) -> list[str]:
    """Assemble argv for a headless FBX -> GLB conversion run.

    Pure function (no I/O) so tests can pin the exact command shape. All paths
    are passed as list elements, never through a shell.
    """
    return [
        blender,
        "--background",
        "--factory-startup",
        "--python",
        str(script_path),
        "--",
        str(input_path),
        str(output_path),
    ]


@contextmanager
def convert_fbx_to_glb(
    fbx_path: str | Path,
    blender: str | None = None,
    timeout: float = FBX_CONVERT_TIMEOUT,
) -> Iterator[Path]:
    """Convert ``fbx_path`` to GLB and yield the temporary GLB path.

    The GLB and its temporary directory exist only for the duration of the
    ``with`` block; the input FBX is never modified. Any failure raises
    FbxConversionError (or FileNotFoundError for a missing input) with the
    Blender executable, the input path, and the failing stage.
    """
    source = Path(fbx_path)
    if not source.is_file():
        raise FileNotFoundError(source)
    try:
        blender_exe = blender if blender is not None else find_blender_executable()
    except BlenderNotFoundError as error:
        raise BlenderNotFoundError(
            "blender_resolution",
            f"Cannot convert {source}: {error}",
        ) from error

    with tempfile.TemporaryDirectory(prefix="easy_bonemap_fbx_") as tmp:
        tmp_dir = Path(tmp)
        script_path = tmp_dir / "convert_fbx.py"
        script_path.write_text(FBX_CONVERT_SCRIPT, encoding="utf-8")
        output_path = tmp_dir / "converted.glb"
        command = build_blender_command(blender_exe, script_path, source, output_path)
        try:
            result = subprocess.run(command, capture_output=True, check=False, timeout=timeout)
        except FileNotFoundError as error:
            raise FbxConversionError(
                "blender_execution",
                f"Blender executable not found while converting {source}: {blender_exe}",
            ) from error
        except subprocess.TimeoutExpired as error:
            raise FbxConversionError(
                "blender_execution",
                f"Blender timed out after {timeout:g}s converting {source} (blender: {blender_exe})",
            ) from error
        stdout = result.stdout.decode("utf-8", errors="replace")
        stderr = result.stderr.decode("utf-8", errors="replace")
        if result.returncode != 0 or FBX_CONVERT_OK not in stdout:
            detail = (stderr or stdout).strip()
            raise FbxConversionError(
                "blender_execution",
                f"Blender exit {result.returncode} while converting {source} to GLB "
                f"(blender: {blender_exe}): {detail}",
            )
        if not output_path.is_file() or output_path.stat().st_size == 0:
            raise FbxConversionError(
                "output_check",
                f"Blender produced no GLB output at {output_path} for {source} "
                f"(blender: {blender_exe})",
            )
        yield output_path


def convert_fbx_to_gltf(
    fbx_path: str | Path,
    blender: str | None = None,
    timeout: float = FBX_CONVERT_TIMEOUT,
) -> GLTF2:
    """Convert an FBX into a pygltflib GLTF2 document via Blender."""
    source = Path(fbx_path)
    if not source.is_file():
        raise FileNotFoundError(source)
    try:
        blender_exe = blender if blender is not None else find_blender_executable()
    except BlenderNotFoundError as error:
        raise BlenderNotFoundError(
            "blender_resolution",
            f"Cannot convert {source}: {error}",
        ) from error
    try:
        with convert_fbx_to_glb(source, blender=blender_exe, timeout=timeout) as glb_path:
            return GLTF2().load(str(glb_path))
    except FbxConversionError:
        raise
    except Exception as error:
        raise FbxConversionError(
            "glb_load",
            f"Failed to load converted GLB for {source} (blender: {blender_exe}): {error}",
        ) from error
