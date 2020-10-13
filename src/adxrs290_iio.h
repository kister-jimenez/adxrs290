#ifndef ADXRS290_IIO_H
#define ADXRS290_IIO_H

#include "iio_types.h"

ssize_t adxrs290_iio_reg_read(void *device, char *buf, size_t len,
		const struct iio_ch_info *channel);
ssize_t adxrs290_iio_reg_write(void *device, char *buf, size_t len,
		const struct iio_ch_info *channel);
ssize_t adxrs290_iio_channel_read(void *device, char *buf, size_t len,
		const struct iio_ch_info *channel);
ssize_t adxrs290_iio_scale_read(void *device, char *buf, size_t len,
		const struct iio_ch_info *channel);
ssize_t adxrs290_iio_hpf_write(void *device, char *buf, size_t len,
		const struct iio_ch_info *channel);
ssize_t adxrs290_iio_hpf_read(void *device, char *buf, size_t len,
		const struct iio_ch_info *channel);
ssize_t adxrs290_iio_lpf_write(void *device, char *buf, size_t len,
		const struct iio_ch_info *channel);
ssize_t adxrs290_iio_lpf_read(void *device, char *buf, size_t len,
		const struct iio_ch_info *channel);
ssize_t adxrs290_iio_read_channel(void *dev_instance, char *pbuf, size_t offset,
		size_t bytes_count, uint32_t ch_mask);

static struct iio_attribute adxrs290_iio_ch_attr_3db_hpf_freq_val = {
	.name = "filter_high_pass_3db_frequency",
	.show = adxrs290_iio_hpf_read,
	.store = adxrs290_iio_hpf_write
};

/* static struct iio_attribute adxrs290_iio_ch_attr_3db_hpf_freq_avail = {
	.name = "filter_high_pass_3db_frequency_available",
	.show = adxrs290_iio_channel_read,
	.store = NULL
};*/

static struct iio_attribute adxrs290_iio_ch_attr_3db_lpf_freq_val = {
	.name = "filter_low_pass_3db_frequency",
	.show = adxrs290_iio_lpf_read,
	.store = adxrs290_iio_lpf_write
};

/* static struct iio_attribute adxrs290_iio_ch_attr_3db_lpf_freq_avail = {
	.name = "filter_low_pass_3db_frequency_available",
	.show = adxrs290_iio_channel_read,
	.store = NULL
}; */

static struct iio_attribute adxrs290_iio_ch_attr_raw = {
	.name = "raw",
	.show = adxrs290_iio_channel_read,
	.store = NULL
};

static struct iio_attribute adxrs290_iio_ch_attr_scale = {
	.name = "scale",
	.show = adxrs290_iio_scale_read,
	.store = NULL
};

static struct iio_attribute *adxrs290_iio_vel_attrs[] = {
	&adxrs290_iio_ch_attr_3db_hpf_freq_val,
	//adxrs290_iio_ch_attr_3db_hpf_freq_avail,
	&adxrs290_iio_ch_attr_3db_lpf_freq_val,
	//&adxrs290_iio_ch_attr_3db_lpf_freq_avail,
	&adxrs290_iio_ch_attr_raw,
	&adxrs290_iio_ch_attr_scale,
	NULL,
};

static struct iio_attribute *adxrs290_iio_temp_attrs[] = {
	&adxrs290_iio_ch_attr_raw,
	&adxrs290_iio_ch_attr_scale,
	NULL,
};

static struct scan_type anglvel_scantype = {
		.sign = 's',
		.realbits = 16,
		.storagebits = 16,
		.shift = 0,
		.is_big_endian = false
};

static struct iio_channel adxrs290_iio_channel_x = {
	.ch_type = IIO_ANGL_VEL,
	.modified=1,
	.channel2=IIO_MOD_X,
	.name = "",
	.scan_index = 0,
	.scan_type = &anglvel_scantype,
	.attributes = adxrs290_iio_vel_attrs,
	.ch_out = false,
};

static struct iio_channel adxrs290_iio_channel_y = {
	.ch_type = IIO_ANGL_VEL,
	.modified=1,
	.channel2=IIO_MOD_Y,
	.name = "",
	.scan_index = 1,
	.scan_type = &anglvel_scantype,
	.attributes = adxrs290_iio_vel_attrs,
	.ch_out = false,
};

static struct scan_type temp_scantype = {
        .sign = 's',
        .realbits = 12,
        .storagebits = 16,
        .shift = 0,
        .is_big_endian = false
};

static struct iio_channel adxrs290_iio_channel_temp = {
	.ch_type = IIO_TEMP,
	.name = "",
	.scan_index = 2,
	.scan_type = &temp_scantype,
	.attributes = adxrs290_iio_temp_attrs,
	.ch_out = false,
};

static struct iio_channel *adxrs290_iio_channels[] = {
	&adxrs290_iio_channel_x,
	&adxrs290_iio_channel_y,
	&adxrs290_iio_channel_temp,
	NULL,
};

static struct iio_attribute adxrs290_iio_reg_attr = {
	.name = "direct_reg_access",
	.show = adxrs290_iio_reg_read,
	.store = adxrs290_iio_reg_write,
};

static struct iio_attribute *adxrs290_iio_debug_attrs[] = {
	&adxrs290_iio_reg_attr,
	NULL,
};


//extern struct iio_device adxrs290_iio_descriptor ;
struct iio_device adxrs290_iio_descriptor = {
	.num_ch = 3,
	.channels = adxrs290_iio_channels,
	.attributes = NULL,
	.debug_attributes = adxrs290_iio_debug_attrs,
	.buffer_attributes = NULL,
	.transfer_dev_to_mem = NULL,
	.transfer_mem_to_dev = NULL,
	.read_data = adxrs290_iio_read_channel,
	.write_data = NULL
};

#endif
