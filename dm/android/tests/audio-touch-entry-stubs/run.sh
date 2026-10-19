#!/usr/bin/env bash
# #211 actual DmAudioTouchEntry lifecycle/click test against narrow Android view stubs.
# The control sink is the only replaced boundary; real android.jar compile happens in CI.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
javac=${JAVAC:-javac}
java=${JAVA:-java}
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-audio-entry.XXXXXX")
trap 'rm -rf "$work"' EXIT

"$javac" --release 11 -d "$work/stubs" \
    $(find "$here/../ime-activity-host-stubs/android" "$here/android" -name '*.java' -type f | sort) \
    "$here/../ime-touch-entry-stubs/android/widget/Button.java" \
    "$here/android/R.java"
"$javac" --release 11 -cp "$work/stubs" -d "$work/classes" \
    "$root/dm/android/app/java/org/d2rmobile/android/diagnostic/DmAudioTouchEntry.java" \
    "$here/DmAudioTouchEntryTest.java"
"$java" -cp "$work/stubs:$work/classes" org.d2rmobile.android.diagnostic.DmAudioTouchEntryTest
