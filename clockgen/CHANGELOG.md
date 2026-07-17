# Changelog

Firmware / software changes, most recent first. Versions are git tags
(`vX.Y.Z`, see the Versioning section of [README.md](README.md)); CI stamps
the tag into the USB device descriptor (`bcdDevice`) and the `version`
console command.

## Unreleased

- Runtime-selectable channel count via UAC2 alternate settings:
  `arecord -c 2` streams ADC L/R only (new default), `-c 3` adds the
  head-switch channel. Previously the stream was fixed at 3 channels.
- Idle power management: PCM1802 (PDWN), its PIO capture and the Si5351
  CLK2 master clock are gated off whenever no stream is open and on USB
  suspend. CXADC clocks (CLK0/CLK1) are never gated.
- DMA-driven capture path replaces the core1 busy-poll of the PIO RX FIFO.
- CDC ACM serial console with `help` / `version` / `clocks` / `power` /
  `status` commands.
- LED status patterns: solid = streaming, short blip = idle/powered down,
  2 Hz blink = Si5351 init failure (unchanged).
- New `global_status` fields `adc_powered`, `usb_alt_setting`,
  `adc_power_cycles` (appended; existing field offsets unchanged).
- `capture-vhs.sh` records 2 channels by default (`CLOCKGEN_CHANNELS=3` to
  include the head-switch channel).
