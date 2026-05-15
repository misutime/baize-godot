#!/usr/bin/env bash
set -euo pipefail

PRESET="dev"
JOBS="8"
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

case "$PRESET" in
	dev)
		PROFILE_PATH="misc/customization/scons-profiles/macos_3d_dev.py"
		;;
	*)
		echo "Unknown preset '$PRESET'. Available presets: dev" >&2
		exit 2
		;;
esac

cd "$REPO_ROOT"

ARGS=("profile=$PROFILE_PATH" "-j$JOBS")
if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
	ARGS+=("${EXTRA_ARGS[@]}")
fi
echo "Running preset '$PRESET': ${ARGS[*]}"

if command -v scons >/dev/null 2>&1; then
	exec scons "${ARGS[@]}"
elif command -v python3 >/dev/null 2>&1; then
	exec python3 -m SCons.Script "${ARGS[@]}"
else
	exec python -m SCons.Script "${ARGS[@]}"
fi
