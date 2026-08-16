#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
actor_root=${OBCX_ACTOR_SOURCE_ROOT:-local_actor}

apply_actor_patch() {
  actor=$1

  case "$actor" in
    bridge)
      repository=obcx-actor-bridge
      base_revision=de8c3046c218c9e2a254abe832e91595f4cc629a
      patched_revision=de8c3046c218c9e2a254abe832e91595f4cc629a
      ;;
    message-store)
      repository=obcx-actor-message-store
      base_revision=3a9dfc2b27375d22531b4308356b75f4bac7077f
      patched_revision=d3511ae5950a0e6454458eb763b9947165397d2a
      ;;
    registry)
      repository=obcx-actor-registry
      base_revision=057b46522872bfbc2dd87435e3751b8d2001e26b
      patched_revision=ff8a4fecdabd91b2b5e930c39454389bb72109eb
      ;;
    template)
      repository=obcx-actor-template
      base_revision=993048e6d7e280167cc2189a51464d8fd9197c68
      patched_revision=4bc3c5558a6864d9a067c486a978f943b90cb1f6
      ;;
    *)
      printf 'unknown actor patch: %s\n' "$actor" >&2
      return 2
      ;;
  esac

  checkout=$actor_root/$repository
  actual_revision=$(git -C "$checkout" rev-parse HEAD)
  if [ "$actual_revision" = "$patched_revision" ]; then
    test -z "$(git -C "$checkout" status --porcelain)"
    return
  fi
  if [ "$actual_revision" != "$base_revision" ]; then
    printf 'actor %s is at %s; expected base %s\n' \
      "$actor" "$actual_revision" "$base_revision" >&2
    return 1
  fi
  test -z "$(git -C "$checkout" status --porcelain)"

  GIT_COMMITTER_NAME=Vollate \
  GIT_COMMITTER_EMAIL=uint44t@gmail.com \
    git -C "$checkout" -c commit.gpgSign=false am \
      --committer-date-is-author-date "$script_dir/patches/$actor.patch"

  actual_revision=$(git -C "$checkout" rev-parse HEAD)
  if [ "$actual_revision" != "$patched_revision" ]; then
    printf 'actor %s patch produced %s; expected %s\n' \
      "$actor" "$actual_revision" "$patched_revision" >&2
    return 1
  fi
  test -z "$(git -C "$checkout" status --porcelain)"
}

if [ "$#" -eq 0 ]; then
  set -- bridge message-store registry template
fi

for actor in "$@"; do
  apply_actor_patch "$actor"
done
