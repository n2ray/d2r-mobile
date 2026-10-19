# USB startup qualification (owned runtime only)

Issue #219. This is an **offline review package** for the already-authorized
Xiaomi serial `c20521e9`; the worker does not run adb, read private phone data,
or load a game/SDK. Parent acceptance remains open until a real device run and
the SDK preconditions below are reviewed.

## Build

From the repository root, with the pinned NDK:

```sh
J=/Volumes/T2/Projects/d2r-work/build/hermes-jobs/touch-parity-32
cmake -S dm/android -B "$J/build-usb-startup" \
  -DCMAKE_TOOLCHAIN_FILE=/Volumes/T2/Projects/d2r-work/tools/android/android-ndk-r29/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "$J/build-usb-startup" --target \
  dm_image_test dm_gate_test dm_gate_vectors dm_runtime_test \
  dm_bootstrap_test dm_thread_test dm_sync_test dm_mutex_test dm_memory_test -j8
```

## Review before touching USB

```sh
dm/android/tests/usb_startup_qualify_selftest.sh
dm/android/tests/usb_startup_qualify.sh \
  --build-dir "$J/build-usb-startup" --dry-run
```

`--dry-run` verifies every manifest binary is ELF64/AArch64, prints its
SHA-256, and does not invoke adb. The manifest is
`dm/android/tests/usb_startup_manifest.tsv`.

## Codex-only device run

Only after explicit approval, with exactly the approved serial:

```sh
dm/android/tests/usb_startup_qualify.sh \
  --build-dir "$J/build-usb-startup" --serial c20521e9
```

The script refuses every other serial, requires `adb get-state == device` and an
exact serial/device line, stages only into a unique
`/data/local/tmp/dm-startup-qualify-*` directory, bounds every test with the
device `timeout` utility plus a host watchdog, requires a `PASS` marker, and
removes only that owned directory on exit. It never uses `logcat`, reads media,
transfers a game, or changes global settings.

## Hashes from the validated local build

These hashes identify the exact artifacts reviewed in this worktree; they are
not a substitute for a device-run result.

| test | binary | SHA-256 | seconds |
|---|---|---|---|
| image_map | dm_image_test | `1e428401e03f3f45eff29a8703cdd07d41d279cd917d5b1b956d1da3b7c3744b` | 45 |
| gate | dm_gate_test | `66d2839c93fa8d23dec2da208c060cafbe780f500ebc906cd21135bb6835f528` | 30 |
| gate_vectors | dm_gate_vectors | `86061e1f6d9070e289377db6bb2aa8624aa4f3ebb17048c7f60c23d944599b6c` | 30 |
| runtime | dm_runtime_test | `b169bdc29b14a5c2f4944999ec13a413a86089d7c89ee266a4b9847ac2f4b645` | 30 |
| bootstrap | dm_bootstrap_test | `3eb4a6457c5d4deabdb9d199a7056130b4c3fc87a155f974c66325b926fd689a` | 45 |
| thread | dm_thread_test | `1f98a093e388e67db42ef59d013a19a66759d0714c27394414e47330ff2d3895` | 60 |
| sync | dm_sync_test | `b79077591bacf7af472d8be97817a25de33cdad0f186d37eafbf1a414bc97901` | 30 |
| mutex | dm_mutex_test | `973ffad29fb9179bd3ff05138aa947443626805c862f0b0b43a60ad1da91807e` | 60 |
| memory | dm_memory_test | `658f9acf83845fb7eac1db4062518e2fcd7adbfd2531e03f69ee42c94cf0ea58` | 45 |

## Next SDK gate (not satisfied by these owned tests)

Before any real SDK/game startup, require all of:

1. A reviewed strict production `prepare` path has accepted the exact SDK
   modules; quarantine diagnostics must not be used as a production bypass.
2. Required Android services/imports and the exact module entry order are
   resolved from owned metadata, with a bounded watchdog and no raw private
   logs.
3. The exact game/SDK binary hashes and an owner-approved input digest are
   recorded; the launch command is explicit and serial-pinned.
4. First-frame/startup result is observed without claiming 60 fps, peripheral
   completeness, game input behavior, or device acceptance.

Until those are independently executed and reviewed, this package claims only
owned runtime startup qualification and remains blocked on real SDK execution.
