# Changelog

The version follows Semantic Versioning (`MAJOR.MINOR.PATCH`). The single source
of truth is `CurVersion` in `src/aboutdialog.cpp`; the top entry below must match
it (`python3 scripts/check_version.py`).

## [1.10.2] — 2026-10-25

### Fixed
- Post-Install Check: the Windows time-sync command shipped in 1.10.1 had a broken shell quote and failed to run (it showed no verdict). Fixed; it is now tested against 9 registry fixtures, including stale copies of the record, and against the real Windows volume.

## [1.10.1] — 2026-10-25

### Fixed
- Post-Install Check: the Windows time-sync check used only the first `RealTimeIsUniversal` record it found in the offline registry, and a registry file can keep stale copies of a record from earlier edits. It now reads every copy and gives no verdict when they disagree.

## [1.10.0] — 2026-10-25

### Added
- Post-Install Check: **Windows dual-boot time sync**. If a Windows volume is mounted, the check reads its offline registry for `RealTimeIsUniversal`: macOS keeps the hardware clock in UTC and Windows in local time, so the clock is wrong after every OS switch (the Dortania guide recommends changing Windows, not macOS). The fix shows the `reg add` command to run in Windows.
- It gives no verdict when no Windows volume is mounted (not installed, encrypted with BitLocker, or just not mounted) or the registry cannot be read reliably, for example when Windows left unsaved changes in the registry logs (turn Fast Startup off).

## [1.9.1] — 2026-10-25

### Fixed
- Post-Install Check: the laptop input-driver check counted VoodooInput (or only VoodooI2CServices) alone as a working driver; it now needs VoodooPS2, the VoodooI2C core, VoodooRMI / SMBus or an Apple SPI / top-case driver.
- The trackpad check treated any digitizer (touchscreen, pen tablet) as a trackpad; only a Touch Pad digitizer or a device named trackpad / touchpad counts now.
- Removed a claim about trackpad lag that I could not confirm in the guide.

## [1.9.0] — 2026-10-25

### Added
- Post-Install Check, three new groups from the Dortania guides:
  - **CPU Power Management**: X86PlatformPlugin (XCPM) is loaded (Haswell and newer Intel; Sandy / Ivy Bridge and AMD give no verdict), and CPUFriend is not loaded without it.
  - **Hardware Decoding / Encoding (VDA)**: H.264 hardware decoding, HEVC hardware decoding and H.264 hardware encoding, read through the VideoToolbox API (the guide's VDADecoderChecker tool tests the same thing). HEVC and encoding are informational since many GPUs lack them.
  - **Trackpad and Keyboard**: a laptop input driver is loaded (VoodooPS2 / VoodooI2C / SPI), VoodooInput loads before VoodooPS2 / VoodooI2C (the guide's fix for "keyboard works but trackpad does not"), and a trackpad or pointing device is visible on a laptop.
- Laptop detection uses the SMBIOS model or a battery, so the keyboard checks give no verdict on a desktop. A mouse plugged into a laptop hides a missing trackpad (no verdict). The keyboard / trackpad checks are not yet tested on real laptop hardware.

## [1.8.3] — 2026-10-25

### Fixed
- CI: macOS workflows use the `macos-15-intel` runner, because `macos-13` has been retired and its jobs stayed queued.
- CI: MinGW workflow uses the valid `cache` input and an unpinned MinGW tool version for aqt.

## [1.8.2] — 2026-10-25

### Fixed
- CI: the MinGW workflow now uses `actions/upload-artifact@v4` and `actions/checkout@v4`; v2 is rejected by GitHub, so the job failed at setup.

## [1.8.1] — 2026-10-25

### Fixed
- CI: the macOS workflows now run on the Intel `macos-13` runner, since the Qt 5 `clang_64` builds are x86_64-only and failed to build on the arm64 `macos-latest`.
- CI: the MinGW workflow uses `windows-2022` instead of the retired `windows-2019` image, which left the job queued forever.
- CI: replaced the invalid `cached` input of install-qt-action with `cache`, and updated `actions/checkout` to v4.

## [1.8.0] — 2026-10-24

### Added
- Post-Install Check: a GPU checklist covering iGPU and dGPU, from the Dortania DRM / hardware-acceleration page. **Graphics card detected**, **Lilu and WhateverGreen loaded**, **no legacy graphics kexts** (IntelGraphicsFixup, NvidiaGraphicsFixup, Shiki), **iGPU accelerated (Metal)**, **discrete GPU accelerated (Metal)** and **an AMD discrete GPU for hardware DRM** (a limitation, not a fault). The iGPU is told apart from the dGPU by vendor first, so it does not depend on the bus line.
- The iGPU / dGPU checks give no verdict when that kind of GPU is not present, and the explanations repeat the guide's checklist (matching SMBIOS, platform-id, no needless ACPI renames).
- Replaces the single "GPU acceleration" check.

## [1.7.1] — 2026-10-24

### Fixed
- Post-Install Check: the USB map and 15-port checks treated any port with a connector type as "mapped", but macOS fills that in from the board's ACPI even without a map. They now look for an injected ports table or a loaded map kext, so an unmapped system is flagged.

## [1.7.0] — 2026-10-24

### Added
- Post-Install Check: more USB checks, based on the Dortania USB guide. **Port map** now looks at the ports themselves (a codeless map kext does not appear in the kext list), **15-port limit** (a controller with 15+ ports where some are unmapped), **USBInjectAll left in** after mapping, and **Bluetooth on an internal port** (connector 255, so Handoff and Continuity keep working).
- Every USB check reports "Unknown" when no USB data could be read, instead of passing.
- A port counts as mapped only if a port map is applied to the controller (a ports table) or a map kext is loaded. Connector types alone are not enough, since macOS also reads them from the board's ACPI.

## [1.6.1] — 2026-10-24

### Fixed
- Post-Install Check: the CPU core-count check no longer reports a problem on hybrid Intel CPUs. A report count between macOS's physical and logical counts (for example 16 cores against 8 physical / 24 logical on an i7-13700F) now gives no verdict. AMD and clearly wrong counts are still flagged.

## [1.6.0] — 2026-10-24

### Added
- Post-Install Check: CPU checks. Pick a Hardware Sniffer `Report.json` at the top of the window and two checks compare it with what macOS reports: **CPU name** (ignores the "13th Gen", "@ 3.6GHz", "n-Core" and "(R)/(TM)" decorations; variants such as i7-8700 vs i7-8700K are different CPUs) and **core count** (matches either physical cores or threads, since hybrid Intel CPUs differ). Without a report they say so instead of guessing.
- A third check needs no report: the CPU name must not be empty, and where the name states a core count (for example AMD "16-Core") it must match the cores macOS sees, which catches wrong AMD core-count patches.
- This cannot detect a CPU spoofed at the CPUID level; the explanation says so.

## [1.5.1] — 2026-10-24

### Added
- A **Post-Install Check** button on the toolbar, right after **Build EFI**, with light and dark icons that follow the theme. It opens the same dialog as Tools ▸ Post-Install Check... (macOS only; on other systems the dialog explains that it must run on the Hackintosh).

## [1.5.0] — 2026-10-24

### Added
- Post-Install Check: two more sleep/wake checks from the Dortania sleep guide. **NVMeFix** (an NVMe drive is present but NVMeFix.kext is not loaded) and **TSC sync** (a hint for server-class Xeon E5/E7/W/Scalable and X/XE Core CPUs without CpuTscSync, VoodooTSCSync or TSCAdjustReset; desktop Xeon E3 and Asus laptops are not covered).
- The failed-sleep advice now mentions the guide's RTC section ("BIOS Restarted in Safemode" on Z3xx boards). That error only appears at shutdown, so it cannot be checked from a running system and is not a separate check.

## [1.4.1] — 2026-10-24

### Fixed
- Post-Install Check: explanations now follow the Dortania sleep guide, which I read from its GitHub source. Before, several were written from memory (powernap, proximitywake, tcpkeepalive, standby), and the failed-sleep advice now lists the guide's main offenders (USB, GPUs, Thunderbolt, NICs, NVMe, CPU power management). `hibernatemode`, SIP and TRIM are not in the guide, so their text says so; for hibernation the guide uses `HibernateMode = None` in config.plist.
- The "Dual-boot other systems" step in Next steps linked to a page that returns 404; it now opens the Dortania OpenCore Multiboot guide.

## [1.4.0] — 2026-10-24

### Added
- Tools ▸ "Post-Install Check..." (macOS only): inspects the running Hackintosh against the Dortania post-install guide and lists what needs attention. Sleep/wake comes first (hibernatemode, standby, autopoweroff, powernap, proximitywake, tcpkeepalive, failed sleep/wake in the last 7 days, assertions blocking sleep), then SIP, GPU/Metal, audio, Bluetooth, Wi-Fi, USB map kext, TRIM and the SMBIOS serial.
- A failing check shows why it matters and the exact fix commands, each with **Copy** and **Run in Terminal**. Run in Terminal opens Terminal.app with the command shown and waits for Return, so you review it and enter any sudo password yourself; the app never runs a fix on its own.
- Checks are data (`src/postcheck/checks.json`), all read-only and sudo-free. A check whose output is missing or unexpected reports "Unknown" instead of guessing. "Copy report" puts the results on the clipboard for forum help.
- On Windows/Linux the dialog explains that the check must run on the Hackintosh itself.

## [1.3.0] — 2026-10-24

### Added
- Help ▸ "OpenCore Post-Install Guide" opens the Dortania guide.
- After generating an EFI, a "Next steps" window lists the post-install tasks that apply to the build (USB map, iServices, audio, power management, sleep, security, ...), each with a link to the matching guide page. The list is data-driven (`src/efibuilder/postinstall.json`; conditions: laptop, desktop, intel, amd, audio, kext:<Name>).
- EFI Builder "Finalize" options, all off by default: quiet boot (drops `-v` / debug boot-args, debug logging off), hide the boot picker (Esc/Space still shows it), and the graphical picker (OpenCanopy, skipped with a note if the driver or Resources are not in the Database).

## [1.2.0] — 2026-10-24

### Added
- EFI Builder: audio codec layout selection, as in OpCore Simplify. The codec's layouts are listed in Customize ▸ Audio (recommended authors in bold, default marked). The chosen id is written as `layout-id` on the audio controller's PCI path, or as `alcid=` when the controller is not in the report. Nothing is added if the base config already has an active `layout-id`.

## [1.1.0] — 2026-10-24

### Added
- EFI Builder reads the `ACPI` folder that Hardware Sniffer dumps next to `Report.json` (auto-detected, or pick it with "Browse ACPI..."). The tables are validated (header length, DSDT present) and used to choose SSDTs from the Database: PLUG, EC/USBX variant (laptop / non-LPCB), AWAC-DISABLE, RTC0, PMC, UNC, SBUS-MCHC, ALS0/PNLF, IMEI. An SSDT is skipped when your DSDT already defines the device, and the build summary warns when a scope such as LPCB or the processor scope is missing.
- Patches that OpCore Simplify generates with `iasl` (battery, XOSI, HPET, CMOS, GPI0, USB reset, Sandy/Ivy Bridge PM) are not generated; the summary lists the ones your hardware needs.

## [1.0.1] — 2026-10-24

### Added
- "Build EFI" toolbar button, placed just before "Browse Catalog", that opens the EFI Builder. It has its own light/dark icon (`efi.png` / `efi0.png`).

## [1.0.0] — 2026-10-24

### Added
- Material design theme (`src/theme/material.qss`) with light and dark variants, applied app-wide.
- Dark theme option; "Material design" and "Dark theme" checkboxes in Preferences (saved as `MaterialTheme` / `DarkTheme`), applied live.
- `src/theme.h`: central theme module (`Theme::apply`, `Theme::isDark`, `Theme::icon`, `Theme::listStyle`).
- EFI Builder (Edit > "EFI Builder (OpCore Simplify)..."): loads a Hardware Sniffer `Report.json`, validates it, checks per-device macOS compatibility, picks a base config / SMBIOS / kexts / boot-args, and hands off to the existing Generate EFI. Rules and datasets are ported from lzhoang2801/OpCore-Simplify (BSD-3-Clause); see `src/efibuilder/LICENSE-OpCore-Simplify.txt` and `scripts/gen_efi_data.py`.
  - Not yet ported: ACPI SSDT generation, USB mapping, Wi-Fi profile extraction, AMD vanilla kernel patches, iGPU framebuffer patching, auto-download of newer OpenCorePkg.
  - Kexts chosen for the build are downloaded automatically (Dortania build repo, direct URL, GitHub releases) into the Database `EFI/OC/Kexts` folder, using the proxy/token from Preferences.
  - Customize dialog: pick the SMBIOS model and the kext list (Edit → EFI Builder).

### Changed
- Versioning switched from the date-style `20250002` to semantic versions. The update check now compares `MAJOR.MINOR.PATCH` numerically (a leading `v` on release tags is ignored) and never treats a legacy date-style tag as newer. Release tags must use the new format, e.g. `1.0.1`.
- Main/sub list styles moved from inline C++ strings to `src/theme/list-{dark,light}.qss`.
- Toolbar icon dark/light selection deduplicated through `Theme::icon`.
- Tooltip follows the active theme and refreshes on every popup.

### Fixed
- Material theme on by default no longer overrides a dark OS appearance.
