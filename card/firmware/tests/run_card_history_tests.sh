#!/usr/bin/env bash
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${test_dir}/../../.." && pwd)"
test_build="$(mktemp -d "${TMPDIR:-/tmp}/card-history-tests.XXXXXX")"
trap 'rm -rf -- "${test_build}"' EXIT
driver_flags=()
sanitizer_flags=()
if [[ "${CARD_HISTORY_SANITIZE:-0}" == 1 ]]; then
    sanitizer_flags=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
for driver_name in opt4001 bme690 sgp41 stcc4 sfa40; do
    driver_flags+=("-I${repo_root}/shared/firmware/driver/${driver_name}/include")
done
"${CC:-cc}" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -pthread \
    "${sanitizer_flags[@]}" -I"${test_dir}/storage" -c "${test_dir}/../src/sd_logger.c" -o "${test_build}/sd_logger.o"
"${CC:-cc}" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-function -pthread \
    "${sanitizer_flags[@]}" -I"${test_dir}/history" -I"${repo_root}/shared/firmware/telemetry/include" \
    -I"${repo_root}/shared/firmware/history/include" "${driver_flags[@]}" \
    "${test_dir}/card_history_check.c" "${test_build}/sd_logger.o" \
    "${repo_root}/shared/firmware/history/env_history_codec.c" -o "${test_build}/card_history_check"
for test_name in replay cancel_timeout cancel_inflight preparing_lease; do
    mkdir "${test_build}/${test_name}"
    "${test_build}/card_history_check" "${test_name}" "${test_build}/${test_name}"
done
