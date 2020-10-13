/*****************************************************************************
 *   @file   main.c
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

#include <sys/platform.h>
#include "adi_initialize.h"
#include <stdio.h>
#include <stdlib.h>
#include "platform_init.h"
#include "spi.h"
#include "spi_extra.h"
#include "adxrs290.h"
#include "adxrs290_iio.h"
#include "drivers/uart/adi_uart.h"
#include "parameters.h"
#include "error.h"
#include "iio.h"
#include "irq.h"
#include "irq_extra.h"
#include "uart.h"
#include "uart_extra.h"
#include "timer.h"

int main(int argc, char *argv[])
{
	/**
	 * Initialize managed drivers and/or services that have been added to 
	 * the project.
	 * @return zero on success 
	 */
	adi_initComponents();
	
	pwr_setup();

	int32_t ret;
	uint32_t data_ch;

	uint8_t ascii_buff[50];
	float ch;

	uint8_t uart_word_len = 8;

	/* iio descriptor. */
	struct iio_desc  *iio_desc;

	/* iio init param */
	struct iio_init_param iio_init_param;

	/* Initialization for UART. */
	struct uart_init_param uart_init_par;

	/* IRQ initial configuration. */
	struct irq_init_param irq_init_param;

	/* IRQ instance. */
	struct irq_ctrl_desc *irq_desc;
	int32_t platform_irq_init_par = 0;

	irq_init_param = (struct irq_init_param ) {
			.irq_ctrl_id = INTC_DEVICE_ID,
			.extra = &platform_irq_init_par
		};

	/* Aducm platform dependent initialization for UART. */
	struct aducm_uart_init_param platform_uart_init_par = {
			.parity = UART_NO_PARITY,
			.stop_bits = UART_ONE_STOPBIT,
			.word_length = UART_WORDLEN_8BITS
	};
	uart_init_par = (struct uart_init_param) {
		.device_id = UART_DEVICE_ID,
		.baud_rate = UART_BAUDRATE,
		.extra = &platform_uart_init_par
	};

	iio_init_param.phy_type = USE_UART;
	iio_init_param.uart_init_param = &uart_init_par;

	ret = irq_ctrl_init(&irq_desc, &irq_init_param);
	if(ret < 0)
		return ret;

	ret = irq_global_enable(irq_desc);
	if (ret < 0)
		return ret;


	struct aducm_spi_init_param aducm_param = {
			.continuous_mode = true,
			.dma = false,
			.half_duplex = false,
			.master_mode = MASTER,
			.spi_channel = SPI1
	};

	struct spi_init_param init_param = {
			.chip_select = 0,
			.extra = &aducm_param,
			.max_speed_hz = 900000,
			.mode = SPI_MODE_3,
			.platform_ops = NULL
	};

	struct adxrs290_init_param adxrs290_param = {
			.spi_init = init_param,
			.mode = ADXRS290_MODE_MEASUREMENT,
			.lpf = ADXRS290_LPF_480HZ,
			.hpf = ADXRS290_HPF_ALL_PASS
	};

	struct adxrs290_dev *adxrs290_device;

	ret = adxrs290_init(&adxrs290_device, &adxrs290_param);
	if (ret)
		return FAILURE;

	/* IIO related */
	ret = iio_init(&iio_desc, &iio_init_param);
		if(ret < 0)
			return ret;

	ret = iio_register(iio_desc, &adxrs290_iio_descriptor, "adxrs290", adxrs290_device);
	if (ret < 0)
		return ret;

	do {
		ret = iio_step(iio_desc);
	} while (true);

	return 0;
}

