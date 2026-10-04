#include <sfa40.h>

#include <errno.h>
#include <stddef.h>

int sfa40_decode_words(const uint16_t words[4], struct sfa40_sample *sample)
{
	if (words == NULL || sample == NULL) {
		return -EINVAL;
	}
	/* The low byte of the status word is reserved and specified to be zero. */
	if (words[3] & 0x00ff) {
		return -EBADMSG;
	}
	int32_t rh = -6000 + (int32_t)((int64_t)125000 * words[1] / 65535);
	uint8_t status = words[3] >> 8;

	*sample = (struct sfa40_sample){
		.hcho_millippb = (int32_t)words[0] * 100,
		.humidity_mpercent = rh < 0 ? 0 : (rh > 100000 ? 100000 : rh),
		.temperature_mc = -45000 + (int32_t)((int64_t)175000 * words[2] / 65535),
		.raw_hcho = words[0],
		.raw_humidity = words[1],
		.raw_temperature = words[2],
		.status = status,
		.ready = !(status & 0x01),
		.within_specification = !(status & 0x03),
	};
	return 0;
}
