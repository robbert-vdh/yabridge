{
  description = "Yet Another VST bridge, run Windows VST2, VST3 and CLAP plugins under Linux";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
      version = "main-${self.shortRev or self.dirtyShortRev or "unknown"}";
      # nixpkgs' yabridgectl reads src.name and src.meta
      src = self // {
        name = "source";
        meta.homepage = "https://github.com/robbert-vdh/yabridge";
      };
    in
    {
      packages = forAllSystems (pkgs: rec {
        yabridge =
          (pkgs.yabridge.override {
            wineWow64Packages = pkgs.wineWow64Packages // {
              yabridge = pkgs.wineWow64Packages.staging;
            };
          }).overrideAttrs
            (old: {
              inherit version;
              inherit src;
              # Already merged in this tree
              patches = pkgs.lib.filter (
                p: !(pkgs.lib.hasSuffix "libyabridge-drop-32-bit-support.patch" (toString p))
              ) old.patches;
            });

        yabridgectl =
          (pkgs.yabridgectl.override {
            inherit yabridge;
          }).overrideAttrs
            (_old: {
              inherit version;
              inherit src;
              cargoDeps = pkgs.rustPlatform.importCargoLock {
                lockFile = ./tools/yabridgectl/Cargo.lock;
                allowBuiltinFetchGit = true;
              };
            });

        default = yabridge;
      });

      overlays.default = final: _prev: {
        inherit (self.packages.${final.stdenv.hostPlatform.system}) yabridge yabridgectl;
      };

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.yabridge ];
          packages = with pkgs; [
            cargo
            rustc
            clippy
            rustfmt
            rust-analyzer
          ];
        };
      });
    };
}
