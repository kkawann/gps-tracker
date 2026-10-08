#ifndef SIM800_ARDUINO_H
#define SIM800_ARDUINO_H

#include <Arduino.h>
#include <Stream.h>

// ============================================================================
// Configuration
// ============================================================================
#define SIM800_LINE_SIZE 256
#define SIM800_SMS_SIZE 160
#define SIM800_PHONE_SIZE 20
#define SIM800_CMD_QUEUE_SIZE 8
#define SIM800_MAX_INIT_RETRIES 3
#define SIM800_WATCHDOG_TIMEOUT 300000UL // 5 min
#define SIM800_TCP_BUFFER_SIZE 512
#define SIM800_HTTP_BUFFER_SIZE 1024

// ── Logging ──
#define SIM800_DEBUG 1
#if SIM800_DEBUG
#define SIM_LOG(x) Serial.print(x)
#define SIM_LOGLN(x) Serial.println(x)
#else
#define SIM_LOG(x)
#define SIM_LOGLN(x)
#endif

// ============================================================================
// Enums
// ============================================================================

typedef enum
{
    SIM800_OK = 0,
    SIM800_ERR_TIMEOUT,
    SIM800_ERR_INVALID_PARAM,
    SIM800_ERR_BUSY,
    SIM800_ERR_NOT_READY,
    SIM800_ERR_QUEUE_FULL,
    SIM800_ERR_NETWORK,
    SIM800_ERR_GPRS,
    SIM800_ERR_TCP
} SIM800_Result_t;

typedef enum
{
    CMD_IDLE,
    CMD_WAIT_RESPONSE
} CommandState_t;

typedef enum
{
    SMS_IDLE,
    SMS_WAIT_PROMPT,
    SMS_WAIT_SEND_CONFIRM
} SmsState_t;

typedef enum
{
    INIT_IDLE,
    INIT_START,
    INIT_AT,
    INIT_ATE0,
    INIT_CMGF,
    INIT_CNMI,
    INIT_CREG,
    INIT_CLIP,
    INIT_CLTS,
    INIT_SAVE,
    INIT_QUERY_CREG,
    INIT_QUERY_CSQ,
    INIT_QUERY_TIME,
    INIT_COMPLETE
} InitState_t;

typedef enum
{
    GPRS_IDLE,
    GPRS_SHUTDOWN,
    GPRS_WAIT_SHUTDOWN,
    GPRS_CONFIG_MODE,
    GPRS_WAIT_CONFIG_MODE,
    GPRS_CONFIG_APN,
    GPRS_WAIT_APN,
    GPRS_BRING_UP,
    GPRS_WAIT_BRING_UP,
    GPRS_GET_IP,
    GPRS_WAIT_IP,
    GPRS_CONNECTED,
    GPRS_ERROR
} GprsState_t;

typedef enum
{
    TCP_IDLE,
    TCP_CONNECTING,
    TCP_CONNECTED,
    TCP_WAIT_SEND_PROMPT,
    TCP_SENDING,
    TCP_CLOSING,
    TCP_ERROR
} TcpState_t;

typedef enum
{
    HTTP_IDLE,
    HTTP_INIT,
    HTTP_SET_CID,
    HTTP_SET_URL,
    HTTP_ACTION,
    HTTP_READ,
    HTTP_TERM,
    HTTP_COMPLETE,
    HTTP_ERROR
} HttpState_t;

// ============================================================================
// Structs
// ============================================================================

typedef struct
{
    char cmd[96];
    char expect[32];
    uint32_t timeout;
    uint32_t timestamp;
    bool active;
} QueuedCommand_t;

typedef struct
{
    bool pending;
    char text[SIM800_SMS_SIZE];
    char sender[SIM800_PHONE_SIZE];
} PendingSMS_t;

typedef struct
{
    bool pending;
    char number[SIM800_PHONE_SIZE];
} PendingCall_t;

typedef struct
{
    uint8_t *data;
    uint16_t length;
    bool ready;
} TcpSendBuffer_t;

typedef struct
{
    char url[128];
    uint16_t http_code;
    char response[SIM800_HTTP_BUFFER_SIZE];
    uint16_t response_len;
    bool complete;
} HttpRequest_t;

typedef struct
{
    // ── Hardware ──
    Stream *serial;

    // ── Command handling ──
    CommandState_t cmd_state;
    const char *expected_response;
    uint32_t cmd_timeout;
    uint32_t cmd_start_time;
    bool cmd_result;
    bool cmd_has_result;

    // ── Command queue ──
    QueuedCommand_t cmd_queue[SIM800_CMD_QUEUE_SIZE];
    uint8_t cmd_queue_head;
    uint8_t cmd_queue_tail;
    uint8_t cmd_queue_size;

    // ── Receive buffer ──
    char rx_line[SIM800_LINE_SIZE];
    uint16_t rx_line_idx;

    // ── SMS ──
    SmsState_t sms_state;
    char sms_number[SIM800_PHONE_SIZE];
    char sms_text[SIM800_SMS_SIZE];
    char sms_sender[SIM800_PHONE_SIZE];
    char sms_body[SIM800_SMS_SIZE];
    int sms_index;
    bool sms_collecting;
    bool sms_from_cmt;
    uint32_t sms_last_line_time;

    // ── Init state machine ──
    InitState_t init_state;
    uint32_t init_next_time;
    bool init_waiting_response;
    uint8_t init_retry_count;

    // ── Status ──
    bool initialized;
    bool module_responsive;
    bool network_registered;
    int signal_rssi;

    // ── Network time ──
    bool net_time_valid;
    uint8_t net_year;
    uint8_t net_month;
    uint8_t net_day;
    uint8_t net_hour;
    uint8_t net_minute;
    uint8_t net_second;

    // ── Callbacks ──
    void (*sms_callback)(const char *text, const char *sender);
    void (*call_callback)(const char *number);
    void (*init_callback)(bool success);
    void (*gprs_callback)(bool connected);
    void (*tcp_connect_callback)(bool connected);
    void (*tcp_data_callback)(const uint8_t *data, uint16_t len);
    void (*http_callback)(uint16_t code, const char *response, uint16_t len);

    // ── Pending callbacks ──
    PendingSMS_t pending_sms;
    PendingCall_t pending_call;

    // ── Watchdog ──
    uint32_t last_activity_time;
    uint32_t last_command_success_time;

    // ── GPRS ──
    GprsState_t gprs_state;
    char apn[32];
    char gprs_user[32];
    char gprs_pass[32];
    char ip_address[16];
    uint32_t gprs_next_step_time;

    // ── TCP ──
    TcpState_t tcp_state;
    char tcp_host[64];
    uint16_t tcp_port;
    uint8_t tcp_rx_buffer[SIM800_TCP_BUFFER_SIZE];
    uint16_t tcp_rx_len;
    bool tcp_data_mode;
    TcpSendBuffer_t tcp_send_buffer;
    // ✅ این دو خط رو اضافه کن:
    bool tcp_reading_binary;
    uint16_t tcp_binary_len;

    // ── HTTP ──
    HttpState_t http_state;
    HttpRequest_t http_request;
    uint32_t http_next_step_time;

} SIM800_t;

// ============================================================================
// Callback typedefs
// ============================================================================
typedef void (*SIM800_SmsCallback_t)(const char *text, const char *sender);
typedef void (*SIM800_CallCallback_t)(const char *number);
typedef void (*SIM800_InitCallback_t)(bool success);
typedef void (*SIM800_GprsCallback_t)(bool connected);
typedef void (*SIM800_TcpConnectCallback_t)(bool connected);
typedef void (*SIM800_TcpDataCallback_t)(const uint8_t *data, uint16_t len);
typedef void (*SIM800_HttpCallback_t)(uint16_t code, const char *response,
                                      uint16_t len);

// ============================================================================
// Public API - Core
// ============================================================================
void SIM800_Init(SIM800_t *sim, Stream *serial);
void SIM800_Process(SIM800_t *sim);
SIM800_Result_t SIM800_SendCommand(SIM800_t *sim, const char *cmd,
                                   const char *expect, uint32_t timeout_ms);
SIM800_Result_t SIM800_QueueCommand(SIM800_t *sim, const char *cmd,
                                    const char *expect, uint32_t timeout_ms);
int SIM800_CommandReady(SIM800_t *sim);
bool SIM800_IsReady(SIM800_t *sim);
bool SIM800_IsNetworkRegistered(SIM800_t *sim);
int SIM800_GetSignalStrength(SIM800_t *sim);
void SIM800_ForceReinit(SIM800_t *sim);
InitState_t SIM800_GetInitState(SIM800_t *sim);
uint8_t SIM800_GetQueueSize(SIM800_t *sim);

// ============================================================================
// Public API - SMS
// ============================================================================
SIM800_Result_t SIM800_SendSMS(SIM800_t *sim, const char *number,
                               const char *text);
SIM800_Result_t SIM800_DeleteSMS(SIM800_t *sim, int index);
void SIM800_SetSmsCallback(SIM800_t *sim, SIM800_SmsCallback_t cb);

// ============================================================================
// Public API - Call
// ============================================================================
void SIM800_SetCallCallback(SIM800_t *sim, SIM800_CallCallback_t cb);

// ============================================================================
// Public API - Init
// ============================================================================
void SIM800_SetInitCallback(SIM800_t *sim, SIM800_InitCallback_t cb);

// ============================================================================
// Public API - Time
// ============================================================================
SIM800_Result_t SIM800_RequestNetworkTime(SIM800_t *sim);
bool SIM800_GetTime(SIM800_t *sim, uint8_t *hour, uint8_t *minute,
                    uint8_t *second);
bool SIM800_GetDate(SIM800_t *sim, uint8_t *day, uint8_t *month,
                    uint8_t *year);

// ============================================================================
// Public API - GPRS
// ============================================================================
SIM800_Result_t SIM800_GprsConnect(SIM800_t *sim, const char *apn,
                                   const char *user, const char *pass);
SIM800_Result_t SIM800_GprsDisconnect(SIM800_t *sim);
bool SIM800_GprsIsConnected(SIM800_t *sim);
const char *SIM800_GprsGetIP(SIM800_t *sim);
void SIM800_SetGprsCallback(SIM800_t *sim, SIM800_GprsCallback_t cb);

// ============================================================================
// Public API - TCP
// ============================================================================
SIM800_Result_t SIM800_TcpConnect(SIM800_t *sim, const char *host,
                                  uint16_t port);
SIM800_Result_t SIM800_TcpSend(SIM800_t *sim, const uint8_t *data,
                               uint16_t len);
SIM800_Result_t SIM800_TcpClose(SIM800_t *sim);
bool SIM800_TcpIsConnected(SIM800_t *sim);
void SIM800_SetTcpConnectCallback(SIM800_t *sim,
                                  SIM800_TcpConnectCallback_t cb);
void SIM800_SetTcpDataCallback(SIM800_t *sim,
                               SIM800_TcpDataCallback_t cb);

// ============================================================================
// Public API - HTTP
// ============================================================================
SIM800_Result_t SIM800_HttpGet(SIM800_t *sim, const char *url);
SIM800_Result_t SIM800_HttpPost(SIM800_t *sim, const char *url,
                                const char *data);
void SIM800_SetHttpCallback(SIM800_t *sim, SIM800_HttpCallback_t cb);

#endif // SIM800_ARDUINO_H