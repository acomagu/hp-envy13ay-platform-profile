# Known devices

| Status | Product name | Board | SKU | BIOS | Kernel | Reporter |
|---|---|---:|---|---|---|---|
| Works | HP ENVY x360 Convertible 13-ay0xxx | 876E | 3N945PA#ABJ | F.24 | 7.0.0 | @acomagu |

## Candidate devices

Please report results for:

- HP ENVY x360 Convertible 13-ay0xxx
- HP ENVY 13-ay series
- Other systems with board name `876E`

## Required report data

```bash
cat /sys/class/dmi/id/sys_vendor
cat /sys/class/dmi/id/product_name
cat /sys/class/dmi/id/board_name
cat /sys/class/dmi/id/product_sku
cat /sys/class/dmi/id/bios_version
uname -a
```
