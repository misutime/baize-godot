"""Behavior tests for FBX input support in the Normalized Skeleton Graph pipeline.

The FBX path converts an ``.fbx`` file to a temporary GLB through a headless
Blender subprocess, then feeds the existing GLTF pipeline unchanged. This
suite locks the following contracts:

* ``analyzer.fbx_converter``:
    - ``find_blender_executable``: resolution order ``EASY_BONEMAP_BLENDER``
      env var (existing file) -> ``blender`` on PATH -> macOS
      ``/Applications/Blender.app/Contents/MacOS/Blender``; raises
      ``BlenderNotFoundError`` (stage ``blender_resolution``) otherwise
    - ``build_blender_command``: exact argv shape
      ``[blender, --background, --factory-startup, --python <script>, --,
      <input.fbx>, <output.glb>]``
    - ``convert_fbx_to_glb``: context manager yielding a GLB inside a
      ``easy_bonemap_fbx_`` temporary directory; the input FBX is never
      modified and nothing is written next to it; failures are staged
      ``FbxConversionError`` (blender_execution / output_check) whose message
      carries the blender executable, the input path, and the stage
    - ``convert_fbx_to_gltf``: convenience wrapper; GLB load failures surface
      as ``FbxConversionError`` stage ``glb_load``
    - the Blender script passed via ``--python`` preserves skins and
      animations (``export_skins``/``export_animations``) and prints the
      ``FBX_CONVERT_OK`` marker
* ``analyzer.gltf_reader.load_document`` routing: ``.fbx`` (case-insensitive)
  goes to the converter, ``.glb``/``.gltf`` behavior is unchanged, other
  suffixes raise ``ValueError``
* CLI (``extract_skeleton``): an ``.fbx`` report keeps ``source`` as the
  original FBX path with format ``easy_bonemap.normalized_skeleton_graph.v1``,
  warns ``fbx_converted_via_blender``, validates the *converted* GLB (not the
  FBX), and propagates converter failures without a silent fallback

No real Blender and no external assets are needed: the subprocess tests run a
shim Blender executable that validates the converter's argv and script, then
emits a GLB with a configurable number of bones. A real-Blender smoke test is
environment-probed and skipped unless ``EASY_BONEMAP_SMOKE_FBX_DIR`` points at
a directory containing the FBX samples. No production code is modified.

Run directly:
    python tools/easy_bonemap/tests/test_fbx_support.py
or from the easy_bonemap directory:
    python -m unittest tests.test_fbx_support
"""

from __future__ import annotations

import contextlib
import io
import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

# The analyzer package lives one directory up (tools/easy_bonemap).
_ANALYZER_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _ANALYZER_ROOT not in sys.path:
    sys.path.insert(0, _ANALYZER_ROOT)

import pygltflib as gltf  # noqa: E402

import extract_skeleton  # noqa: E402  (top-level module in tools/easy_bonemap)

from analyzer import fbx_converter  # noqa: E402
from analyzer import gltf_reader  # noqa: E402
from analyzer.gltf_reader import load_document  # noqa: E402
from analyzer.skeleton_graph import extract_normalized_skeleton_graph  # noqa: E402

REPORT_FORMAT = "easy_bonemap.normalized_skeleton_graph.v1"
MACOS_BLENDER = "/Applications/Blender.app/Contents/MacOS/Blender"


# ---------------------------------------------------------------------------
# Helpers: minimal GLB generation (no buffers required) and the shim Blender
# ---------------------------------------------------------------------------


def write_chain_glb(path: str | os.PathLike, bones: int, binary: bool = True) -> Path:
    """Write a minimal GLB/GLTF whose single skin has ``bones`` chain joints."""
    nodes = []
    for i in range(bones):
        node = gltf.Node(name="Bone%d" % i, translation=[i * 0.1, 0.0, 0.0])
        if i > 0:
            nodes[i - 1].children = list(nodes[i - 1].children or []) + [i]
        nodes.append(node)
    document = gltf.GLTF2(
        asset=gltf.Asset(version="2.0", generator="easy-bonemap-tests"),
        scene=0,
        scenes=[gltf.Scene(nodes=[0])],
        nodes=nodes,
        skins=[gltf.Skin(name="FakeSkin", joints=list(range(bones)))],
    )
    target = Path(path)
    if binary:
        document.save_binary(str(target))
    else:
        document.save(str(target))
    return target


def pygltf_root() -> str:
    """Directory the shim subprocess must prepend to sys.path for pygltflib."""
    module_file = os.path.abspath(gltf.__file__)
    package_dir = os.path.dirname(module_file)
    if os.path.basename(package_dir) == "pygltflib":
        return os.path.dirname(package_dir)
    return package_dir


FAKE_BLENDER_SCRIPT = r'''#!/usr/bin/env python3
"""Shim Blender for tests: validate the converter argv/script, emit a GLB.

The real converter calls:
    blender --background --factory-startup --python <script> -- <fbx> <glb>
This shim replays the checks real Blender + the script would perform: flags
present, script content preserves skins/animations and carries the marker,
the output goes to a different directory than the input. Then it writes a GLB
with EASY_BONEMAP_TEST_FAKE_BONES bones (or simulates the failure modes
selected through EASY_BONEMAP_TEST_FAKE_*).
"""
import os
import sys


def fail(message):
    sys.stderr.write(message + "\n")
    sys.exit(1)


args = sys.argv[1:]
if "--" not in args:
    fail("FAKE_BLENDER: missing '--' separator in argv: %r" % (args,))
sep = args.index("--")
head, tail = args[:sep], args[sep + 1:]
if len(tail) != 2:
    fail("FAKE_BLENDER: expected exactly <input> <output> after '--', got %r" % (tail,))
input_path, output_path = tail
if not os.path.exists(input_path):
    fail("FAKE_BLENDER: input does not exist: %s" % input_path)

if os.environ.get("EASY_BONEMAP_TEST_FAKE_VERIFY") == "1":
    for flag in ("--background", "--factory-startup"):
        if flag not in head:
            fail("FAKE_BLENDER: missing %s in %r" % (flag, head))
    if "--python" not in head:
        fail("FAKE_BLENDER: missing --python in %r" % (head,))
    script_path = head[head.index("--python") + 1]
    if not os.path.isfile(script_path):
        fail("FAKE_BLENDER: python script not found: %s" % script_path)
    body = open(script_path, encoding="utf-8").read()
    for needle in (
        "import_scene.fbx",
        "export_scene.gltf",
        "export_skins",
        "export_animations",
        "GLB",
        "FBX_CONVERT_OK",
    ):
        if needle not in body:
            fail("FAKE_BLENDER: converter script missing %r" % needle)
    if os.path.dirname(os.path.abspath(output_path)) == os.path.dirname(
        os.path.abspath(input_path)
    ):
        fail("FAKE_BLENDER: output would be written next to input: %s" % output_path)

if os.environ.get("EASY_BONEMAP_TEST_FAKE_EXIT") == "1":
    sys.stderr.write("simulated blender failure: boom\n")
    sys.exit(1)

if os.environ.get("EASY_BONEMAP_TEST_FAKE_BAD_MARKER") == "1":
    sys.stdout.write("SOME_OTHER_OUTPUT\n")
else:
    sys.stdout.write("FBX_CONVERT_OK\n")

if os.environ.get("EASY_BONEMAP_TEST_FAKE_NO_OUTPUT") == "1":
    sys.exit(0)

if os.environ.get("EASY_BONEMAP_TEST_FAKE_BAD_GLB") == "1":
    with open(output_path, "wb") as fh:
        fh.write(b"this is not a glb at all")
    sys.exit(0)

sys.path.insert(0, os.environ["EASY_BONEMAP_TEST_PYGLTF_ROOT"])
import pygltflib as pygltf  # noqa: E402

bones = int(os.environ.get("EASY_BONEMAP_TEST_FAKE_BONES", "5"))
nodes = []
for i in range(bones):
    node = pygltf.Node(name="Bone%d" % i, translation=[i * 0.1, 0.0, 0.0])
    if i > 0:
        nodes[i - 1].children = list(nodes[i - 1].children or []) + [i]
    nodes.append(node)
document = pygltf.GLTF2(
    asset=pygltf.Asset(version="2.0", generator="fake-blender"),
    scene=0,
    scenes=[pygltf.Scene(nodes=[0])],
    nodes=nodes,
    skins=[pygltf.Skin(name="FakeSkin", joints=list(range(bones)))],
)
document.save_binary(output_path)
sys.exit(0)
'''


@contextlib.contextmanager
def fake_blender_env(bones: int = 5, **overrides: str):
    """Environment the shim Blender subprocess reads (inherited by children)."""
    env = {
        "EASY_BONEMAP_TEST_PYGLTF_ROOT": pygltf_root(),
        "EASY_BONEMAP_TEST_FAKE_BONES": str(bones),
        "EASY_BONEMAP_TEST_FAKE_VERIFY": "1",
    }
    env.update(("EASY_BONEMAP_TEST_" + key, value) for key, value in overrides.items())
    with mock.patch.dict(os.environ, env, clear=False):
        yield


# ---------------------------------------------------------------------------
# Exception contract
# ---------------------------------------------------------------------------


class FbxConversionErrorTest(unittest.TestCase):
    def test_is_runtime_error_with_stage(self) -> None:
        error = fbx_converter.FbxConversionError("glb_load", "could not load")
        self.assertIsInstance(error, RuntimeError)
        self.assertEqual(error.stage, "glb_load")
        self.assertEqual(str(error), "could not load")

    def test_blender_not_found_is_a_conversion_error(self) -> None:
        self.assertTrue(issubclass(fbx_converter.BlenderNotFoundError, fbx_converter.FbxConversionError))


# ---------------------------------------------------------------------------
# Converter command construction (pure function)
# ---------------------------------------------------------------------------


class BuildBlenderCommandTest(unittest.TestCase):
    def test_command_shape(self) -> None:
        command = fbx_converter.build_blender_command(
            "/opt/blender", Path("/tmp/convert.py"), Path("/assets/rig.fbx"), Path("/tmp/out.glb")
        )
        self.assertEqual(
            command,
            [
                "/opt/blender",
                "--background",
                "--factory-startup",
                "--python",
                "/tmp/convert.py",
                "--",
                "/assets/rig.fbx",
                "/tmp/out.glb",
            ],
        )


# ---------------------------------------------------------------------------
# Blender executable resolution order
# ---------------------------------------------------------------------------


class FindBlenderExecutableTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="ebm_blender_")
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        self.fake = root / "fake_blender"
        self._make_executable(self.fake)
        self.bin_dir = root / "bin"
        self.bin_dir.mkdir()
        self.on_path = self.bin_dir / "blender"
        self._make_executable(self.on_path)

    def tearDown(self) -> None:
        os.environ.pop("EASY_BONEMAP_BLENDER", None)

    @staticmethod
    def _make_executable(path: Path) -> None:
        path.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    @staticmethod
    def _env_without_blender_var() -> dict:
        env = dict(os.environ)
        env.pop("EASY_BONEMAP_BLENDER", None)
        env["PATH"] = "/usr/bin:/bin"
        return env

    def test_env_var_wins_over_path(self) -> None:
        env = self._env_without_blender_var()
        env["EASY_BONEMAP_BLENDER"] = str(self.fake)
        env["PATH"] = str(self.bin_dir) + os.pathsep + env["PATH"]
        with mock.patch.dict(os.environ, env, clear=True):
            # ``blender`` on PATH is a different file; the env var must win.
            self.assertEqual(fbx_converter.find_blender_executable(), str(self.fake))

    def test_env_var_missing_file_raises(self) -> None:
        missing = str(self._tmp.name + "/does_not_exist")
        with mock.patch.dict(os.environ, {"EASY_BONEMAP_BLENDER": missing}, clear=False):
            with self.assertRaises(fbx_converter.BlenderNotFoundError) as ctx:
                fbx_converter.find_blender_executable()
        self.assertEqual(ctx.exception.stage, "blender_resolution")
        self.assertIn(missing, str(ctx.exception))

    def test_path_fallback(self) -> None:
        env = self._env_without_blender_var()
        env["PATH"] = str(self.bin_dir) + os.pathsep + env["PATH"]
        with mock.patch.dict(os.environ, env, clear=True):
            self.assertEqual(fbx_converter.find_blender_executable(), str(self.on_path))

    def test_macos_app_fallback(self) -> None:
        real_is_file = pathlib.Path.is_file
        real_isfile = os.path.isfile

        def fake_is_file(self):
            if str(self) == MACOS_BLENDER:
                return True
            return real_is_file(self)

        def fake_isfile(path):
            if str(path) == MACOS_BLENDER:
                return True
            return real_isfile(path)

        env = self._env_without_blender_var()
        with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(
            pathlib.Path, "is_file", autospec=True, side_effect=fake_is_file
        ), mock.patch("os.path.isfile", side_effect=fake_isfile):
            self.assertEqual(fbx_converter.find_blender_executable(), MACOS_BLENDER)

    def test_no_blender_anywhere_raises(self) -> None:
        real_is_file = pathlib.Path.is_file

        def fake_is_file(self):
            # Force the macOS app path to read as absent on every host.
            if str(self) == MACOS_BLENDER:
                return False
            return real_is_file(self)

        env = self._env_without_blender_var()
        with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(
            pathlib.Path, "is_file", autospec=True, side_effect=fake_is_file
        ):
            with self.assertRaises(fbx_converter.BlenderNotFoundError) as ctx:
                fbx_converter.find_blender_executable()
        self.assertEqual(ctx.exception.stage, "blender_resolution")
        message = str(ctx.exception)
        self.assertIn("blender", message.lower())
        self.assertIn("EASY_BONEMAP_BLENDER", message)


# ---------------------------------------------------------------------------
# load_document extension routing
# ---------------------------------------------------------------------------


class ExtensionRoutingTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="ebm_route_")
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)

    def test_glb_unchanged_and_converter_not_invoked(self) -> None:
        glb_path = write_chain_glb(self.root / "model.glb", bones=3)
        with mock.patch.object(gltf_reader, "convert_fbx_to_gltf") as convert:
            document = load_document(glb_path)
        convert.assert_not_called()
        self.assertEqual(document.asset.version, "2.0")
        report = extract_normalized_skeleton_graph(document, source=str(glb_path))
        self.assertEqual(report["bone_count"], 3)

    def test_gltf_unchanged_and_converter_not_invoked(self) -> None:
        gltf_path = write_chain_glb(self.root / "model.gltf", bones=2, binary=False)
        with mock.patch.object(gltf_reader, "convert_fbx_to_gltf") as convert:
            document = load_document(gltf_path)
        convert.assert_not_called()
        self.assertEqual(len(document.skins), 1)
        self.assertEqual(list(document.skins[0].joints), [0, 1])

    def test_fbx_routes_to_converter(self) -> None:
        fbx = self.root / "model.fbx"
        fbx.write_bytes(b"fake fbx payload")
        expected = gltf.GLTF2(asset=gltf.Asset(version="2.0"))
        with mock.patch.object(gltf_reader, "convert_fbx_to_gltf", return_value=expected) as convert:
            document = load_document(fbx)
        convert.assert_called_once_with(fbx)
        self.assertIs(document, expected)

    def test_fbx_uppercase_extension_routes(self) -> None:
        fbx = self.root / "MODEL.FBX"
        fbx.write_bytes(b"fake fbx payload")
        expected = gltf.GLTF2(asset=gltf.Asset(version="2.0"))
        with mock.patch.object(gltf_reader, "convert_fbx_to_gltf", return_value=expected) as convert:
            load_document(fbx)
        convert.assert_called_once_with(fbx)

    def test_unsupported_extension_raises_valueerror(self) -> None:
        other = self.root / "model.dae"
        other.write_bytes(b"x")
        with self.assertRaises(ValueError) as ctx:
            load_document(other)
        message = str(ctx.exception)
        self.assertIn(".dae", message)
        self.assertIn("fbx", message.lower())


# ---------------------------------------------------------------------------
# convert_fbx_to_glb against the shim Blender
# ---------------------------------------------------------------------------


class FakeBlenderConversionTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="ebm_convert_")
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        self.fbx_path = root / "model.fbx"
        self.fbx_path.write_bytes(b"FAKE FBX PAYLOAD")
        self.fbx_before = self.fbx_path.read_bytes()
        self.blender = root / "fake_blender"
        self.blender.write_text(FAKE_BLENDER_SCRIPT, encoding="utf-8")
        self.blender.chmod(self.blender.stat().st_mode | stat.S_IXUSR)

    def convert(self, **kwargs):
        return fbx_converter.convert_fbx_to_glb(self.fbx_path, blender=str(self.blender), **kwargs)

    def test_yields_temp_glb_and_keeps_input_untouched(self) -> None:
        with fake_blender_env(bones=5), self.convert() as glb_path:
            glb = Path(glb_path)
            self.assertTrue(glb.is_file())
            self.assertEqual(glb.suffix.lower(), ".glb")
            self.assertTrue(glb.parent.name.startswith("easy_bonemap_fbx_"))
            self.assertNotEqual(glb.parent, self.fbx_path.parent)
            # Nothing may be written next to the input FBX, and the input
            # itself must be byte-identical after conversion.
            self.assertFalse((self.fbx_path.parent / "model.glb").exists())
            self.assertEqual(self.fbx_path.read_bytes(), self.fbx_before)

    def test_converted_glb_extracts_expected_bones(self) -> None:
        with fake_blender_env(bones=7), self.convert() as glb_path:
            document = gltf.GLTF2().load(glb_path)
        self.assertEqual(len(document.skins), 1)
        self.assertEqual(list(document.skins[0].joints), list(range(7)))
        report = extract_normalized_skeleton_graph(document, source=str(self.fbx_path))
        self.assertEqual(report["bone_count"], 7)

    def test_temp_dir_is_removed_after_with_block(self) -> None:
        with fake_blender_env(), self.convert() as glb_path:
            glb = Path(glb_path)
            self.assertTrue(glb.is_file())
            parent = glb.parent
        self.assertFalse(glb.exists())
        self.assertFalse(parent.exists())

    def test_missing_input_raises_filenotfound(self) -> None:
        missing = self._tmp.name + "/missing.fbx"
        with self.assertRaises(FileNotFoundError) as ctx:
            with fbx_converter.convert_fbx_to_glb(missing, blender=str(self.blender)):
                pass
        self.assertIn("missing.fbx", str(ctx.exception))

    def test_missing_blender_raises_blender_execution(self) -> None:
        with fake_blender_env(), self.assertRaises(fbx_converter.FbxConversionError) as ctx:
            with fbx_converter.convert_fbx_to_glb(self.fbx_path, blender="/nonexistent/blender"):
                pass
        self.assertEqual(ctx.exception.stage, "blender_execution")
        message = str(ctx.exception)
        self.assertIn("/nonexistent/blender", message)
        self.assertIn(str(self.fbx_path), message)

    def test_nonzero_exit_raises_with_stderr_and_paths(self) -> None:
        with fake_blender_env(FAKE_EXIT="1"), self.assertRaises(fbx_converter.FbxConversionError) as ctx:
            with self.convert():
                pass
        self.assertEqual(ctx.exception.stage, "blender_execution")
        message = str(ctx.exception)
        self.assertIn("boom", message)  # subprocess stderr surfaces
        self.assertIn(str(self.blender), message)
        self.assertIn(str(self.fbx_path), message)

    def test_missing_marker_raises(self) -> None:
        with fake_blender_env(FAKE_BAD_MARKER="1"), self.assertRaises(fbx_converter.FbxConversionError) as ctx:
            with self.convert():
                pass
        self.assertEqual(ctx.exception.stage, "blender_execution")
        # The unexpected stdout (marker missing) must surface in the message.
        self.assertIn("SOME_OTHER_OUTPUT", str(ctx.exception))
        self.assertIn(str(self.fbx_path), str(ctx.exception))

    def test_missing_output_raises_output_check(self) -> None:
        with fake_blender_env(FAKE_NO_OUTPUT="1"), self.assertRaises(fbx_converter.FbxConversionError) as ctx:
            with self.convert():
                pass
        self.assertEqual(ctx.exception.stage, "output_check")
        message = str(ctx.exception)
        self.assertIn(".glb", message)
        self.assertIn(str(self.fbx_path), message)

    def test_subprocess_invocation_shape_and_timeout(self) -> None:
        calls = []

        def fake_run(args, **kwargs):
            calls.append((args, kwargs))
            output = args[args.index("--") + 2]
            Path(output).write_bytes(b"placeholder glb")
            return subprocess.CompletedProcess(
                args, 0, stdout=(fbx_converter.FBX_CONVERT_OK + "\n").encode("utf-8"), stderr=b""
            )

        with mock.patch.object(fbx_converter.subprocess, "run", side_effect=fake_run):
            with self.convert(timeout=42.0) as glb_path:
                self.assertTrue(Path(glb_path).is_file())
                # The script lives in the temporary directory, so capture it
                # before the ``with`` block tears the directory down.
                script_body = Path(calls[0][0][calls[0][0].index("--python") + 1]).read_text(encoding="utf-8")

        args, kwargs = calls[0]
        self.assertEqual(kwargs["timeout"], 42.0)
        self.assertEqual(args[0], str(self.blender))
        self.assertIn("--background", args)
        self.assertIn("--factory-startup", args)
        self.assertEqual(args[args.index("--python") + 1], args[args.index("--") - 1])
        self.assertIn(fbx_converter.FBX_CONVERT_OK, script_body)  # marker placeholder replaced
        self.assertNotIn("__FBX_CONVERT_OK__", script_body)
        self.assertEqual(args[args.index("--") + 1], str(self.fbx_path))
        self.assertEqual(Path(args[args.index("--") + 2]).suffix.lower(), ".glb")

    def test_default_timeout_is_module_constant(self) -> None:
        self.assertEqual(fbx_converter.FBX_CONVERT_TIMEOUT, 300.0)
        calls = []

        def fake_run(args, **kwargs):
            calls.append(kwargs)
            output = args[args.index("--") + 2]
            Path(output).write_bytes(b"placeholder glb")
            return subprocess.CompletedProcess(
                args, 0, stdout=(fbx_converter.FBX_CONVERT_OK + "\n").encode("utf-8"), stderr=b""
            )

        with mock.patch.object(fbx_converter.subprocess, "run", side_effect=fake_run):
            with self.convert():
                pass
        self.assertEqual(calls[0]["timeout"], fbx_converter.FBX_CONVERT_TIMEOUT)

    def test_script_constant_preserves_skins_and_animations(self) -> None:
        script = fbx_converter.FBX_CONVERT_SCRIPT
        # The placeholder is replaced at module level, so the shipped script
        # already carries the marker the parent process waits for.
        for needle in (
            "import_scene.fbx",
            "export_scene.gltf",
            "export_skins",
            "export_animations",
            "GLB",
            fbx_converter.FBX_CONVERT_OK,
        ):
            self.assertIn(needle, script)


# ---------------------------------------------------------------------------
# convert_fbx_to_gltf convenience wrapper
# ---------------------------------------------------------------------------


class ConvertFbxToGltfTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="ebm_gltf_")
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        self.fbx_path = root / "model.fbx"
        self.fbx_path.write_bytes(b"FAKE FBX PAYLOAD")
        self.blender = root / "fake_blender"
        self.blender.write_text(FAKE_BLENDER_SCRIPT, encoding="utf-8")
        self.blender.chmod(self.blender.stat().st_mode | stat.S_IXUSR)

    def test_returns_loaded_document(self) -> None:
        with fake_blender_env(bones=4):
            document = fbx_converter.convert_fbx_to_gltf(self.fbx_path, blender=str(self.blender))
        self.assertEqual(len(document.skins), 1)
        self.assertEqual(list(document.skins[0].joints), list(range(4)))

    def test_bad_glb_wrapped_as_glb_load(self) -> None:
        with fake_blender_env(FAKE_BAD_GLB="1"):
            with self.assertRaises(fbx_converter.FbxConversionError) as ctx:
                fbx_converter.convert_fbx_to_gltf(self.fbx_path, blender=str(self.blender))
        self.assertEqual(ctx.exception.stage, "glb_load")
        message = str(ctx.exception)
        self.assertIn("Failed to load converted GLB", message)
        self.assertIn(str(self.fbx_path), message)
        self.assertIn(str(self.blender), message)


# ---------------------------------------------------------------------------
# CLI: source preservation, validation target, failure propagation
# ---------------------------------------------------------------------------


class CliFbxTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="ebm_cli_")
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        self.root = root
        self.fbx_path = root / "character.fbx"
        self.fbx_path.write_bytes(b"FAKE FBX PAYLOAD")
        self.out_json = root / "out.json"
        self.blender = root / "fake_blender"
        self.blender.write_text(FAKE_BLENDER_SCRIPT, encoding="utf-8")
        self.blender.chmod(self.blender.stat().st_mode | stat.S_IXUSR)

    def run_cli(self, *extra: str) -> tuple[int, str]:
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            code = extract_skeleton.main([str(self.fbx_path), "-o", str(self.out_json), *extra])
        return code, captured.getvalue()

    def test_fbx_end_to_end_report_contract(self) -> None:
        with fake_blender_env(bones=6), mock.patch.dict(
            os.environ, {"EASY_BONEMAP_BLENDER": str(self.blender)}, clear=False
        ):
            code, _ = self.run_cli("--skip-validator")
        self.assertEqual(code, 0)
        report = json.loads(self.out_json.read_text(encoding="utf-8"))
        self.assertEqual(report["format"], REPORT_FORMAT)
        self.assertEqual(report["source"], str(self.fbx_path))
        self.assertEqual(report["bone_count"], 6)
        self.assertIn("fbx_converted_via_blender", report["warnings"])
        self.assertIn("validator_skipped", report["warnings"])

    def test_validation_runs_on_converted_glb_not_fbx(self) -> None:
        glb_path = write_chain_glb(self.root / "converted.glb", bones=2)
        validated = []

        @contextlib.contextmanager
        def fake_convert(fbx_path, blender=None, timeout=fbx_converter.FBX_CONVERT_TIMEOUT):
            yield glb_path

        with mock.patch.object(extract_skeleton, "convert_fbx_to_glb", side_effect=fake_convert), mock.patch.object(
            extract_skeleton, "validate_asset", side_effect=lambda path: validated.append(str(path)) or {
                "issues": {"numErrors": 0}
            }
        ):
            code, _ = self.run_cli()
        self.assertEqual(code, 0)
        self.assertEqual(validated, [str(glb_path)])
        report = json.loads(self.out_json.read_text(encoding="utf-8"))
        self.assertEqual(report["source"], str(self.fbx_path))
        self.assertEqual(report["format"], REPORT_FORMAT)
        self.assertIn("fbx_converted_via_blender", report["warnings"])
        self.assertNotIn("validator_skipped", report["warnings"])

    def test_validator_failure_propagates(self) -> None:
        with mock.patch.object(
            extract_skeleton,
            "convert_fbx_to_glb",
            side_effect=lambda p, blender=None, timeout=300.0: contextlib.nullcontext(self.root / "converted.glb"),
        ), mock.patch.object(extract_skeleton, "validate_asset", side_effect=RuntimeError("validator died")):
            with self.assertRaises(RuntimeError) as ctx:
                self.run_cli()
        self.assertIn("validator died", str(ctx.exception))

    def test_converter_failure_propagates_with_fbx_path(self) -> None:
        def boom(fbx_path, blender=None, timeout=fbx_converter.FBX_CONVERT_TIMEOUT):
            raise fbx_converter.FbxConversionError(
                "blender_execution", "Blender conversion failed for %s (stage blender_execution)" % fbx_path
            )

        with mock.patch.object(extract_skeleton, "convert_fbx_to_glb", side_effect=boom):
            with self.assertRaises(fbx_converter.FbxConversionError) as ctx:
                self.run_cli()
        self.assertEqual(ctx.exception.stage, "blender_execution")
        self.assertIn(str(self.fbx_path), str(ctx.exception))
        self.assertFalse(self.out_json.exists(), "no report may be written after a failed conversion")

    def test_missing_input_propagates(self) -> None:
        missing = self.root / "missing.fbx"
        with self.assertRaises(FileNotFoundError):
            extract_skeleton.main([str(missing), "-o", str(self.out_json), "--skip-validator"])

    def test_glb_cli_regression(self) -> None:
        glb_path = write_chain_glb(self.root / "plain.glb", bones=4)
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            code = extract_skeleton.main([str(glb_path), "-o", str(self.out_json), "--skip-validator"])
        self.assertEqual(code, 0)
        report = json.loads(self.out_json.read_text(encoding="utf-8"))
        self.assertEqual(report["format"], REPORT_FORMAT)
        self.assertEqual(report["source"], str(glb_path))
        self.assertEqual(report["bone_count"], 4)
        self.assertNotIn("fbx_converted_via_blender", report["warnings"])


# ---------------------------------------------------------------------------
# Real-Blender smoke test (environment-probed, skipped unless assets present)
# ---------------------------------------------------------------------------

SMOKE_FBX_DIR = os.environ.get("EASY_BONEMAP_SMOKE_FBX_DIR")
SMOKE_ASSETS = (("Running.fbx", 99), ("Robot.FBX", 41))


def _smoke_blender() -> str | None:
    try:
        return fbx_converter.find_blender_executable()
    except fbx_converter.BlenderNotFoundError:
        return None


_SMOKE_BLENDER = _smoke_blender()


class RealBlenderSmokeTest(unittest.TestCase):
    """Optional end-to-end check against a real headless Blender.

    Skipped unless ``EASY_BONEMAP_SMOKE_FBX_DIR`` points at a directory
    containing the FBX samples and Blender is resolvable. The samples live
    outside the repository; their location comes from the environment, never
    hardcoded. Expected bone counts come from the Blender 5.1 import
    experiment: Running.fbx -> 99 bones, Robot.FBX -> 41 bones.
    """

    _REASON = (
        "real-Blender smoke test requires EASY_BONEMAP_SMOKE_FBX_DIR with Running.fbx/Robot.FBX "
        "and a resolvable Blender (EASY_BONEMAP_BLENDER, PATH, or macOS app)"
    )

    @classmethod
    def setUpClass(cls) -> None:
        if not SMOKE_FBX_DIR or _SMOKE_BLENDER is None:
            raise unittest.SkipTest(cls._REASON)
        missing = [name for name, _ in SMOKE_ASSETS if not (Path(SMOKE_FBX_DIR) / name).is_file()]
        if missing:
            raise unittest.SkipTest(cls._REASON + "; missing: %s" % ", ".join(missing))
        cls.blender = _SMOKE_BLENDER

    def _converted(self, name: str) -> gltf.GLTF2:
        fbx = Path(SMOKE_FBX_DIR) / name
        before = fbx.read_bytes()
        document = fbx_converter.convert_fbx_to_gltf(fbx, blender=self.blender)
        self.assertEqual(fbx.read_bytes(), before, "input FBX must not be modified")
        return document

    def test_running_fbx_bone_count(self) -> None:
        report = extract_normalized_skeleton_graph(self._converted("Running.fbx"), source="Running.fbx")
        self.assertEqual(report["bone_count"], 99)

    def test_robot_fbx_bone_count(self) -> None:
        report = extract_normalized_skeleton_graph(self._converted("Robot.FBX"), source="Robot.FBX")
        self.assertEqual(report["bone_count"], 41)

    def test_conversion_preserves_skins_and_animations(self) -> None:
        document = self._converted("Running.fbx")
        self.assertGreaterEqual(len(document.skins), 1)
        self.assertGreaterEqual(len(document.animations), 1)


if __name__ == "__main__":
    unittest.main()
