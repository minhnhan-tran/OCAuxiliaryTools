# Changelog

The version follows Semantic Versioning (`MAJOR.MINOR.PATCH`). The single source
of truth is `CurVersion` in `src/aboutdialog.cpp`; the top entry below must match
it (`python3 scripts/check_version.py`).

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
