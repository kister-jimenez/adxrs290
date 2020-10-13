/***************************************************************************//**
 *   @file   iio.c
 *   @brief  Implementation of iio.
 *   This module implements read/write ops, required by libtinyiiod and further
 *   calls show/store functions, corresponding to device/channel/attribute.
 *   @author Cristian Pop (cristian.pop@analog.com)
 *   @author Mihail Chindris (mihail.chindris@analog.com)
********************************************************************************
 * Copyright 2019(c) Analog Devices, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *  - The use of this software may or may not infringe the patent rights
 *    of one or more patent holders.  This license does not release you
 *    from the requirement that you obtain separate licenses from these
 *    patent holders to use this software.
 *  - Use of the software either in source or binary form, must be run
 *    on or directly connected to an Analog Devices Inc. component.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/

#include "iio.h"
#include "iio_types.h"
#include "ctype.h"
#include "tinyiiod.h"
#include "util.h"
#include "list.h"
#include "delay.h"
#include "error.h"
#include "uart.h"

#ifdef ENABLE_IIO_NETWORK
#include "tcp_socket.h"
#include "circular_buffer.h"
#endif

/******************************************************************************/
/********************** Macros and Constants Definitions **********************/
/******************************************************************************/

#define IIOD_PORT		30431
#define MAX_SOCKET_TO_HANDLE	4

/******************************************************************************/
/*************************** Types Declarations *******************************/
/******************************************************************************/

static char header[] =
	"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
	"<!DOCTYPE context ["
	"<!ELEMENT context (device | context-attribute)*>"
	"<!ELEMENT context-attribute EMPTY>"
	"<!ELEMENT device (channel | attribute | debug-attribute | buffer-attribute)*>"
	"<!ELEMENT channel (scan-element?, attribute*)>"
	"<!ELEMENT attribute EMPTY>"
	"<!ELEMENT scan-element EMPTY>"
	"<!ELEMENT debug-attribute EMPTY>"
	"<!ELEMENT buffer-attribute EMPTY>"
	"<!ATTLIST context name CDATA #REQUIRED description CDATA #IMPLIED>"
	"<!ATTLIST context-attribute name CDATA #REQUIRED value CDATA #REQUIRED>"
	"<!ATTLIST device id CDATA #REQUIRED name CDATA #IMPLIED>"
	"<!ATTLIST channel id CDATA #REQUIRED type (input|output) #REQUIRED name CDATA #IMPLIED>"
	"<!ATTLIST scan-element index CDATA #REQUIRED format CDATA #REQUIRED scale CDATA #IMPLIED>"
	"<!ATTLIST attribute name CDATA #REQUIRED filename CDATA #IMPLIED>"
	"<!ATTLIST debug-attribute name CDATA #REQUIRED>"
	"<!ATTLIST buffer-attribute name CDATA #REQUIRED>"
	"]>"
	"<context name=\"xml\" description=\"no-OS analog 1.1.0-g0000000 #1 Tue Nov 26 09:52:32 IST 2019 armv7l\" >"
	"<context-attribute name=\"no-OS\" value=\"1.1.0-g0000000\" />";
static char header_end[] = "</context>";

static const char * const iio_modifier_names[] = {
	[IIO_MOD_X] = "x",
	[IIO_MOD_Y] = "y",
};

/* Parameters used in show and store functions */
struct attr_fun_params {
	void			*dev_instance;
	char			*buf;
	size_t			len;
	struct iio_ch_info	*ch_info;
};

/**
 * @struct iio_interface
 * @brief Links a physical device instance "void *dev_instance"
 * with a "iio_device *iio" that describes capabilities of the device.
 */
struct iio_interface {
	/** Will be: device[0...n] n beeing the count of registerd devices */
	const char		dev_id[10];
	/** Device name */
	const char		*name;
	/** Opened channels */
	uint32_t		ch_mask;
	/** Physical instance of a device */
	void			*dev_instance;
	/** Device descriptor(describes channels and attributes) */
	struct iio_device	*dev_descriptor;
};

struct iio_desc {
	struct tinyiiod		*iiod;
	struct tinyiiod_ops	*iiod_ops;
	enum pysical_link_type	phy_type;
	void			*phy_desc;
	struct list_desc	*interfaces_list;
	char			*xml_desc;
	uint32_t		xml_size;
	uint32_t		xml_size_to_last_dev;
	uint32_t		dev_count;
	struct uart_desc	*uart_desc;
#ifdef ENABLE_IIO_NETWORK
	/* FIFO for socket descriptors */
	struct circular_buffer	*sockets;
	/* Client socket active during an iio_step */
	struct tcp_socket_desc	*current_sock;
	/* Instance of server socket */
	struct tcp_socket_desc	*server;
#endif
};

static struct iio_desc			*g_desc;

/******************************************************************************/
/************************ Functions Definitions *******************************/
/******************************************************************************/

#ifdef ENABLE_IIO_NETWORK

static inline int32_t _pop_sock(struct iio_desc *desc,
				struct tcp_socket_desc **sock)
{
	return cb_read(desc->sockets, sock, sizeof(*sock));
}

static inline int32_t _push_sock(struct iio_desc *desc,
				 struct tcp_socket_desc *sock)
{
	return cb_write(desc->sockets, &sock, sizeof(sock));
}

static inline int32_t _nb_active_sockets(struct iio_desc *desc)
{
	uint32_t size;

	cb_size(desc->sockets, &size);

	return size / sizeof(struct tcp_socket_desc *);
}

/* Blocking until new socket available.
 * Will iterate through a list of sockets */
static int32_t _get_next_socket(struct iio_desc *desc)
{
	struct tcp_socket_desc	*sock;
	int32_t			ret;
	uint32_t		nb_active_sockets;

	/* Get all new waiting sockets.
	 * If none is available, wait until first connection */
	do {
		ret = socket_accept(desc->server, &sock);
		if (ret == -EAGAIN) {
			nb_active_sockets = _nb_active_sockets(desc);
			if (nb_active_sockets == 0) {
				/* Wait until a connection exists */
				mdelay(1);
				continue;
			} else {
				break;
			}
		} else if (IS_ERR_VALUE(ret)) {
			return ret;
		}
		/* Add socket to queue */
		ret = _push_sock(desc, sock);
		if (IS_ERR_VALUE(ret))
			return ret;
	} while (true);

	ret = _pop_sock(desc, &desc->current_sock);
	if (IS_ERR_VALUE(ret)) {
		desc->current_sock = NULL;
		return ret;
	}

	return SUCCESS;
}

static int32_t network_read(const void *data, uint32_t len)
{
	uint32_t	i;
	int32_t		ret;

	if ((int32_t)g_desc->current_sock == -1)
		return -1;

	if (g_desc->current_sock == NULL) {
		ret = _get_next_socket(g_desc);
		if (IS_ERR_VALUE(ret))
			return ret;
	}

	i = 0;
	do {
		ret = socket_recv(g_desc->current_sock,
				  (void *)((uint8_t *)data + i), len - i);
		if (IS_ERR_VALUE(ret)) {
			*(int8_t *)data = '*';
			break;
		}

		i += ret;
	} while (i < len);

	if (ret == -ENOTCONN) {
		/* A socket connection is disconnected, so we release
		 * the resources and don't add it again in the list */
		socket_remove(g_desc->current_sock);
		g_desc->current_sock = (void *)-1;
	}

	return i;
}
#endif

static ssize_t iio_phy_read(char *buf, size_t len)
{
	if (g_desc->phy_type == USE_UART)
		return (ssize_t)uart_read(g_desc->uart_desc, (uint8_t *)buf,
					  (size_t)len);
#ifdef ENABLE_IIO_NETWORK
	else
		return network_read((void *)buf, (uint32_t)len);
#endif

	return -EINVAL;
}

/** Write to a peripheral device (UART, USB, NETWORK) */
static ssize_t iio_phy_write(const char *buf, size_t len)
{
	if (g_desc->phy_type == USE_UART)
		return (ssize_t)uart_write(g_desc->uart_desc,
					   (uint8_t *)buf, (size_t)len);
#ifdef ENABLE_IIO_NETWORK
	else
		return socket_send(g_desc->current_sock, buf, len);
#endif

	return -EINVAL;
}

/**
 * @brief Get channel number.
 * @param ch - String containing channel name + channel number.
 * @return - Channel number. Ex: for "altvoltage0" return 0, for "voltage2"
 * return 2.
 */
static int32_t iio_get_channel_number(const char *ch)
{
	char *p = (char*)ch;
	int32_t ch_num = FAILURE;

	while (*p) {
		if (isdigit(*p))
			ch_num = strtol(p, &p, 10);
		else
			p++;
	}

	return ch_num;
}

/* Get string for channel id from channel type */
static char *get_channel_id(enum iio_chan_type type)
{
	switch (type) {
	case IIO_VOLTAGE:
		return "voltage";
	case IIO_CURRENT:
		return "current";
	case IIO_ALTVOLTAGE:
		return "altvoltage";
	case IIO_ANGL_VEL:
		return "anglvel";
	case IIO_TEMP:
		return "temp";
	}

	return "";
}

static inline void _print_ch_id(char *buff, struct iio_channel *ch)
{
	if(ch->modified) {
		sprintf(buff, "%s_%s", get_channel_id(ch->ch_type), iio_modifier_names[ch->channel2]);
	}
	else {
		if(ch->indexed) {
			sprintf(buff, "%s%d", get_channel_id(ch->ch_type), (int)ch->channel);
		}
		else {
			sprintf(buff, "%s", get_channel_id(ch->ch_type));
		}
	}
}

/**
 * @brief Get channel ID from a list of channels.
 * @param channel - Channel name.
 * @param channels - List of channels.
 * @param ch_out - If "true" is output channel, if "false" is input channel.
 * @return Channel ID, or negative value if attribute is not found.
 */
static inline struct iio_channel *iio_get_channel(const char *channel,
		struct iio_channel **channels, bool ch_out)
{
	int16_t i = 0;
	char	ch_id[20];

	while (channels[i]) {
		_print_ch_id(ch_id, channels[i]);
		if (!strcmp(channel, ch_id) && (channels[i]->ch_out == ch_out))
			return channels[i];
		i++;
	}

	return NULL;
}

/**
 * @brief Find interface with "device_name".
 * @param device_name - Device name.
 * @param iio_interfaces - List of interfaces.
 * @return Interface pointer if interface is found, NULL otherwise.
 */
static struct iio_interface *iio_get_interface(const char *device_name)
{
	struct iio_interface	*interface;
	struct iio_interface	cmp_val;
	int32_t					ret;

	strcpy(cmp_val.dev_id, device_name);

	ret = list_read_find(g_desc->interfaces_list,
			     (void **)&interface, &cmp_val);
	if (IS_ERR_VALUE(ret))
		return NULL;

	return interface;
}

/**
 * @brief Read all attributes from an attribute list.
 * @param device - Physical instance of a device.
 * @param buf - Buffer where values are read.
 * @param len - Maximum length of value to be stored in buf.
 * @param channel - Channel properties.
 * @param attributes - List of attributes to be read.
 * @return Number of bytes read or negative value in case of error.
 */
static ssize_t iio_read_all_attr(struct attr_fun_params *params,
				 struct iio_attribute **attributes)
{
	int16_t i = 0, j = 0;
	char local_buf[256];
	ssize_t attr_length;
	uint32_t *pattr_length;

	while (attributes[i]) {
		attr_length = attributes[i]->show(params->dev_instance,
						  local_buf, params->len, params->ch_info);
		pattr_length = (uint32_t *)(params->buf + j);
		*pattr_length = bswap_constant_32(attr_length);
		j += 4;
		if (attr_length >= 0) {
			sprintf(params->buf + j, "%s", local_buf);
			if (attr_length & 0x3) /* multiple of 4 */
				attr_length = ((attr_length >> 2) + 1) << 2;
			j += attr_length;
		}
		i++;
	}

	return j;
}

/**
 * @brief Write all attributes from an attribute list.
 * @param device - Physical instance of a device.
 * @param buf - Values to be written.
 * @param len - Length of buf.
 * @param channel - Channel properties.
 * @param attributes - List of attributes to be written.
 * @return Number of written bytes or negative value in case of error.
 */
static ssize_t iio_write_all_attr(struct attr_fun_params *params,
				  struct iio_attribute **attributes)
{
	int16_t i = 0, j = 0;
	int16_t attr_length;

	while (attributes[i]) {
		attr_length = bswap_constant_32((uint32_t)(params->buf + j));
		j += 4;
		attributes[i]->store(params->dev_instance, (params->buf + j),
				     attr_length, params->ch_info);
		j += attr_length;
		if (j & 0x3)
			j = ((j >> 2) + 1) << 2;
		i++;
	}

	return params->len;
}

/**
 * @brief Read/write attribute.
 * @param params - Structure describing parameters for store and show functions
 * @param attributes - Array of attributes.
 * @param attr_name - Attribute name to be modified
 * @param is_write -If it has value "1", writes attribute, otherwise reads
 * 		attribute.
 * @return Length of chars written/read or negative value in case of error.
 */
static ssize_t iio_rd_wr_attribute(struct attr_fun_params *params,
				   struct iio_attribute **attributes,
				   char *attr_name,
				   bool is_write)
{
	int16_t i = 0;

	/* Search attribute */
	while (attributes[i]) {
		if (!strcmp(attr_name, attributes[i]->name))
			break;
		i++;
	}

	if (!attributes[i])
		return -ENOENT;

	if (is_write)
		return attributes[i]->store(params->dev_instance, params->buf,
					    params->len, params->ch_info);
	else
		return attributes[i]->show(params->dev_instance, params->buf,
					   params->len, params->ch_info);
}

/**
 * @brief Read global attribute of a device.
 * @param device - String containing device name.
 * @param attr - String containing attribute name.
 * @param buf - Buffer where value is read.
 * @param len - Maximum length of value to be stored in buf.
 * @param debug - Read raw value if set.
 * @return Number of bytes read.
 */
static ssize_t iio_read_attr(const char *device_id, const char *attr, char *buf,
			     size_t len, bool debug)
{
	struct iio_interface	*dev;
	struct attr_fun_params	params;
	struct iio_attribute	**attributes;

	dev = iio_get_interface(device_id);
	if (!dev)
		return FAILURE;

	params.buf = buf;
	params.len = len;
	params.dev_instance = dev->dev_instance;
	params.ch_info = NULL;
	if (debug)
		attributes = dev->dev_descriptor->debug_attributes;
	else
		attributes = dev->dev_descriptor->attributes;

	if (!strcmp(attr, ""))
		return iio_read_all_attr(&params, attributes);
	else
		return iio_rd_wr_attribute(&params,attributes, attr, 0);
}

/**
 * @brief Write global attribute of a device.
 * @param device - String containing device name.
 * @param attr - String containing attribute name.
 * @param buf - Value to be written.
 * @param len - Length of data.
 * @param debug - Write raw value if set.
 * @return Number of written bytes.
 */
static ssize_t iio_write_attr(const char *device_id, const char *attr,
			      const char *buf,
			      size_t len, bool debug)
{
	struct iio_interface	*dev;
	struct attr_fun_params	params;
	struct iio_attribute	**attributes;

	dev = iio_get_interface(device_id);
	if (!dev)
		return -ENODEV;

	params.buf = (char *)buf;
	params.len = len;
	params.dev_instance = dev->dev_instance;
	params.ch_info = NULL;
	if (debug)
		attributes = dev->dev_descriptor->debug_attributes;
	else
		attributes = dev->dev_descriptor->attributes;

	if (!strcmp(attr, ""))
		return iio_write_all_attr(&params, attributes);
	else
		return iio_rd_wr_attribute(&params, attributes, attr, 1);
}

/**
 * @brief Read channel attribute.
 * @param device_name - String containing device name.
 * @param channel - String containing channel name.
 * @param ch_out -Channel type input/output.
 * @param attr - String containing attribute name.
 * @param buf - Buffer where value is stored.
 * @param len - Maximum length of value to be stored in buf.
 * @return - Number of bytes read.
 */
static ssize_t iio_ch_read_attr(const char *device_id, const char *channel,
				bool ch_out, const char *attr, char *buf, size_t len)
{
	struct iio_interface	*dev;
	struct iio_ch_info	ch_info;
	struct iio_channel	*ch;
	struct attr_fun_params	params;

	dev = iio_get_interface(device_id);
	if (!dev)
		return FAILURE;

	ch = iio_get_channel(channel, dev->dev_descriptor->channels, ch_out);
	if (!ch)
		return -ENOENT;
	ch_info.ch_out = ch_out;
	ch_info.ch_num = ch->scan_index;
	params.buf = buf;
	params.len = len;
	params.dev_instance = dev->dev_instance;
	params.ch_info = &ch_info;
	if (!strcmp(attr, ""))
		return iio_read_all_attr(&params, ch->attributes);
	else
		return iio_rd_wr_attribute(&params, ch->attributes, attr, 0);
}

/**
 * @brief Write channel attribute.
 * @param device - String containing device name.
 * @param channel - String containing channel name.
 * @param ch_out - Channel type input/output.
 * @param attr - String containing attribute name.
 * @param buf - Value to be written.
 * @param len - Length of data in "buf" parameter.
 * @return Number of written bytes.
 */
static ssize_t iio_ch_write_attr(const char *device_id, const char *channel,
				 bool ch_out, const char *attr, const char *buf, size_t len)
{
	struct iio_interface	*dev;
	struct iio_ch_info	ch_info;
	struct iio_channel	*ch;
	struct attr_fun_params	params;

	dev = iio_get_interface(device_id);
	if (!dev)
		return -ENOENT;

	ch = iio_get_channel(channel, dev->dev_descriptor->channels, ch_out);
	if (!ch)
		return -ENOENT;
	ch_info.ch_out = ch_out;
	ch_info.ch_num = ch->scan_index;
	params.buf = (char *)buf;
	params.len = len;
	params.dev_instance = dev->dev_instance;
	params.ch_info = &ch_info;
	if (!strcmp(attr, ""))
		return iio_write_all_attr(&params, ch->attributes);
	else
		return iio_rd_wr_attribute(&params, ch->attributes, attr, 1);
}

/**
 * @brief  Open device.
 * @param device - String containing device name.
 * @param sample_size - Sample size.
 * @param mask - Channels to be opened.
 * @return SUCCESS, negative value in case of failure.
 */
static int32_t iio_open_dev(const char *device, size_t sample_size,
			    uint32_t mask)
{
	struct iio_interface *iface;
	uint32_t ch_mask;

	iface = iio_get_interface(device);
	if (!iface)
		return -ENODEV;

	ch_mask = 0xFFFFFFFF >> (32 - iface->dev_descriptor->num_ch);

	if (mask & ~ch_mask)
		return -ENOENT;

	iface->ch_mask = mask;

	return SUCCESS;
}

/**
 * @brief Close device.
 * @param device - String containing device name.
 * @return SUCCESS, negative value in case of failure.
 */
static int32_t iio_close_dev(const char *device)
{
	struct iio_interface *iface;

	iface = iio_get_interface(device);
	if (!iface)
		return FAILURE;
	iface->ch_mask = 0;

	return SUCCESS;
}

/**
 * @brief Get device mask, this specifies the channels that are used.
 * @param device - String containing device name.
 * @param mask - Channels that are opened.
 * @return SUCCESS, negative value in case of failure.
 */
static int32_t iio_get_mask(const char *device, uint32_t *mask)
{
	struct iio_interface *iface;

	iface = iio_get_interface(device);
	if (!iface)
		return -ENODEV;

	*mask = iface->ch_mask;

	return SUCCESS;
}

/**
 * @brief Transfer data from device into RAM.
 * @param device - String containing device name.
 * @param bytes_count - Number of bytes.
 * @return Bytes_count or negative value in case of error.
 */
static ssize_t iio_transfer_dev_to_mem(const char *device, size_t bytes_count)
{
	struct iio_interface *iio_interface = iio_get_interface(device);

	if (iio_interface->dev_descriptor->transfer_dev_to_mem)
		return iio_interface->dev_descriptor->transfer_dev_to_mem(
			       iio_interface->dev_instance,
			       bytes_count, iio_interface->ch_mask);

	return -ENOENT;
}

/**
 * @brief Read chunk of data from RAM to pbuf. Call
 * "iio_transfer_dev_to_mem()" first.
 * This function is probably called multiple times by libtinyiiod after a
 * "iio_transfer_dev_to_mem" call, since we can only read "bytes_count" bytes.
 * @param device - String containing device name.
 * @param pbuf - Buffer where value is stored.
 * @param offset - Offset to the remaining data after reading n chunks.
 * @param bytes_count - Number of bytes to read.
 * @return: Bytes_count or negative value in case of error.
 */
static ssize_t iio_read_dev(const char *device, char *pbuf, size_t offset,
			    size_t bytes_count)
{
	struct iio_interface *iio_interface = iio_get_interface(device);

	if (iio_interface->dev_descriptor->read_data)
		return iio_interface->dev_descriptor->read_data(
			       iio_interface->dev_instance,
			       pbuf, offset,
			       bytes_count, iio_interface->ch_mask);

	return -ENOENT;
}

/**
 * @brief Transfer memory to device.
 * @param device - String containing device name.
 * @param bytes_count - Number of bytes to transfer.
 * @return Bytes_count or negative value in case of error.
 */
static ssize_t iio_transfer_mem_to_dev(const char *device, size_t bytes_count)
{
	struct iio_interface *iio_interface = iio_get_interface(device);

	if (iio_interface->dev_descriptor->transfer_mem_to_dev)
		return iio_interface->dev_descriptor->transfer_mem_to_dev(
			       iio_interface->dev_instance,
			       bytes_count, iio_interface->ch_mask);

	return -ENOENT;
}

/**
 * @brief Write chunk of data into RAM.
 * This function is probably called multiple times by libtinyiiod before a
 * "iio_transfer_mem_to_dev" call, since we can only write "bytes_count" bytes
 * at a time.
 * @param device - String containing device name.
 * @param buf - Values to write.
 * @param offset - Offset in memory after the nth chunk of data.
 * @param bytes_count - Number of bytes to write.
 * @return Bytes_count or negative value in case of error.
 */
static ssize_t iio_write_dev(const char *device, const char *buf,
			     size_t offset, size_t bytes_count)
{
	struct iio_interface *iio_interface = iio_get_interface(device);
	if(iio_interface->dev_descriptor->write_data)
		return iio_interface->dev_descriptor->write_data(
			       iio_interface->dev_instance,
			       (char*)buf, offset, bytes_count,
			       iio_interface->ch_mask);

	return -ENOENT;
}

/**
 * @brief Get a merged xml containing all devices.
 * @param outxml - Generated xml.
 * @return SUCCESS in case of success or negative value otherwise.
 */
static ssize_t iio_get_xml(char **outxml)
{
	if (!outxml)
		return FAILURE;

	*outxml = g_desc->xml_desc;

	return g_desc->xml_size;
}

/**
 * @brief Execute an iio step
 * @param desc - IIo descriptor
 * @return SUCCESS in case of success or negative value otherwise.
 */
ssize_t iio_step(struct iio_desc *desc)
{
	int32_t ret;

#ifdef ENABLE_IIO_NETWORK
	if (desc->phy_type == USE_NETWORK) {
		if (desc->current_sock != NULL &&
		    (int32_t)desc->current_sock != -1) {
			ret = _push_sock(desc, desc->current_sock);
			if (IS_ERR_VALUE(ret))
				return ret;
		}
		desc->current_sock = NULL;
	}
#endif
	return tinyiiod_read_command(desc->iiod);
}

/*
 * Generate an xml describing a device and write it to buff.
 * Will return the size of the xml.
 * If buff_size is 0, no data will be written to buff, but size will be returned
 */
static uint32_t iio_generate_device_xml(struct iio_device *device, char *name,
					int32_t id, char *buff,
					uint32_t buff_size)
{
	struct iio_channel	*ch;
	struct iio_attribute	*attr;
	char			ch_id[50];
	int32_t			i;
	int32_t			j;
	int32_t			k;
	int32_t			n;

	if (buff_size == -1)
		n = 0;
	else
		n = buff_size;

	if (buff == NULL)
		/* Set dummy value for buff. It is used only for counting */
		buff = ch_id;

	i = 0;
	i += snprintf(buff, max(n - i, 0),
		      "<device id=\"device%d\" name=\"%s\">", id, name);

	/* Write channels */
	if (device->channels)
		for (j = 0; device->channels[j]; j++) {
			ch = device->channels[j];
			_print_ch_id(ch_id, ch);
			i += snprintf(buff + i, max(n - i, 0),
				      "<channel id=\"%s\" name=\"%s\""
				      " type=\"%s\" >",
				      ch_id, ch->name,
				      ch->ch_out ? "output" : "input");

			if (ch->scan_type)
				i += snprintf(buff + i, max(n - i, 0),
					      "<scan-element index=\"%d\""
					      " format=\"%s:%c%d/%d&gt;&gt;%d\" />",
					      ch->scan_index,
					      ch->scan_type->is_big_endian ? "be" : "le",
					      ch->scan_type->sign,
					      ch->scan_type->realbits,
					      ch->scan_type->storagebits,
					      ch->scan_type->shift);

			/* Write channel attributes */
			if (ch->attributes)
				for (k = 0; ch->attributes[k]; k++) {
					attr = ch->attributes[k];
					i += snprintf(buff + i, max(n - i, 0),
						      "<attribute name=\"%s\""
						      " filename=\"%s_%s_%s_%s\" />",
						      attr->name,
						      ch->ch_out ? "out" : "in",
						      ch_id, ch->name,
						      attr->name);
				}

			i += snprintf(buff + i, max(n - i, 0), "</channel>");
		}

	/* Write device attributes */
	if (device->attributes)
		for (j = 0; device->attributes[j]; j++)
			i += snprintf(buff + i, max(n - i, 0),
				      "<attribute name=\"%s\" />",
				      device->attributes[j]->name);

	/* Write debug attributes */
	if (device->debug_attributes)
		for (j = 0; device->debug_attributes[j]; j++)
			i += snprintf(buff + i, max(n - i, 0),
				      "<debug-attribute name=\"%s\" />",
				      device->debug_attributes[j]->name);

	/* Write buffer attributes */
	if (device->buffer_attributes)
		for (j = 0; device->buffer_attributes[j]; j++)
			i += snprintf(buff + i, max(n - i, 0),
				      "<buffer-attribute name=\"%s\" />",
				      device->buffer_attributes[j]->name);

	i += snprintf(buff + i, max(n - i, 0), "</device>");

	return i;
}

/**
 * @brief Register interface.
 * @param desc - iio descriptor
 * @param dev_descriptor - Device descriptor
 * @param name - Name to identify the registered device
 * @param dev_instance - Opened instance of the device
 * @return SUCCESS in case of success or negative value otherwise.
 */
ssize_t iio_register(struct iio_desc *desc, struct iio_device *dev_descriptor,
		     char *name, void *dev_instance)
{
	struct iio_interface	*iio_interface;
	int32_t ret;
	int32_t	n;
	int32_t	new_size;
	char	*aux;

	iio_interface = (struct iio_interface *)calloc(1,
			sizeof(*iio_interface));
	if (!iio_interface)
		return -ENOMEM;
	iio_interface->dev_instance = dev_instance;
	iio_interface->name = name;
	iio_interface->dev_descriptor = dev_descriptor;

	/* Get number of bytes needed for the xml of the new device */
	n = iio_generate_device_xml(iio_interface->dev_descriptor, iio_interface->name,
				    desc->dev_count, NULL, -1);

	new_size = desc->xml_size + n;
	aux = realloc(desc->xml_desc, new_size);
	if (!aux) {
		free(iio_interface);
		return -ENOMEM;
	}

	ret = desc->interfaces_list->push(desc->interfaces_list, iio_interface);
	if (IS_ERR_VALUE(ret)) {
		free(iio_interface);
		free(aux);
		return ret;
	}

	desc->xml_desc = aux;
	/* Print the new device xml at the end of the xml */
	iio_generate_device_xml(iio_interface->dev_descriptor,
				iio_interface->name,
				desc->dev_count,
				desc->xml_desc + desc->xml_size_to_last_dev,
				new_size - desc->xml_size_to_last_dev);
	sprintf((char *)iio_interface->dev_id, "device%d", (int)desc->dev_count);
	desc->xml_size_to_last_dev += n;
	desc->xml_size += n;
	/* Copy end header at the end */
	strcat(desc->xml_desc, header_end);

	desc->dev_count++;

	return SUCCESS;
}

/**
 * @brief Unregister interface.
 * @param name - Name of the registered device
 * @return SUCCESS in case of success or negative value otherwise.
 */
ssize_t iio_unregister(struct iio_desc *desc, char *name)
{
	struct iio_interface	*to_remove_interface;
	struct iio_interface	search_interface;
	int32_t			ret;
	int32_t			n;
	char			*aux;

	/* Get if the item is found, get will remove it from the list */
	search_interface.name = name;
	ret = list_get_find(desc->interfaces_list,
			    (void **)&to_remove_interface, &search_interface);
	if (IS_ERR_VALUE(ret))
		return ret;
	free(to_remove_interface);

	/* Get number of bytes needed for the xml of the device */
	n = iio_generate_device_xml(to_remove_interface->dev_descriptor,
				    to_remove_interface->name,
				    desc->dev_count, NULL, -1);

	/* Overwritte the deleted device */
	aux = desc->xml_desc + desc->xml_size_to_last_dev - n;
	strcpy(aux, aux + n);

	/* Decrease the xml size */
	desc->xml_size -= n;
	desc->xml_size_to_last_dev -= n;

	return SUCCESS;
}

static int32_t iio_cmp_interfaces(struct iio_interface *a,
				  struct iio_interface *b)
{
	return strcmp(a->dev_id, b->dev_id);
}

/**
 * @brief Set communication ops and read/write ops that will be called
 * from "libtinyiiod".
 * @param iiod - Structure containing new tinyiiod instance.
 * @param iio_server_ops - Structure containing read/write ops (Ex: read/write to
 * 			UART).
 * @return SUCCESS in case of success or negative value otherwise.
 */
ssize_t iio_init(struct iio_desc **desc, struct iio_init_param *init_param)
{
	int32_t			ret;
	struct iio_desc		*ldesc;
	struct tinyiiod_ops	*ops;

	if (!init_param)
		return -EINVAL;

	ops = (struct tinyiiod_ops *)calloc(1, sizeof(struct tinyiiod_ops));
	if (!ops)
		return FAILURE;

	ldesc = (struct iio_desc *)calloc(1, sizeof(*ldesc));
	if (!ldesc)
		goto free_ops;
	ldesc->iiod_ops = ops;

	/* device operations */
	ops->read_attr = iio_read_attr;
	ops->write_attr = iio_write_attr;
	ops->ch_read_attr = iio_ch_read_attr;
	ops->ch_write_attr = iio_ch_write_attr;
	ops->transfer_dev_to_mem = iio_transfer_dev_to_mem;
	ops->read_data = iio_read_dev;
	ops->transfer_mem_to_dev = iio_transfer_mem_to_dev;
	ops->write_data = iio_write_dev;

	ops->open = iio_open_dev;
	ops->close = iio_close_dev;
	ops->get_mask = iio_get_mask;

	ops->read = iio_phy_read;
	ops->write = iio_phy_write;

	ldesc->xml_size = sizeof(header) + sizeof(header_end);
	ldesc->xml_desc = (char *)malloc(ldesc->xml_size);
	if (!ldesc->xml_desc)
		goto free_desc;

	strcpy(ldesc->xml_desc, header);
	strcat(ldesc->xml_desc, header_end);
	ldesc->xml_size_to_last_dev = sizeof(header) - 1;

	ldesc->phy_type = init_param->phy_type;
	if (init_param->phy_type == USE_UART) {
		ret = uart_init((struct uart_desc **)&ldesc->uart_desc,
				init_param->uart_init_param);
		if (IS_ERR_VALUE(ret))
			goto free_desc;
	}
#ifdef ENABLE_IIO_NETWORK
	else if (init_param->phy_type == USE_NETWORK) {
		ret = socket_init(&ldesc->server,
				  init_param->tcp_socket_init_param);
		if (IS_ERR_VALUE(ret))
			goto free_desc;
		ret = socket_bind(ldesc->server, IIOD_PORT);
		if (IS_ERR_VALUE(ret))
			goto free_pylink;
		ret = socket_listen(ldesc->server, 0);
		if (IS_ERR_VALUE(ret))
			goto free_pylink;
		ret = cb_init(&ldesc->sockets, sizeof(struct tcp_socket_desc *)
			      * MAX_SOCKET_TO_HANDLE);
		if (IS_ERR_VALUE(ret))
			goto free_pylink;
	}
#endif
	else {
		goto free_desc;
	}

	ops->read = iio_phy_read;
	ops->write = iio_phy_write;

	ops->get_xml = iio_get_xml;

	ret = list_init(&ldesc->interfaces_list, LIST_PRIORITY_LIST,
			(f_cmp)iio_cmp_interfaces);
	if (IS_ERR_VALUE(ret))
		goto free_pylink;

	ldesc->iiod = tinyiiod_create(ops);
	if (!(ldesc->iiod))
		goto free_list;

	*desc = ldesc;
	g_desc = ldesc;

	return SUCCESS;

free_list:
	list_remove(ldesc->interfaces_list);
free_pylink:
	if (ldesc->phy_type == USE_UART)
		uart_remove(ldesc->uart_desc);
#ifdef ENABLE_IIO_NETWORK
	else {
		socket_remove(ldesc->server);
		if (ldesc->sockets)
			cb_remove(ldesc->sockets);
	}
#endif
free_desc:
	free(ldesc);
free_ops:
	free(ops);

	return FAILURE;
}

/**
 * @brief Free the resources allocated by "iio_init()".
 * @param iiod: Structure containing tinyiiod instance.
 * @return SUCCESS in case of success or negative value otherwise.
 */
ssize_t iio_remove(struct iio_desc *desc)
{
	struct iio_interface	*iio_interface;

	while (SUCCESS == list_get_first(desc->interfaces_list,
					 (void **)&iio_interface))
		free(iio_interface);
	list_remove(desc->interfaces_list);

	free(desc->iiod_ops);
	tinyiiod_destroy(desc->iiod);

	free(desc->xml_desc);

	if (desc->phy_type == USE_UART) {
		uart_remove(desc->phy_desc);
	}
#ifdef ENABLE_IIO_NETWORK
	else {
		socket_remove(desc->server);
		cb_remove(desc->sockets);
	}
#endif

	free(desc);

	return SUCCESS;
}
