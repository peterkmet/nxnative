# Virtual amiibo: manual test checklist

Spec: [2026-10-25-virtual-amiibo.md](2026-10-25-virtual-amiibo.md). Host: `make -C dm/host GAME=01007EF00011E000 dmhost`, then copy `dm/host/dmhost` to `/Users/lego/github/switch/botw/01007EF00011E000_build/tools/dmhost` (`cp` to `dmhost.new`, then `mv`). Start BotW with `/Users/lego/github/switch/The Legend of Zelda Breath of the Wild.app`.

## Menu and picker

- [ ] Before the amiibo rune is open, Amiibo → Scan Amiibo… is disabled.
- [ ] Open the amiibo rune. The log has `nfp::Initialize: virtual amiibo, folder …_amiibo`. The menu item is enabled; ⌘⇧A opens the picker.
- [ ] First open with no `amiibo_tags.json` in the amiibo folder: the table shows "Loading the amiibo list…", then the BotW amiibo (about 27). `amiibo_tags.json` now exists in the amiibo folder.
- [ ] "Show all amiibo" shows the full list; the search field filters by name and series.
- [ ] Turn off the network, delete `amiibo_tags.json`, open the picker: it shows "The amiibo list is not available" and the game keeps running.

## Scan

- [ ] Choose "Wolf Link" (double-click or Return). The picker closes, and the game takes the amiibo (Wolf Link appears, or the game says the amiibo was used today).
- [ ] If the game does not take it: note it. The host signals one activate / deactivate pair per scan; Ryujinx repeats the pair while the tag is found (`INfp.cs:150-170`).
- [ ] Copy the `nfp:` lines that follow the scan from the log (the nfp call flow), for BotW's note.
- [ ] With "Random tag UUID" on (default), scan the same amiibo again on the same game day: the game gives the reward again.
- [ ] With "Random tag UUID" off, a second scan on the same day: the game says the amiibo was used today.
- [ ] `<amiibo ID>.json` exists in the amiibo folder (`<DM_SAVE>_amiibo`), not in the save folder.

## Focus

- [ ] While the picker is open, keys typed into it do not reach the game, and the game does not pause for focus loss.
- [ ] With the picker open, click the game window, then switch to another app: the game reports focus loss (pauses as with any app switch).
- [ ] With the game window key, click the open picker: the game does not pause.
- [ ] Close the picker: the game window is key again and takes input.

## Other games

- [ ] A game without amiibo (for example Super Mario Bros. Wonder) runs as before; no network request.
- [ ] Echoes of Wisdom (`01008CF01BAAC000`, provisional profile): its amiibo screen takes a scanned amiibo.
