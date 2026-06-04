#!/usr/bin/env bash
set -euo pipefail

PRESET="dev"
JOBS="10"
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
	case "$1" in
		--preset|-p)
			PRESET="${2:-}"
			shift 2
			;;
		--jobs|-j)
			JOBS="${2:-}"
			shift 2
			;;
		--)
			shift
			EXTRA_ARGS+=("$@")
			break
			;;
		*)
			EXTRA_ARGS+=("$1")
			shift
			;;
	esac
done

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"

cd "$REPO_ROOT"

case "$PRESET" in
	dev)
		MONO_EDITOR="bin/godot.macos.editor.dev.arm64.mono"
		;;
	pro)
		MONO_EDITOR="bin/godot.macos.editor.arm64.mono"
		;;
	*)
		echo "Unknown preset '$PRESET'. Available presets: dev, pro" >&2
		exit 2
		;;
esac

echo "Step 1/4: build macOS C# editor binary"
BUILD_ARGS=("$SCRIPT_DIR/build-macos.sh" --preset "$PRESET" --jobs "$JOBS" module_mono_enabled=yes generate_bundle=no)
if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
	BUILD_ARGS+=("${EXTRA_ARGS[@]}")
fi
bash "${BUILD_ARGS[@]}"

if [[ ! -x "$MONO_EDITOR" ]]; then
	if [[ "$PRESET" == "dev" ]]; then
		MONO_CANDIDATE="$(find bin -maxdepth 1 -type f -name "godot.macos.editor.dev*.mono" -perm -111 | sort | head -n 1)"
	else
		MONO_CANDIDATE="$(find bin -maxdepth 1 -type f -name "godot.macos.editor*.mono" ! -name "godot.macos.editor.dev*.mono" -perm -111 | sort | head -n 1)"
	fi
	if [[ -n "$MONO_CANDIDATE" ]]; then
		MONO_EDITOR="$MONO_CANDIDATE"
	else
		echo "Cannot find macOS C# editor binary in bin/." >&2
		echo "Expected something like: $MONO_EDITOR" >&2
		exit 1
	fi
fi

echo "Step 2/4: generate C# glue with $MONO_EDITOR"
"./$MONO_EDITOR" --headless --generate-mono-glue modules/mono/glue

echo "Step 3/4: build GodotSharp assemblies"
./modules/mono/build_scripts/build_assemblies.py --godot-output-dir ./bin --godot-platform=macos

echo "Step 4/4: generate macOS app bundle"
bash "$SCRIPT_DIR/build-macos.sh" --preset "$PRESET" --jobs "$JOBS" module_mono_enabled=yes generate_bundle=yes

echo "macOS C# editor build finished."
