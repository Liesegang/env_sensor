#ifndef OUTDOOR_GAS_INDICES_H_
#define OUTDOOR_GAS_INDICES_H_

#include <sgp41.h>

void sgp41_indices_reset(void);
/* Called once per successful 1-Hz measurement; gaps restart learning. */
void sgp41_indices_process(struct sgp41_sample *sample, int64_t timestamp_ms);

#endif
