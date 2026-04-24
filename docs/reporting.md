# Required Report Data

You can report from [here](https://github.com/acomagu/hp-envy13ay-platform-profile/issues/new?template=device-report.yml).

Please include the following information when reporting results.

## DMI information

```bash
cat /sys/class/dmi/id/sys_vendor
cat /sys/class/dmi/id/product_name
cat /sys/class/dmi/id/board_name
cat /sys/class/dmi/id/product_sku
cat /sys/class/dmi/id/bios_version
````

## Kernel and distribution

```bash
uname -a
cat /etc/os-release
```

## Module log

```bash
dmesg | grep -i hp_envy13ay
```

## platform_profile output

```bash
cat /sys/firmware/acpi/platform_profile_choices 2>/dev/null || true
cat /sys/firmware/acpi/platform_profile 2>/dev/null || true

PROFILE_DIR="$(find /sys/class/platform-profile -mindepth 1 -maxdepth 1 -type l | head -n1)"
echo "$PROFILE_DIR"
cat "$PROFILE_DIR/choices" 2>/dev/null || true
cat "$PROFILE_DIR/profile" 2>/dev/null || true
```

## power-profiles-daemon output

```bash
powerprofilesctl --version
powerprofilesctl list
powerprofilesctl get
```

## EC value results

For each profile, run:

```bash
PROFILE_DIR="$(find /sys/class/platform-profile -mindepth 1 -maxdepth 1 -type l | head -n1)"

echo performance | sudo tee "$PROFILE_DIR/profile"
sudo dd if=/sys/kernel/debug/ec/ec0/io bs=1 skip=$((0x29)) count=1 status=none | xxd -p

echo balanced | sudo tee "$PROFILE_DIR/profile"
sudo dd if=/sys/kernel/debug/ec/ec0/io bs=1 skip=$((0x29)) count=1 status=none | xxd -p

echo cool | sudo tee "$PROFILE_DIR/profile"
sudo dd if=/sys/kernel/debug/ec/ec0/io bs=1 skip=$((0x29)) count=1 status=none | xxd -p

echo quiet | sudo tee "$PROFILE_DIR/profile"
sudo dd if=/sys/kernel/debug/ec/ec0/io bs=1 skip=$((0x29)) count=1 status=none | xxd -p
```

Expected mapping:

```text
performance -> 00
balanced    -> 01
cool        -> 02
quiet       -> 03
```

## Notes

Additional information such as the following is welcome:

* whether GNOME/KDE power profile UI changes the profile
* whether `powerprofilesctl set power-saver` selects `quiet`
* whether fan noise, temperature, or performance behavior changes
* any suspend/resume issues
