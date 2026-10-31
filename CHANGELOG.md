# Changelog

User-visible changes of this fork against [seedds/nxnative](https://github.com/seedds/nxnative).

## 2026-10-31

### Changed

- The amiibo picker is now the **Amiibo** panel, built like the Cheats panel. **Amiibo → Show Amiibo Window** (⌘⇧A) shows it beside the game window, where it stays open after a scan and keeps its place and size. The game keeps the keyboard and the controllers; the panel takes the keyboard only while you type in its search field. The header says whether the game is looking for an amiibo. **Scan** (or a double-click, or Return in the search field) scans the selected amiibo. **Reload List** and **Open Folder** are at the bottom.

## 2026-10-26

### Added

- Virtual amiibo on the Mac host. **Amiibo → Scan Amiibo…** (⌘⇧A) opens a picker with the amiibo of the running game; a double-click or Return scans the selected amiibo. The game reads it as a figure on the controller. "Show all amiibo" lists every amiibo; "Random tag UUID" (on by default) lets a game that limits the reward per tag UUID (Breath of the Wild) give it again; Echoes of Wisdom limits it per amiibo, so the option does not repeat its reward.
- The picker downloads Ryujinx's amiibo list the first time it opens and keeps it as `amiibo_tags.json` in the amiibo folder. No other network access.
- Data that a game writes to an amiibo stays in `<amiibo ID>.json` in the amiibo folder (`DM_AMIIBO_DIR`, default `<DM_SAVE>_amiibo`), in Ryujinx's amiibo file format. Files can be copied between Ryujinx and the host.
- When the game is not looking for an amiibo, the picker says so and keeps the choice open.

## 2026-10-25

### Added

- ONE PIECE: Grand Gourmet (`01008BF025F34000`): its shaders are read from Unity 6000.0 files without asset bundles.

### Fixed

- Mac: buttons of Nintendo controllers map by their labels.
- Mac: the controller's Home and Capture buttons go to the game, not to macOS.
