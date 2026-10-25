# Changelog

User-visible changes of this fork against [seedds/nxnative](https://github.com/seedds/nxnative).

## 2026-10-26

### Added

- Virtual amiibo on the Mac host. **Amiibo → Scan Amiibo…** (⌘⇧A) opens a picker with the amiibo of the running game; a double-click or Return scans the selected amiibo. The game reads it as a figure on the controller. "Show all amiibo" lists every amiibo; "Random tag UUID" (on by default) lets a game give its daily amiibo reward again.
- The picker downloads Ryujinx's amiibo list the first time it opens and keeps it as `amiibo_tags.json` in the amiibo folder. No other network access.
- Data that a game writes to an amiibo stays in `<amiibo ID>.json` in the amiibo folder (`DM_AMIIBO_DIR`, default `<DM_SAVE>_amiibo`), in Ryujinx's amiibo file format. Files can be copied between Ryujinx and the host.
- When the game is not looking for an amiibo, the picker says so and keeps the choice open.

## 2026-10-25

### Added

- ONE PIECE: Grand Gourmet (`01008BF025F34000`): its shaders are read from Unity 6000.0 files without asset bundles.

### Fixed

- Mac: buttons of Nintendo controllers map by their labels.
- Mac: the controller's Home and Capture buttons go to the game, not to macOS.
