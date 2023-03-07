/**
* Copyright (c) 2018, Nordic Semiconductor ASA
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification,
* are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form, except as embedded into a Nordic
*    Semiconductor ASA integrated circuit in a product or a software update for
*    such product, must reproduce the above copyright notice, this list of
*    conditions and the following disclaimer in the documentation and/or other
*    materials provided with the distribution.
*
* 3. Neither the name of Nordic Semiconductor ASA nor the names of its
*    contributors may be used to endorse or promote products derived from this
*    software without specific prior written permission.
*
* 4. This software, with or without modification, must only be used with a
*    Nordic Semiconductor ASA integrated circuit.
*
* 5. Any software provided in binary form under this license must not be reverse
*    engineered, decompiled, modified and/or disassembled.
*
* THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/
#include <zephyr.h>
#include <device.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(serial_dfu, 3);

#include "dfu_drv.h"
#include "dfu_host.h"
#include "dfu_file.h"

static struct k_work wk_start_dfu;

/**@brief Start to send DFU file
 *
 * @param[in] ip_addr: address of init packet
 * @param[in] ip_size: file size of init packet
 * @param[in] fw_addr: address of firmware bin
 * @param[in] fw_size: file size of firmware bin
 *
 * @return 0: success
 * @return neg: error
 */
static int dfu_file_send(u32_t ip_addr, u32_t ip_size, u32_t fw_addr, u32_t fw_size)
{
	int err_code;

	u8_t* p_ip_data = (u8_t*)ip_addr;
	u8_t* p_fw_data = (u8_t*)fw_addr;

	err_code = dfu_host_setup();

	if (!err_code) {
		err_code = dfu_host_send_ip(p_ip_data, ip_size);
	}

	if (!err_code) {
		err_code = dfu_host_send_fw(p_fw_data, fw_size);
	}

	return err_code;
}

/**@brief Handler for start dfu worker */
static void wk_start_dfu_handler(struct k_work* unused)
{
	int rc;

	u32_t ip_addr = 0;
	u32_t ip_size = 0;
	u32_t fw_addr = 0;
	u32_t fw_size = 0;

	if (dfu_file_type() != IMAGE_TYPE_NRF52) {
		LOG_ERR("File type is invalid");
		return;
	}

	LOG_INF("Start serial DFU...");

	dfu_file_info(&ip_addr, &ip_size, &fw_addr, &fw_size);

	rc = dfu_file_send(ip_addr, ip_size, fw_addr, fw_size);
	if (rc == 0) {
		LOG_INF("nRF52 Serial DFU success");
	}
	else {
		LOG_ERR("DFU error: %d", rc);
	}
}

/**@brief Initialize serial dfu module
 *
 * @param[in] p_device: pointer of UART device
 *
 * @return 0: success
 * @return neg: error
 */
int serial_dfu_init(struct device* p_device)
{
	k_work_init(&wk_start_dfu, wk_start_dfu_handler);

	return dfu_drv_init(p_device);
}

/**@brief Un-initialize serial dfu module */
void serial_dfu_uninit(void) 
{
	void dfu_drv_uninit();
}

/**@brief Start to perform serial dfu */
void serial_dfu_start(void)
{
	//k_work_submit(&wk_start_dfu);
	wk_start_dfu_handler(NULL);
}


