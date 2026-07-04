---
applyTo: "**"
---
# AGENTS.md

Agent instructions for cxadc-linux.

## Scope

This repository contains:

- Linux kernel module (`cxadc`) under `src/kernel/`
- Userland tools under `src/tools/`: `leveladj`, `levelmon`, `cx-capture` (C),
  `cxadc-status` (Python), plus a shared C library in `src/tools/common/`
- Convenience shell scripts under `src/scripts/`
- Config files (modprobe/udev/systemd) under `config/`
- Clockgen firmware/scripts under `clockgen/`
- All userland build output goes under `build/` (git-ignored); kernel build
  artifacts are produced under `src/kernel/` (git-ignored) and `cxadc.ko` is
  copied to `build/`.

Prefer conservative, reviewable changes. Driver and capture behavior changes can impact data integrity.

Primary references:

- Project overview and installation: [README.md](../README.md)
- Utility docs: [src/scripts/README.md](../src/scripts/README.md)
- Clockgen docs: [clockgen/README.md](../clockgen/README.md), [clockgen/BUILD.md](../clockgen/BUILD.md)

## Cross-Repo Integration Context

CX cards and clockgen devices are primarily operated as part of the GDH capture
stack:

- [digitization-toolkit](https://github.com/GDH-Technologies/digitization-toolkit)
	provides the capture orchestration GUI and workflow context.
- [capture-node](https://github.com/GDH-Technologies/capture-node) owns the
	host-side capture lifecycle and hardware access.

When changing user-facing behavior, capture semantics, or parameter contracts,
keep docs and assumptions aligned with:

- [`capture-setup.yaml` in digitization-toolkit](https://github.com/GDH-Technologies/digitization-toolkit/blob/main/capture-setup.yaml)
- [`capture-node` service/API behavior](https://github.com/GDH-Technologies/capture-node/blob/main/README.md)

## Build and Validate

Kernel module and tools (module built in `src/kernel/`, tools in `build/bin/`):

```bash
make
```

Install userspace tools + helper scripts:

```bash
sudo make install
```

Install the kernel module:

```bash
sudo make install-module
```

Clean artifacts:

```bash
make clean
```

## Important Constraints

- Never assume Secure Boot is disabled; keep docs/instructions explicit.
- Do not load `cxadc` and stock `cx88` drivers at the same time.
- Preserve sysfs parameter compatibility and naming in `/sys/class/cxadc/*/device/parameters/`.
- Avoid disruptive behavior in tools that might interfere with active capture sessions. In particular, diagnostic tools (`cxadc-status`) must never open `/dev/cxadcN`; inspect sysfs/procfs only.
- Keep DKMS installation instructions consistent with README (current package version is `0.5`).

## Clockgen Notes

When touching `clockgen/` flows:

- scripts are in `clockgen/scripts`
- firmware sources are in `clockgen/firmware`
- helper scripts should resolve firmware via either `$WORKSPACE/firmware` or `$WORKSPACE/../firmware`
- current firmware UF2 output path is `clockgen/firmware/build/build/firmware.uf2`

Local smoke commands used in this workspace:

```bash
bash clockgen/scripts/ci/ci-build.sh
bash clockgen/scripts/ci/ci-package.sh
```

## Plan Output Location

- For the builtin `Plan` agent, create and save plan files under `$WORKSPACE/.github/plans`.
- Do not use temporary locations for plan artifacts.
- If the directory does not exist, create `$WORKSPACE/.github/plans` before writing plan files.

## Coding and Change Hygiene

- Keep kernel-facing changes isolated and minimal.
- Keep userland tools simple C programs with clear CLI behavior.
- Update README/docs in the same change when defaults, flags, or workflows are altered.
