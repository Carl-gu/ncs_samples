#ifndef APP_CMD_H__
#define APP_CMD_H__

#include <stdint.h>
#include <zephyr.h>
#include <device.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************
    cmd format:

    -------------------------------------------------------------
        Field     |  Start  |  Length  |  OPCODE  |  PDU  |  CRC  |
    -------------------------------------------------------------
        Len(byte) |    1    |    2     |    1     | >= 0  |   2   |
    -------------------------------------------------------------

    Length = len(OPCODE + PDU)
    CRC = crc16(Length + OPCODE + PDU)

    All use little endian

*******************************************************************/
#define CMD_TYPE_REQUEST          0
#define CMD_TYPE_RESPONSE         1

#define CMD_FMT_START_REQ         0x59
#define CMD_FMT_START_RSP         0x51

#define CMD_FMT_SIZE_START        1
#define CMD_FMT_SIZE_LEN          2
#define CMD_FMT_SIZE_OPCODE       1
#define CMD_FMT_SIZE_CRC          2

#define CMD_FMT_OFFSET_START      0
#define CMD_FMT_OFFSET_LEN        1
#define CMD_FMT_OFFSET_OPCODE     3
#define CMD_FMT_OFFSET_PDU        4

// Internal commands: 0x10 - 0x1F
#define CMD_OP_INTERNAL     0x10
#define CMD_OP_PING         (CMD_OP_INTERNAL + 1)
#define CMD_OP_RAW_DATA     (CMD_OP_INTERNAL + 2)

/* Response data for ok */
#define CMD_RSP_OK          { 'o', 'k' }
/* Response data for timeout */
#define CMD_RSP_TIMEOUT     { 't', 'o' }
/* Response data for un-registered */
#define CMD_RSP_UNREG       { 'u', 'r' }


/**@typedef cmd work mode */
typedef enum
{
    CMD_MODE_IDLE,              /* Idle */
    CMD_MODE_HOST,              /* Send request and wait for response */
    CMD_MODE_SLAVE,             /* Receive request and return response */
} cmd_mode_t;

/**@typedef cmd work state */
typedef enum
{
    CMD_STATE_IDLE,             /* Idle */
    CMD_STATE_REQ_SENDING,      /* The request is being sent */
    CMD_STATE_REQ_SENT,         /* The request is sent */
    CMD_STATE_REQ_RECEIVING,    /* The request is being received */
    CMD_STATE_REQ_RECEIVED,     /* The request is received */
    CMD_STATE_RSP_SENDING,      /* The response is being sent */
    CMD_STATE_RSP_SENT,         /* The response is sent */
    CMD_STATE_RSP_RECEIVING,    /* The response is being receiving */
    CMD_STATE_RSP_RECEIVED,     /* Thre response is received */
    CMD_STATE_ERR_TIMEOUT,      /* Error for waiting response timeout */
    CMD_STATE_ERR_SEND,         /* Error for UART sending */
    CMD_STATE_ERR_RECEIVE,      /* Error for UART receiving */
} cmd_state_t;

/**@typedef sending respond callback type
 *
 * @param[in] p_data: pointer of data
 * @param[in] length: data length
 *
 * @return 0: success
 * @return neg: error
 */
typedef int (*cmd_respond_t)(uint8_t* p_data, uint16_t len);

/**@typedef process request callback function
 *
 * @param[in] p_req: pointer of request data
 * @param[in] req_len: request data length
 * @param[in] respond: sending response callback function
 *
 * @return 0: success
 * @return neg: error
 */
typedef int (*req_cb_t)(uint8_t* p_req, uint16_t req_len, cmd_respond_t respond);

/**@typedef process response callback function
 *
 * @param[in] p_rsp: pointer of response data
 * @param[in] rsp_len: response data length
 *
 * @return n/a
 */
typedef void (*rsp_cb_t)(uint8_t* p_rsp, uint16_t rsp_len);

/**@typedef app cmd callback */
typedef struct
{
    uint8_t     op_code;
    req_cb_t    proc_req;       /* request processing callback */
    rsp_cb_t    proc_rsp;       /* response processing callback */
} cmd_cb_t;

/**@typedef app cmd */
typedef struct
{
    uint8_t     type;           /* request or response */
    uint8_t     op_code;        /* op code */
    uint8_t*    p_data;         /* PDU data */
    uint16_t    length;         /* PDU data length */
} app_cmd_t;

/**@typedef app cmd context */
typedef struct
{
    cmd_mode_t   mode;
    cmd_state_t  state;
    app_cmd_t    cmd;
} cmd_context_t;

/**@typedef app cmd event */
typedef struct
{
    uint8_t  op_code;
    uint8_t* p_data;
    uint16_t length;
    bool     timeout;
} cmd_event_t;

/**@brief cmd event callback */
typedef void (*cmd_event_cb_t)(cmd_event_t* p_event);

/**@brief Initialize app_cmd module.
 *
 * @param[in] p_device: pointer of uart device.
 */
int app_cmd_init(struct device* p_device);

/**@brief Un-initialize app cmd module */
void app_cmd_uninit(void);

/**@brief Register response processing callback */
void app_cmd_event_cb_register(cmd_event_cb_t cb);

/**@brief Send a request cmd
 *
 * @param[in] op_code: cmd op code
 * @param[in] p_data: pointer of cmd data
 * @param[in] length: length of cmd data
 *
 * @return 0: success
 * @return -1: error
 */
uint32_t app_cmd_request(uint8_t op_code, uint8_t* p_data, uint16_t length);

/**@brief Add a cmd to the list
 *
 * @param[in] op_code: op code of cmd
 * @param[in] req_cb: request callback function
 * @param[in] rsp_cb: response callback function
 *
 * @return 0: success
 * @return -1: cmd list is full
 * @return -2: op code is existed
 * @return -2: op code is not added, use app_cmd_add to add it
 */
int app_cmd_add(uint8_t op_code, req_cb_t req_cb, rsp_cb_t rsp_cb);

/*@brief Send a ping request */
void cmd_request_ping(void);

/*@brief Send a raw data request */
void cmd_request_raw_data(uint8_t* p_data, uint16_t length);


#ifdef __cplusplus
}
#endif

#endif /* APP_CMD_H__ */
