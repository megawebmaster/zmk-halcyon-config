#!/usr/bin/env bash
# Local mirror of zmkfirmware/zmk/.github/workflows/build-user-config.yml.
#
# The west workspace at /west is created when the image is built; this script only
# re-syncs the config and compiles every entry of build.yaml, writing artifacts to
# /workspace/firmware.
#
# Usage (inside compose): docker compose run --rm build [filter]
#   filter - optional substring; only matrix rows whose "<shield> <board>" contains it
#            are built.

set -uo pipefail

WORKSPACE=/workspace
BASE_DIR=/west
CONFIG_DIR="${BASE_DIR}/config"
BUILD_ROOT="${BASE_DIR}/build"
OUT_DIR="${WORKSPACE}/firmware"
MATRIX_FILE="${WORKSPACE}/build.yaml"
FALLBACK_BINARY=bin
FILTER="${1:-}"

if [ ! -d "${BASE_DIR}/.west" ]; then
  echo "error: no west workspace at ${BASE_DIR}. Rebuild the image: docker compose build" >&2
  exit 1
fi

if [ ! -f "${MATRIX_FILE}" ]; then
  echo "error: ${MATRIX_FILE} not found. Is the repository mounted at ${WORKSPACE}?" >&2
  exit 1
fi

# CI copies the config into a fresh directory for every job. Do the same on every run so
# keymap and .conf edits on the host always reach the build. west.yml is kept because the
# manifest path points at it (the copy below restores an identical one anyway).
find "${CONFIG_DIR}" -mindepth 1 ! -name west.yml -delete
cp -R "${WORKSPACE}"/config/* "${CONFIG_DIR}/"

mkdir -p "${BUILD_ROOT}" "${OUT_DIR}"

# Emit one tab-separated record per matrix entry.
read_matrix() {
  python3 - "${MATRIX_FILE}" <<'PY'
import sys, yaml

with open(sys.argv[1]) as f:
    doc = yaml.safe_load(f) or {}

for entry in doc.get("include") or []:
    fields = [
        str(entry.get(key) or "")
        for key in ("board", "shield", "cmake-args", "snippet", "artifact-name")
    ]
    print("\t".join(field.replace("\t", " ") for field in fields))
PY
}

matrix=$(read_matrix) || { echo "error: failed to parse ${MATRIX_FILE}" >&2; exit 1; }

if [ -z "${matrix}" ]; then
  echo "error: empty build matrix in ${MATRIX_FILE}" >&2
  exit 1
fi

built=()
failed=()
skipped=0

while IFS=$'\t' read -r board shield cmake_args snippet artifact_name; do
  [ -n "${board}" ] || continue

  display_name="${shield:+$shield - }${board}"

  if [ -n "${FILTER}" ] && [[ "${shield} ${board}" != *"${FILTER}"* ]]; then
    skipped=$((skipped + 1))
    continue
  fi

  # Same naming as CI: note ${board//\//_} turns halcyon_wireless//zmk into
  # halcyon_wireless__zmk.
  name="${artifact_name:-${shield:+$shield-}${board//\//_}-zmk}"

  # Stable per-row build directory keyed on everything that affects the CMake cache, so
  # rebuilds are incremental but a stale cache is impossible.
  slug=$(printf '%s\0%s\0%s\0%s' "${board}" "${shield}" "${snippet}" "${cmake_args}" \
         | sha256sum | cut -c1-16)
  build_dir="${BUILD_ROOT}/${slug}"

  west_args=(build -s zmk/app -d "${build_dir}" -b "${board}")
  [ -n "${snippet}" ] && west_args+=(-S "${snippet}")

  cmake_extra=(-DZMK_CONFIG="${CONFIG_DIR}" -DZMK_EXTRA_MODULES="${WORKSPACE}")
  [ -n "${shield}" ] && cmake_extra+=(-DSHIELD="${shield}")
  if [ -n "${cmake_args}" ]; then
    # cmake-args is a single string of several flags; word-splitting is intended here.
    read -ra split_cmake_args <<<"${cmake_args}"
    cmake_extra+=("${split_cmake_args[@]}")
  fi

  echo
  echo "=== Building ${display_name} ==="
  if (cd "${BASE_DIR}" && west "${west_args[@]}" -- "${cmake_extra[@]}"); then
    if [ -f "${build_dir}/zephyr/zmk.uf2" ]; then
      cp "${build_dir}/zephyr/zmk.uf2" "${OUT_DIR}/${name}.uf2"
      built+=("${name}.uf2")
    elif [ -f "${build_dir}/zephyr/zmk.${FALLBACK_BINARY}" ]; then
      cp "${build_dir}/zephyr/zmk.${FALLBACK_BINARY}" "${OUT_DIR}/${name}.${FALLBACK_BINARY}"
      built+=("${name}.${FALLBACK_BINARY}")
    else
      echo "error: no firmware produced for ${display_name}" >&2
      failed+=("${display_name}")
    fi
  else
    # CI runs with fail-fast: false, so keep going and report at the end.
    echo "error: build failed for ${display_name}" >&2
    failed+=("${display_name}")
  fi
done <<<"${matrix}"

# The bind mount is created as root, so hand the whole directory back to the host user.
chown -R "${HOST_UID:-1000}:${HOST_GID:-1000}" "${OUT_DIR}" 2>/dev/null || true

echo
echo "=== Summary ==="
for artifact in "${built[@]:-}"; do
  [ -n "${artifact}" ] && echo "  ok      ${artifact}"
done
for entry in "${failed[@]:-}"; do
  [ -n "${entry}" ] && echo "  FAILED  ${entry}"
done
[ "${skipped}" -gt 0 ] && echo "  (${skipped} row(s) skipped by filter '${FILTER}')"

if [ "${#failed[@]}" -gt 0 ]; then
  exit 1
fi

if [ "${#built[@]}" -eq 0 ]; then
  echo "error: nothing was built" >&2
  exit 1
fi

echo "Artifacts in ./firmware"
