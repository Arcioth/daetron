{
  description = "daetron — raw NETLINK_KOBJECT_UEVENT, no libudev";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/a3b98866eecd";

  outputs = { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      packages.${system} = rec {
        daetron = pkgs.stdenv.mkDerivation {
          pname = "daetron";
          version = "0.1.0";
          src = ./.;
          nativeBuildInputs = [ pkgs.gcc pkgs.musl pkgs.gnumake ];
          buildPhase = ''
            musl-gcc -static -O2 -s -Wall -Wextra -std=c11 -o daetron \
              src/main.c src/nl.c src/rules.c src/dump.c src/run.c
          '';
          installPhase = ''
            install -d $out/bin
            install -m755 daetron $out/bin/daetron
            ln -s daetron $out/bin/uevent
          '';
        };
        default = daetron;
      };

      devShells.${system}.default = pkgs.mkShell {
        packages = [ pkgs.gcc pkgs.musl pkgs.gnumake ];
      };
    };
}
