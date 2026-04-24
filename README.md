# hp-envy13ay-platform-profile

Experimental Linux `platform_profile` driver for **HP ENVY x360 13-ay0xxx** laptops with DMI board name **876E**.

This driver exposes the HP thermal/power mode stored at EC offset `0x29` through the Linux `platform_profile` interface, so tools such as `powerprofilesctl`, GNOME Settings, and KDE Power Management can switch profiles.

## Known devices

| Status | Product name | Board | SKU | BIOS | Kernel | Reporter |
|---|---|---:|---|---|---|---|
| Works | HP ENVY x360 Convertible 13-ay0xxx | 876E | 3N945PA#ABJ | F.24 | 7.0.0 | @acomagu |

### Candidate devices

Please report results for:

- HP ENVY x360 Convertible 13-ay0xxx
- HP ENVY 13-ay series
- Other systems with board name `876E`

See [docs/reporting.md](docs/reporting.md) to report.

## EC profile mapping

This driver currently assumes the following EC register mapping:

| EC offset | Value | HP name | Linux platform_profile |
|---:|---:|---|---|
| `0x29` | `0x00` | power / performance | `performance` |
| `0x29` | `0x01` | recommended | `balanced` |
| `0x29` | `0x02` | cool | `cool` |
| `0x29` | `0x03` | silent | `quiet` |

The `0x29` mapping is based on user reports for HP ENVY x360 13-ay series machines and must be validated per device/BIOS version.

## Warning

This module writes directly to the embedded controller.

Use at your own risk. Incorrect EC writes can cause unexpected thermal, fan, power, or firmware behavior.

The module is DMI-gated to board name `876E`, but HP may reuse or change EC layouts across models and BIOS versions.

## Requirements

- Linux with `platform_profile` support
- Kernel headers / module build environment
- Exported `ec_read` and `ec_write` symbols

On some kernels, `ec_read` / `ec_write` may not be exported to external modules.

## Installation

<details><summary>NixOS instruction</summary>

Example:

```nix
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    hp-envy13ay-platform-profile = {
      url = "github:acomagu/hp-envy13ay-platform-profile";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, hp-envy13ay-platform-profile, ... }: {
    nixosConfigurations.YOUR_HOSTNAME = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";

      modules = [
        ./configuration.nix

        hp-envy13ay-platform-profile.nixosModules.default
        {
          hardware.hp-envy13ay-platform-profile.enable = true;
        }
      ];
    };
  };
}
````

Then run:

```bash
sudo nixos-rebuild test
sudo modprobe hp_envy13ay_platform_profile
```

</details>

### Manual build

```bash
make
sudo insmod hp-envy13ay-platform-profile.ko
```

Unload:

```bash
sudo rmmod hp_envy13ay_platform_profile
```

## Verify

See [docs/verification.md](docs/verification.md).

## Reporting

Your behavior report will help upstream this code to the kernel.

See [docs/reporting.md](docs/reporting.md).

## Upstreaming plan

This repository is intended as a validation driver.

If the EC mapping is confirmed across enough HP ENVY x360 13-ay machines, the preferred long-term solution is to add support to the upstream `hp-wmi` driver under `drivers/platform/x86/hp/hp-wmi.c`.

## License

GPL-2.0-only

