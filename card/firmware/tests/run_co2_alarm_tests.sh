#!/usr/bin/env bash
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
test_build="$(mktemp -d "${TMPDIR:-/tmp}/card-co2-alarm-tests.XXXXXX")"
trap 'rm -rf -- "${test_build}"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"${test_dir}/alarms" \
    "${test_dir}/co2_alarm_check.c" -o "${test_build}/co2_alarm_check"
for test_name in normal late_worker stop_pending stop_active \
    init_pwm init_gpio init_runtime init_pin runtime_get pwm_start \
    schedule_initial schedule_stop schedule_next worker_stop \
    stop_first_error stop_runtime_error stop_pwm_error stop_gpio_error; do
    "${test_build}/co2_alarm_check" "${test_name}"
done
