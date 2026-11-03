#!/usr/bin/env bash
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${test_dir}/../../.." && pwd)"
test_build="$(mktemp -d "${TMPDIR:-/tmp}/card-cadence-tests.XXXXXX")"
trap 'rm -rf -- "${test_build}"' EXIT
for test_cadence in 0 1; do
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DCONFIG_CARD_TEST_CADENCE="${test_cadence}" \
        "${test_dir}/card_cadence_check.c" -o "${test_build}/card_cadence_${test_cadence}"
    "${test_build}/card_cadence_${test_cadence}"
done
driver_flags=()
for driver_name in opt4001 bme690 sgp41 stcc4 sfa40; do
    driver_flags+=("-I${repo_root}/shared/firmware/driver/${driver_name}/include")
done
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-function -I"${test_dir}/cadence" \
    -I"${repo_root}/shared/firmware/telemetry/include" -I"${repo_root}/shared/firmware/history/include" "${driver_flags[@]}" \
    "${test_dir}/card_pairing_button_check.c" "${repo_root}/shared/firmware/history/env_history_codec.c" -o "${test_build}/card_pairing_button_check"
"${test_build}/card_pairing_button_check"
"${PYTHON:-python3}" "${test_dir}/check_card_awake.py"
