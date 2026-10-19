# NativeActivity IME/JNI integration (#190)

`dm/android/app/native.c` now reaches the accepted Activity text host through
`DmTextEditorJniBridge` and the packaged Java classes. This is an owned
diagnostic integration, not a game text-field implementation.

## Trigger and gate

- A physical-keyboard `KEYCODE_T` DOWN (repeat count 0, keyboard source,
  excluding gamepad source) explicitly starts one text session. Normal App
  creation and lifecycle start do not attach the editor or show a keyboard.
- Starting a session invalidates the current touch/gamepad contribution to
  neutral. While active, native touch and gamepad routes are gated (no
  movement/skill/gamepad delivery); the Java editor is the only input consumer.
- BACK while active ends the text session without finishing the Activity.
  Pause, window destruction, input-queue loss, focus loss, submit, cancel and
  destroy end the session and require a fresh physical input after the gate
  reopens.
- This keyboard trigger is an owned diagnostic entry only; no mobile/game
  usability or persistent gameplay control is claimed.

## JNI boundary

The bridge is loaded through the Activity class loader and constructed only on
the main looper. Native callbacks carry the checked session id (never a freed C
pointer). Java forwards only bounded UTF-16 code-unit counts for commit,
delete, submit and cancel; it never logs user text and never copies a document
or composition state into a native mirror. This is an event-only diagnostic
limit; no guest text consumer is invented.

`show`/`hide`/`release` failures are checked and cleared at each JNI stage.
Release invalidates the Java session before host cleanup, so late callbacks are
ignored. Native clears its bridge/session before deleting globals and never
retains a raw pointer across a Java callback.

## Tests

`dm/android/tests/ime-native-app-stubs/run.sh` compiles the exact four Java
production classes and the actual bridge/host/View/connection against narrow
Activity/ViewGroup/View/IMM stubs, with a test-only event seam for native
delivery. It verifies construction inactivity, explicit show, bounded
commit/delete/submit events, host detachment, release idempotence and late
callback suppression; exact sources are compiled against real
`android-35/android.jar`.

`native_callbacks` host regression compiles the actual extracted native
functions, including the new text routing/gate, against owned Android/WSI
boundaries. The actual NDK `dm_android_app` target is linked separately. No
phone/adb/game/SDK runtime/rtld/hardware is used.

## Remaining integration

Actual native caller UI attachment, real keyboard visibility/insets/occlusion,
Chinese/English device text input and the game/guest text consumer remain
parent #38 work. Parents #38/#19/#14 remain open; this is not App/game/device
acceptance.
