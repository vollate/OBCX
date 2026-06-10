{
  description = "C++ development environment for OBCX";

  # Use the nixos-unstable channel for up-to-date packages,
  # or change to "github:NixOS/nixpkgs/nixos-23.11" for stable.
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      # Define the systems you want to support
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
        "x86_64-darwin"
      ];

      # Helper function to generate attributes for each system
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          inherit (pkgs) lib stdenv;

          # Linux-only deps. liburing is the io_uring backend (kernel API,
          # Linux-only). stdenv.cc.cc.lib carries the gcc runtime needed by
          # libstdc++ users on Linux. Both must be omitted on Darwin.
          linuxOnlyDeps = lib.optionals stdenv.isLinux (
            with pkgs;
            [
              liburing
              stdenv.cc.cc.lib
            ]
          );

          obcxDependencies =
            (with pkgs; [
              boost
              brotli
              fmt
              zlib
              gtest
              nlohmann_json
              openssl
              spdlog
              sqlite
              tomlplusplus
              ftxui
              libxml2
              re2
              zstd
            ])
            ++ linuxOnlyDeps;
        in
        {
          default = pkgs.mkShell.override { stdenv = pkgs.clangStdenv; } {
            nativeBuildInputs =
              (with pkgs; [
                clang-tools
                cmake
                ninja
                llvmPackages.bintools
                pkg-config
                cmake-format
                ffmpeg
              ])
              ++ lib.optionals stdenv.isLinux (
                with pkgs;
                [
                  # `perf` is part of the Linux kernel tooling; not available on
                  # Darwin.
                  perf
                ]
              );

            buildInputs = obcxDependencies;

            shellHook = "";
          };
        }
      );
    };
}
