#!/usr/bin/env python3
"""Append the stock Qualcomm DTB bundle to an A5000 diagnostic Image."""
import sys
import zlib

FDT_MAGIC = b"\xd0\x0d\xfe\xed"


def main(stock_kernel, shim_image, output):
    stock = open(stock_kernel, "rb").read()
    if not stock.startswith(b"\x1f\x8b"):
        raise SystemExit("stock kernel is not gzip-compressed")
    dec = zlib.decompressobj(16 + zlib.MAX_WBITS)
    dec.decompress(stock)
    if not dec.eof:
        raise SystemExit("incomplete gzip stream in stock kernel")
    dtbs = dec.unused_data
    if not dtbs.startswith(FDT_MAGIC):
        raise SystemExit("stock kernel has no appended FDT bundle")

    pos = 0
    count = 0
    while pos + 8 <= len(dtbs) and dtbs[pos:pos + 4] == FDT_MAGIC:
        size = int.from_bytes(dtbs[pos + 4:pos + 8], "big")
        if size < 40 or pos + size > len(dtbs):
            raise SystemExit("invalid FDT entry in stock bundle")
        count += 1
        pos += size
    if count < 1 or pos != len(dtbs):
        raise SystemExit("DTB bundle is malformed or has unparsed trailing data")

    with open(output, "wb") as out:
        out.write(open(shim_image, "rb").read())
        out.write(dtbs)
    print(f"{output}: appended {count} stock DTBs ({len(dtbs)} bytes)")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(f"usage: {sys.argv[0]} STOCK_KERNEL SHIM_IMAGE OUTPUT")
    main(*sys.argv[1:])
