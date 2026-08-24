{
  description = "an ANF simplification and solving tool";
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixpkgs-unstable";
    cryptominisat = {
      url = "github:msoos/cryptominisat";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };
  outputs =
    {
      self,
      nixpkgs,
      cryptominisat,
    }:
    let
      inherit (nixpkgs) lib;
      systems = lib.intersectLists lib.systems.flakeExposed lib.platforms.linux;
      forAllSystems = lib.genAttrs systems;
      nixpkgsFor = forAllSystems (system: nixpkgs.legacyPackages.${system});
      fs = lib.fileset;

      bosphorus-package =
        {
          stdenv,
          cmake,
          pkg-config,
          boost,
          zlib,
          gmp,
          m4ri,
          brial,
          cryptominisat,
        }:
        stdenv.mkDerivation {
          name = "bosphorus";
          src = fs.toSource {
            root = ./.;
            fileset = fs.unions [
              ./src
              ./cmake
              ./CMakeLists.txt
              ./bosphorusConfig.cmake.in
            ];
          };

          nativeBuildInputs = [
            cmake
            pkg-config
          ];
          buildInputs = [
            boost
            zlib
            gmp
            m4ri
            brial
            cryptominisat
          ];
          cmakeFlags = [
            "-Dcryptominisat5_DIR=${cryptominisat}"
          ];
        };
    in
    {
      packages = forAllSystems (
        system:
        let
          bosphorus = nixpkgsFor.${system}.callPackage bosphorus-package {
            cryptominisat = cryptominisat.packages.${system}.default;
          };
        in
        {
          inherit bosphorus;
          default = bosphorus;
        }
      );
    };
}
