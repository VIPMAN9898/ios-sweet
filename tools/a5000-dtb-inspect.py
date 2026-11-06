#!/usr/bin/env python3
"""Inspect an AArch64 flattened device tree for OnePlus 5 bring-up inputs.

This is deliberately an evidence collector, not an image builder. It refuses
to label Qualcomm reference-board DTBs as OnePlus 5 hardware.
"""
import argparse
import json
import struct
import sys

MAGIC = 0xD00DFEED
BEGIN_NODE, END_NODE, PROP, NOP, END = 1, 2, 3, 4, 9


def u32(data, off):
    return struct.unpack_from(">I", data, off)[0]


def strings(raw):
    return [x.decode("ascii", "replace") for x in raw.split(b"\0") if x]


def parse(blob):
    if len(blob) < 40 or u32(blob, 0) != MAGIC:
        raise ValueError("input is not an FDT blob")
    total = u32(blob, 4)
    struct_off, strings_off = u32(blob, 8), u32(blob, 12)
    struct_size, strings_size = u32(blob, 36), u32(blob, 32)
    if total > len(blob) or struct_off + struct_size > total or strings_off + strings_size > total:
        raise ValueError("FDT section extends beyond input")
    st = blob[struct_off:struct_off + struct_size]
    names = blob[strings_off:strings_off + strings_size]
    root = {"name": "", "props": {}, "children": []}
    stack = [root]
    pos = 0
    while pos + 4 <= len(st):
        token = u32(st, pos)
        pos += 4
        if token == BEGIN_NODE:
            end = st.find(b"\0", pos)
            if end < 0:
                raise ValueError("unterminated node name")
            name = st[pos:end].decode("ascii", "replace")
            pos = (end + 4) & ~3
            child = {"name": name, "props": {}, "children": []}
            stack[-1]["children"].append(child)
            stack.append(child)
        elif token == END_NODE:
            if len(stack) == 1:
                raise ValueError("unbalanced FDT node")
            stack.pop()
        elif token == PROP:
            if pos + 8 > len(st):
                raise ValueError("truncated FDT property")
            size, nameoff = u32(st, pos), u32(st, pos + 4)
            pos += 8
            end = names.find(b"\0", nameoff)
            if end < 0 or pos + size > len(st):
                raise ValueError("invalid FDT property bounds")
            pname = names[nameoff:end].decode("ascii", "replace")
            stack[-1]["props"][pname] = st[pos:pos + size]
            pos = (pos + size + 3) & ~3
        elif token == NOP:
            continue
        elif token == END:
            break
        else:
            raise ValueError(f"unknown FDT token {token}")
    if len(stack) != 1:
        raise ValueError("unterminated FDT node")
    if len(root["children"]) != 1:
        raise ValueError("FDT must contain exactly one root node")
    return root["children"][0]


def walk(node, path=""):
    here = path + "/" + node["name"] if path else "/"
    yield here, node
    for child in node["children"]:
        yield from walk(child, here.rstrip("/"))


def cstrings(raw):
    return strings(raw or b"")


def cells(raw):
    if not raw or len(raw) % 4:
        return []
    return [u32(raw, i) for i in range(0, len(raw), 4)]


def reg_ranges(node, addr_cells, size_cells):
    vals = cells(node["props"].get("reg"))
    stride = addr_cells + size_cells
    if not stride:
        return []
    out = []
    for i in range(0, len(vals) - stride + 1, stride):
        addr = 0
        size = 0
        for v in vals[i:i + addr_cells]:
            addr = (addr << 32) | v
        for v in vals[i + addr_cells:i + stride]:
            size = (size << 32) | v
        out.append({"base": f"0x{addr:x}", "size": f"0x{size:x}"})
    return out


def prop_u32(node, key):
    v = node["props"].get(key)
    return u32(v, 0) if v and len(v) == 4 else None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dtb", help="candidate bootloader or board FDT")
    ap.add_argument("--allow-reference", action="store_true",
                    help="inspect non-OnePlus trees for comparison; output remains marked unverified")
    args = ap.parse_args()
    try:
        with open(args.dtb, "rb") as f:
            root = parse(f.read())
    except (OSError, ValueError, struct.error) as e:
        ap.error(str(e))

    nodes = list(walk(root))
    rootp = root["props"]
    compatibles = cstrings(rootp.get("compatible"))
    is_oneplus = "oneplus,cheeseburger" in compatibles
    if not is_oneplus and not args.allow_reference:
        print(json.dumps({"status": "rejected", "reason": "root compatible lacks oneplus,cheeseburger",
                          "model": cstrings(rootp.get("model")), "compatible": compatibles}, indent=2))
        return 2

    address_cells = prop_u32(root, "#address-cells") or 2
    size_cells = prop_u32(root, "#size-cells") or 1
    mem = []
    reserved = []
    cpus = []
    gic = []
    ufs = []
    displays = []
    for path, node in nodes:
        p = node["props"]
        comp = cstrings(p.get("compatible"))
        if p.get("device_type") == b"memory\0" or node["name"].startswith("memory@"):
            mem.extend({"path": path, **r} for r in reg_ranges(node, address_cells, size_cells))
        if path.startswith("/reserved-memory/") and p.get("reg"):
            reserved.extend({"path": path, **r} for r in reg_ranges(node, address_cells, size_cells))
        if p.get("device_type") == b"cpu\0":
            cpus.append({"path": path, "compatible": comp, "mpidr_cells": cells(p.get("reg"))})
        if any("gic" in x.lower() for x in comp):
            gic.append({"path": path, "compatible": comp,
                        "reg": reg_ranges(node, address_cells, size_cells)})
        if any("ufshc" in x.lower() or "ufs" in x.lower() for x in comp) or "ufshc@" in node["name"]:
            ufs.append({"path": path, "compatible": comp,
                        "reg": reg_ranges(node, address_cells, size_cells)})
        if any(x in " ".join(comp).lower() for x in ("framebuffer", "simple-framebuffer")) or "framebuffer@" in node["name"]:
            displays.append({"path": path, "compatible": comp,
                             "reg": reg_ranges(node, address_cells, size_cells),
                             "width": prop_u32(node, "width"), "height": prop_u32(node, "height"),
                             "stride": prop_u32(node, "stride"),
                             "format": cstrings(p.get("format"))})

    result = {
        "status": "oneplus-board-tree" if is_oneplus else "reference-tree-only",
        "model": cstrings(rootp.get("model")), "compatible": compatibles,
        "board_id_cells": cells(rootp.get("qcom,board-id")),
        "memory": mem, "reserved_memory": reserved, "cpus": cpus,
        "psci_method": next((cstrings(n["props"].get("method")) for path, n in nodes if path == "/psci"), []),
        "interrupt_controllers": gic, "ufs": ufs, "display_nodes": displays,
        "build_ready": bool(is_oneplus and mem and reserved and cpus and gic and ufs and displays),
    }
    print(json.dumps(result, indent=2))
    return 0 if is_oneplus else 2


if __name__ == "__main__":
    sys.exit(main())
