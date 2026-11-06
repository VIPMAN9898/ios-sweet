#!/bin/bash
# Build a separate A5000-only, no-storage boot-handoff diagnostic image.
# Optional: pass --live-fdt PATH to package a captured FDT as boot header v2.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SHIM="$ROOT/shim"
OUT="$SHIM/out-a5000"
STOCK_BOOT=${1:-/tmp/a5000-firmware/boot.img}
LIVE_DTB=
if [ "${2:-}" = "--live-fdt" ]; then
	LIVE_DTB=${3:?usage: build-a5000-diag.sh [STOCK_BOOT] [--live-fdt FDT]}
elif [ "$#" -gt 1 ]; then
	echo "usage: build-a5000-diag.sh [STOCK_BOOT] [--live-fdt FDT]" >&2
	exit 2
fi
CC=aarch64-linux-gnu-gcc
OBJCOPY=aarch64-linux-gnu-objcopy
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra -DFB_BASE=0x9d400000UL -DFB_W=1080 -DFB_H=1920 -DFB_STRIDE=4320"

mkdir -p "$OUT"
python3 "$ROOT/tools/unpack_bootv2.py" "$STOCK_BOOT" "$OUT/stock" > "$OUT/stock-header.txt"
grep -q '^header_version: 0$' "$OUT/stock-header.txt" || {
	echo "expected the OnePlus 5 Android boot header v0" >&2
	exit 1
}

for f in start.S console.c fdt.c a5000_diag.c; do
	$CC $CFLAGS -I"$SHIM" -c "$SHIM/$f" -o "$OUT/${f%.*}.o"
done
$CC $CFLAGS -static -T "$SHIM/shim.ld" -Wl,--build-id=none \
	-Wl,--no-warn-rwx-segments -Wl,-N "$OUT/start.o" "$OUT/console.o" \
	"$OUT/fdt.o" "$OUT/a5000_diag.o" -o "$OUT/shim.elf"
$OBJCOPY -O binary "$OUT/shim.elf" "$OUT/Image"
size=$(aarch64-linux-gnu-nm "$OUT/shim.elf" | awk '$3=="_end"{print strtonum("0x"$1)}')
truncate -s "$size" "$OUT/Image"

if [ -n "$LIVE_DTB" ]; then
	PROBE="$OUT/a5000-live-fdt-v2.img"
	mkbootimg --header_version 2 --kernel "$OUT/Image" \
		--ramdisk "$OUT/stock/ramdisk" --dtb "$LIVE_DTB" \
		--base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
		--tags_offset 0x100 --dtb_offset 0x1f00000 --pagesize 4096 \
		--board OnePlus5 --cmdline "console=null" -o "$PROBE"
else
	PROBE="$OUT/a5000-fdt-probe.img"
	python3 "$ROOT/tools/pack-a5000-diag-kernel.py" \
		"$OUT/stock/kernel" "$OUT/Image" "$OUT/kernel-with-dtbs.img"
	mkbootimg --header_version 0 --kernel "$OUT/kernel-with-dtbs.img" \
		--ramdisk "$OUT/stock/ramdisk" --base 0 --kernel_offset 0x8000 \
		--ramdisk_offset 0x1000000 --tags_offset 0x100 --pagesize 4096 \
		--board OnePlus5 --cmdline "console=null" -o "$PROBE"
fi

printf '\nBuilt temporary diagnostic only; it is not an iOS image.\n'
sha256sum "$PROBE"
ls -lh "$PROBE"
