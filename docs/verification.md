# Verification guide

## 1. Confirm DMI

```bash
cat /sys/class/dmi/id/sys_vendor
cat /sys/class/dmi/id/product_name
cat /sys/class/dmi/id/board_name
cat /sys/class/dmi/id/product_sku
cat /sys/class/dmi/id/bios_version
````

The initial supported board is:

```text
board_name: 876E
```

## 2. Confirm module load

```bash
lsmod | grep hp_envy13ay
dmesg | grep -i hp_envy13ay
```

Expected:

```text
registered platform profile driver
```

## 3. Confirm platform_profile

```bash
cat /sys/firmware/acpi/platform_profile_choices
cat /sys/firmware/acpi/platform_profile
```

or:

```bash
PROFILE_DIR="$(find /sys/class/platform-profile -mindepth 1 -maxdepth 1 -type l | head -n1)"
cat "$PROFILE_DIR/choices"
cat "$PROFILE_DIR/profile"
```

Expected choices:

```text
performance balanced cool quiet
```

Exact order may vary.

## 4. Switch profiles

```bash
echo performance | sudo tee "$PROFILE_DIR/profile"
echo balanced | sudo tee "$PROFILE_DIR/profile"
echo cool | sudo tee "$PROFILE_DIR/profile"
echo quiet | sudo tee "$PROFILE_DIR/profile"
```

## 5. Check EC value

```bash
sudo dd if=/sys/kernel/debug/ec/ec0/io bs=1 skip=$((0x29)) count=1 status=none | xxd -p
```

Expected:

| profile     | EC value |
| ----------- | -------: |
| performance |     `00` |
| balanced    |     `01` |
| cool        |     `02` |
| quiet       |     `03` |

## 6. Check power-profiles-daemon

```bash
powerprofilesctl --version
powerprofilesctl list
powerprofilesctl get
```

Try:

```bash
powerprofilesctl set performance
powerprofilesctl set balanced
powerprofilesctl set power-saver
```

Then check:

```bash
cat "$PROFILE_DIR/profile"
sudo dd if=/sys/kernel/debug/ec/ec0/io bs=1 skip=$((0x29)) count=1 status=none | xxd -p
```

