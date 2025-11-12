{
  description = "Development environment for mos monorepo";

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

            # python env for glad
            pythonEnv = pkgs.python3.withPackages(ps: with ps; [
              jinja2
            ]);

            isDarwin = pkgs.stdenv.isDarwin;

          in
            {
              default = pkgs.mkShellNoCC {
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

                  libGL         # for GLFW

                  ncurses       # for readline

                  pythonEnv     # for glad

                ] ++ (with pkgs.llvmPackages_20; [

                  clangUseLLVM
                  clang-tools
                  lldb
                  bintools      # required for lto

                ]) ++ (pkgs.lib.optionals (!pkgs.stdenv.isDarwin) [

                  # to build GLFW on linux
                  libffi
                  libxkbcommon
                  pkg-config
                  wayland
                  wayland-scanner
                  xorg.libX11
                  xorg.libXrandr
                  xorg.libXinerama
                  xorg.libXcursor
                  xorg.libXi

                  # gcc
                  gcc15

                  # some perf things
                  linuxKernel.packages.linux_6_6.perf
                  poop

                ]) ++ (pkgs.lib.optionals (pkgs.stdenv.isDarwin) [
                  git
                ]) ;
              };
            });
      };
}
