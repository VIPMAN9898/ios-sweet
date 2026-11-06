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

A OnePlus OxygenOS 10 OTA (`OnePlus5Oxygen_23_OTA_069_all_2010292138_6700.zip`) was inspected using HTTP range reads; the 22,345,000-byte `boot.img` was extracted to `/tmp/a5000-firmware/boot.img` and passed the archive CRC check. It has Android boot header v0. The gzip kernel contains 20 embedded Qualcomm MSM8998 MTP reference DTBs, not a `oneplus,cheeseburger` board tree. The full OTA was not downloaded or installed. Keep this artifact in temporary storage; it is not a boot image for the iOS port.

The available reference sources narrow the target, but do not yet establish the live bootloader tree:

- The [upstream OnePlus 5 DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/msm8998-oneplus-cheeseburger.dts) sets compatible `oneplus,cheeseburger` / `qcom,msm8998` and board ID `<8 0 16859 23>`. OTA DTB entry 10 has the same board-ID tuple, while its root still identifies as a Qualcomm MSM8998 MTP base tree.
- The [OnePlus common DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/msm8998-oneplus-common.dtsi) describes the bootloader framebuffer at `0x9d400000`, size `0x2400000`, 1080×1920, stride 4320, `a8r8g8b8`.
- The [MSM8998 SoC DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/msm8998.dtsi) describes eight Kryo 280 cores and PSCI via SMC. OTA DTB entry 10 lists GICD `0x17a00000`, GICR `0x17b00000`, and UFS controller `0x1da4000`.

Treat these as reference values only. Before using them in a build, obtain or read the A5000's actual bootloader FDT/overlay and verify its RAM banks, reservations, and controller handoff state. Keep private dumps and serial numbers out of Git. Do not add guessed hardware values.

`tools/a5000-dtb-inspect.py` is the host-side intake tool for that evidence. It
summarizes RAM, reserved memory, CPUs, PSCI, interrupt controllers, UFS, and
display nodes. By default it exits with a rejection unless the root compatible
contains `oneplus,cheeseburger`; `--allow-reference` is only for inspecting a
comparison tree. Running it on OTA DTB 10 reports the matching board-ID tuple,
but the root is still Qualcomm MSM8998 MTP and the tree is not build-ready.

From that DTB and boot-chain evidence, record only verified values for:

1. RAM banks and reserved regions, including the bootloader framebuffer and payload placement constraints.
2. CPU topology, PSCI conduit, interrupt controller, timers, and GIC redistributor layout.
3. UFS controller state at handoff and the A5000 device-tree nodes needed to read storage.
4. Display geometry and the bootloader framebuffer format/address.
5. PMIC, key, and other hardware needed for early diagnostics.

Then create a `cheeseburger` target profile and keep its generated ADT, shim image, and build output separate from `sweet`. Only after a host-side build and review of the extracted values should a temporary boot test be planned.

## Device safety boundary

The user asked to build the A5000 target and start flashing on 2026-11-06. No boot or flash command has been run. Do not use `sweet` artifacts on the OnePlus 5. Before any persistent partition write, follow `AGENTS.md` and confirm the exact partition, image, and data impact immediately beforehand; start with a temporary boot only after an A5000-specific image has been built and reviewed.
