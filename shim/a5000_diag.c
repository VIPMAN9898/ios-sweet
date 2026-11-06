/*
 * Read-only OnePlus 5 boot-handoff probe.
 *
 * It reads the ABL-provided FDT, writes only to the reserved splash buffer,
 * performs no MMIO or storage access, and asks PSCI to reset after 25 seconds.
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"

#define PSCI_SYSTEM_RESET 0x84000009UL

static volatile int framebuffer_valid;

static uint64_t read_counter(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

static uint64_t read_frequency(void)
{
	uint64_t v;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

static void __attribute__((noreturn)) reset_after(unsigned seconds)
{
	uint64_t end = read_counter() + read_frequency() * seconds;
	while (read_counter() < end)
		__asm__ volatile("yield");
	register uint64_t x0 __asm__("x0") = PSCI_SYSTEM_RESET;
	register uint64_t x1 __asm__("x1") = 0;
	register uint64_t x2 __asm__("x2") = 0;
	register uint64_t x3 __asm__("x3") = 0;
	__asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : : "memory");
	for (;;)
		__asm__ volatile("wfe");
}

void shim_main(uint64_t dtb)
{
	uint64_t fb = 0, size = 0;
	if (!fdt_splash_region(dtb, &fb, &size) || fb != FB_BASE ||
	    size < (uint64_t)FB_STRIDE * FB_H)
		reset_after(25);

	con_fb = (volatile uint32_t *)(uintptr_t)fb;
	framebuffer_valid = 1;
	con_clear(BLUE);
	con_str("A5000", "boot handoff probe");
	con_str("MODE", "FDT read; no MMIO/storage");
	con_hex2("DTB", "ptr=", (uint32_t)dtb, "magic=", 0xd00dfeed);
	con_hex2("SPLASH", "base=", (uint32_t)fb, "bytes=", (uint32_t)size);
	con_hex2("PANEL", "width=", FB_W, "height=", FB_H);
	con_hex2("FB", "stride=", FB_STRIDE, "depth=", 32);
	con_str("RESET", "automatic in 25 sec");
	reset_after(25);
}

void secondary_main(uint64_t idx)
{
	(void)idx;
	for (;;)
		__asm__ volatile("wfe");
}

void exc_handler(uint64_t slot, uint64_t *regs)
{
	(void)regs;
	if (framebuffer_valid) {
		con_clear(BLUE);
		con_hex("EXCEPTION", slot);
	}
	reset_after(1);
}
