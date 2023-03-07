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

#include <stdio.h>
#include <string.h>
#include <zephyr.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(dfu_req, 3);

#include "dfu_host.h"
#include "crc32.h"
#include "dfu_drv.h"

#define REQ_DATA_SIZE_MAX		UART_SLIP_SIZE_MAX
#define RSP_DATA_SIZE_MAX		UART_SLIP_SIZE_MAX

/**
* @brief DFU protocol operation.
*/
typedef enum
{
	NRF_DFU_OP_PROTOCOL_VERSION  = 0x00,     //!< Retrieve protocol version.
	NRF_DFU_OP_OBJECT_CREATE     = 0x01,     //!< Create selected object.
	NRF_DFU_OP_RECEIPT_NOTIF_SET = 0x02,     //!< Set receipt notification.
	NRF_DFU_OP_CRC_GET           = 0x03,     //!< Request CRC of selected object.
	NRF_DFU_OP_OBJECT_EXECUTE    = 0x04,     //!< Execute selected object.
	NRF_DFU_OP_OBJECT_SELECT     = 0x06,     //!< Select object.
	NRF_DFU_OP_MTU_GET           = 0x07,     //!< Retrieve MTU size.
	NRF_DFU_OP_OBJECT_WRITE      = 0x08,     //!< Write selected object.
	NRF_DFU_OP_PING              = 0x09,     //!< Ping.
	NRF_DFU_OP_HARDWARE_VERSION  = 0x0A,     //!< Retrieve hardware version.
	NRF_DFU_OP_FIRMWARE_VERSION  = 0x0B,     //!< Retrieve firmware version.
	NRF_DFU_OP_ABORT             = 0x0C,     //!< Abort the DFU procedure.
	NRF_DFU_OP_RESPONSE          = 0x60,     //!< Response.
	NRF_DFU_OP_INVALID           = 0xFF
} nrf_dfu_op_t;

/**
* @brief DFU operation result code.
*/
typedef enum
{
	NRF_DFU_RES_CODE_INVALID                 = 0x00,    //!< Invalid opcode.
	NRF_DFU_RES_CODE_SUCCESS                 = 0x01,    //!< Operation successful.
	NRF_DFU_RES_CODE_OP_CODE_NOT_SUPPORTED   = 0x02,    //!< Opcode not supported.
	NRF_DFU_RES_CODE_INVALID_PARAMETER       = 0x03,    //!< Missing or invalid parameter value.
	NRF_DFU_RES_CODE_INSUFFICIENT_RESOURCES  = 0x04,    //!< Not enough memory for the data object.
	NRF_DFU_RES_CODE_INVALID_OBJECT          = 0x05,    //!< Data object does not match the firmware and hardware requirements, the signature is wrong, or parsing the command failed.
	NRF_DFU_RES_CODE_UNSUPPORTED_TYPE        = 0x07,    //!< Not a valid object type for a Create request.
	NRF_DFU_RES_CODE_OPERATION_NOT_PERMITTED = 0x08,    //!< The state of the DFU process does not allow this operation.
	NRF_DFU_RES_CODE_OPERATION_FAILED        = 0x0A,    //!< Operation failed.
	NRF_DFU_RES_CODE_EXT_ERROR               = 0x0B,    //!< Extended error. The next byte of the response contains the error code of the extended error (see @ref nrf_dfu_ext_error_code_t.
} nrf_dfu_result_t;

/**
* @brief @ref NRF_DFU_OP_OBJECT_SELECT response details.
*/
typedef struct
{
	u32_t offset;                    //!< Current offset.
	u32_t crc;                       //!< Current CRC.
	u32_t max_size;                  //!< Maximum size of selected object.
} nrf_dfu_response_select_t;

/**
* @brief @ref NRF_DFU_OP_CRC_GET response details.
*/
typedef struct
{
	u32_t offset;                    //!< Current offset.
	u32_t crc;                       //!< Current CRC.
} nrf_dfu_response_crc_t;

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

static u8_t  ping_id = 0;
static u16_t prn = 0;
static u16_t mtu = 0;

static u8_t send_data[REQ_DATA_SIZE_MAX];
static u8_t receive_data[RSP_DATA_SIZE_MAX];

static u16_t get_u16_le(const u8_t* p_data)
{
	u16_t data;

	data  = ((u16_t)*(p_data + 0) << 0);
	data += ((u16_t)*(p_data + 1) << 8);

	return data;
}

static void put_u16_le(u8_t* p_data, u16_t data)
{
	*(p_data + 0) = (u8_t)(data >> 0);
	*(p_data + 1) = (u8_t)(data >> 8);
}

static u32_t get_u32_le(const u8_t* p_data)
{
	u32_t data;

	data  = ((u32_t)*(p_data + 0) <<  0);
	data += ((u32_t)*(p_data + 1) <<  8);
	data += ((u32_t)*(p_data + 2) << 16);
	data += ((u32_t)*(p_data + 3) << 24);

	return data;
}

static void put_u32_le(u8_t* p_data, u32_t data)
{
	*(p_data + 0) = (u8_t)(data >>  0);
	*(p_data + 1) = (u8_t)(data >>  8);
	*(p_data + 2) = (u8_t)(data >> 16);
	*(p_data + 3) = (u8_t)(data >> 24);
}

static int send_raw(const u8_t* pData, u32_t nSize)
{
	return dfu_drv_tx(pData, nSize);
}

static int get_rsp(nrf_dfu_op_t oper, u32_t* p_data_cnt)
{
	LOG_DBG("%s", __func__);

	int rc;

	rc = dfu_drv_rx(receive_data, sizeof(receive_data), p_data_cnt);

	if (!rc)
	{
		if (*p_data_cnt >= 3 &&
			receive_data[0] == NRF_DFU_OP_RESPONSE &&
			receive_data[1] == oper)
		{
			if (receive_data[2] != NRF_DFU_RES_CODE_SUCCESS)
			{
				u16_t rsp_error = receive_data[2];

				// get 2-byte error code, if applicable
				if (*p_data_cnt >= 4)
					rsp_error = (rsp_error << 8) + receive_data[3];

				LOG_ERR("Bad result code (0x%X)!", rsp_error);

				rc = 1;
			}
		}
		else
		{
			LOG_ERR("Invalid response!");

			rc = 1;
		}
	}

	return rc;
}

static int req_ping(u8_t id)
{
	LOG_DBG("%s", __func__);	

	int rc;
	u8_t send_data[2] = { NRF_DFU_OP_PING };

	send_data[1] = id;
	rc = send_raw(send_data, sizeof(send_data));

	if (!rc)
	{
		u32_t data_cnt;

		rc = get_rsp(NRF_DFU_OP_PING, &data_cnt);

		if (!rc)
		{
			if (data_cnt != 4 ||
				receive_data[3] != id)
			{
				LOG_ERR("Bad ping id!");

				rc = 1;
			}
		}
	}

	return rc;
}

static int req_set_prn(u16_t prn)
{
	LOG_DBG("%s", __func__);	

	int rc;
	u8_t send_data[3] = { NRF_DFU_OP_RECEIPT_NOTIF_SET };

	LOG_INF("Set Packet Receipt Notification %u", prn);

	put_u16_le(send_data + 1, prn);
	rc = send_raw(send_data, sizeof(send_data));

	if (!rc)
	{
		u32_t data_cnt;

		rc = get_rsp(NRF_DFU_OP_RECEIPT_NOTIF_SET, &data_cnt);
	}

	return rc;
}

static int req_get_mtu(u16_t* p_mtu)
{
	LOG_DBG("%s", __func__);

	int rc;
	u8_t send_data[1] = { NRF_DFU_OP_MTU_GET };

	rc = send_raw(send_data, sizeof(send_data));

	if (!rc)
	{
		u32_t data_cnt;

		rc = get_rsp(NRF_DFU_OP_MTU_GET, &data_cnt);

		if (!rc)
		{
			if (data_cnt == 5)
			{
				u16_t mtu = get_u16_le(receive_data + 3);

				*p_mtu = mtu;

				LOG_INF("MTU: %d", mtu);
			}
			else
			{
				LOG_ERR("Invalid MTU!");

				rc = 1;
			}
		}
	}

	return rc;
}

static int req_obj_select(u8_t obj_type, nrf_dfu_response_select_t* p_select_rsp)
{
	int rc;
	u8_t send_data[2] = { NRF_DFU_OP_OBJECT_SELECT };

	LOG_DBG("Selecting Object: type:%u", obj_type);

	send_data[1] = obj_type;
	rc = send_raw(send_data, sizeof(send_data));

	if (!rc)
	{
		u32_t data_cnt;

		rc = get_rsp(NRF_DFU_OP_OBJECT_SELECT, &data_cnt);

		if (!rc)
		{
			if (data_cnt == 15)
			{
				p_select_rsp->max_size = get_u32_le(receive_data + 3);
				p_select_rsp->offset   = get_u32_le(receive_data + 7);
				p_select_rsp->crc      = get_u32_le(receive_data + 11);

				LOG_DBG("Object selected:  max_size:%u offset:%u crc:0x%08X", p_select_rsp->max_size, p_select_rsp->offset, p_select_rsp->crc);
			}
			else
			{
				LOG_ERR("Invalid object response!");

				rc = 1;
			}
		}
	}

	return rc;
}

static int req_obj_create(u8_t obj_type, u32_t obj_size)
{
	LOG_DBG("%s", __func__);	

	int rc;
	u8_t send_data[6] = { NRF_DFU_OP_OBJECT_CREATE };

	send_data[1] = obj_type;
	put_u32_le(send_data + 2, obj_size);
	rc = send_raw(send_data, sizeof(send_data));

	if (!rc)
	{
		u32_t data_cnt;

		rc = get_rsp(NRF_DFU_OP_OBJECT_CREATE, &data_cnt);
	}

	return rc;
}

static int stream_data(const u8_t* p_data, u32_t data_size)
{
	LOG_DBG("%s", __func__);	

	int rc = 0;
	u32_t pos, stp;
	u32_t stp_max = 5;

	if (p_data == NULL || !data_size)
	{
		rc = 1;
	}

	if (!rc)
	{
		if (mtu >= 5)
		{
			stp_max = (mtu - 1) / 2 - 1;
		}
		else
		{
			LOG_ERR("MTU is too small to send data!");

			rc = 1;

			return 1;
		}
	}

	for (pos = 0; !rc && pos < data_size; pos += stp)
	{
		send_data[0] = NRF_DFU_OP_OBJECT_WRITE;
		stp = MIN((data_size - pos), stp_max);
		memcpy(send_data + 1, p_data + pos, stp);
		rc = send_raw(send_data, stp + 1);
	}

	return rc;
}

static int req_get_crc(nrf_dfu_response_crc_t* p_crc_rsp)
{
	LOG_DBG("%s", __func__);

	int rc;
	u8_t send_data[1] = { NRF_DFU_OP_CRC_GET };

	rc = send_raw(send_data, sizeof(send_data));

	if (!rc)
	{
		u32_t data_cnt;

		rc = get_rsp(NRF_DFU_OP_CRC_GET, &data_cnt);

		if (!rc)
		{
			if (data_cnt == 11)
			{
				p_crc_rsp->offset = get_u32_le(receive_data + 3);
				p_crc_rsp->crc    = get_u32_le(receive_data + 7);
			}
			else
			{
				LOG_ERR("Invalid CRC response!");

				rc = 1;
			}
		}
	}

	return rc;
}

static int req_obj_execute()
{
	LOG_DBG("%s", __func__);

	int rc;
	u8_t send_data[1] = { NRF_DFU_OP_OBJECT_EXECUTE };

	rc = send_raw(send_data, sizeof(send_data));

	if (!rc)
	{
		u32_t data_cnt;

		rc = get_rsp(NRF_DFU_OP_OBJECT_EXECUTE, &data_cnt);
	}

	return rc;
}

static int stream_data_crc(const u8_t* p_data, u32_t data_size, u32_t pos, u32_t* p_crc)
{
	LOG_DBG("%s", __func__);

	int rc;
	nrf_dfu_response_crc_t rsp_crc;

	LOG_DBG("Streaming Data: len:%u offset:%u crc:0x%08X", data_size, pos, *p_crc);

	rc = stream_data(p_data, data_size);

	if (!rc)
	{
		*p_crc = crc32_compute(p_data, data_size, p_crc);

		rc = req_get_crc(&rsp_crc);
	}

	if (!rc)
	{
		if (rsp_crc.offset != pos + data_size)
		{
			LOG_ERR("Invalid offset (%u -> %u)!", pos + data_size, rsp_crc.offset);

			rc = 2;
		}
		if (rsp_crc.crc != *p_crc)
		{
			LOG_ERR("Invalid CRC (0x%08X -> 0x%08X)!", *p_crc, rsp_crc.crc);

			rc = 2;
		}
	}

	return rc;
}

static int try_recover_ip(const u8_t* p_data, u32_t data_size,
						  nrf_dfu_response_select_t* p_rsp_recover,
						  const nrf_dfu_response_select_t* p_rsp_select)
{
	LOG_DBG("%s", __func__);	

	int rc = 0;
	u32_t pos_start, len_remain;
	u32_t crc_32;

	*p_rsp_recover = *p_rsp_select;

	pos_start = p_rsp_recover->offset;

	if (pos_start > 0 && pos_start <= data_size)
	{
		crc_32 = crc32_compute(p_data, pos_start, NULL);

		if (p_rsp_select->crc != crc_32)
		{
			pos_start = 0;
		}
	}
	else
	{
		pos_start = 0;
	}

	if (pos_start > 0 && pos_start < data_size)
	{
		len_remain = data_size - pos_start;
		rc = stream_data_crc(p_data + pos_start, len_remain, pos_start, &crc_32);
		if (!rc)
		{
			pos_start += len_remain;
		}
		else if (rc == 2)
		{
			// when there is a CRC error, discard previous init packet
			rc = 0;
			pos_start = 0;
		}
	}

	if (!rc && pos_start == data_size)
	{
		rc = req_obj_execute();
	}

	p_rsp_recover->offset = pos_start;

	return rc;
}

static int try_recover_fw(const u8_t* p_data, u32_t data_size,
						  nrf_dfu_response_select_t* p_rsp_recover,
						  const nrf_dfu_response_select_t* p_rsp_select)
{
	LOG_DBG("%s", __func__);

	int rc = 0;
	u32_t max_size, stp_size;
	u32_t pos_start, len_remain;
	u32_t crc_32;
	int obj_exec = 1;

	*p_rsp_recover = *p_rsp_select;

	pos_start = p_rsp_recover->offset;

	if (pos_start > data_size)
	{
		LOG_ERR("Invalid firmware offset reported!");

		rc = 1;
	}
	else if (pos_start > 0)
	{
		max_size = p_rsp_select->max_size;
		crc_32 = crc32_compute(p_data, pos_start, NULL);
		len_remain = pos_start % max_size;

		if (p_rsp_select->crc != crc_32)
		{
			pos_start -= ((len_remain > 0) ? len_remain : max_size);
			p_rsp_recover->offset = pos_start;

			return rc;
		}

		if (len_remain > 0)
		{
			stp_size = max_size - len_remain;

			rc = stream_data_crc(p_data + pos_start, stp_size, pos_start, &crc_32);
			if (!rc)
			{
				pos_start += stp_size;
			}
			else if (rc == 2)
			{
				rc = 0;

				pos_start -= len_remain;

				obj_exec = 0;
			}

			p_rsp_recover->offset = pos_start;
		}

		if (!rc && obj_exec)
		{
			rc = req_obj_execute();
		}
	}

	return rc;
}

bool dfu_host_bl_mode_check(void)
{
	int rc;
	int cnt = 3;	// Try 3 times

	while (cnt--) {
		rc = req_ping(ping_id++);
		if (!rc) {
			break;
		}
	}

	return rc == 0;
}

int dfu_host_setup(void)
{
	int rc;

	ping_id++;

	rc = req_ping(ping_id);

	if (!rc)
	{
		rc = req_set_prn(prn);
	}

	if (!rc)
	{
		rc = req_get_mtu(&mtu);
	}

	return rc;
}

int dfu_host_send_ip(const u8_t* p_data, u32_t data_size)
{
	int rc = 0;
	u32_t crc_32 = 0;
	nrf_dfu_response_select_t rsp_select;
	nrf_dfu_response_select_t rsp_recover;

	LOG_INF("Sending init packet...");

	if (p_data == NULL || !data_size)
	{
		LOG_ERR("Invalid init packet!");

		rc = 1;
	}

	if (!rc)
	{
		rc = req_obj_select(0x01, &rsp_select);
	}

	if (!rc)
	{
		rc = try_recover_ip(p_data, data_size, &rsp_recover, &rsp_select);

		if (!rc && rsp_recover.offset == data_size)
			return rc;
	}

	if (!rc)
	{
		if (data_size > rsp_select.max_size)
		{
			LOG_ERR("Init packet too big!");

			rc = 1;
		}
	}

	if (!rc)
	{
		rc = req_obj_create(0x01, data_size);
	}

	if (!rc)
	{
		rc = stream_data_crc(p_data, data_size, 0, &crc_32);
	}

	if (!rc)
	{
		rc = req_obj_execute();
	}

	return rc;
}

int dfu_host_send_fw(const u8_t* p_data, u32_t data_size)
{
	int rc = 0;
	u32_t max_size, stp_size, pos;
	u32_t crc_32 = 0;
	nrf_dfu_response_select_t rsp_select;
	nrf_dfu_response_select_t rsp_recover;
	u32_t pos_start;

	LOG_INF("Sending firmware file...");

	if (p_data == NULL || !data_size)
	{
		LOG_ERR("Invalid firmware data!");

		rc = 1;
	}

	if (!rc)
	{
		rc = req_obj_select(0x02, &rsp_select);
	}

	if (!rc)
	{
		rc = try_recover_fw(p_data, data_size, &rsp_recover, &rsp_select);
	}

	if (!rc)
	{
		max_size = rsp_select.max_size;

		pos_start = rsp_recover.offset;
		crc_32 = crc32_compute(p_data, pos_start, &crc_32);

		for (pos = pos_start; pos < data_size; pos += stp_size)
		{
			stp_size = MIN((data_size - pos), max_size);

			rc = req_obj_create(0x02, stp_size);

			if (!rc)
			{
				rc = stream_data_crc(p_data + pos, stp_size, pos, &crc_32);
			}

			if (!rc)
			{
				rc = req_obj_execute();
			}

			if (rc)
				break;
		}
	}

	return rc;
}
