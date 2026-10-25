# Changelog

The version follows Semantic Versioning (`MAJOR.MINOR.PATCH`). The single source
of truth is `CurVersion` in `src/aboutdialog.cpp`; the top entry below must match
it (`python3 scripts/check_version.py`).

## [1.17.0] — 2026-10-25

### Added
- Post-Install Check: 4 new checks taken from the Dortania OpenCore Post-Install guide.
  - Clover leftovers: Clover's NVRAM scripts (rc.clover.lib and the rc.boot.d / rc.shutdown.d files) are still installed (NVRAM page).
  - ROM matches en0: the NVRAM ROM equals the en0 MAC address, which iMessage/FaceTime prefer (iServices page). Skipped when there is no en0 or no ROM.
  - Battery recognised: the firmware has an ACPI battery but macOS has no AppleSmartBattery; points to ECEnabler and SMCBatteryManager (battery page). Skipped on Macs without a battery.
  - RestrictEvents on a Mac Pro SMBIOS: the memory notification fix (memory page). Skipped on other models.

## [1.16.1] — 2026-10-25

### Fixed
- The USB Mapping toolbar icon was a wide, thin shape that looked small next to the other icons. It is now an upright, bolder USB symbol that fills the icon and stays readable at toolbar size.

## [1.16.0] — 2026-10-25

### Added
- Toolbar button "USB Mapping", right after "Post-Install Check". It opens the same window as Tools > USB Mapping, and has its own icon in light and dark themes.

## [1.15.2] — 2026-10-25

### Fixed
- USB Mapping: at the default window size a horizontal scrollbar appeared because the columns were slightly wider than the table. The columns are a little narrower and the window a little wider, so everything fits.

## [1.15.1] — 2026-10-25

### Changed
- USB Mapping: the "Devices seen" column now shows each device as a coloured chip by kind (hub, Bluetooth, input, storage, media, other), and rows that have a device are tinted with a bold port name so the real ports stand out. The column is sized to fit the longest row.

### Fixed
- USB Mapping: text in this window lost the space after a comma or full stop ("1.Click", "controller(s),15 port(s)") because of kerning at the app's 12 px font. Kerning is now off for this window.

## [1.15.0] — 2026-10-25

### Added
- **Tools > USB Mapping**: a native C++/Qt port of [corpnewt/USBMap](https://github.com/corpnewt/USBMap) (MIT, licence in `src/usbmap/LICENSE-USBMap.txt`), so no Python or Terminal is needed.
  - **Discover ports** reads `ioreg` and lists every USB controller and port, with its class, detected connector type and the devices plugged into it. Run it again after plugging a device into another port: ports with a device are remembered across runs.
  - Tick the ports to keep (a warning appears above 15 per controller, macOS's limit), pick a connector type (USB 2/3 Type-A, Type-C variants, Internal) and give each port a nickname.
  - **Build USBMap.kext** writes the kext (`AppleUSBHostMergeProperties`, or the legacy `AppleUSBMergeNub` for macOS 10.11 - 10.14). The choices are saved to `~/.ocat/USBMap/map.json` and reloaded next time. **Load ioreg dump** works from a saved dump on any OS.
- The generated `Info.plist` was checked against USBMap's own output on the same `ioreg` data and is identical, including with a port nickname, a connector override and a disabled port.

### Not included yet
- Adding the kext to `EFI/OC/Kexts` and `config.plist` for you (the dialog tells you the steps), and hub mapping on old EHCI machines (written but untested without hardware).

## [1.14.1] — 2026-10-25

### Fixed
- Post-Install Check: the LAN router and internet checks asked macOS for "the" default route, which with some VPNs is a tunnel that has no gateway address, so a working network was reported as "not connected" (or the internet check was skipped). They now use the first IPv4 default route that has a real gateway.
- The Ethernet-driver explanation said Aquantia controllers are supported natively; the kext guide lists AppleEthernetAquantiaAqtion.kext for them, so it now says that.

## [1.14.0] — 2026-10-25

### Added
- Post-Install Check: a new **LAN** group with five checks.
  - **The Ethernet controller has a working driver** (Dortania kext guide): fails when the PCI bus has an Ethernet controller but no network interface was created for it. The fix lists the controller's vendor:device ID, its vendor name and any Ethernet kexts that are loaded.
  - **en0 exists and is marked built-in** (Dortania "Fixing en0"): Apple services need it. The fix shows en0 and the network ports, and the note explains the network-settings reset and the `built-in` property.
  - **The wired link runs at gigabit speed or faster, full duplex**, **the router answers** (ping and ARP), and **DNS works and the internet is reachable**. These three are general advice, not from the guide, and say so. The internet check loads Apple's own connectivity page (captive.apple.com) and nothing else.
- Each check has a **Run here** script that shows what the check saw. They only read information.
- The checks are Skipped when they do not apply: no wired controller, no cable, or no network connection at all.

## [1.13.1] — 2026-10-25

### Fixed
- Post-Install Check: the USB and NVMe scripts dumped the whole IORegistry subtree (up to 12,000 characters), but the window shows only the first 1,500, so the lines that mattered (the loaded USB map kexts, "NVMeFix loaded") were cut off. They now print just the controllers, port counts and the kext list, and every script's output fits.

## [1.13.0] — 2026-10-25

### Added
- Post-Install Check: almost every problem now has a **Run here** script next to the link to the guide page, so the window no longer ends at "open the guide".
  - **Serial number:** generates a valid serial / MLB for this Mac's SMBIOS model with the bundled macserial, plus a SmUUID.
  - **Diagnostics:** the GPU, audio, USB (ports and map kexts), NVMe, CPU / XCPM, SIP, Wi-Fi, Bluetooth and keyboard / trackpad problems each get a script that lists the IDs and loaded kexts you need to choose the right fix. Wi-Fi, Bluetooth and the hardware lists say plainly when nothing is found.
- These scripts only read information. Fixes that change your EFI or `config.plist` still have to be made there; the app does not edit them from this window. The sleep and power fixes that change settings keep asking for confirmation and your password.

## [1.12.1] — 2026-10-25

### Fixed
- Post-Install Check: the laptop test behind the keyboard/trackpad checks relied on a MacBook model name or a working battery driver, so a laptop that uses an iMac/Mac mini SMBIOS (common on Hackintosh) without a battery kext was wrongly Skipped as "not a laptop". It now also counts the firmware's ACPI lid or battery device (PNP0C0D / PNP0C0A).

## [1.12.0] — 2026-10-25

### Added
- Post-Install Check: a new **Skipped** result (grey) for checks that do not apply to this Mac, instead of "Unknown". The summary counts them separately ("N skipped").

### Changed
- Trackpad and Keyboard: "A laptop keyboard / trackpad driver is loaded" and "A trackpad is detected" are now Skipped on a desktop (no battery or laptop model), and "VoodooInput loads before VoodooPS2 / VoodooI2C" is Skipped when neither of those is loaded. Each one shows why it was skipped.

## [1.11.2] — 2026-10-25

### Changed
- New app icon (a blue-to-teal ring) on every platform: `Icon.icns` (macOS, all sizes from 16 to 1024), `my.ico` (Windows, 16 to 256) and `icon.png` (Linux, and the About / splash image). The background is transparent.

## [1.11.1] — 2026-10-25

### Fixed
- Post-Install Check: closing the window while a fix was still running (for example with the password prompt open) let the finished process call into the half-destroyed dialog. The window now stops the running fix and disconnects it when it closes.

## [1.11.0] — 2026-10-25

### Added
- Post-Install Check: a **Run** button on fixes that are safe to run from the window. It first shows the exact command (and, for power settings, the value they have now), asks you to confirm, and then runs it. Power-management fixes (`pmset -a <setting> <number>`) use macOS's own administrator password prompt, and the check is re-run afterwards so you can see whether it now passes. Read-only diagnostic fixes run without a password and show their output.
- Nothing else can be run this way: the app only accepts `pmset -a <setting> <number>` as an admin command, whatever the data says (the unit tests cover shell tricks such as `;`, `&&`, `$()` and newlines). `trimforce`, which asks for confirmation and restarts, stays in Terminal.

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
