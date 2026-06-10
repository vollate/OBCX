#!/usr/bin/env sh
set -eu

IMAGE_TAG="obcx:nix-amd64"
CMAKE_BUILD_TYPE="Release"
CMAKE_BUILD_PARALLEL_LEVEL=""

usage() {
  cat <<'EOF'
Usage: scripts/build-podman-image.sh [OPTIONS]

Build the amd64 Nix-based OBCX runtime image with Podman.

Options:
  -t, --tag TAG          Image tag to build (default: obcx:nix-amd64)
      --build-type TYPE CMake build type (default: Release)
  -j, --jobs N          Pass --parallel N to cmake --build
  -h, --help            Show this help text

Examples:
  scripts/build-podman-image.sh
  scripts/build-podman-image.sh -j 2
  scripts/build-podman-image.sh -t obcx:test --build-type MinSizeRel -j 2
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    -t|--tag)
      if [ "$#" -lt 2 ]; then
        echo "error: $1 requires a tag" >&2
        exit 2
      fi
      IMAGE_TAG="$2"
      shift 2
      ;;
    --build-type)
      if [ "$#" -lt 2 ]; then
        echo "error: $1 requires a CMake build type" >&2
        exit 2
      fi
      CMAKE_BUILD_TYPE="$2"
      shift 2
      ;;
    -j|--jobs)
      if [ "$#" -lt 2 ]; then
        echo "error: $1 requires a job count" >&2
        exit 2
      fi
      CMAKE_BUILD_PARALLEL_LEVEL="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "error: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      echo "error: unexpected argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [ "$#" -gt 0 ]; then
  echo "error: unexpected argument: $1" >&2
  usage >&2
  exit 2
fi

if [ -n "${CMAKE_BUILD_PARALLEL_LEVEL}" ]; then
  podman build \
    --platform linux/amd64 \
    --security-opt seccomp=unconfined \
    --build-arg "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}" \
    --build-arg "CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL}" \
    -f packaging/podman/Containerfile \
    -t "${IMAGE_TAG}" \
    .
else
  podman build \
    --platform linux/amd64 \
    --security-opt seccomp=unconfined \
    --build-arg "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}" \
    -f packaging/podman/Containerfile \
    -t "${IMAGE_TAG}" \
    .
fi
