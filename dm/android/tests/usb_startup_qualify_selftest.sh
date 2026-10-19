#!/usr/bin/env bash
# Offline counterexamples for usb_startup_qualify.sh; no phone or adb is touched.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
main="$here/usb_startup_qualify.sh"
work=$(mktemp -d "${TMPDIR:-/tmp}/dm-usb-selftest.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin"
: > "$work/adb.log"
for name in image_map gate gate_vectors runtime bootstrap thread sync mutex memory; do
    case "$name" in
        image_map) bin=dm_image_test ;;
        gate) bin=dm_gate_test ;;
        gate_vectors) bin=dm_gate_vectors ;;
        runtime) bin=dm_runtime_test ;;
        bootstrap) bin=dm_bootstrap_test ;;
        thread) bin=dm_thread_test ;;
        sync) bin=dm_sync_test ;;
        mutex) bin=dm_mutex_test ;;
        memory) bin=dm_memory_test ;;
    esac
    printf 'owned fake ELF %s\n' "$name" > "$work/bin/$bin"
done
cat > "$work/readelf" <<'EOF'
#!/usr/bin/env bash
printf 'Class:                             ELF64\n'
printf 'Machine:                           AArch64\n'
EOF
cat > "$work/adb" <<'EOF'
#!/usr/bin/env bash
set -u
log=${FAKE_ADB_LOG:?}
echo "$*" >> "$log"
if [[ "${1:-}" == devices ]]; then
    printf 'List of devices attached\nc20521e9\tdevice\n'
    exit 0
fi
[[ "${1:-}" == -s && "${2:-}" == c20521e9 ]] || exit 9
shift 2
case "${1:-}" in
    get-state) echo device ;;
    push|pull) exit 0 ;;
    shell)
        command=${2:-}
        case "$command" in
            *"command -v timeout"*) echo /system/bin/timeout ;;
            *"rm -rf"*) exit 0 ;;
            *"mkdir -p"*|*chmod*) exit 0 ;;
            *"timeout "*)
                if [[ -n "${FAKE_FAIL_BIN:-}" && "$command" == *"$FAKE_FAIL_BIN"* ]]; then
                    echo 'owned failure counterexample' >&2; exit 2
                fi
                if [[ -n "${FAKE_TIMEOUT_BIN:-}" && "$command" == *"$FAKE_TIMEOUT_BIN"* ]]; then
                    exit 124
                fi
                echo 'PASS: owned fake runtime test'
                ;;
            *) exit 0 ;;
        esac
        ;;
    *) exit 0 ;;
esac
EOF
chmod +x "$work/readelf" "$work/adb"
common=(--build-dir "$work/bin" --adb "$work/adb" --readelf "$work/readelf" --serial c20521e9)

if ! "$main" "${common[@]}" --dry-run >/dev/null; then
    echo 'FAIL: dry-run rejected valid owned binaries' >&2; exit 1
fi
if ! FAKE_ADB_LOG="$work/adb.log" "$main" "${common[@]}" >"$work/success.out"; then
    cat "$work/success.out" >&2; echo 'FAIL: valid fake device run rejected' >&2; exit 1
fi
grep -q '^PASS: USB startup qualification' "$work/success.out" || {
    echo 'FAIL: success marker missing' >&2; exit 1;
}
grep -q "rm -rf '/data/local/tmp/dm-startup-qualify-" "$work/adb.log" || {
    echo 'FAIL: own remote directory was not cleaned' >&2; exit 1;
}
if "$main" "${common[@]:0:6}" --serial deadbeef >/dev/null 2>&1; then
    echo 'FAIL: wrong serial accepted' >&2; exit 1
fi
if FAKE_ADB_LOG="$work/adb.log" FAKE_FAIL_BIN=dm_runtime_test \
    "$main" "${common[@]}" >/dev/null 2>&1; then
    echo 'FAIL: device test failure accepted' >&2; exit 1
fi
if FAKE_ADB_LOG="$work/adb.log" FAKE_TIMEOUT_BIN=dm_thread_test \
    "$main" "${common[@]}" >/dev/null 2>&1; then
    echo 'FAIL: device timeout accepted' >&2; exit 1
fi
echo 'PASS: USB qualification offline self-test (ELF/hash, success, wrong serial, failure, timeout, cleanup)'
