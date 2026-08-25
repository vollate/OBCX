# Repository Guidance

## C++ Style

- Do not use `using namespace` outside test code. Use fully qualified names,
  namespace aliases, or targeted `using` declarations instead.

## Commit Checks

- Before every commit, run `nix fmt` from the repository root. This formats the
  core repository and every repository under `local_actor/` with the root
  `.clang-format`.

## Commit message

- Allow to commit without gpg sign but **must remind user to amend them with gpg sign**
