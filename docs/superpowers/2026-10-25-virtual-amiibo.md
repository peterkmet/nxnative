# Virtual amiibo on the Mac host

## Intent

The player owns no amiibo figures, but wants the rewards that games give for them. The Mac host acts as an NFC reader with a virtual amiibo on it: the player picks an amiibo from a list, and the game reads it as if a figure touched the controller. The model is Ryujinx's virtual amiibo (Actions → Scan Amiibo): no figure dumps and no amiibo keys, only the amiibo ID.

First game: The Legend of Zelda: Breath of the Wild (`01007EF00011E000`, profile in `dm/games/`). Second check: The Legend of Zelda: Echoes of Wisdom (`01008CF01BAAC000`), which has no profile in the repository; it runs with a provisional profile from `DM_GAMES_DIR` and is checked by hand only. Both link the full `nn::nfp` library in `sdk` (Initialize, ListDevices, StartDetection, Attach(De)ActivateEvent, GetTagInfo, Mount, GetModelInfo, GetCommonInfo, GetRegisterInfo, Open/Get/Set/CreateApplicationArea, Flush, Unmount, StopDetection and others; read from the symbol names in the patched modules).

## Current state

- `dm/host/hle_sdk16.c:166` takes over the `nn::nfp` family. It reports one NFC device (`NFP_DEVICE_ID`) on the connected controller. `StartDetection` sets state 1 (searching), and detection never finds a tag: every call that needs a tag returns `NFP_RES_TAG_NOT_FOUND` (module 115, description 97). The activate and deactivate events are bound to kernel events that are never signalled. Super Mario (`hle_sdk16.c:170`) attaches no events and polls `GetDeviceState`.
- `hle_mii.c:33` builds the default Mii `CharInfo` (0x58 bytes) in the static `mii_default`; the file exports only `dm_mii_standin`.
- `hle_account.c:50-55` gives the user nickname (`DM_NICK`, default `Player`).
- `dm.h:99` `hle_signal_system_event` signals a bound guest SystemEvent; `dm.h:102` `hle_signal_bound_system_event` is for an event that may not be bound.
- `$DM_SAVE` is the committed save directory of the save transaction (`hle_fs.c:35-36`): at mount the host clones it to `<DM_SAVE>.work`, and at Commit it replaces `<DM_SAVE>`. `hle_fs.c:482` keeps device data in the sibling `<DM_SAVE>_device`.
- The Mac front end (`dm/host/front_mac.m`) has a main menu (`front_mac.m:257`). When the game window resigns key, `front_mac.m:288` releases the input and sets focus state 2 (focus lost).
- `game_profile.h` has `DM_GAME_ID`, the title ID as a string (`dm/games/game.py:138`).
- The host makes no network connection today (`hle_offline.c`).

## Reference: Ryujinx

Read from the Ryujinx source (MIT): `src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/VirtualAmiibo.cs`, `NfpManager/INfp.cs`, `ResultCode.cs`, `NfpManager/Types/NfpDeviceState.cs` (mirror `NextendoNetwork/Ryujinx-Nextendo`) and `src/Ryujinx/UI/ViewModels/AmiiboWindowViewModel.cs`.

- Device states (`NfpDeviceState`): Initialized 0, SearchingForTag 1, TagFound 2, TagRemoved 3, TagMounted 4, Unavailable 5, Finalized 6. StartDetection → 1; a scan → 2; Mount → 4; Unmount → 2; StopDetection → 0.
- Events (`INfp.cs:150-170`): while the state is TagFound, a loop signals the activate event, waits 125 ms and signals the deactivate event. The state stays TagFound. StopDetection signals nothing.
- Results (module 115; `ResultCode.cs`): DeviceNotFound 64, WrongArgument 65, WrongDeviceState 73, NfcDisabled 80, TagNotFound 97, ApplicationAreaIsNull 128, ApplicationAreaAlreadyCreated 168. The code is `(description << 9) | 115`.
- Amiibo ID: 16 hex digits, `head` + `tail` from the list. `GetModelInfo`: character ID = digits 0–3 (byte-swapped), character variant = digits 4–5, figure type = digits 6–7, model number = digits 8–11, series = digits 12–13.
- `GetTagInfo`: a 9-byte UUID (`uuid[3] = 0x88 ^ uuid[0] ^ uuid[1] ^ uuid[2]`, `uuid[8] = uuid[3] ^ uuid[4] ^ uuid[5] ^ uuid[6]`), protocol and tag type `0xffffffff`. The UUID is random once and stored per amiibo, or random on every scan (`UseRandomUuid`).
- `GetCommonInfo`: last write date, write counter, version 1, application area size 0xD8. `GetRegisterInfo`: the default Mii with the user's name as its nickname, first write date, the amiibo's nickname as UTF-8 bytes, font region 0.
- Application area: one per amiibo, keyed by the area ID the game opens. Open with no such area and Create when one exists fail; Set replaces the bytes. The write counter and the dates never change; Flush does nothing.
- Storage: one JSON file per amiibo, `<amiibo ID>.json`, with FileVersion, TagUuid (base64), AmiiboId, FirstWriteDate, LastWriteDate (System.Text.Json DateTime, ISO 8601, for example `2024-05-01T12:34:56.789+02:00`), WriteCounter, ApplicationAreas (ApplicationAreaId, ApplicationArea as base64) and NickName.
- The list: `https://raw.githubusercontent.com/Ryubing/Nfc/refs/heads/main/tags.json` (857 amiibo, each with `name`, `amiiboSeries`, `head`, `tail`, `image` and `gamesSwitch[].gameID`). The window shows the amiibo whose `gamesSwitch` names the running game (27 for BotW, 28 for Echoes of Wisdom), with a "show all" switch.

## Goals

1. The player opens the picker during the game, chooses an amiibo of the running game, and the game reads it as a scanned figure.
2. Each nfp call that needs a tag answers from the virtual amiibo, with the states, the events and the results of the Ryujinx model.
3. Data that the game writes to the amiibo (the application area) stays from run to run, in files compatible with Ryujinx's amiibo JSON files.
4. A random tag UUID on every scan, so that games that limit scans per figure and day (BotW) give the reward again.

## Non-goals

- Real NFC readers and real figure dumps (`.bin`), amiibo keys, encryption.
- The iPhone front end. The core stays in C without AppKit, so that a later iPhone picker can use it.
- Amiibo images in the picker.
- The amiibo settings applets (`StartNicknameAndOwnerSettings`, `StartGameDataEraser`, `StartRestorer`, `StartFormatter`): they keep returning TagNotFound.

## Anti-goals

- A game that does not use amiibo changes its behavior.
- The picker steals keys or controller input from the game while it is closed.
- A failed list download or a bad amiibo file stops the game.

## Behavior

```gherkin
Feature: Virtual amiibo

  Scenario: Scan an amiibo the game supports
    Given the game called StartDetection
    When the player chooses "Wolf Link" in the picker
    Then GetDeviceState returns 2 (tag found)
    And the activate event is signalled, then the deactivate event 125 ms later
    And GetModelInfo after Mount returns the IDs of Wolf Link

  Scenario: Scan while the game does not search
    Given the game did not call StartDetection
    When the player chooses an amiibo
    Then the device state does not change
    And the host writes one log line that the game is not searching for a tag

  Scenario: Data the game writes stays
    Given the game created an application area on "Wolf Link" and wrote data to it
    When the player restarts the game and scans "Wolf Link" again
    Then GetApplicationArea returns the data that the game wrote

  Scenario: Detection stopped
    Given an amiibo is scanned
    When the game calls StopDetection
    Then the device state is 0 (initialized)
    And the next scan needs a new choice in the picker

  Scenario: Random tag UUID
    Given "Random tag UUID" is on
    When the player scans "Wolf Link" twice
    Then GetTagInfo returns two different UUIDs
    Given "Random tag UUID" is off
    When the player scans "Wolf Link" twice
    Then GetTagInfo returns the UUID stored in the amiibo file both times

  Scenario: Call in the wrong state
    Given no amiibo is scanned
    When the game calls Mount
    Then Mount returns WrongDeviceState

  Scenario: List not available
    Given the list download fails and no cached list exists
    When the player opens the picker
    Then the picker says that the list is not available
    And the game continues
```

## Invariants

- `GetDeviceState` returns 2 or 4 only after a `dm_amiibo_scan` that followed the last StartDetection.
- With the picker never opened, the host makes no network request, and every nfp call answers as before this change.
- A file that the host writes parses as a Ryujinx `VirtualAmiiboFile`. A Ryujinx file read and written by the host keeps every field.
- No amiibo file lies inside a save mount.

## Decisions

- **Model:** Ryujinx's virtual amiibo, ported to C. Its state machine, events, result codes, field layout, UUID rule and JSON file format are the reference. Reason: the user asked for it as in Ryujinx.
- **Core and front end apart:** the nfp state and the amiibo file store go into a new `dm/host/hle_nfp.c` (C, no AppKit), which replaces the nfp block in `hle_sdk16.c`. The Mac picker goes into `front_mac.m` and calls one core function, `dm_amiibo_scan(const char *amiibo_id, const char *name, int random_uuid)`. One mutex in `hle_nfp.c` guards the nfp state; `dm_amiibo_scan` and each nfp call take it. Reason: the host keeps HLE families in `hle_*.c`, the picker runs on the AppKit main thread and the nfp calls on guest threads.
- **Device states and results:** the Ryujinx values above. Mount in a state other than 2 or 4 returns WrongDeviceState. GetModelInfo, GetCommonInfo, GetRegisterInfo and the application-area calls in a state other than 4 return WrongDeviceState; GetTagInfo in a state other than 2 or 4 returns WrongDeviceState. Mount with model type ≠ 0 or `(target & 3) == 0` returns WrongArgument. Create, Recreate and SetApplicationArea with a size over 0xD8 return WrongArgument before any allocation. OpenApplicationArea with no area of that ID and GetApplicationArea with an empty area return ApplicationAreaIsNull; CreateApplicationArea when the area exists returns ApplicationAreaAlreadyCreated. A device handle other than `NFP_DEVICE_ID` returns DeviceNotFound. Each value is checked against `ResultCode.cs` before use.
- **Events:** after a scan, a host thread signals the activate event, waits 125 ms and signals the deactivate event, once per scan (Ryujinx repeats the pair while the state is TagFound; one pair is enough for the game to see the tag, and the plan checks it against BotW's flow). The host signals with `hle_signal_bound_system_event`; a game that attached no event sees only the state change.
- **Tag lifetime:** a scan is valid until StopDetection (state 0) or Finalize. StopDetection signals nothing.
- **Scan while not searching:** the host ignores the scan and writes one log line. The menu item is always enabled; while the state is not 1, the picker shows a note that the game is not looking for an amiibo, and a scan leaves the picker open. A scan in state 2 is ignored too: a second amiibo needs StartDetection again (Ryujinx places a tag only while SearchingForTag).
- **Focus with the picker:** the flag that keeps focus state 2 back is cleared when the picker resigns key. Each resign-key (game window or picker) decides on the next main-loop turn and reports focus 2 only when neither the game window nor the picker is key. Input is released at once.
- **FileVersion:** the host keeps the value it reads and writes it back; a new file gets 0.
- **Picker:** a menu "Amiibo" in the main menu bar of the host window, item "Scan Amiibo…" with ⌘⇧A. The picker is an NSPanel. It has a search field, a table of names and series, a "Show all amiibo" checkbox and a "Random tag UUID" checkbox. Double-click or Return scans the selected amiibo and closes the picker. The game keeps running while the picker is open. The picker takes key focus, so keys typed into it do not reach the game; the host releases the game's input (as today) but does not report focus state 2 while the picker is the key window. Reason: like Ryujinx's Actions → Scan Amiibo; a ⌘ shortcut goes through the menu (`front_mac.m:257`); BotW may pause or leave its amiibo screen on focus loss.
- **List:** `front_mac.m` downloads Ryujinx's `tags.json` with NSURLSession the first time the picker opens and no cached copy exists, and parses it with NSJSONSerialization. A download over 16 MB or one that does not parse is dropped. The cache `amiibo_tags.json` in the amiibo folder is written to a temp file and renamed only after the new file parses. The player deletes the cache to get a new list. The list is not committed to the repository: it has no licence. Reason: Ryujinx downloads it too.
- **Game filter:** the list shows the amiibo whose `gamesSwitch[].gameID` equals `DM_GAME_ID`, compared case-insensitively; "Show all amiibo" shows all of them.
- **Amiibo folder:** `DM_AMIIBO_DIR`, default `<DM_SAVE>_amiibo` (a sibling of the save, as `<DM_SAVE>_device`). One JSON file per amiibo, `<amiibo ID>.json`, in Ryujinx's format, so that files can be copied between Ryujinx and the host. Reason: a file inside `<DM_SAVE>` is lost at the game's next Commit (`hle_fs.c:35-36`); the save folder and its siblings are kept across builds.
- **JSON:** `hle_nfp.c` reads and writes the amiibo file with a small parser and writer for this one fixed schema. `TagUuid` and `ApplicationArea` are base64. Dates are ISO 8601 `YYYY-MM-DDTHH:MM:SS` with an optional fraction and an optional offset; the host writes `YYYY-MM-DDTHH:MM:SS` with the local offset. Unknown keys are kept on write. A file that does not parse is renamed to `<amiibo ID>.json.bad` with a log line, and the host makes a new one. Reason: the core stays without Foundation; the schema has 8 keys and one array.
- **Random tag UUID:** the picker checkbox, on by default, the only switch. Reason: the user wants the rewards; BotW gives one reward per figure and day, keyed by the tag. The application area is keyed by the amiibo ID, so a random UUID keeps the written data.
- **Register info:** `hle_mii.c` exports `void dm_mii_default_charinfo(void *out)` (the default Mii, 0x58 bytes); `hle_nfp.c` calls it and sets the Mii's nickname to the user nickname (`DM_NICK`, default `Player`, as `hle_account.c`). The amiibo nickname goes into RegisterInfo as UTF-8, as Ryujinx writes it; the default is the list name cut to 10 Unicode characters. Font region 0. The dates are the dates in the amiibo file.
- **Write counter and dates:** never changed after the file is made, as in Ryujinx. Flush does nothing.
- **Struct layouts:** checked in BotW 1.9.0 (`sdk.patched.bin`, `main.patched.bin`). The `nn::nfp` get functions pass the caller's struct to the IUser proxy as a fixed-size out buffer (attribute 0x1a), so the API struct is the IPC struct. The proxy thunks give the sizes: `TagInfo` 0x58 (cmd 13), `RegisterInfo` 0x100 (cmd 14), `CommonInfo` 0x40 (cmd 15), `ModelInfo` 0x40 (cmd 16). The field layouts are Ryujinx's:
  - `TagInfo`: UUID 10 bytes at 0x00, UUID length u8 at 0x0a, reserved 0x15, protocol u32 at 0x20, tag type u32 at 0x24, reserved 0x30. BotW reads the length at 0x0a and that many UUID bytes from 0x00.
  - `ModelInfo`: character ID at 0x00 as 2 bytes, high byte first; variant u8 at 0x02; figure type u8 at 0x03; model number u16 at 0x04 in native (little-endian) order; series u8 at 0x06; reserved 0x39 from 0x07. BotW reads bytes 0–2 as one big-endian value (Link = 0x010000) and the model number with `ldrh` (its Zelda figures are 0x34b–0x35c). This matches the digit rule above.
  - `CommonInfo`: last write year u16 at 0x00, month u8 at 0x02, day u8 at 0x03, write counter u16 at 0x04, version u16 at 0x06, application area size u32 at 0x08, reserved 0x34. BotW passes the u32 at 0x08 as the GetApplicationArea size and rejects a size over 0x100.
  - `RegisterInfo`: Mii CharInfo 0x58 at 0x00, first write year u16 at 0x58, month u8 at 0x5a, day u8 at 0x5b, nickname 0x29 bytes at 0x5c, font region u8 at 0x85, reserved to 0x100. The sdk shows only the size; the offsets are Ryujinx's.
  - `ApplicationAreaCreateInfo`: access ID u32 at 0x00, initial data size u32 at 0x04, initial data pointer at 0x08. `CreateApplicationArea` (cmd 12) and `RecreateApplicationArea` (cmd 24) read only these 16 bytes and send the data as an in buffer of that size.
  - `GetApplicationArea(buf, &out_size, handle, size)`: the sdk first asks the area size (cmd 22), then reads min(area size, `size`) bytes (cmd 8) and writes the u32 count from the reply to `*out_size`. BotW passes a 0x100-byte buffer. `SetApplicationArea(handle, buf, size)` sends `buf` and `size` unchanged (cmd 9).
  - BotW 1.9.0 imports no CreateApplicationArea, RecreateApplicationArea, SetApplicationArea or Flush; it only reads the area.
- **Log lines:** each scan, mount, area open / create / write and unmount writes one `nfp:` log line with the amiibo ID and the result.
- **Tests:** `dm/host/tests/t_nfp.c` with `t_nfp_stub.c` (stubs for `hle_signal_bound_system_event`, `hle_bind_system_event`, `hle_hid_connected_npad`, `dm_entropy64` and `dm_mii_default_charinfo`; they record the signalled events), built from `../hle_nfp.c` and added to `run:` in `tests/Makefile`. It covers the state machine (the Gherkin scenarios without the picker), the result codes and the JSON store (read, write, a file in Ryujinx's form, a damaged file). No UI tests; the user tests the picker by hand.

## Known consequences

- `docs/development.md:15` ("No amiibo") and `docs/reference/services.md:228-229` ("The host answers as a console with no amiibo") become wrong; the change updates them.
- The one-time `nfp_no_tag` log line goes away; the `nfp:` lines replace it.
- The host opens a network connection for the first time, only when the player opens the picker and no cached list exists.

## Rollback

Revert the commits. The old nfp block answers "no tag" again. The amiibo folder stays and harms nothing.

## Open for the builder

- Which nfp calls BotW and Echoes of Wisdom make in their amiibo flow, in which order, and which results they check: logged in the first bring-up run.
