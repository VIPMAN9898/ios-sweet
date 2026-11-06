#!/bin/bash
# Build the separate OnePlus 5 XNU shim. This never invokes fastboot or writes
# a phone partition. --shim-only builds the ARM64 shim without an XNU payload.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SHIM="$ROOT/shim"
OUT="$SHIM/out-a5000"
SHIM_ONLY=0
if [ "${1:-}" = "--shim-only" ]; then
	SHIM_ONLY=1
else
	LIVE_DTB=${1:?usage: build-a5000.sh LIVE_FDT XNU_PAYLOAD [OUT_IMAGE] | --shim-only}
	PAYLOAD=${2:?usage: build-a5000.sh LIVE_FDT XNU_PAYLOAD [OUT_IMAGE] | --shim-only}
	BOOT_IMAGE=${3:-"$OUT/a5000-xnu-boot.img"}
	[ -f "$LIVE_DTB" ] || { echo "missing live FDT: $LIVE_DTB" >&2; exit 2; }
	[ -f "$PAYLOAD" ] || { echo "missing XNU payload: $PAYLOAD" >&2; exit 2; }
fi

CC=aarch64-linux-gnu-gcc
OBJCOPY=aarch64-linux-gnu-objcopy
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra \
-DFB_BASE=0x9d400000UL -DFB_PHYS=0x9d400000UL -DFB_W=1080 -DFB_H=1920 -DFB_STRIDE=4320 \
-DGICD_BASE=0x17a00000UL -DGICR_BASE=0x17b00000UL -DUFS_BASE=0x01da4000UL"

mkdir -p "$OUT"
for f in start.S shim.c console.c mmu.c fdt.c loader.c ufs.c; do
	$CC $CFLAGS -I"$SHIM" -c "$SHIM/$f" -o "$OUT/${f%.*}.o"
done

abs=$(aarch64-linux-gnu-readelf -r "$OUT/shim.o" "$OUT/console.o" "$OUT/mmu.o" \
	"$OUT/fdt.o" "$OUT/loader.o" "$OUT/ufs.o" | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }

$CC $CFLAGS -static -T "$SHIM/shim.ld" -Wl,--build-id=none \
	-Wl,--no-warn-rwx-segments -Wl,-N "$OUT/start.o" "$OUT/shim.o" \
	"$OUT/console.o" "$OUT/mmu.o" "$OUT/fdt.o" "$OUT/loader.o" "$OUT/ufs.o" \
	-o "$OUT/shim.elf"
$OBJCOPY -O binary "$OUT/shim.elf" "$OUT/Image"
size=$(aarch64-linux-gnu-nm "$OUT/shim.elf" | awk '$3=="_end"{print strtonum("0x"$1)}')
truncate -s "$size" "$OUT/Image"

if [ "$SHIM_ONLY" = 1 ]; then
	echo "Built A5000 shim only; no XNU payload or phone image was created."
	sha256sum "$OUT/Image"
	ls -lh "$OUT/Image"
	exit 0
fi

mkbootimg --header_version 2 --kernel "$OUT/Image" --ramdisk "$PAYLOAD" --dtb "$LIVE_DTB" \
	--base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
	--tags_offset 0x100 --dtb_offset 0x1f00000 --pagesize 4096 \
	--board OnePlus5 --os_version 10.0.0 --os_patch_level 2020-10 \
	--cmdline "console=null" -o "$BOOT_IMAGE"

echo "Built A5000 temporary XNU boot image (not flashed): $BOOT_IMAGE"
sha256sum "$BOOT_IMAGE"
ls -lh "$OUT/Image" "$BOOT_IMAGE"
