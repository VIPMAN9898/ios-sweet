/*
 * Just enough flattened-device-tree parsing to find what ABL put in
 * /chosen (initrd location). Big-endian, 4-byte aligned tokens.
 */
#include "fdt.h"

#define FDT_MAGIC	0xd00dfeed
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP		4
#define FDT_END		9
#define FDT_HEADER_SIZE	40
#define FDT_MAX_DEPTH	32

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static uintptr_t align4(uintptr_t a)
{
	return (a + 3) & ~3UL;
}

static uintptr_t skip_str(uintptr_t p)
{
	/* byte loads are fine on Device memory as long as they are aligned to 1 */
	while (*(volatile char *)p)
		p++;
	return p + 1;
}

static int prefix(const char *a, const char *b)
{
	while (*b && *a == *b)
		a++, b++;
	return *b == 0;
}

struct fdt_node_state {
	char name[64];
	uint32_t child_addr_cells, child_size_cells;
	uint32_t reg_addr_cells, reg_size_cells;
	uint64_t reg_base, reg_size;
	int have_reg, have_label, splash_name;
};

/*
 * Read the framebuffer carveout without touching it. The caller decides
 * whether the returned region is large enough before drawing any pixels.
 */
int fdt_splash_region(uintptr_t fdt, uint64_t *base, uint64_t *size)
{
	if (!fdt || !base || !size || be32(fdt) != FDT_MAGIC)
		return 0;
	uint32_t total = be32(fdt + 4);
	uint32_t off = be32(fdt + 8), len = be32(fdt + 36);
	uint32_t stroff = be32(fdt + 12), strsize = be32(fdt + 32);
	if (total < FDT_HEADER_SIZE || off > total || len > total - off ||
	    stroff > total || strsize > total - stroff)
		return 0;
	uintptr_t p = fdt + off, end_struct = p + len;
	uintptr_t strings = fdt + stroff;
	struct fdt_node_state stack[FDT_MAX_DEPTH];
	unsigned depth = 0;

	while (p + 4 <= end_struct) {
		uint32_t tok = be32(p);
		p += 4;
		if (tok == FDT_BEGIN_NODE) {
			if (depth == FDT_MAX_DEPTH)
				return 0;
			uintptr_t name = p;
			uintptr_t after = align4(skip_str(p));
			if (after > end_struct)
				return 0;
			struct fdt_node_state *n = &stack[depth];
			for (unsigned i = 0; i < sizeof(n->name); i++)
				n->name[i] = 0;
			for (unsigned i = 0; i < sizeof(n->name); i++) {
				char c = *(volatile char *)(name + i);
				n->name[i] = c;
				if (!c) break;
			}
			if (n->name[sizeof(n->name) - 1])
				return 0;
			n->reg_addr_cells = depth ? stack[depth - 1].child_addr_cells : 2;
			n->reg_size_cells = depth ? stack[depth - 1].child_size_cells : 2;
			n->child_addr_cells = n->reg_addr_cells;
			n->child_size_cells = n->reg_size_cells;
			n->have_reg = n->have_label = 0;
			n->splash_name = prefix(n->name, "splash_region");
			depth++;
			p = after;
		} else if (tok == FDT_END_NODE) {
			if (!depth)
				return 0;
			struct fdt_node_state *n = &stack[depth - 1];
			if ((n->splash_name || n->have_label) && n->have_reg) {
				*base = n->reg_base;
				*size = n->reg_size;
				return 1;
			}
			depth--;
		} else if (tok == FDT_PROP) {
			if (p + 8 > end_struct || !depth)
				return 0;
			uint32_t nbytes = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			if (val + nbytes > end_struct || nameoff >= strsize)
				return 0;
			uintptr_t propname = strings + nameoff;
			uintptr_t propend = strings + strsize;
			if (propname >= propend)
				return 0;
			struct fdt_node_state *n = &stack[depth - 1];
			if (streq((const char *)propname, "#address-cells") && nbytes == 4)
				n->child_addr_cells = be32(val);
			else if (streq((const char *)propname, "#size-cells") && nbytes == 4)
				n->child_size_cells = be32(val);
			else if (streq((const char *)propname, "label"))
				n->have_label = streq((const char *)val, "cont_splash_mem");
			else if (streq((const char *)propname, "reg") &&
			         n->reg_addr_cells >= 1 && n->reg_addr_cells <= 2 &&
			         n->reg_size_cells >= 1 && n->reg_size_cells <= 2 &&
			         nbytes == 4 * (n->reg_addr_cells + n->reg_size_cells)) {
				uintptr_t q = val;
				n->reg_base = 0;
				for (uint32_t i = 0; i < n->reg_addr_cells; i++, q += 4)
					n->reg_base = (n->reg_base << 32) | be32(q);
				n->reg_size = 0;
				for (uint32_t i = 0; i < n->reg_size_cells; i++, q += 4)
					n->reg_size = (n->reg_size << 32) | be32(q);
				n->have_reg = 1;
			}
			p = (val + nbytes + 3) & ~3UL;
		} else if (tok == FDT_NOP) {
			continue;
		} else {
			return 0;
		}
	}
	return 0;
}

int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);		/* off_dt_struct */
	uintptr_t strings = fdt + be32(fdt + 12);	/* off_dt_strings */
	int depth = 0, in_chosen = 0;

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			in_chosen = depth == 2 && streq(name, "chosen");
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (in_chosen)
				return 0;
			depth--;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			if (in_chosen && streq((const char *)(strings + nameoff), prop)) {
				if (len == 4)
					*out = be32(val);
				else if (len == 8)
					*out = (uint64_t)be32(val) << 32 | be32(val + 4);
				else
					return 0;
				return 1;
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:	/* FDT_END or garbage */
			return 0;
		}
	}
}
