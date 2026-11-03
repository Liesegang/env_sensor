#!/usr/bin/env bash
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
test_build="$(mktemp -d "${TMPDIR:-/tmp}/card-storage-tests.XXXXXX")"
trap 'rm -rf -- "${test_build}"' EXIT
"${CC:-cc}" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -pthread \
    -I"${test_dir}/storage" "${test_dir}/storage_check.c" -o "${test_build}/storage_check"
for test_name in roundtrip rollback capacity legacy utc24h clock_late clock_unknown_fallback missing history_boundary history_reboot_gap history_clock history_seek history_exact history_producer history_archive_cache; do
    mkdir "${test_build}/${test_name}"
    "${test_build}/storage_check" "${test_name}" "${test_build}/${test_name}"
done
"${CC:-cc}" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -pthread \
    -I"${test_dir}/storage" -I"${test_dir}/../../../shared/firmware/driver/epd_dashboard/include" \
    "${test_dir}/display_history_check.c" -o "${test_build}/display_history_check"
for test_name in bins unknown utcmerge image image_periodic graph_periodic image_inflight pairing_periodic saved_inflight saved_utc_inflight clock_rebase; do
    "${test_build}/display_history_check" "${test_name}"
done
