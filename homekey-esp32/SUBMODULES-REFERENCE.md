# homekey-esp32 submodules — reference

This file preserves the submodule list that was originally in
`homekey-esp32/.gitmodules`. The file was removed from git tracking
because Cloudflare Workers Builds fails to clone our repo when it
encounters any `.gitmodules` inside the tree (even though these are
not real git submodules at our repo level — the subtree from
`rednblkx/HomeKey-ESP32` was imported as plain files, not gitlinks).

If you want to build the `homekey-esp32/` firmware locally, you need
these components populated under `homekey-esp32/components/`. Fastest
path: clone each repository manually.

| Path | Source | Branch |
|---|---|---|
| `components/DigitalDoorKey` | <https://github.com/rednblkx/DigitalDoorKey.git> | `main` |
| `components/HomeSpan/upstream` | <https://github.com/HomeSpan/HomeSpan.git> | `release-2.1.6` |
| `components/msgpack-c/upstream` | <https://github.com/msgpack/msgpack-c.git> | `c_master` |
| `components/loggable` | <https://github.com/rednblkx/loggable-thing.git> | `main` |
| `components/event_bus` | <https://github.com/rednblkx/esp32-event-bus.git> | `main` |
| `components/loggable_espidf` | <https://github.com/rednblkx/loggable-espidf.git> | `main` |
| `components/pn532_cxx` | <https://github.com/rednblkx/pn532_cxx.git> | `main` |
| `components/pn532_hal` | <https://github.com/rednblkx/esp-hal-pn532.git> | `main` |

One-shot fetch from `homekey-esp32/`:

```bash
cd homekey-esp32
git clone --depth 1 -b main    https://github.com/rednblkx/DigitalDoorKey.git    components/DigitalDoorKey
git clone --depth 1 -b release-2.1.6 https://github.com/HomeSpan/HomeSpan.git    components/HomeSpan/upstream
git clone --depth 1 -b c_master https://github.com/msgpack/msgpack-c.git          components/msgpack-c/upstream
git clone --depth 1              https://github.com/rednblkx/loggable-thing.git   components/loggable
git clone --depth 1              https://github.com/rednblkx/esp32-event-bus.git  components/event_bus
git clone --depth 1              https://github.com/rednblkx/loggable-espidf.git  components/loggable_espidf
git clone --depth 1              https://github.com/rednblkx/pn532_cxx.git        components/pn532_cxx
git clone --depth 1              https://github.com/rednblkx/esp-hal-pn532.git    components/pn532_hal
```

Don't `git add` any of these — they're transient local populations,
not part of our repo. `homekey-esp32/` is frozen as a regression
baseline (see root `TODO.md` §7.4); you normally won't need to build
it from source.

## Gotcha — `just upstream-pull` re-introduces `.gitmodules`

Every time you run `just upstream-pull`, the subtree merge from
`rednblkx/HomeKey-ESP32` brings `.gitmodules` back. Immediately after
the pull, remove it again so Cloudflare builds stay green:

```bash
just upstream-pull
git rm homekey-esp32/.gitmodules
git commit -m "homekey-esp32: strip .gitmodules (CF submodule workaround)"
```

If we ever need to build firmware from the subtree regularly, revisit
this — possible future fix is a `.gitattributes` / `export-ignore`
approach or a separate branch without the subtree for CF.
