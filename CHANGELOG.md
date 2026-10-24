# Changelog

Project versioning uses the date-style `CurVersion` in `src/aboutdialog.cpp`
(also compared against GitHub release tags for update checks), so it is left
untouched here; entries below are listed under Unreleased.

## [Unreleased] — 2026-10-24

### Added
- Material design theme (`src/theme/material.qss`) with light and dark variants, applied app-wide.
- Dark theme option; "Material design" and "Dark theme" checkboxes in Preferences (saved as `MaterialTheme` / `DarkTheme`), applied live.
- `src/theme.h`: central theme module (`Theme::apply`, `Theme::isDark`, `Theme::icon`, `Theme::listStyle`).

### Changed
- Main/sub list styles moved from inline C++ strings to `src/theme/list-{dark,light}.qss`.
- Toolbar icon dark/light selection deduplicated through `Theme::icon`.
- Tooltip follows the active theme and refreshes on every popup.

### Fixed
- Material theme on by default no longer overrides a dark OS appearance.
