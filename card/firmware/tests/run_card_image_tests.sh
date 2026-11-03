#!/usr/bin/env bash
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${test_dir}/../../.." && pwd)"
test_build="$(mktemp -d "${TMPDIR:-/tmp}/card-image-tests.XXXXXX")"
trap 'rm -rf -- "${test_build}"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-function \
    -I"${test_dir}/images" -I"${repo_root}/shared/firmware/telemetry/include" \
    "${test_dir}/card_image_check.c" -o "${test_build}/card_image_check"
"${test_build}/card_image_check"
