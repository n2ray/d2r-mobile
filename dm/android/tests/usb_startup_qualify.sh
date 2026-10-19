#!/usr/bin/env bash
# Bounded Android USB startup qualification for owned runtime binaries.
# The worker never runs this against a phone; Codex executes after review.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
manifest="$here/usb_startup_manifest.tsv"
serial=c20521e9
adb=${DM_USB_ADB:-/opt/homebrew/bin/adb}
readelf_override=${DM_USB_READELF:-}
build_dir=${DM_ANDROID_BUILD_DIR:-/Volumes/T2/Projects/d2r-work/build/android/arm64-v8a}
dry_run=0
adb_budget=45

usage() {
    cat >&2 <<'EOF'
Usage: usb_startup_qualify.sh [--build-dir DIR] [--adb PATH] [--readelf PATH]
                             [--dry-run] [--manifest PATH]
Only serial c20521e9 is accepted. No adb is run by default in a worker;
--dry-run validates local ARM64 binaries/hashes and prints the bounded plan.
EOF
}

while (($#)); do
    case "$1" in
        --build-dir) build_dir=$2; shift 2 ;;
        --adb) adb=$2; shift 2 ;;
        --readelf) readelf_override=$2; shift 2 ;;
        --manifest) manifest=$2; shift 2 ;;
        --serial) serial=$2; shift 2 ;;
        --dry-run) dry_run=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

[[ "$serial" == c20521e9 ]] || { echo "refusing non-approved serial" >&2; exit 2; }
[[ -f "$manifest" ]] || { echo "manifest missing: $manifest" >&2; exit 2; }
[[ -d "$build_dir" ]] || { echo "build directory missing: $build_dir" >&2; exit 2; }
[[ -x "$adb" || "$dry_run" == 1 ]] || { echo "adb not executable: $adb" >&2; exit 2; }

sha() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}';
    else shasum -a 256 "$1" | awk '{print $1}'; fi
}
readelf_bin=$readelf_override
if [[ -z "$readelf_bin" ]]; then
    for candidate in /Volumes/T2/Projects/d2r-work/tools/android/android-ndk-r29/toolchains/llvm/prebuilt/*/bin/llvm-readelf; do
        [[ -x "$candidate" ]] && { readelf_bin=$candidate; break; }
    done
fi
[[ -n "$readelf_bin" && -x "$readelf_bin" ]] || { echo "llvm-readelf not found; set DM_USB_READELF" >&2; exit 2; }

# Validate the complete manifest before touching a device.
declare -a names=() bins=() secs=() digests=()
while IFS=$'\t' read -r name bin timeout_s; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    [[ "$name" =~ ^[a-z0-9_]+$ && "$bin" =~ ^[a-z0-9_]+$ && "$timeout_s" =~ ^[0-9]+$ ]] || {
        echo "invalid manifest row" >&2; exit 2;
    }
    path="$build_dir/$bin"
    [[ -f "$path" ]] || { echo "missing binary: $path" >&2; exit 2; }
    elf=$("$readelf_bin" -h "$path" 2>/dev/null || true)
    grep -q 'Class:.*ELF64' <<<"$elf" || { echo "not ELF64: $path" >&2; exit 2; }
    grep -q 'Machine:.*AArch64' <<<"$elf" || { echo "not AArch64: $path" >&2; exit 2; }
    names+=("$name"); bins+=("$bin"); secs+=("$timeout_s"); digests+=("$(sha "$path")")
done < "$manifest"
((${#names[@]} > 0)) || { echo "empty manifest" >&2; exit 2; }

printf '%-12s %-24s %s\n' 'test' 'sha256' 'timeout'
for i in "${!names[@]}"; do
    printf '%-12s %-24s %ss\n' "${names[$i]}" "${digests[$i]}" "${secs[$i]}"
done
if ((dry_run)); then
    printf 'DRY-RUN: serial=%s build=%s tests=%d; no adb command executed\n' \
        "$serial" "$build_dir" "${#names[@]}"
    exit 0
fi

[[ -x "$adb" ]] || { echo "adb not executable: $adb" >&2; exit 2; }
run_bounded() {
    local budget=$1; shift
    if command -v timeout >/dev/null 2>&1; then timeout "$budget" "$@"; else
        perl -e 'alarm shift; exec @ARGV' "$budget" "$@"
    fi
}
device_state=$(run_bounded 15 "$adb" -s "$serial" get-state 2>/dev/null || true)
[[ "$device_state" == device ]] || { echo "approved device is not ready: $serial" >&2; exit 3; }
devices=$(run_bounded 15 "$adb" devices 2>/dev/null || true)
grep -qx "$serial[[:space:]]*device" <<<"$devices" || {
    echo "approved serial not present as device (unauthorized/offline rejected)" >&2; exit 3;
}
remote=/data/local/tmp/dm-startup-qualify-$$-$(date +%s)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/dm-usb-qualify.XXXXXX")
cleanup() {
    rm -rf "$tmp"
    run_bounded 15 "$adb" -s "$serial" shell "rm -rf '$remote'" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM
run_bounded 20 "$adb" -s "$serial" shell "mkdir -p '$remote'" >/dev/null
for bin in "${bins[@]}"; do
    run_bounded 45 "$adb" -s "$serial" push "$build_dir/$bin" "$remote/$bin" >/dev/null
done
run_bounded 20 "$adb" -s "$serial" shell "chmod 700 '$remote'/*" >/dev/null

# Verify the device watchdog once; refuse a shell that cannot bound a test.
watchdog=$(run_bounded 15 "$adb" -s "$serial" shell "command -v timeout" 2>/dev/null || true)
[[ -n "$watchdog" ]] || { echo "device timeout watchdog unavailable" >&2; exit 4; }

summary="$tmp/summary.tsv"
printf 'test\tsha256\texit\tmarker\n' > "$summary"
for i in "${!names[@]}"; do
    out="$tmp/${names[$i]}.out"
    set +e
    run_bounded $((secs[i] + 15)) "$adb" -s "$serial" shell \
        "cd '$remote' && timeout ${secs[$i]} ./${bins[$i]}" >"$out" 2>&1
    rc=$?
    set -e
    marker=no
    grep -q 'PASS' "$out" && marker=yes
    printf '%s\t%s\t%s\t%s\n' "${names[$i]}" "${digests[$i]}" "$rc" "$marker" >> "$summary"
    if ((rc != 0)); then
        cat "$out" >&2
        echo "FAIL: ${names[$i]} exit=$rc (timeout is nonzero)" >&2
        exit 5
    fi
    if [[ "$marker" != yes ]]; then
        cat "$out" >&2
        echo "FAIL: ${names[$i]} missing PASS termination marker" >&2
        exit 5
    fi
done
cat "$summary"
echo "PASS: USB startup qualification on $serial (${#names[@]} tests, no game/SDK payload)"
