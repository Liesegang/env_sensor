#include "gas_indices.h"
#include "sensirion_gas_index_algorithm.h"

#include <stddef.h>

static GasIndexAlgorithmParams voc;
static GasIndexAlgorithmParams nox;
static bool have_previous;
static int64_t previous_ms;

void sgp41_indices_reset(void)
{
	GasIndexAlgorithm_init(&voc, GasIndexAlgorithm_ALGORITHM_TYPE_VOC);
	GasIndexAlgorithm_init(&nox, GasIndexAlgorithm_ALGORITHM_TYPE_NOX);
	have_previous = false;
}

void sgp41_indices_process(struct sgp41_sample *sample, int64_t timestamp_ms)
{
	if (sample == NULL) { return; }
	if (have_previous && (timestamp_ms <= previous_ms || timestamp_ms - previous_ms > 1500)) {
		/* A missed measurement must not compress time in the 1-Hz algorithm. */
		sgp41_indices_reset();
	}
	have_previous = true;
	previous_ms = timestamp_ms;
	int32_t voc_index = 0, nox_index = 0;
	if (sample->raw_voc > 0 && sample->raw_voc < 65000) {
		GasIndexAlgorithm_process(&voc, sample->raw_voc, &voc_index);
	} else {
		GasIndexAlgorithm_reset(&voc);
	}
	if (sample->nox_valid && !sample->conditioning && sample->raw_nox > 0 && sample->raw_nox < 65000) {
		GasIndexAlgorithm_process(&nox, sample->raw_nox, &nox_index);
	} else {
		GasIndexAlgorithm_reset(&nox);
	}
	sample->voc_index_valid = voc_index >= 1 && voc_index <= 500;
	sample->nox_index_valid = nox_index >= 1 && nox_index <= 500;
	sample->voc_index = sample->voc_index_valid ? (uint16_t)voc_index : 0;
	sample->nox_index = sample->nox_index_valid ? (uint16_t)nox_index : 0;
}
