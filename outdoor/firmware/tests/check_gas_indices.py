"""Exercise the real fixed-point algorithm + driver adapter without a device."""
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
driver = app / 'driver/sgp41'
source = r'''
#include <assert.h>
#include <stdio.h>
#include "gas_indices.h"

static struct sgp41_sample sample = { .raw_voc = 29429, .raw_nox = 17492 };
static int64_t time_ms;
static void next(void) {
    sgp41_indices_process(&sample, time_ms);
    time_ms += 1000;
}
int main(void) {
    sgp41_indices_reset();
    for (int i = 0; i < 10; i++) {
        sample.conditioning = true; sample.nox_valid = false; next();
        assert(!sample.nox_index_valid && sample.nox_index == 0);
        assert(!sample.voc_index_valid && sample.voc_index == 0);
    }
    sample.conditioning = false; sample.nox_valid = true;
    for (int i = 10; i < 46; i++) {
        next(); assert(!sample.voc_index_valid && sample.voc_index == 0);
    }
    next();
    assert(sample.voc_index_valid && sample.voc_index >= 1 && sample.voc_index <= 500);
    assert(!sample.nox_index_valid && sample.nox_index == 0);
    for (int i = 47; i < 56; i++) { next(); assert(!sample.nox_index_valid); }
    next(); assert(sample.nox_index_valid && sample.nox_index >= 1 && sample.nox_index <= 500);

    for (int i = 0; i < 3600; i++) { next(); }
    assert(sample.voc_index >= 95 && sample.voc_index <= 105);
    assert(sample.nox_index == 1);
    uint16_t baseline = sample.voc_index;
    sample.raw_voc = 27000;
    for (int i = 0; i < 30; i++) { next(); }
    assert(sample.voc_index > baseline);

    time_ms += 1000; next(); /* One missing sample -> restart, not an old index. */
    assert(!sample.voc_index_valid && sample.voc_index == 0);
    assert(!sample.nox_index_valid && sample.nox_index == 0);
    for (int i = 0; i < 60; i++) { next(); }
    assert(sample.voc_index_valid && sample.nox_index_valid);

    sample.raw_voc = 65535; next();
    assert(!sample.voc_index_valid && sample.voc_index == 0);
    assert(sample.nox_index_valid);
    sample.raw_voc = 29429; sample.conditioning = true; sample.nox_valid = false;
    next(); assert(!sample.nox_index_valid && sample.nox_index == 0);
    sample.conditioning = false; sample.nox_valid = true;
    for (int i = 0; i < 60; i++) { next(); }
    assert(sample.voc_index_valid && sample.nox_index_valid);
    sgp41_indices_process(&sample, time_ms - 2000); /* Restart / clock rewind. */
    assert(!sample.voc_index_valid && !sample.nox_index_valid);
    sgp41_indices_process(NULL, time_ms);
    puts("Gas Index: conditioning, blackout, baseline, response, gaps and invalid input OK");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='outdoor-gas-index-') as temporary:
    root = Path(temporary)
    (root / 'check.c').write_text(source)
    # Keep upstream unchanged. Compile it separately: Clang warns about its
    # generated double parentheses; our adapter and checks still use -Werror.
    vendor_object = root / 'algorithm.o'
    subprocess.run(['cc', '-std=c11', '-Wno-parentheses', '-I', str(driver / 'vendor'), '-c',
                    str(driver / 'vendor/sensirion_gas_index_algorithm.c'),
                    '-o', str(vendor_object)], check=True)
    command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-I', str(driver), '-I', str(driver / 'include'), '-I', str(driver / 'vendor'),
               str(root / 'check.c'), str(driver / 'gas_indices.c'),
               str(vendor_object), '-o', str(root / 'check')]
    subprocess.run(command, check=True)
    subprocess.run([str(root / 'check')], check=True)
