# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

`cxadc` turns a cheap Conexant CX2388x TV capture card into a raw ADC: the driver
reprograms the chip's video front-end and DMAs unsigned 8-bit or 16-bit samples
(14–54 MSPS) straight to userspace via `read()` on `/dev/cxadcN`. It is used to
digitize RF from analog tape/disc, so **capture output is irreplaceable** — a
silently corrupted or discontinuous capture is worse than a failed one.

This is the `GDH-Technologies/cxadc-linux` fork of `happycube/cxadc-linux3`,
carrying GDH-specific tooling (`cx-capture`, `cxadc-status`, device aliases,
overrun accounting) and a self-hosted deploy pipeline. The upstream wiki is a
hardware reference only; installation/behavior docs here take precedence.

Cards and clockgen are normally driven by two sibling repos —
[digitization-toolkit](https://github.com/GDH-Technologies/digitization-toolkit)
(orchestration GUI, `capture-setup.yaml`) and
[capture-node](https://github.com/GDH-Technologies/capture-node) (host capture
service). Changing capture semantics, CLI flags, exit codes, or sysfs parameter
names is a cross-repo contract change.

## Build and validate

```bash
make                 # kernel module + userland tools
make module          # module only (needs kernel-devel for the running kernel)
make tools           # build/bin/{leveladj,levelmon,cx-capture}
make clean           # kernel clean + rm -rf build/
sudo make install    # C tools + cxadc-status + src/scripts/cx* -> /usr/local/bin
sudo make install-module    # modules_install + depmod
sudo make install-config    # config/cxadc.conf -> modprobe.d, cxadc.rules -> udev
```

Clockgen firmware (RP2040, needs `gcc-arm-none-eabi` + cmake; fetches pico-sdk 1.5.1):

```bash
bash clockgen/scripts/ci/ci-build.sh    # UF2 lands at clockgen/firmware/build/build/firmware.uf2
bash clockgen/scripts/ci/ci-package.sh
```

There is **no test suite**. CI (`.github/workflows/build.yml`) does: build module,
assert no build artifacts escaped `build/` and `src/kernel/`, build tools,
`py_compile` + `--plain`/`--json` smoke of `cxadc-status`, `ruff` (non-blocking).
Reproduce that gate locally before pushing:

```bash
make module && make tools
python3 -m py_compile src/tools/cxadc-status/cxadc-status
python3 src/tools/cxadc-status/cxadc-status --plain --no-color
python3 src/tools/cxadc-status/cxadc-status --json
```

Real verification needs hardware: `cxadc-status`, then `cx-capture -d cxadc0 -t 5
-o /tmp/x.u8` and check the exit code.

## Architecture

### Kernel module — `src/kernel/cxadc.c` (single file, ~1.5k lines)

- A 64 MB DMA ring (`VBI_DMA_BUFF_SIZE`, 8192 4K pages) is filled continuously by
  the chip's RISC engine; `read()` copies out of it and blocks on `readQ` until
  the IRQ handler advances `lgpcnt`. No mmap, no poll — large sequential
  blocking reads are the only interface.
- **Ring overruns**: the hardware page counter wraps with the ring, so a reader
  more than a full ring (~2.2 s @ 28.6 MSPS) behind is silently served
  overwritten pages. The IRQ handler counts writer laps into a monotonic
  `writer_total_pages`; `read()` compares it to the reader's own progress and
  bumps `overrun_count`, exposed read-only at
  `/sys/class/cxadc/cxadcN/device/parameters/overrun_count` and reset on each
  `open()`. This is the sample-continuity guarantee — preserve it.
- Parameters are **per-device `device_attribute`s** in an attribute group named
  `parameters` (not `module_param` sysfs), hence the
  `/sys/class/cxadc/cxadcN/device/parameters/<name>` path. Module params of the
  same name only supply boot defaults. `level`, `sixdb` and `center_offset` are
  re-applied on every `read()` loop, which is what lets gain change mid-capture.
- One card = one open (`in_use` → `-EBUSY`). Up to 256 cards.
- Sample rate: `tenxfsc` < 10 selects legacy fixed PLL/SCONV register pairs;
  ≥ 10 is treated as MHz (11–99) or Hz and computes PLL int/frac against
  `crystal`, clamped to the safe `PLLfin` window.

### Userland — `src/tools/`

`common/` is a small shared C library linked into all three C tools:

- `utils.c` — sysfs get/set plus `cxadc_resolve_device()`, the **single place**
  device inputs are resolved. Every tool and script accepts `cxadc0`, `0`,
  `/dev/cxadc0`, `/dev/cx/vcr0-video`, or a bare alias `vcr0-video`, and
  resolves back to the canonical `cxadcN` before touching sysfs. Names are
  validated against path traversal. Add new tools through this, never by
  hand-building sysfs paths.
- `cx_analyze.c` — one-pass 256-bin histogram over sample MSBs producing
  min/max/percentile/clip/DC stats, plus `cx_suggest_level()` and a sign-search
  DC-offset controller (`center_offset` transfer sign is undocumented).
- `cx_clockgen.c` — finds the RP2040 clockgen ALSA card (`CXADCADCClockGe`) and
  sets its clock enum via `amixer` through `execvp` (no shell).

Tools:

- `cx-capture` — the capture entry point. Fans one 1 MiB read block out to up to
  16 sinks split into **reliable** (`-o` file, `--stdout`: blocking, lossless)
  and **monitor** (`--tcp`, `--listen`, `--fifo`: non-blocking, drop on
  backpressure) so a slow viewer can never stall the reader into an overrun.
  Exit codes are a contract: `0` clean, `1` I/O error, `2` usage, **`3` capture
  completed but the driver reported overruns (samples lost)**.
  `--auto_adjust` is opt-in because it steps gain mid-stream.
- `leveladj` / `levelmon` — one-shot gain/DC adjust and live level monitor.
- `cxadc-status` (Python, no hard deps; `rich` optional) — read-only summary of
  driver, cards, parameters, holders (via `/proc/*/fd`) and clockgen.

`src/scripts/cx*` are thin bash wrappers over the same sysfs parameters
(`cxfreq`, `cxlevel`, `cxvalues`, `cxresolve`, `cxlvlcavdd`, …); they are
installed alongside the C tools and share the device-input forms above.

### Build layout

Kbuild cannot emit objects outside the module source dir, so kernel artifacts
land in `src/kernel/` (git-ignored) and `cxadc.ko` is copied to `build/`. All
userland output goes to `build/bin` and `build/obj`. CI fails if artifacts appear
anywhere else — don't relocate object rules.

## Constraints that will bite you

- **Version marker lives in three files and CI enforces agreement**: `dkms.conf`
  `PACKAGE_VERSION`, the `version N.N` string in the `src/kernel/cxadc.c` header
  comment, and README's `Current DKMS package version: \`N.N\``. Both
  `deploy.yml` and `src/scripts/deploy_dkms_install.sh` parse all three and abort
  on mismatch. Bump all three together (currently `1.1`).
- **`cxadc-status` must never open `/dev/cxadcN`** — it runs during live
  captures; sysfs/procfs/ALSA-read-only only.
- **Never rename or repurpose sysfs parameters** under
  `/sys/class/cxadc/*/device/parameters/` — udev rules, sibling repos and every
  script bind to those names.
- `cxadc` and stock `cx88*` drivers must not be loaded together; `config/cxadc.conf`
  blacklists them and `cxadc-status` reports conflicts.
- Never assume Secure Boot is disabled in docs or scripts.
- Driver and capture-path changes are data-integrity changes: keep them minimal,
  isolated and reviewable, and update README in the same commit when defaults,
  flags or workflows change.

## Deployment

`deploy.yml` pushes `master` to self-hosted runners `cs0`, `cs1`, `wm`. Each host
has this repo checked out at `/home/rdodge/Repos/cxadc-linux`; the job refuses to
run on a dirty tree or wrong branch, builds as the runner user, then calls
`src/scripts/deploy_dkms_install.sh` as root (path is hardcoded there **and** in
the `/etc/sudoers.d/cxadc-linux-deploy` command aliases — changing the script
path or name breaks passwordless sudo). Post-deploy it asserts DKMS is
`installed` (not merely `built`), that `modinfo -n cxadc` resolves under
`updates/dkms/` **or** `extra/` (dkms.conf asks for the former; Fedora's DKMS
overrides it and installs to the latter), and that vermagic matches the running
kernel.

The sudoers alias also pins the **env var list**: `deploy.yml` invokes
`run_as_root env DKMS_TARGET_KERNEL=… DKMS_INSTALL_ALL_KERNELS=…
PRUNE_OLD_DKMS_VERSIONS=… bash …`, and sudo matches that command line
positionally. Adding, removing or reordering a variable there drops the NOPASSWD
match and every rig starts prompting for a password. `DKMS_PRUNE_DRY_RUN` exists
in the script for exactly this reason but is deliberately *not* in the CI path.

`DKMS_INSTALL_ALL_KERNELS` and `PRUNE_OLD_DKMS_VERSIONS` both **default on**, in
the script and in the `${VAR:-true}` fallbacks in `deploy.yml` (the fallbacks are
what govern `workflow_run` deploys, where `inputs.*` is empty — changing only the
input defaults is a no-op for automatic deploys). Together they converge every
kernel in `/lib/modules` onto exactly one version and remove the rest: old DKMS
versions, their `/usr/src/cxadc-<ver>` trees, state for absent kernels, and
unowned `cxadc.ko*`. **Order is the safety property** — the prune is gated on
every bootable kernel already carrying the current version, verified from `dkms
status` rather than from what the build loop thinks it did. Pruning first would
strip the driver from fallback kernels. Don't reorder those two phases.

Module reload is **opt-in** (`reload_module` dispatch input) because unloading
`cxadc` kills any capture in flight. Prefer landing a DKMS build and letting the
next reboot pick it up.

## Conventions

- Plan files from the `Plan` agent go in `.github/plans/`, never a temp dir.
- `.github/copilot-instructions.md` is the sibling agent-instruction file; keep
  it in sync when the constraints above change.
- Commits follow `type: summary` (`feat:`, `fix:`) loosely; work lands via PR to
  `master`.
