{
  description = "C++ development environment for OBCX";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      pkgsFor = system: import nixpkgs { inherit system; };
    in
    {
      formatter = forAllSystems (
        system:
        let
          pkgs = pkgsFor system;
        in
        pkgs.writeShellApplication {
          name = "treefmt";
          runtimeInputs = [
            pkgs.clang-tools
            pkgs.treefmt
          ];
          text = ''
            exec ${pkgs.treefmt}/bin/treefmt "$@"
          '';
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = pkgsFor system;
          stdenv = pkgs.gcc16Stdenv;

          obcxDependencies =
            (with pkgs; [
              boost
              brotli
              fmt
              zlib
              gtest
              nlohmann_json
              openspec
              openssl
              spdlog
              sqlite
              tomlplusplus
              ftxui
              libxml2
              re2
              zstd
              liburing
              stdenv.cc.cc.lib
            ]);
        in
        {
          default = pkgs.mkShell.override { inherit stdenv; } {
            nativeBuildInputs =
              (with pkgs; [
                cmake
                ninja
                git
                gcc16
                binutils
                pkg-config
                cmake-format
                clang-tools
                doxygen
                ffmpeg
                perf
                treefmt
              ]);

            buildInputs = obcxDependencies;

            shellHook = ''
              export CC=gcc
              export CXX=g++
            '';
          };
        }
      );
    };
}
