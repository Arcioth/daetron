{
  description = "arclinkdae — raw NETLINK_KOBJECT_UEVENT, no libudev";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/a3b98866eecd";

  outputs = { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      packages.${system} = rec {
        arclinkdae = pkgs.stdenv.mkDerivation {
          pname = "arclinkdae";
          version = "0.1.0";
          src = ./.;
          nativeBuildInputs = [ pkgs.gcc pkgs.musl pkgs.gnumake ];
          buildPhase = ''
            musl-gcc -static -O2 -s -Wall -Wextra -std=c11 -o arclinkdae \
              src/main.c src/nl.c src/rules.c src/dump.c src/run.c
          '';
          installPhase = ''
            install -d $out/bin
            install -m755 arclinkdae $out/bin/arclinkdae
            ln -s arclinkdae $out/bin/uevent
          '';
        };
        default = arclinkdae;
      };

      devShells.${system}.default = pkgs.mkShell {
        packages = [ pkgs.gcc pkgs.musl pkgs.gnumake ];
      };
    };
}
