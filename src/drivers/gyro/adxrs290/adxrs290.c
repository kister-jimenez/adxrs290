/***************************************************************************//**
 *   @file   adxrs290.c
 *   @author Kister Genesis Jimenez (kister.jimenez@analog.com)
********************************************************************************
 * Copyright 2020(c) Analog Devices, Inc.
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "error.h"
#include "adxrs290.h"

/******************************************************************************/
/************************ Functions Definitions *******************************/
/******************************************************************************/

/**
 * @brief Read device register.
 * @param dev - Device handler.
 * @param address - Register address.
 * @param data - Pointer to the register value container.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t adxrs290_reg_read(struct adxrs290_dev *dev, uint8_t address,
			  uint8_t *data)
{
	uint8_t buff[] = {address | 0x80, 0};

	if(spi_write_and_read(dev->spi_desc, buff, 2) != SUCCESS)
		return FAILURE;

	*data = (uint8_t)buff[1];
	return SUCCESS;
}

/**
 * @brief Write device register.
 * @param dev - Device handler.
 * @param address - Register address.
 * @param data - New register value.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t adxrs290_reg_write(struct adxrs290_dev *dev, uint8_t address,
			   uint8_t data)
{
	uint8_t buff[] = {address & 0x7F, data};

	if(spi_write_and_read(dev->spi_desc, buff, 2) != SUCCESS)
		return FAILURE;
	return SUCCESS;
}

/**
 * @brief Set device operation mode.
 * @param dev - Device handler.
 * @param mode - mode of operation.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t adxrs290_set_op_mode(struct adxrs290_dev *dev, enum adxrs290_mode mode)
{
	int32_t ret = SUCCESS;
	uint8_t val = 0;
	ret = adxrs290_reg_read(dev, ADXRS290_REG_POWER_CTL, &val);
	if (ret!= SUCCESS)
	{
		return FAILURE;
	}
	switch(mode) {
		case ADXRS290_MODE_STANDBY:
			val &= ~ADXRS290_MEASUREMENT;
			break;
		case ADXRS290_MODE_MEASUREMENT:
			val |= ADXRS290_MEASUREMENT;
			break;
		default:
			return FAILURE;
	}
	ret = adxrs290_reg_write(dev, ADXRS290_REG_POWER_CTL, val);
	return ret;
}

/**
 * @brief Get the low-pass filter pole location.
 * @param dev - Device handler.
 * @param lpf - Pointer to Low-pass pole location container.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t adxrs290_get_lpf(struct adxrs290_dev *dev, enum adxrs290_lpf *lpf)
{
	uint8_t data;
	if(adxrs290_reg_read(dev, ADXRS290_REG_FILTER, &data) != SUCCESS)
		return FAILURE;
	*lpf = ADXRS290_LPF(data);
	return SUCCESS;
}

/**
 * @brief Set the low-pass filter pole location.
 * @param dev - Device handler.
 * @param lpf - Low-pass pole location.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t adxrs290_set_lpf(struct adxrs290_dev *dev, enum adxrs290_lpf lpf)
{
	int32_t ret;
	uint8_t reg_val;

	ret = adxrs290_reg_read(dev, ADXRS290_REG_FILTER, &reg_val);
	if (ret != SUCCESS)
		return FAILURE;
	reg_val &= ~ADXRS290_LPF_MASK;
	reg_val |= lpf & ADXRS290_LPF_MASK;
	return adxrs290_reg_write(dev, ADXRS290_REG_FILTER, reg_val);
}

/**
 * @brief Get the high-pass filter pole location.
 * @param dev - Device handler.
 * @param hpf - Pointer to high-pass pole location container.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t adxrs290_get_hpf(struct adxrs290_dev *dev, enum adxrs290_hpf *hpf)
{
	uint8_t data;
	if(adxrs290_reg_read(dev, ADXRS290_REG_FILTER, &data) != SUCCESS)
		return FAILURE;
	*hpf = ADXRS290_HPF(data);
	return SUCCESS;
}

/**
 * @brief Set the low-pass filter pole location.
 * @param dev - Device handler.
 * @param hpf - High-pass pole location.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t adxrs290_set_hpf(struct adxrs290_dev *dev, enum adxrs290_hpf hpf)
{
	int32_t ret;
	uint8_t reg_val;

	ret = adxrs290_reg_read(dev, ADXRS290_REG_FILTER, &reg_val);
	if (ret != SUCCESS)
		return FAILURE;
	reg_val &= ~ADXRS290_HPF_MASK;
	reg_val |= (hpf<<4) & ADXRS290_HPF_MASK;
	return adxrs290_reg_write(dev, ADXRS290_REG_FILTER, reg_val);
}

/**
 * @brief Get the Gyro data channels.
 * @param dev - Device handler.
 * @param ch - Channel to read.
 * @param rate - Pointer to rate value.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t adxrs290_get_rate_data(struct adxrs290_dev *dev,
		enum adxrs290_channel ch, int16_t *rate)
{
	uint8_t data[3];

	data[0]=0x80 | (ADXRS290_REG_DATAX0+ch*2);
	data[1]=0x80 | (ADXRS290_REG_DATAX1+ch*2);
	data[2]=0;

	if(spi_write_and_read(dev->spi_desc, data, 3) != SUCCESS)
		return FAILURE;
	*rate = ((int16_t)data[2])<<8 | data[1];
	return SUCCESS;
}

/**
 * @brief Get the temperature data.
 * @param dev - Device handler.
 * @param temp - Pointer to temperature value.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t adxrs290_get_temp_data(struct adxrs290_dev *dev, int16_t *temp)
{
	uint8_t data[3];

	data[0]=0x80 | ADXRS290_REG_TEMP0;
	data[1]=0x80 | ADXRS290_REG_TEMP1;
	data[2]=0;

	if(spi_write_and_read(dev->spi_desc, data, 3) != SUCCESS)
		return FAILURE;

	*temp = (((int16_t)data[2])<<8 | data[1]) & 0x0FFF;

	return SUCCESS;
}

/**
 * @brief Get the angular X/Y rate data and temperature.
 * @param dev - Device handler.
 * @param rateX - Pointer to X rate value.
 * @param rateY - Pointer to Y rate value..
 * @param temp - Pointer to temperature value.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t adxrs290_get_rate_data_all(struct adxrs290_dev *dev,
		int16_t *rateX, int16_t *rateY, int16_t *temp)
{
	uint8_t data[7];
	uint8_t i;
	for (i=0; i<7; i++)
	{
		data[i]=0x80 | (ADXRS290_REG_DATAX0+i);
	}
	data[7]=0;

	if(spi_write_and_read(dev->spi_desc, data, 7) != SUCCESS)
		return FAILURE;

	*rateX = ((int16_t)data[2])<<8 | data[1];
	*rateY= ((int16_t)data[4])<<8 | data[3];
	*temp= ((int16_t)data[6])<<8 | data[5];
	return SUCCESS;
}

/**
 * Initialize the device.
 * @param device - The device structure.
 * @param init_param - The structure that contains the device initial
 *		       parameters.
 * @return SUCCESS in case of success, negative error code otherwise.
 */
int32_t adxrs290_init(struct adxrs290_dev **device,
		     const struct adxrs290_init_param *init_param)
{
	struct adxrs290_dev *dev;
	int32_t ret = 0;
	uint8_t val;

	dev = (struct adxrs290_dev *)malloc(sizeof(*dev));
	if (!dev)
		goto error;

	ret = spi_init(&dev->spi_desc, &init_param->spi_init);
	if (ret!= SUCCESS)
		goto error;
	ret = adxrs290_reg_read(dev, ADXRS290_REG_DEV_ID, &val);
	if ((ret!=SUCCESS) && (val != ADXRS290_DEV_ID))
		goto error;
	// Enable measurement mode.
	if (init_param->mode==ADXRS290_MODE_MEASUREMENT)
	{
		ret |= adxrs290_reg_write(dev, ADXRS290_REG_POWER_CTL,
				ADXRS290_MEASUREMENT | ADXRS290_TSM);
		if (ret!=SUCCESS)
			goto error;
	}

	// Set initial Band pass filter poles.
	ret |= adxrs290_set_lpf(dev, init_param->lpf);
	if (ret!=SUCCESS)
		goto error;
	ret |= adxrs290_set_hpf(dev, init_param->hpf);
	if (ret!=SUCCESS)
		goto error;

	*device = dev;

	return ret;

error:
	free(dev);

	return ret;
}

/**
 * @brief Free memory allocated by adxrs290_setup().
 * @param dev - Device handler.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t adxl345_remove(struct adxrs290_dev *dev)
{
	int32_t ret;

	ret = spi_remove(dev->spi_desc);

	free(dev);

	return ret;
}


