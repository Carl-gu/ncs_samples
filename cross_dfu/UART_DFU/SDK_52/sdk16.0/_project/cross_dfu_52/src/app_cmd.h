#ifndef APP_CMD_H__
#define APP_CMD_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

typedef uint32_t (*cmd_respond_t)(uint8_t* p_data, uint16_t len);
typedef int (*req_cb_t)(uint8_t* p_req, uint16_t req_len, cmd_respond_t respond);
typedef void (*rsp_cb_t)(uint8_t* p_rsp, uint16_t rsp_len);

typedef struct
{
    uint8_t     op_code;
    req_cb_t    proc_req;
    rsp_cb_t    proc_rsp;
} cmd_cb_t;

typedef struct
{
    uint8_t     type;
    uint8_t     op_code;
    uint8_t*    p_data;
    uint16_t    length;
} app_cmd_t;

typedef struct
{
    cmd_mode_t   mode;
    cmd_state_t  state;
    app_cmd_t    cmd;
} cmd_context_t;

typedef struct
{
    uint8_t  op_code;
    uint8_t* p_data;
    uint16_t length;
    bool     timeout;
} cmd_event_t;

typedef void (*cmd_event_cb_t)(cmd_event_t* p_event);

/**@brief Register a cmd.
 *
 * @param[in] _op_code: op code of cmd.
 * @param[in] _req_cb: request callback function of cmd.
 * @param[in] _rsp_cb: response callback function of cmd.
 */
#define CMD_CALLBACK_REG(_op_code, _req_cb, _rsp_cb)                       \
NRF_SECTION_ITEM_REGISTER(cmd_cb_list, cmd_cb_t cmd_cb_ ## _op_code) =     \
{                                                                          \
    .op_code   = _op_code,                                                 \
    .proc_req  = _req_cb,                                                  \
    .proc_rsp  = _rsp_cb,                                                  \
}

uint32_t app_cmd_init(void);

uint32_t app_cmd_request(uint8_t op_code, uint8_t* p_data, uint16_t length);

void app_cmd_uart_event_handler(app_uart_evt_t * p_event);

void app_cmd_event_cb_register(cmd_event_cb_t cb);

void cmd_request_ping(void);


#ifdef __cplusplus
}
#endif

#endif /* APP_CMD_H__ */
