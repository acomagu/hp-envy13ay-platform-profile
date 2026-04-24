{ lib , stdenv , kernel }:

stdenv.mkDerivation {
  pname = "hp-envy13ay-platform-profile";
  version = "0.1.0";

  src = ../.;

  nativeBuildInputs = kernel.moduleBuildDependencies;

  makeFlags = [
    "KERNELRELEASE=${kernel.modDirVersion}"
    "KDIR=${kernel.dev}/lib/modules/${kernel.modDirVersion}/build"
  ];

  buildPhase = ''
    runHook preBuild
    make $makeFlags
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    install -D hp_envy13ay_platform_profile.ko \
      $out/lib/modules/${kernel.modDirVersion}/extra/hp_envy13ay_platform_profile.ko

    runHook postInstall
  '';

  meta = {
    description = "Experimental platform_profile driver for HP ENVY x360 13-ay0xxx board 876E";
    license = lib.licenses.gpl2Only;
    platforms = lib.platforms.linux;
  };
}
