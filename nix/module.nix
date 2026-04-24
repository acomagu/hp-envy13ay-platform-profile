{ config, lib, ... }:

let
  cfg = config.hardware.hp-envy13ay-platform-profile;
  package = config.boot.kernelPackages.callPackage ./package.nix { };
in
{
  options.hardware.hp-envy13ay-platform-profile = {
    enable = lib.mkEnableOption "HP ENVY 13-ay platform_profile driver";
  };

  config = lib.mkIf cfg.enable {
    boot.extraModulePackages = [ package ];

    boot.kernelModules = [ "hp_envy13ay_platform_profile" ];
  };
}
