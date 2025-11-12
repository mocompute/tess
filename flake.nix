{
  description = "Environment for Tess language development";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-25.05";
  };

  outputs = { nixpkgs, ... }:
    let
      overlays = [];

      forAllSystems = function:
        nixpkgs.lib.genAttrs [
          "x86_64-linux"
          "aarch64-linux"
          "aarch64-darwin"
        ] (system: function (
          import nixpkgs {
            inherit system;
            inherit overlays;
          }));
    in
      {
        devShells = forAllSystems (pkgs:
          let
            isDarwin = pkgs.stdenv.isDarwin;
          in
            {
              default = pkgs.mkShellNoCC {
                # Seems necessary on my MacBook to make lldb work.
                shellHook = ''
                  ${if isDarwin then ''
                    export LLDB_DEBUGSERVER_PATH=/Applications/Xcode.app/Contents/SharedFrameworks/LLDB.framework/Versions/A/Resources/debugserver
                    ''
                    else ""}
                  '';

                buildInputs = with pkgs; [
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
