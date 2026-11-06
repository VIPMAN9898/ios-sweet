# OnePlus 5 (A5000) port status

## Goal and scope

Start a separate target in `ios-sweet` for the OnePlus 5, model A5000, while preserving the existing Redmi Note 10 Pro (`sweet`, SM7150) work. The current source tree and the boot images described in the main README are for `sweet`; they are not compatible A5000 images.

## Confirmed device facts

- The user identifies the phone as model **A5000**.
- During the read-only inspection, Fastboot reported `product: msm8998`, `unlocked: yes`, and `secure: yes`. These are the Fastboot values as returned; `secure: yes` is recorded without inferring an overall security or lock state from it.
- The phone was detectable by Fastboot. ADB showed no device while the phone was in Fastboot mode.
- LineageOS's device metadata identifies the OnePlus 5 as `cheeseburger`, with a Qualcomm MSM8998 Snapdragon 835 SoC and the product model `ONEPLUS A5000`.

## Why this needs a separate port

The current `sweet` target is tied to SM7150 hardware. Its shim and Apple Device Tree generator embed board-specific memory layout, GIC and framebuffer addresses, UFS registers, CPU topology, and device identity. The XNU patch set and drivers also contain `sweet`-specific assumptions. A matching SoC family does not make those addresses or drivers interchangeable with MSM8998.

## First implementation gate

Obtain the stock boot image that matches the A5000's installed firmware and extract its device tree. The project contains no boot image or DTB; the bounded home-tree filename scan found no A5000- or `cheeseburger`-named artifacts, but did not rule out a generically named `boot.img`. Record the firmware/build provenance with the extracted DTB, and keep private dumps and serial numbers out of Git.

From that DTB and boot-chain evidence, record only verified values for:

1. RAM banks and reserved regions, including the bootloader framebuffer and payload placement constraints.
2. CPU topology, PSCI conduit, interrupt controller, timers, and GIC redistributor layout.
3. UFS controller state at handoff and the A5000 device-tree nodes needed to read storage.
4. Display geometry and the bootloader framebuffer format/address.
5. PMIC, key, and other hardware needed for early diagnostics.

Then create a `cheeseburger` target profile and keep its generated ADT, shim image, and build output separate from `sweet`. Only after a host-side build and review of the extracted values should a temporary boot test be planned.

## Device safety boundary

This is a software-porting task. Do not flash, erase, unlock, relock, or otherwise modify the phone. Any future live boot is a separate action and requires the user's explicit instruction. The current work has not run a boot command.
