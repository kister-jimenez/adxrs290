
#include "iio_types.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "adxrs290.h"
#include "util.h"
#include "error.h"

#define NUM_CHANNELS 3

float hpf_values[] = {
	0.000,
	0.011,
	0.022,
	0.044,
	0.087,
	0.175,
	0.350,
	0.700,
	1.400,
	2.800,
	11.30
};

float lpf_values[] = {
	480.0,
	320.0,
	160.0,
	80.0,
	56.6,
	40.0,
	28.3,
	20
};

ssize_t adxrs290_iio_reg_read(void *device, char *buf, size_t len,
		const struct iio_ch_info *channel)
{
	uint8_t val;
	adxrs290_reg_read((struct adxrs290_dev *)device, atoi(buf)&0xFF, &val);
	return snprintf(buf, len, "%d", val);

}

ssize_t adxrs290_iio_reg_write(void *device, char *buf, size_t len,
		 const struct iio_ch_info *channel)
{
	unsigned int reg;
	unsigned int val;
	if(sscanf(buf,"0x%x 0x%x", &reg, &val)==2) {
		adxrs290_reg_write((struct adxrs290_dev *)device, reg&0xFF, val&0xFF);
	}
	return len;
}

ssize_t adxrs290_iio_channel_read(void *device, char *buf, size_t len,
		const struct iio_ch_info *channel)
{
	int16_t data;
	adxrs290_get_rate_data((struct adxrs290_dev *)device,
			channel->ch_num, &data);
	if (channel->ch_num==ADXRS290_CHANNEL_TEMP) {
		data = (data<<4)>>4;
	}
	return snprintf(buf, len, "%d", data);
}


ssize_t adxrs290_iio_scale_read(void *device, char *buf, size_t len,
		const struct iio_ch_info *channel)
{
	if (channel->ch_num==ADXRS290_CHANNEL_TEMP) {
		// Temperature scale 1 LSB = 0.1 degree Celsius
		return snprintf(buf, len, "0.10000");
	}
	// Angular velocity scale 1 LSB = 0.005 degrees/sec
	return snprintf(buf, len, "0.00500");
}

ssize_t adxrs290_iio_hpf_read(void *device, char *buf, size_t len,
		const struct iio_ch_info *channel)
{
	uint8_t index;
	adxrs290_get_hpf((struct adxrs290_dev *)device, &index);
	if (index>0x0A) {
		index = 0x0A;
	}
	return snprintf(buf, len, "%0.5f", hpf_values[index]);
}

ssize_t adxrs290_iio_hpf_write(void *device, char *buf, size_t len,
		 const struct iio_ch_info *channel)
{
	// TODO: Implement hpf write
	return len;
}

ssize_t adxrs290_iio_lpf_read(void *device, char *buf, size_t len,
		const struct iio_ch_info *channel)
{
	uint8_t index;
	adxrs290_get_lpf((struct adxrs290_dev *)device, &index);
	if (index>0x07) {
		index = 0x07;
	}
	return snprintf(buf, len, "%0.5f", lpf_values[index]);
}

ssize_t adxrs290_iio_lpf_write(void *device, char *buf, size_t len,
		 const struct iio_ch_info *channel)
{
	// TODO: Implement lpf write
	return len;
}

ssize_t adxrs290_iio_read_channel(void *dev_instance, char *pbuf, size_t offset,
			     size_t bytes_count, uint32_t ch_mask)
{
	// TODO: Implement  channel reading
	return bytes_count;
}
