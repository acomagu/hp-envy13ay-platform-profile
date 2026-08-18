# hp-envy13ay-platform-profile

Experimental Linux platform driver for **HP ENVY x360 13-ay0xxx** laptops with DMI board name **876E**.

It provides two things:

1. **`platform_profile`** — exposes the HP thermal/power mode stored at EC offset `0x29`, so tools such as `powerprofilesctl`, GNOME Settings, and KDE Power Management can switch profiles.
2. **`hwmon`** — exposes the fan speed and the EC's CPU/skin temperatures, none of which are otherwise visible to Linux on this machine.

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

## Sensors (hwmon)

Registered as hwmon name `hp_envy13ay`.

| Attribute | Source | Meaning |
|---|---|---|
| `fan1_input` | ACPI `\_TZ.TSZ0.FRSP()` | Fan speed, RPM |
| `fan1_max` / `fan1_min` | ACPI `\_TZ.TSZ0.FMAX()` / `FMIN()` | Fan limits (5700 / 4500 RPM observed) |
| `temp1_input` (`CPU`) | ECMP `0x8B0` (`CTMP`) | EC's CPU temperature; matches `thermal_zone0` |
| `temp2_input` (`Skin 1`) | ECMP `0x8B6` (`EST1`) | Skin temperature |
| `temp3_input` (`Skin 2`) | ECMP `0x8B7` (`EST2`) | Skin temperature |
| `temp4_input` (`Skin 3`) | ECMP `0x8B2` (`EST3`) | Skin temperature |

Disable with `hp_envy13ay_platform_profile.hwmon=0` if you only want `platform_profile`.

### Why the skin temperatures matter

Stock Linux cannot see the fan at all on this machine. `hp-wmi` registers a `pwm1_enable` attribute but the underlying WMI query returns `-ENODATA`, and no hwmon device exposes `fan*_input`. The DSDT provides `FRSP`/`FMAX`/`FMIN`/`FSSP` under `_TZ.TSZ0`, but those are HP-private names that the ACPI thermal driver never evaluates. `TSZ0` also has no `_ACx`/`_ALx` trip points and an empty `_PSV`, so the fan is entirely EC-autonomous.

Load testing on BIOS F.24 established what actually drives that fan:

| Test | Skin at start | Fan behaviour | Peak CPU temperature |
|---|---|---|---|
| From idle | 35 degC | Started the moment `EST1`/`EST2` crossed 37 -> 38 | **99 degC** (fan stayed off up to 97 degC) |
| Immediately after, skin still warm | 42-43 degC | Already spinning before load began | 77 degC |

Raw samples from the first run, 10 s apart:

```
 t   CTMP EST1 EST2 EST3 FRPM
  0    45   35   35   40    0
 20    80   35   36   41    0     CPU at 80 degC, fan off
 50    97   37   37   47    0     CPU at 97 degC, skin still 37
 60    99   38   38   48   27     skin reaches 38, fan starts
120    98   43   42   55   57     FNMX = 5700 rpm
```

So the fan curve is gated on skin temperature with a threshold around 37 degC, and CPU temperature alone never starts the fan.

This is the same behaviour described in HP Support Community reports for the 13-ay0009na (Ryzen 7 4700U, BIOS F.23/F.24), where the skin temperature channel freezes at room temperature after long uptime and the fan then stays off while the CPU reaches 100 degC. A warm restart does not clear it; only a full power-down does. The reports say BIOS F.25 fixed it, though HP's published release notes do not mention any thermal or EC change.

- [HP Envy x360 fan control bug and throttling (fan off at 100c, latest BIOS)](https://h30434.www3.hp.com/t5/Notebook-Boot-and-Lockup/HP-Envy-x360-fan-control-bug-and-throttling-fan-off-at-100c/td-p/8885596)
- [HP Envy x360 thermal sensor bug sits at 100c with fan off](https://h30434.www3.hp.com/t5/Notebook-Boot-and-Lockup/HP-Envy-x360-thermal-sensor-bug-sits-at-100c-with-fan-off/td-p/8690808)

With these sensors exposed, the failure is visible directly: `temp2`/`temp3` stuck at a low value while `temp1` is high and `fan1_input` reads 0.

### EC shared-memory window

The temperatures come from the DSDT's

```
OperationRegion (ECMP, SystemMemory, 0xFE0B0000, 0x1000)
```

This is plain MMIO. It is absent from the e820 map and unclaimed in `/proc/iomem`, so a read-only mapping cannot collide with the ACPI EC driver, and no EC transaction is involved.

Byte positions were derived from the DSDT field list starting at `Offset (0x8A2)` and cross-checked three ways:

1. the running byte total lands exactly on the next declared `Offset (0x8B6)`
2. `NPST` reads `3`, which is what `EC0._REG` and `_WAK` write
3. `CTMP` tracks `thermal_zone0`, consistent with `TSZ0._TMP` returning `0x0AAC + CTMP * 0x0A`

`EST1`/`EST2`/`EST3` are declared in the DSDT but never referenced by any AML, i.e. they are EC firmware state exposed through the shared window.

The EC updates the window without locking against the reader, so a byte can occasionally be caught mid-update (one bogus sample was observed, `EST1` momentarily reading 1 while `EST2` stayed at 42). Readings outside 5-127 degC are treated as torn reads and the previous value is reported instead.

## Warning

This module writes directly to the embedded controller (for `platform_profile`) and maps a fixed physical address (for the sensors).

Use at your own risk. Incorrect EC writes can cause unexpected thermal, fan, power, or firmware behavior.

The module is DMI-gated to board name `876E`, but HP may reuse or change EC layouts across models and BIOS versions. The `0xFE0B0000` window and the field offsets are specific to this DSDT; do not assume they carry over to other HP machines.

The hwmon side is read-only. It does not touch `FSSP`/`FWPM` and never commands the fan.

## Requirements

- Linux with `platform_profile` support
- `CONFIG_HWMON`
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

```bash
# platform_profile
cat /sys/firmware/acpi/platform_profile_choices
cat /sys/firmware/acpi/platform_profile

# sensors
sensors hp_envy13ay-*
```

Or find the hwmon directory directly:

```bash
grep -l '^hp_envy13ay$' /sys/class/hwmon/hwmon*/name
```

See [docs/verification.md](docs/verification.md) for more.

## Reporting

Your behavior report will help upstream this code to the kernel.

See [docs/reporting.md](docs/reporting.md).

## Upstreaming plan

This repository is intended as a validation driver.

The two halves have different prospects:

- **`platform_profile` and fan speed** are reasonable upstream candidates. Both go through vendor-defined interfaces (EC offset `0x29`, and the ACPI methods `FRSP`/`FMAX`/`FMIN`), so they could be added to `drivers/platform/x86/hp/hp-wmi.c` behind a DMI quirk once the mapping is confirmed across enough machines.
- **The skin temperatures** require a hardcoded physical address and offsets read out of one specific DSDT. That does not belong in a generic multi-model driver as-is. If the values prove useful, the right upstream shape is probably a separate model-specific driver, or a quirk table that carries the window address per board.

## License

GPL-2.0-only
