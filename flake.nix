{
  description = "The Tess Language compiler";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-25.05";
  };

  outputs = { self, nixpkgs, ... }:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];

      forAllSystems = function:
        nixpkgs.lib.genAttrs supportedSystems (system: function {
          inherit system;
          pkgs = import nixpkgs { inherit system; };
        });

      mkPackage = { pkgs, config ? "release" }:
        pkgs.stdenv.mkDerivation {
          pname = "tess";
          version = "0.1.0";

          src = ./.;
          nativeBuildInputs = with pkgs; [ gnumake ];

          # Used by tess Makefile
          CONFIG = config;

          # not autotools
          dontConfigure = true;

          buildPhase = ''
            runHook preBuild
            make -j all
            runHook postBuild
          '';

          checkPhase = ''
            runHook preCheck
            make -j test
            runHook postCheck
          '';

          doCheck = true;

          installPhase = ''
            runHook preInstall
            make install DESTDIR=$out PREFIX=
            runHook postInstall
          '';

          meta = with pkgs.lib; {
            description = "The Tess Language compiler";
            platforms = platforms.all;
            # TODO other meta fields
            # license = licenses.mit
          };
        };
    in
      {
        packages = forAllSystems({ pkgs, system }: {
          default = mkPackage { inherit pkgs; config = "release"; };
          release = mkPackage { inherit pkgs; config = "release"; };
          debug   = mkPackage { inherit pkgs; config = "debug"; };
          asan    = mkPackage { inherit pkgs; config = "asan"; };
        });

        overlays.default = final: prev: { tess = mkPackage { pkgs = final; }; };

        devShells = forAllSystems ({ pkgs, system }:
          let
            isDarwin = pkgs.stdenv.isDarwin;
          in
            {
              default = pkgs.mkShellNoCC {
                inputsFrom = [ self.packages.${system}.default ];

                # Seems necessary on my MacBook to make lldb work.
                shellHook = ''
                  ${if isDarwin then ''
                    export LLDB_DEBUGSERVER_PATH=/Applications/Xcode.app/Contents/SharedFrameworks/LLDB.framework/Versions/A/Resources/debugserver
                    ''
                    else ""}
                  '';

                packages = with pkgs; [
                  bashInteractive
                  cmake
                  tree

                ] ++ (with pkgs.llvmPackages_20; [

                  clangUseLLVM
                  clang-tools
                  lldb
                  bintools      # required for lto

                ]) ++ (pkgs.lib.optionals (!pkgs.stdenv.isDarwin) [

                  # gcc
                  gcc15

                  # some perf things
                  linuxKernel.packages.linux_6_6.perf
                  poop

                ]) ++ (pkgs.lib.optionals (pkgs.stdenv.isDarwin) [
                  # I don't know why this is necessary
                  git
                ]) ;
              };
            });
      };
}
