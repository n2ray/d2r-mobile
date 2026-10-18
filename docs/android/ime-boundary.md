# Android IME boundary (#173)

`dm/android/app/java/org/d2rmobile/android/diagnostic/DmInputConnection.java`
is a minimal, host-testable implementation of the real Android
`android.view.inputmethod.InputConnection` interface for the owned diagnostic
package (`org.d2rmobile.android.diagnostic`). It is not wired into the current
NativeActivity APK.

## Owned contract

- One `DmInputConnection` instance is one editor session. Construct a new
  instance for a new session. `closeConnection()`, `reset()`, `submit()` and
  `cancel()` are terminal; later callbacks return `false` and do not reach the
  sink. A stale connection therefore cannot insert into a newer session.
- A caller supplies `DmInputConnection.Sink`:
  `onCommit(text)`, `onDelete(deletedText)`, `onSubmit(text)` and `onCancel()`.
  Callbacks run in order on the owning UI/control thread. This class does not
  log user text.
- Composing text is separate from committed text. `setComposingText()` changes
  only the composing buffer and never calls `onCommit()`. `commitText()` replaces
  composing with the committed value and emits exactly one commit.
  `finishComposingText()` commits any remaining composing text once; after an
  explicit commit it is a no-op.
- Owned text is bounded at `MAX_TEXT_UTF16_UNITS = 512` UTF-16 code units,
  including pending composing text. Over-limit or malformed UTF-16 input is
  rejected atomically: committed text and pending composing text are unchanged.
- Deletion uses UTF-16 code units for `deleteSurroundingText()` and Unicode code
  points for `deleteSurroundingTextInCodePoints()`. Both avoid splitting a
  surrogate pair; the sink receives the exact removed committed text.
- `submit()` and editor actions `DONE`/`GO`/`SEND`/`SEARCH` emit submit once;
  `cancel()` emits cancel once. `sendKeyEvent()` and other unsupported optional
  operations return `false` rather than synthesizing keys.
- `setComposingRegion()` and absolute selection are intentionally unsupported
  in this slice. No generic editor, JNI bridge, keyboard display or guest text
  consumption is introduced here.

## Focused host test

`dm/android/tests/ime-stubs/run.sh` compiles the exact production source plus
`DmInputConnectionTest.java` against the real `android.jar` and runs Latin
commit, Chinese composing/commit, emoji/surrogate deletion, length bound,
malformed UTF-16, submit/cancel and stale/closed-session cases. It uses no
phone, adb, game, JNI or Android runtime execution. Override `ANDROID_JAR`,
`ANDROID_SDK_ROOT`, `JAVAC` or `JAVA` for another host SDK/JDK.

Packaging into the diagnostic APK is a later integration step after the pending
Java packaging work is accepted. This boundary alone is not App/keyboard/device
or guest-input acceptance; parent issues remain open.
