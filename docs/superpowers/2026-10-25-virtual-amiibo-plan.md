# Virtual amiibo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Mac host gives the game a virtual amiibo that the player picks from a list, as Ryujinx's Scan Amiibo does.

**Architecture:** A new C file `dm/host/hle_nfp.c` holds the amiibo file store (Ryujinx's JSON format) and the `nn::nfp` take-overs with Ryujinx's state machine; it replaces the nfp block in `hle_sdk16.c`. The Mac front end (`front_mac.m`) adds an Amiibo menu and a picker panel that downloads Ryujinx's amiibo list and calls `dm_amiibo_scan`.

**Tech Stack:** C11 (clang), Objective-C / AppKit, GameController-free; Foundation `NSURLSession` and `NSJSONSerialization` in the front end only.

**Spec:** `docs/superpowers/2026-10-25-virtual-amiibo.md`

## Global Constraints

- Code, comments, docs and commit messages in English. Conventional Commits, one type per commit; each commit builds and passes the tests.
- Keep existing comments. New comments only where needed, one line, describing what the code does. Match the style of `hle_*.c`: `HLE_FORCE(mangled, why, ret, cname, ...)` take-overs, one-line `LOG` calls.
- `hle_nfp.c` uses no AppKit and no Foundation.
- Amiibo folder: `DM_AMIIBO_DIR`, default `<DM_SAVE>_amiibo`. One file per amiibo, `<amiibo ID>.json`. No amiibo file inside `<DM_SAVE>`.
- Results (source: the spec's Reference, "Results", read from Ryujinx `ResultCode.cs`): `(description << 9) | 115`; DeviceNotFound 64, WrongArgument 65, WrongDeviceState 73, TagNotFound 97, ApplicationAreaIsNull 128, ApplicationAreaAlreadyCreated 168.
- Device states (source: the spec's Reference, "Device states", Ryujinx `NfpDeviceState.cs`): Initialized 0, SearchingForTag 1, TagFound 2, TagRemoved 3, TagMounted 4.
- The amiibo list URL: `https://raw.githubusercontent.com/Ryubing/Nfc/refs/heads/main/tags.json`. The list is never committed.
- No network request unless the player opens the picker and no cached `amiibo_tags.json` exists.
- All commands run from the repository root `/Users/lego/github/switch/nxnative`. Host build: `make -C dm/host GAME=01007EF00011E000 dmhost`. Unit tests: `make -C dm/host/tests GAME=01007EF00011E000 build/t_nfp && TMPDIR="$PWD/dm/host/tests/build" dm/host/tests/build/t_nfp`. Python tools: `/Users/lego/github/switch/.venv/bin/python` (has capstone). Authors run the build and only the tests of the changed code, never the full suite.
- Do not download or run third-party code to check behavior. The Ryujinx sources named in the spec are reference text only; copies are in the spec's Reference section.
- Write a test only for: a check against a reference (original program, golden snapshot), a parser or data format, a rule with more than one outcome that no reference check or type check covers (dates, parsing, prices, state machines), a regression of a real bug (one test per bug).
- No tests for: constants and data tables; wiring, scaffold, CLI arguments, logging; what the compiler, a linter or a standard check catches; UI and layout (the user tests them manually); what another test already covers. No mutation tests.
- Tests pass in any order and in parallel; each test owns its temp paths (`mkdtemp` under `$TMPDIR`).
- Deviation from the spec: the test stubs go inline in `t_nfp.c` (the pattern of `tests/t_hid.c:9-25`), not into a separate `t_nfp_stub.c`.
- Commits: plain `git commit` with the checkout's git config (the fork owner's identity). AGENTS.md:29's `-c user.name=seedds` form is the upstream author's identity and does not apply in this fork; the plan approval is the request to commit.
- File-touching take-overs (Mount, the application-area calls, Flush) and `dm_amiibo_scan`'s file load use `HLE_FORCE_BIG` (`hle.h:41`, as `hle_fs.c:22`) where they are take-overs; the JSON parser keeps its buffers on the heap.
- Weakened assertions: none. No task removes or loosens an existing assertion.

## Review Focus

1. A Ryujinx amiibo file with keys in another order, extra keys, `null` NickName or an empty ApplicationAreas array: the host reads it and keeps the unknown keys (test in Task 2).
2. An amiibo file that is cut short or is not JSON: the host renames it to `.json.bad` and continues with a new file (test in Task 2).
3. `GetApplicationArea` with a buffer smaller than the stored area: the host copies at most the buffer size and reports the copied size (test in Task 4).
4. A scan from the picker while a guest thread is inside an nfp call: both take the one mutex; no torn state (Task 4 takes the mutex in every take-over; review checks it).
5. Two scans in a row without StopDetection: the second replaces the first amiibo and signals the event pair again (test in Task 4).

---

### Task 1: Check the SDK struct layouts in BotW 1.9.0

**Files:**
- Modify: `docs/superpowers/2026-10-25-virtual-amiibo.md` (Decisions, "Struct layouts")

**Interfaces:**
- Produces: the confirmed size and field offsets of `nn::nfp::TagInfo`, `ModelInfo`, `CommonInfo`, `RegisterInfo` and `ApplicationAreaCreateInfo`, and the field layout of `ApplicationAreaCreateInfo` (the argument order of the calls is fixed by their mangled names, `hle_sdk16.c:205-208`), written into the spec. Task 4 uses them.

- [ ] **Step 1:** Disassemble the `nn::nfp` client functions in BotW's patched `sdk` (`/Users/lego/github/switch/botw/01007EF00011E000_build/patched_v1114112/sdk.patched.bin`; symbols by name, e.g. `_ZN2nn3nfp10GetTagInfoEPNS0_7TagInfoERKNS0_12DeviceHandleE`) with the repository's tools (`dm/tools/sdis.py`, run with `/Users/lego/github/switch/.venv/bin/python`). Read how each copies the IPC reply into the caller's struct: the byte count of the copy is the API struct size.
- [ ] **Step 2:** Compare with the Ryujinx layouts: TagInfo 0x58 (UUID 10 bytes, UUID length u8 at 0x0a, reserved 0x15, protocol u32 at 0x20, tag type u32 at 0x24, reserved 0x30), ModelInfo 0x40 (character ID u16, variant u8, type u8, model number u16 big-endian, series u8, reserved 0x39), CommonInfo 0x40 (last write year u16, month u8, day u8, write counter u16, version u16, area size u32, reserved 0x34), RegisterInfo 0x100 (Mii CharInfo 0x58, first write year u16, month u8, day u8, nickname 0x29 bytes, font region u8, reserved). Read `ApplicationAreaCreateInfo` from `CreateApplicationArea`'s use of its argument.
- [ ] **Step 3:** Write the confirmed layouts (or the differences) into the spec's "Struct layouts" decision and remove the word "assumption" there.
- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/2026-10-25-virtual-amiibo.md
git commit -m "docs(nfp): confirm the nn::nfp struct layouts in BotW 1.9.0"
```

### Task 2: Amiibo file store

**Files:**
- Create: `dm/host/hle_nfp.c` (store part)
- Create: `dm/host/tests/t_nfp.c`
- Modify: `dm/host/tests/Makefile` (`run:` target list and run lines, one build rule after `t_hid`, `tests/Makefile:73-74` pattern)

**Interfaces:**
- Produces (in `hle_nfp.c`, `static` unless named here):
  - `typedef struct { uint32_t id; uint8_t *data; size_t size; } amiibo_area;`
  - `typedef struct { char id[17]; uint8_t uuid[9]; int uuid_len; char first[40], last[40]; uint16_t writes; char nick[64]; amiibo_area *areas; int n_areas; char *extra; } amiibo_file;` — `first` / `last` hold the dates as read; `extra` holds unknown top-level members verbatim for the writer.
  - `static const char *amiibo_dir(void)` — `DM_AMIIBO_DIR`, else `<DM_SAVE>_amiibo`, else `./amiibo`; creates the folder.
  - `static int amiibo_load(const char *id, const char *default_nick, amiibo_file *f)` — reads `<dir>/<id>.json`; a missing file makes a new one (FileVersion 0, empty TagUuid, both dates now, WriteCounter 0, no areas, NickName = `default_nick` cut to 10 Unicode characters) and saves it; a file that does not parse is renamed to `<id>.json.bad`, logged, and replaced by a new one. Returns 0 on success.
  - `static int amiibo_save(const amiibo_file *f)` — writes to `<id>.json.tmp`, then `rename`.
  - `static void amiibo_free(amiibo_file *f)`.

- [ ] **Step 1: Write the failing tests** in `t_nfp.c` (`#include "../hle_nfp.c"`, stubs inline as `tests/t_hid.c:9-25`; each test sets `DM_AMIIBO_DIR` to its own `mkdtemp` folder):
  - `test_new_file`: `amiibo_load("0100000000040002", "Wolf Link", &f)` on an empty folder → returns 0; the file exists; `f.uuid_len == 0`, `f.writes == 0`, `f.n_areas == 0`, `strcmp(f.nick, "Wolf Link") == 0`.
  - `test_ryujinx_file`: a file written by the test in Ryujinx's form, keys in this order: `{"FileVersion":0,"TagUuid":"AQIDBAUGBwgJ","AmiiboId":"0100000000040002","FirstWriteDate":"2024-05-01T12:34:56.789+02:00","LastWriteDate":"2024-05-02T08:00:00+02:00","WriteCounter":3,"ApplicationAreas":[{"ApplicationAreaId":1234,"ApplicationArea":"3q2+7w=="}],"NickName":null,"Extra":{"a":[1,2]}}` → `uuid_len == 9`, `uuid == {1,2,3,4,5,6,7,8,9}`, `writes == 3`, one area with id 1234 and bytes `de ad be ef`, `nick` empty (null); after `amiibo_save` and a new `amiibo_load` every field is the same and the saved text contains `"Extra":{"a":[1,2]}`.
  - `test_bad_file`: a file with the text `{"FileVersion":0,"TagUu` → `amiibo_load` returns 0 with a new file; `<id>.json.bad` exists with the old text.
- [ ] **Step 2: Add `t_nfp` to `tests/Makefile`**: in the `run:` prerequisites and as a run line `TMPDIR="$(abspath $(B))" $(B)/t_nfp`; build rule `$(B)/t_nfp: t_nfp.c ../hle_nfp.c ../hle.h ../dm.h $(GAME_H) | $(B)` with `clang -O1 -Wall -Werror -Wno-unused-function -I.. $(GINC) -o $@ t_nfp.c`.
- [ ] **Step 3: Run the tests to verify they fail**

Run: `make -C dm/host/tests GAME=01007EF00011E000 build/t_nfp`
Expected: compile error, `../hle_nfp.c` not found.

- [ ] **Step 4: Implement the store in `dm/host/hle_nfp.c`**: a recursive-descent JSON reader for one object (strings with `\"` `\\` `\uXXXX` escapes, integers, `null`, nested arrays and objects skipped by span for `extra`), base64 encode and decode, the writer in Ryujinx's key order (`FileVersion, TagUuid, AmiiboId, FirstWriteDate, LastWriteDate, WriteCounter, ApplicationAreas, NickName`, then `extra`), dates for a new file as `YYYY-MM-DDTHH:MM:SS±HH:MM` local time. File header comment: what the file does and that the format is Ryujinx's `VirtualAmiiboFile`.
- [ ] **Step 5: Run the tests to verify they pass**

Run: `make -C dm/host/tests GAME=01007EF00011E000 build/t_nfp && TMPDIR="$PWD/dm/host/tests/build" dm/host/tests/build/t_nfp`
Expected: exit 0, no `FAIL:` line.

- [ ] **Step 6: Build the host** — `make -C dm/host GAME=01007EF00011E000 dmhost` succeeds (`hle_nfp.c` is picked up by `$(wildcard hle_*.c)`, `dm/host/Makefile:19`; it has no take-overs yet).
- [ ] **Step 7: Commit**

```bash
git add dm/host/hle_nfp.c dm/host/tests/t_nfp.c dm/host/tests/Makefile
git commit -m "feat(nfp): amiibo file store in Ryujinx's JSON format"
```

### Task 3: Export the default Mii

**Files:**
- Modify: `dm/host/hle_mii.c` (next to `mii_default`, `hle_mii.c:33`)
- Modify: `dm/host/dm.h` (declaration next to `dm.h:99-102`)

**Interfaces:**
- Produces: `void dm_mii_default_charinfo(void *out);` — writes the default Mii 0 (0x58 bytes): `mii_default(out, 0, 0)`, or the index that Ryujinx's `StoreData.BuildDefault(…, 0)` gives (read `hle_mii.c:33-60`).

- [ ] **Step 1:** Add the function and the declaration. No test: it calls an existing builder (test list: wiring).
- [ ] **Step 2: Build** — `make -C dm/host GAME=01007EF00011E000 dmhost` succeeds.
- [ ] **Step 3: Commit**

```bash
git add dm/host/hle_mii.c dm/host/dm.h
git commit -m "feat(mii): export the default Mii's CharInfo"
```

### Task 4: nfp take-overs with the virtual amiibo

**Files:**
- Modify: `dm/host/hle_nfp.c` (take-overs, state, `dm_amiibo_scan`)
- Modify: `dm/host/hle_sdk16.c:166-213` (remove the nfp block; it moves to `hle_nfp.c`)
- Modify: `dm/host/dm.h` (declarations, next to `dm.h:99-102`)
- Modify: `dm/host/hle_hid.c:179` (the comment names `hle_nfp.c` instead of `hle_sdk16.c`)
- Test: `dm/host/tests/t_nfp.c`

**Interfaces:**
- Consumes: Task 2's store; Task 1's struct layouts; Task 3's `dm_mii_default_charinfo`; `hle_signal_bound_system_event` (`dm.h:102`), `hle_bind_system_event` (`dm.h:100`), `hle_hid_connected_npad` (defined in `hle_hid.c:179`, declared only in `hle_sdk16.c:176`: declare `uint32_t hle_hid_connected_npad(void);` in `hle_nfp.c`), `dm_entropy64` (`hle_os.c:98`, declared as in `hle_mii.c:11`).
- Produces (declared in `dm.h`):
  - `int dm_amiibo_scan(const char *amiibo_id, const char *name, int random_uuid);` — returns 1 when the tag is placed, 0 when the state is not 1 (the host logs one line and changes nothing).
  - `int dm_amiibo_searching(void);` — 1 while the state is 1; the menu item reads it.

- [ ] **Step 1: Write the failing tests** in `t_nfp.c`; inline stubs for `hle_signal_bound_system_event` and `hle_signal_system_event` (record each signalled event pointer in order), `hle_bind_system_event`, `hle_hid_connected_npad` (returns 0), `dm_entropy64` (a counter-based sequence) and `dm_mii_default_charinfo` (a fixed 0x58-byte pattern). The tests call the take-overs by their `cname`:
  - `test_scan_flow`: Init, ListDevices → one handle; StartDetection; `GetDeviceState == 1`; `dm_amiibo_scan("0100000000040002", "Wolf Link", 0) == 1`; `GetDeviceState == 2`; within 1 s the stub saw activate, then deactivate; Mount(h, 0, 3) → OK; `GetDeviceState == 4`; GetModelInfo → character ID bytes from digits `0100` byte-swapped, variant `00`, type `00`, model number `0004`, series `00`; Unmount → state 2; StopDetection → state 0; no further event.
  - `test_not_searching`: without StartDetection `dm_amiibo_scan(...) == 0` and `GetDeviceState == 0`.
  - `test_wrong_state`: no scan → Mount returns `(73 << 9) | 115`; GetTagInfo returns `(73 << 9) | 115`; a handle with another id returns `(64 << 9) | 115`; Mount with model type 1 returns `(65 << 9) | 115`.
  - `test_area`: after scan and Mount: OpenApplicationArea(1234) → `(128 << 9) | 115`; CreateApplicationArea(1234, 4 bytes) → OK; again → `(168 << 9) | 115`; Open(1234) → OK; GetApplicationArea with a 2-byte buffer → 2 bytes, size 2; SetApplicationArea(8 bytes) → OK; a new Init / scan / Mount / Open / Get after `amiibo_free` of everything reads the 8 bytes back from the file.
  - `test_uuid`: random off → two scans give the same 9-byte UUID, stored in the file, with `uuid[3] == 0x88 ^ uuid[0] ^ uuid[1] ^ uuid[2]` and `uuid[8] == uuid[3] ^ uuid[4] ^ uuid[5] ^ uuid[6]`; random on → two scans give different UUIDs and the file's TagUuid does not change.
  - `test_rescan`: two scans without StopDetection → the second amiibo's ModelInfo; the event pair is signalled twice.
- [ ] **Step 2: Run the tests to verify they fail**

Run: `make -C dm/host/tests GAME=01007EF00011E000 build/t_nfp`
Expected: compile error, `dm_amiibo_scan` undefined.

- [ ] **Step 3: Implement** in `hle_nfp.c`: move every nfp `HLE_FORCE` from `hle_sdk16.c:166-213` with the same mangled names and `cname`s; one `pthread_mutex_t` guards the state (state, amiibo ID, name, UUID, opened area ID, the loaded `amiibo_file`, the two event pointers from `AttachActivateEvent` / `AttachDeactivateEvent`); every take-over and `dm_amiibo_scan` take it. Results, states and the wrong-state rules as in the spec's Decisions. `dm_amiibo_scan` stores the scan, sets state 2 and starts a detached thread that signals activate, sleeps 125 ms and signals deactivate (`hle_signal_bound_system_event`). The settings applets keep returning TagNotFound. RegisterInfo: `dm_mii_default_charinfo`, Mii nickname from `DM_NICK` (default `Player`), amiibo nickname UTF-8, font region 0. One `nfp:` log line per scan, mount, area open / create / write and unmount. `Initialize` logs `nfp::Initialize: virtual amiibo, folder <amiibo folder>`. Header comment: the Ryujinx model, one line.
- [ ] **Step 4: Run the tests to verify they pass**

Run: `make -C dm/host/tests GAME=01007EF00011E000 build/t_nfp && TMPDIR="$PWD/dm/host/tests/build" dm/host/tests/build/t_nfp`
Expected: exit 0, no `FAIL:` line.

- [ ] **Step 5: Build the host and boot BotW without a scan** — `make -C dm/host GAME=01007EF00011E000 dmhost`; copy `dm/host/dmhost` to `/Users/lego/github/switch/botw/01007EF00011E000_build/tools/dmhost` (`cp` to `dmhost.new`, then `mv`); run the command from `/Users/lego/github/switch/botw/build_mac2.log:54` without `DM_WINDOW=1 DM_AUDIO=1`, under `perl -e 'alarm 90; exec @ARGV'`, with the output in `run/nfp_boot.log`. Expected: the line `nfp::Initialize: virtual amiibo, folder …_amiibo`, no `[RED]` line that names nfp, and at least one line `[nvn] frame step: game time … s in … s (… of real time)`.
- [ ] **Step 6: Commit**

```bash
git add dm/host/hle_nfp.c dm/host/hle_sdk16.c dm/host/hle_hid.c dm/host/dm.h dm/host/tests/t_nfp.c
git commit -m "feat(nfp): virtual amiibo state machine after Ryujinx"
```

### Task 5: Amiibo menu and picker on the Mac

**Files:**
- Modify: `dm/host/front_mac.m` (menu at `front_mac.m:257-263`, focus observers at `front_mac.m:288-289`, a new picker section)

**Interfaces:**
- Consumes: `dm_amiibo_scan`, `dm_amiibo_searching` (Task 4); `DM_GAME_ID` from `game_profile.h`; the amiibo folder rule (`DM_AMIIBO_DIR`, else `<DM_SAVE>_amiibo`).

- [ ] **Step 1:** Add a menu "Amiibo" after "Window" with the item "Scan Amiibo…", key equivalent `a` with ⌘⇧. The item is enabled only while `dm_amiibo_searching()` returns 1 (`validateMenuItem:` or `NSMenuDelegate menuNeedsUpdate:`).
- [ ] **Step 2:** The picker: an `NSPanel` (titled, closable, utility style) with an `NSSearchField`, an `NSTableView` with columns Name and Series, a checkbox "Show all amiibo" (off) and a checkbox "Random tag UUID" (on). The rows: entries of `tags.json`'s `amiibo` array whose `gamesSwitch[].gameID` equals `DM_GAME_ID` case-insensitively, or all with "Show all amiibo"; the search filters by name and series. Double-click or Return calls `dm_amiibo_scan([head stringByAppendingString:tail], name, randomChecked)` and closes the panel. While the list loads, the table shows one row "Loading the amiibo list…"; on failure, "The amiibo list is not available".
- [ ] **Step 3:** The list: read `<amiibo folder>/amiibo_tags.json` if it exists; else download the URL from the Global Constraints with `NSURLSession`; drop a reply over 16 MB or one that `NSJSONSerialization` does not parse; write the reply to `amiibo_tags.json.tmp` and rename it to `amiibo_tags.json` after it parses.
- [ ] **Step 4:** Focus: a static flag `g_picker_opening` is set before the picker's `makeKeyAndOrderFront:` and cleared when the picker closes; the `NSWindowDidResignKeyNotification` observer (`front_mac.m:288`) skips `dm_focus_set(2)` while it is set (input is still released); when the picker closes, the game window becomes key again (the existing `DidBecomeKey` observer resumes input and sets focus 1).
- [ ] **Step 5: Build** — `make -C dm/host GAME=01007EF00011E000 dmhost` with no new warning in `front_mac.m`.
- [ ] **Step 6: Manual check (BotW)** — install the host as in Task 4 Step 5 and start the game with `/Users/lego/github/switch/The Legend of Zelda Breath of the Wild.app`; load a save, open the amiibo rune, choose Amiibo → Scan Amiibo… → "Wolf Link". Expected: the game takes the amiibo (Wolf Link appears or the game says the amiibo was used today). Record in `run/log.txt` which nfp calls follow the scan. If the game does not take the tag with one activate / deactivate pair, change Task 4's event thread to repeat the pair while the state is 2, as Ryujinx does (`INfp.cs:150-170`), and record it in the ledger.
- [ ] **Step 7: Commit**

```bash
git add dm/host/front_mac.m
git commit -m "feat(host): Amiibo menu and picker on the Mac"
```

### Task 6: Documentation

**Files:**
- Modify: `docs/development.md:15` (remove "No amiibo.")
- Modify: `docs/reference/services.md:46-47` and `:228` (check `:46`: it names nfp at boot and stays true) ("no amiibo" → the virtual amiibo; link a new section)
- Modify: `docs/reference/services.md` (a new section "Virtual amiibo (`nfp`)" after the SDK 20 nfp section at `:225-229`, in the file's Symptom / Cause / Fix / Found on form: the picker, the amiibo folder, the Ryujinx file format and list, the random tag UUID, the result codes)
- Modify: `README.md:201` ("No amiibo and no online features." → "Virtual amiibo on the Mac (Amiibo → Scan Amiibo…); no online features.")
- Modify: `dm/games/01007EF00011E000/README.md` (`:94` and `:121`: amiibo through the picker; the nfp call flow recorded in Task 5 Step 6)
- Check: `dm/games/010015100B514000/README.md:17` (Mario finds no amiibo until a scan; reword only if it says the host has none)

- [ ] **Step 1:** Write the changes above. The docs skill rules apply (ASD-STE100, one term per concept: "picker", "tag UUID").
- [ ] **Step 2:** Run `python dm/tools/docs_symptoms.py --check` and `python -m unittest dm/tools/tests/test_docs.py` (with `/Users/lego/github/switch/.venv/bin/python`); both pass. If `docs_symptoms.py --check` reports `docs/agents/symptoms.md` out of date, run it without `--check` and add the file.
- [ ] **Step 3: Commit**

```bash
git add docs/development.md docs/reference/services.md README.md dm/games/01007EF00011E000/README.md dm/games/010015100B514000/README.md docs/agents/symptoms.md
git commit -m "docs: virtual amiibo on the Mac host"
```
