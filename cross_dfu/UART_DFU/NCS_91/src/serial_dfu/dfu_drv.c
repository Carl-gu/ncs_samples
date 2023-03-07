/**
* Copyright (c) 2017, Nordic Semiconductor ASA
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

#include <string.h>
#include <zephyr.h>
#include <device.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(dfu_drv, 3);

#include "dfu_drv.h"
#include "slip.h"
#include "../app_uart.h"

#define DFU_BUFF_SIZE				(UART_SLIP_SIZE_MAX * 2 + 1)
#define DFU_RX_MAX_DELAY			1000				// in milliseconds

static uart_buff_t m_dfu_buff;
static bool slip_pkt_ready;

/**@brief Check a slip packet
 *
 * @param[in] p_data: pointer of data
 * @param[in] length: length of data
 *
 * @return true: input data is a valid slip packet
 * @return false: input data is a invalid slip packet
 */
static bool slip_packet_check(u8_t* p_data, u16_t length)
{
	return p_data[length - 1] == SLIP_END;
}

/**@brief Send data by UART
 *
 * @param[in] p_data: pointer of data
 * @param[in] length: length of data
 *
 * @return 0: success
 * @return neg: error
 */
int dfu_drv_tx(const u8_t *p_data, u16_t length)
{
	int rc = 0;
	u32_t slip_pkt_len;

	if (length > UART_SLIP_SIZE_MAX) {
		rc = -3;
	}
	else {
		encode_slip(m_dfu_buff.p_data, &slip_pkt_len, p_data, length);

		rc = app_uart_send(m_dfu_buff.p_data, slip_pkt_len);
	}

	return rc;
}

/**@brief Receive data by UART
 *
 * @param[in] p_data: pointer of data
 * @param[in] max_len: max length of data
 * @param[out] p_real_len: pointer of real length
 *
 * @return 0: success
 * @return neg: error
 */
int dfu_drv_rx(u8_t *p_data, u32_t max_len, u32_t *p_real_len)
{
	int rc = 0;
	u32_t start_time;

	start_time = k_uptime_get_32();
	while (true) {
		// Check timeout
		if (k_uptime_get_32() - start_time > DFU_RX_MAX_DELAY) {
			LOG_ERR("Wait response timeout");
			rc = -1;
			break;
		}

		// Wait slip packet
		if (slip_pkt_ready == true) {
			decode_slip(p_data, p_real_len, m_dfu_buff.p_data, m_dfu_buff.length);

			if (*p_real_len > max_len) {
				rc = -2;
			}

			uart_buffer_reset(&m_dfu_buff);
			app_uart_rx_reset();

			slip_pkt_ready = false;

			break;
		}
	}
	return rc;
}

/**@brief UART rx data ready handler
 *
 * @param p_data: pointer of data
 * @param length: length of data
 *
 * @return n/a
 */
static void rx_ready_handler(u8_t* p_data, u16_t length)
{
	if (!slip_pkt_ready) {
		m_dfu_buff.length = length;

		if (slip_packet_check(p_data, length)) {
			slip_pkt_ready = true;
		}
	}
}

/**@brief Initialize serial DFU driver
 *
 * @param p_device: pointer of device
 *
 * @return 0: success
 * @return neg: error
 */
int dfu_drv_init(struct device* p_device)
{
	int rc;
	u8_t* p_mem;

	slip_pkt_ready = false;

	p_mem = k_malloc(DFU_BUFF_SIZE);
	if (!p_mem) {
		LOG_ERR("Insufficient memory");
		return -ENOSR;
	}

	rc = app_uart_init(p_device, p_mem, DFU_BUFF_SIZE);
	if (rc != 0) {
		LOG_ERR("UART device init failed");
		return -ENXIO;
	}

	m_dfu_buff.p_data = p_mem;
	m_dfu_buff.length = 0;
	m_dfu_buff.max_len = DFU_BUFF_SIZE;

	app_uart_rx_cb_set(rx_ready_handler);

	return rc;
}

/**@brief Un-initialize serial DFU driver
 *
 * @return n/a
 */
void dfu_drv_uninit()
{
	app_uart_uninit();

	k_free(m_dfu_buff.p_data);
	m_dfu_buff.length = 0;
}