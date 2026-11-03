#!/usr/bin/env bash
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
test_build="$(mktemp -d "${TMPDIR:-/tmp}/env-telemetry-tests.XXXXXX")"
trap 'rm -rf -- "${test_build}"' EXIT
for test_name in check lifecycle_check security_check hold_check; do
    "${CC:-cc}" -std=c11 -Werror -I"${test_dir}" -I"${test_dir}/../include" \
        "${test_dir}/${test_name}.c" "${test_dir}/../env_telemetry_encode.c" \
        -o "${test_build}/${test_name}"
    "${test_build}/${test_name}"
done
repo_root="$(cd -- "${test_dir}/../../../.." && pwd)"
driver_flags=()
for driver_name in opt4001 bme690 sgp41 stcc4 sfa40; do
    driver_flags+=("-I${repo_root}/shared/firmware/driver/${driver_name}/include")
done
"${CC:-cc}" -std=c11 -Werror -I"${test_dir}" -I"${test_dir}/../include" \
    "${driver_flags[@]}" -I"${test_dir}/../../history/include" "${test_dir}/card_init_check.c" \
    "${test_dir}/../env_telemetry_encode.c" "${test_dir}/../../history/env_history_codec.c" -o "${test_build}/card_init_check"
for failure_stage in normal identity_existing backend auth auth_info enable identity_save identity_missing_after_save identity_length identity_read identity_mismatch; do
    "${test_build}/card_init_check" "${failure_stage}"
done
