{
  description = "Experimental Linux platform_profile driver for HP ENVY x360 13-ay0xxx / board 876E";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
  let
    systems = [ "x86_64-linux" ];
    forAllSystems = nixpkgs.lib.genAttrs systems;
  in
  {
    packages = forAllSystems (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in {
        default = pkgs.linuxPackages.callPackage ./nix/package.nix { };
      }
    );

    nixosModules.default = import ./nix/module.nix;
  };
}
