# Contributing

Thanks for looking. Any help is welcome: code, tests on a console, or a report of what does not work.

## Ground rules

- **Never commit anything from the game**: no APK, OBB, `.so`, unpacked videos, saves, NRO or NSP. `.gitignore`
  covers the usual paths; check `git status` before committing.
- **Test on a console** and attach `debug.log` from that run. Say docked or handheld, and which `config.ini`
  settings you changed.
- **One change per pull request**, with a short explanation of the cause and what it does.
- `runtime/` is [android32](https://github.com/aks796/android32), unchanged. A fix that belongs there goes upstream;
  a port file with the same name as a runtime file, or a weak `port_*` function, changes behaviour here.

## Useful tools

| What | How |
|---|---|
| The log | `debug.log` in the game folder, rewritten on every start |
| Every file the game opens | `trace_files = true` in `config.ini` |
| Every Java call | `log_java_calls = true` (slow: logging every call costs frames) |
| Keys sent to the game | always in `debug.log` (`[input] key 23 down`) |
| Play, menu or cutscene | `[input] in play`, `[input] in a menu`, `[input] in a cutscene` lines |
| Crashes | `crash.log` in the game folder (stub addresses are named) and `sdmc:/atmosphere/crash_reports/` |
| Compile check without the console | `runtime/tools/check.sh` |

## Workflow

- `tools/setup_deps.sh` once, then `./build.sh && ./launcher/build.sh` after each change. After adding or removing
  a library in `portlibs32/`, `./build.sh clean` first.
- Symbols and object offsets in `source/` (DRM flags, the Gameplay / Hud / Character fields) are for
  `libModernCombat4.so` 1.2.3e, CRC32 `f9bc349f`; another version needs them checked again.
- Releases: bump `APP_VERSION` in `launcher/Makefile` and `PORT_NPDM_VERSION` in `Makefile`, write
  `docs/releases/v<version>.md`, tag `v<version>`, then `tools/make_release.sh --publish`.
