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

#pragma once

#ifndef DFU_DRV_H__
#define DFU_DRV_H__

#include <zephyr.h>
#include <device.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define UART_SLIP_SIZE_MAX		128


/**@brief Initialize serial DFU driver
 *
 * @param p_device: pointer of device
 *
 * @return 0: success
 * @return neg: error
 */
int dfu_drv_init(struct device* p_device);


/**@brief Un-initialize serial DFU driver */
void dfu_drv_uninit();

/**@brief Receive data by UART
 *
 * @param[in] p_data: pointer of data
 * @param[in] max_len: max length of data
 * @param[out] p_real_len: pointer of real length
 *
 * @return 0: success
 * @return neg: error
 */
int dfu_drv_rx(u8_t *p_data, u32_t max_len, u32_t *p_real_len);

/**@brief Send data by UART
 *
 * @param[in] p_data: pointer of data
 * @param[in] length: length of data
 *
 * @return 0: success
 * @return neg: error
 */
int dfu_drv_tx(const u8_t *p_data, u16_t length);

#ifdef __cplusplus
}   /* ... extern "C" */
#endif  /* __cplusplus */


#endif // DFU_DRV_H__
