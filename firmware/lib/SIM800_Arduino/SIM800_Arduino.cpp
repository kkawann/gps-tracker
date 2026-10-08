#include "SIM800_Arduino.h"
#include <esp_task_wdt.h> // ⚠️ جدید - برای esp_task_wdt_reset در SIM800_SendSMS

// ============================================================================
// Private Function Prototypes
// ============================================================================
static void SIM800_ProcessGprsStateMachine(SIM800_t *sim);
static void SIM800_ProcessTcpStateMachine(SIM800_t *sim);
static void SIM800_ProcessHttpStateMachine(SIM800_t *sim);
static void SIM800_HandleGprsURC(SIM800_t *sim, const char *line);
static void SIM800_HandleTcpURC(SIM800_t *sim, const char *line);
static void SIM800_HandleHttpURC(SIM800_t *sim, const char *line);
static void SIM800_TcpSendData(SIM800_t *sim);
static void SIM800_ProcessByte(SIM800_t *sim, uint8_t c);
static void SIM800_ProcessLine(SIM800_t *sim, const char *line);
static void SIM800_HandleURC(SIM800_t *sim, const char *line);
static void SIM800_HandleCommandResponse(SIM800_t *sim, const char *line);
static void SIM800_HandleSMS(SIM800_t *sim, const char *line);
static void SIM800_ProcessInitMachine(SIM800_t *sim);
static void SIM800_ProcessCommandTimeout(SIM800_t *sim);
static void SIM800_SendInitCommand(SIM800_t *sim, const char *cmd, InitState_t next_state);
static void SIM800_ProcessCommandQueue(SIM800_t *sim);
static bool SIM800_IsURC(const char *line);
static void SIM800_ResetCommandState(SIM800_t *sim);
static void SIM800_ProcessPendingCallbacks(SIM800_t *sim);
static void SIM800_ProcessWatchdog(SIM800_t *sim);
static void SIM800_FinishSMSCollection(SIM800_t *sim);
static bool SIM800_ExtractQuotedField(const char *line, int field_index,
                                      char *output, size_t max_len);

// ============================================================================
// Public Functions
// ============================================================================

void SIM800_Init(SIM800_t *sim, Stream *serial)
{
    if (!sim || !serial)
        return;

    memset(sim, 0, sizeof(SIM800_t));
    sim->net_time_valid = false;
    sim->serial = serial;
    sim->cmd_state = CMD_IDLE;
    sim->sms_state = SMS_IDLE;
    sim->init_state = INIT_START;
    sim->rx_line_idx = 0;
    sim->sms_index = -1;
    sim->network_registered = false;
    sim->signal_rssi = 99;
    sim->cmd_has_result = false;
    sim->initialized = false;
    sim->module_responsive = false;
    sim->init_next_time = millis() + 2000;
    sim->init_waiting_response = false;
    sim->init_retry_count = 0;
    sim->cmd_queue_head = 0;
    sim->cmd_queue_tail = 0;
    sim->cmd_queue_size = 0;
    sim->last_activity_time = millis();
    sim->last_command_success_time = millis();
    sim->pending_sms.pending = false;
    sim->pending_call.pending = false;

    sim->gprs_state = GPRS_IDLE;
    sim->apn[0] = '\0';
    sim->gprs_user[0] = '\0';
    sim->gprs_pass[0] = '\0';
    sim->ip_address[0] = '\0';
    sim->gprs_next_step_time = 0;

    sim->tcp_state = TCP_IDLE;
    sim->tcp_host[0] = '\0';
    sim->tcp_port = 0;
    sim->tcp_rx_len = 0;
    sim->tcp_data_mode = false;
    sim->tcp_send_buffer.ready = false;
    sim->tcp_send_buffer.data = NULL;

    // ✅ مقداردهی متغیرهای باینری
    sim->tcp_reading_binary = false;
    sim->tcp_binary_len = 0;

    sim->http_state = HTTP_IDLE;
    sim->http_request.complete = false;
    sim->http_next_step_time = 0;

    while (serial->available())
        serial->read();
    SIM_LOGLN("[SIM] Init complete");
}

void SIM800_Process(SIM800_t *sim)
{
    if (!sim || !sim->serial)
        return;

    while (sim->serial->available())
    {
        uint8_t c = sim->serial->read();
        SIM800_ProcessByte(sim, c);
        sim->last_activity_time = millis();
    }

    if (!sim->init_waiting_response &&
        sim->init_state != INIT_IDLE &&
        sim->init_state != INIT_COMPLETE)
    {
        SIM800_ProcessInitMachine(sim);
    }

    SIM800_ProcessCommandTimeout(sim);

    if (sim->cmd_state == CMD_IDLE && sim->cmd_queue_size > 0)
    {
        SIM800_ProcessCommandQueue(sim);
    }

    if (sim->sms_collecting && sim->sms_from_cmt)
    {
        if (millis() - sim->sms_last_line_time > 1000)
        {
            SIM800_FinishSMSCollection(sim);
        }
    }

    SIM800_ProcessPendingCallbacks(sim);
    SIM800_ProcessWatchdog(sim);
    SIM800_ProcessGprsStateMachine(sim);
    SIM800_ProcessTcpStateMachine(sim);
    SIM800_ProcessHttpStateMachine(sim);
}

SIM800_Result_t SIM800_SendCommand(SIM800_t *sim, const char *cmd,
                                   const char *expect, uint32_t timeout_ms)
{
    if (!sim || !cmd)
        return SIM800_ERR_INVALID_PARAM;
    if (sim->cmd_state != CMD_IDLE)
        return SIM800_ERR_BUSY;

    char buf[128];
    snprintf(buf, sizeof(buf), "%s\r\n", cmd);

    size_t len = strlen(buf);
    if (sim->serial->write((uint8_t *)buf, len) != len)
    {
        SIM_LOGLN("[SIM] Write failed");
        return SIM800_ERR_BUSY;
    }

    sim->expected_response = expect;
    sim->cmd_timeout = timeout_ms;
    sim->cmd_start_time = millis();
    sim->cmd_state = CMD_WAIT_RESPONSE;
    sim->cmd_result = false;
    sim->cmd_has_result = false;

    SIM_LOG("[>>] ");
    SIM_LOGLN(cmd);

    return SIM800_OK;
}

SIM800_Result_t SIM800_QueueCommand(SIM800_t *sim, const char *cmd,
                                    const char *expect, uint32_t timeout_ms)
{
    if (!sim || !cmd)
        return SIM800_ERR_INVALID_PARAM;
    if (sim->cmd_queue_size >= SIM800_CMD_QUEUE_SIZE)
        return SIM800_ERR_QUEUE_FULL;

    QueuedCommand_t *qcmd = &sim->cmd_queue[sim->cmd_queue_tail];
    strncpy(qcmd->cmd, cmd, sizeof(qcmd->cmd) - 1);
    qcmd->cmd[sizeof(qcmd->cmd) - 1] = '\0';

    if (expect)
    {
        strncpy(qcmd->expect, expect, sizeof(qcmd->expect) - 1);
        qcmd->expect[sizeof(qcmd->expect) - 1] = '\0';
    }
    else
    {
        qcmd->expect[0] = '\0';
    }

    qcmd->timeout = timeout_ms;
    qcmd->timestamp = millis();
    qcmd->active = true;

    sim->cmd_queue_tail = (sim->cmd_queue_tail + 1) % SIM800_CMD_QUEUE_SIZE;
    sim->cmd_queue_size++;

    SIM_LOG("[QUEUE+] ");
    SIM_LOGLN(cmd);

    return SIM800_OK;
}

int SIM800_CommandReady(SIM800_t *sim)
{
    if (!sim)
        return -1;
    if (sim->cmd_state == CMD_IDLE && sim->cmd_has_result)
    {
        int result = sim->cmd_result ? 1 : -1;
        sim->cmd_has_result = false;
        return result;
    }
    return 0;
}

SIM800_Result_t SIM800_SendSMS(SIM800_t *sim, const char *number, const char *text)
{
    if (!sim || !number || !text)
        return SIM800_ERR_INVALID_PARAM;
    if (!sim->initialized)
        return SIM800_ERR_NOT_READY;

    // ⚠️ FIX: قبلاً این حلقه فقط delay(10) بود و SIM800_Process صدا
    // نمی‌شد، یعنی تا ۵ ثانیه بایت‌های ورودی UART خونده نمی‌شدن (می‌تونست
    // وسط یک ترانزیشن حیاتی GPRS/TCP بایت گم کنه) و واچ‌داگ هم فید
    // نمی‌شد. الان پردازش رو ادامه می‌دیم.
    unsigned long waitStart = millis();
    while (sim->cmd_state != CMD_IDLE && millis() - waitStart < 5000)
    {
        SIM800_Process(sim);
        esp_task_wdt_reset();
        delay(10);
    }

    if (sim->cmd_state != CMD_IDLE)
    {
        SIM_LOGLN("[SMS] CMD busy, queuing...");
    }

    strncpy(sim->sms_number, number, sizeof(sim->sms_number) - 1);
    sim->sms_number[sizeof(sim->sms_number) - 1] = '\0';
    strncpy(sim->sms_text, text, sizeof(sim->sms_text) - 1);
    sim->sms_text[sizeof(sim->sms_text) - 1] = '\0';

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);

    // ⚠️ FIX: دیگه sms_state رو اینجا زودتر از موعد ست نمی‌کنیم.
    // قبلاً همین‌جا sms_state به SMS_WAIT_PROMPT می‌رفت در حالی که دستور
    // فقط queue شده بود، نه ارسال. در این فاصله صف GPRS/TCP (که چک
    // sms_state می‌کنه) اشتباهاً معطل SMS می‌موند و باعث تاخیرهای طولانی
    // در ری‌کانکت MQTT می‌شد. ست کردنش رو کاملاً می‌سپاریم به همون جای
    // درستش در SIM800_ProcessCommandQueue، دقیقاً وقتی دستور واقعاً
    // دیکیو و ارسال می‌شه.
    SIM_LOG("[SMS] Queuing to ");
    SIM_LOGLN(number);

    return SIM800_QueueCommand(sim, cmd, ">", 15000);
}

SIM800_Result_t SIM800_DeleteSMS(SIM800_t *sim, int index)
{
    if (!sim || index < 1)
        return SIM800_ERR_INVALID_PARAM;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", index);
    return SIM800_QueueCommand(sim, cmd, "OK", 5000);
}

void SIM800_SetSmsCallback(SIM800_t *sim, SIM800_SmsCallback_t callback)
{
    if (!sim)
        return;
    sim->sms_callback = callback;
    SIM_LOGLN("[SMS] Callback registered");
}

void SIM800_SetCallCallback(SIM800_t *sim, SIM800_CallCallback_t callback)
{
    if (!sim)
        return;
    sim->call_callback = callback;
    SIM_LOGLN("[CALL] Callback registered");
}

void SIM800_SetInitCallback(SIM800_t *sim, SIM800_InitCallback_t callback)
{
    if (!sim)
        return;
    sim->init_callback = callback;
    SIM_LOGLN("[INIT] Callback registered");
}

bool SIM800_IsReady(SIM800_t *sim)
{
    return sim && sim->initialized && sim->init_state == INIT_COMPLETE;
}

bool SIM800_IsNetworkRegistered(SIM800_t *sim)
{
    return sim && sim->network_registered;
}

int SIM800_GetSignalStrength(SIM800_t *sim)
{
    return sim ? sim->signal_rssi : 99;
}

void SIM800_ForceReinit(SIM800_t *sim)
{
    if (!sim)
        return;
    sim->init_state = INIT_START;
    sim->init_next_time = millis();
    sim->init_retry_count = 0;
    sim->initialized = false;
    sim->module_responsive = false;
    sim->cmd_queue_head = 0;
    sim->cmd_queue_tail = 0;
    sim->cmd_queue_size = 0;
    SIM_LOGLN("[SIM] Force reinit");
}

InitState_t SIM800_GetInitState(SIM800_t *sim)
{
    return sim ? sim->init_state : INIT_IDLE;
}

uint8_t SIM800_GetQueueSize(SIM800_t *sim)
{
    return sim ? sim->cmd_queue_size : 0;
}

// ============================================================================
// Private Functions
// ============================================================================

static void SIM800_ResetCommandState(SIM800_t *sim)
{
    sim->cmd_state = CMD_IDLE;
    sim->expected_response = NULL;
    sim->cmd_has_result = false;
    sim->init_waiting_response = false;
}

static void SIM800_ProcessByte(SIM800_t *sim, uint8_t c)
{
    // ────────────────────────────────────────────────────
    // 1. حالت binary - خواندن دیتای TCP
    // ────────────────────────────────────────────────────
    if (sim->tcp_reading_binary)
    {
        // ارسال بایت به callback
        if (sim->tcp_data_callback)
        {
            sim->tcp_data_callback(&c, 1);
        }

        if (sim->tcp_binary_len > 0)
        {
            sim->tcp_binary_len--;
        }

        if (sim->tcp_binary_len == 0)
        {
            SIM_LOGLN("[TCP] Binary read complete");
            sim->tcp_reading_binary = false;
            sim->rx_line_idx = 0;
        }
        return;
    }

    // ────────────────────────────────────────────────────
    // 2. بررسی prompt > (فقط وقتی TCP connected نیست)
    //    اگه TCP connected هست، > رو نادیده بگیر
    // ────────────────────────────────────────────────────
    if (c == '>')
    {
        // TCP send prompt
        if (sim->tcp_state == TCP_WAIT_SEND_PROMPT)
        {
            SIM_LOGLN("[TCP] Got send prompt");
            sim->cmd_state = CMD_IDLE;
            sim->rx_line_idx = 0;
            SIM800_TcpSendData(sim);
            return;
        }

        // SMS prompt - فقط وقتی در حالت انتظار SMS هستیم
        if (sim->sms_state == SMS_WAIT_PROMPT)
        {
            sim->rx_line_idx = 0;
            SIM800_HandleSMS(sim, ">");
            return;
        }

        // ✅ در هر حالت دیگه، > رو نادیده بگیر
        // (ممکنه بخشی از دیتای TCP باشه)
        return;
    }

    // ────────────────────────────────────────────────────
    // 3. Line ending
    // ────────────────────────────────────────────────────
    if (c == '\r' || c == '\n')
    {
        if (sim->rx_line_idx > 0)
        {
            sim->rx_line[sim->rx_line_idx] = '\0';
            SIM800_ProcessLine(sim, sim->rx_line);
            sim->rx_line_idx = 0;
        }
        return;
    }

    // ────────────────────────────────────────────────────
    // 4. پر کردن بافر
    // ────────────────────────────────────────────────────
    if (sim->rx_line_idx < SIM800_LINE_SIZE - 1)
    {
        sim->rx_line[sim->rx_line_idx++] = c;
        sim->rx_line[sim->rx_line_idx] = '\0';

        // ✅ چک +IPD فقط وقتی ':' میاد
        if (c == ':' && sim->rx_line_idx >= 6)
        {
            // بررسی اینکه خط با +IPD شروع شده
            if (strncmp(sim->rx_line, "+IPD,", 5) == 0)
            {
                int len = 0;
                if (sscanf(sim->rx_line, "+IPD,%d:", &len) == 1 && len > 0)
                {
                    SIM_LOG("[TCP] Binary incoming: ");
                    SIM_LOGLN(len);

                    sim->tcp_reading_binary = true;
                    sim->tcp_binary_len = (uint16_t)len;
                    sim->rx_line_idx = 0;
                    return;
                }
            }
        }
    }
    else
    {
        SIM_LOGLN("[!] RX overflow - reset");
        sim->rx_line_idx = 0;
    }
}
static bool SIM800_IsURC(const char *line)
{
    return (strstr(line, "+CREG:") ||
            strstr(line, "+CSQ:") ||
            strstr(line, "+CCLK:") ||
            strstr(line, "+CLIP:") ||
            strstr(line, "+CMTI:") ||
            strstr(line, "+CMT:") ||
            strstr(line, "+CMGR:") ||
            strstr(line, "RDY") ||
            strstr(line, "+CPIN:") ||
            strstr(line, "SMS READY") ||
            strstr(line, "CALL READY") ||
            strstr(line, "NORMAL POWER DOWN") ||
            strstr(line, "UNDER-VOLTAGE"));
}

static bool SIM800_ExtractQuotedField(const char *line, int field_index,
                                      char *output, size_t max_len)
{
    int quote_count = 0;
    const char *p = line;

    while (*p)
    {
        if (*p == '"')
        {
            if (quote_count / 2 == field_index)
            {
                p++;
                size_t i = 0;
                while (*p && *p != '"' && i < max_len - 1)
                {
                    output[i++] = *p++;
                }
                output[i] = '\0';
                return true;
            }
            quote_count++;
        }
        p++;
    }
    return false;
}

// ============================================================================
// ✅ ProcessLine - منطق اصلاح شده
// ============================================================================
static void SIM800_ProcessLine(SIM800_t *sim, const char *line)
{
    if (strlen(line) == 0)
        return;

    SIM_LOG("[<<] ");
    SIM_LOGLN(line);

    // ─────────────────────────────────────────────
    // 1. Module reboot detection
    // ─────────────────────────────────────────────
    if (strcmp(line, "RDY") == 0 || strstr(line, "NORMAL POWER DOWN"))
    {
        SIM_LOGLN("[!] MODULE REBOOT DETECTED");
        sim->network_registered = false;
        sim->signal_rssi = 99;
        sim->sms_collecting = false;
        sim->sms_index = -1;
        sim->initialized = false;
        sim->module_responsive = false;
        SIM800_ResetCommandState(sim);
        sim->sms_state = SMS_IDLE;
        sim->gprs_state = GPRS_IDLE;
        sim->tcp_state = TCP_IDLE;
        sim->http_state = HTTP_IDLE;
        sim->init_state = INIT_START;
        sim->init_next_time = millis() + 3000;
        sim->init_retry_count = 0;
        sim->cmd_queue_head = 0;
        sim->cmd_queue_tail = 0;
        sim->cmd_queue_size = 0;
        return;
    }

    // ─────────────────────────────────────────────
    // 2. GPRS/TCP specific lines - همیشه پردازش کن
    // ─────────────────────────────────────────────
    if (strcmp(line, "SHUT OK") == 0)
    {
        // ✅ اول command response
        if (sim->cmd_state == CMD_WAIT_RESPONSE)
            SIM800_HandleCommandResponse(sim, line);
        // ✅ بعد GPRS URC (state transition)
        SIM800_HandleGprsURC(sim, line);
        return;
    }

    if (strcmp(line, "SEND OK") == 0 || strcmp(line, "CLOSE OK") == 0)
    {
        if (sim->cmd_state == CMD_WAIT_RESPONSE)
            SIM800_HandleCommandResponse(sim, line);
        SIM800_HandleTcpURC(sim, line);
        return;
    }

    if (strstr(line, "CONNECT OK") || strstr(line, "CONNECT FAIL") ||
        strcmp(line, "CLOSED") == 0 || strstr(line, "+RECEIVE,") ||
        strstr(line, "+IPD,"))
    {
        if (sim->cmd_state == CMD_WAIT_RESPONSE)
            SIM800_HandleCommandResponse(sim, line);
        SIM800_HandleTcpURC(sim, line);
        return;
    }

    // ─────────────────────────────────────────────
    // 3. HTTP URCs
    // ─────────────────────────────────────────────
    if (strstr(line, "+HTTPACTION:") || strstr(line, "+HTTPREAD:"))
    {
        SIM800_HandleHttpURC(sim, line);
        return;
    }

    // ─────────────────────────────────────────────
    // 4. IP Address (AT+CIFSR response)
    // ─────────────────────────────────────────────
    if ((sim->gprs_state == GPRS_WAIT_IP) &&
        strchr(line, '.') &&
        !strstr(line, "ERROR") &&
        !strstr(line, "AT+") &&
        !strstr(line, "+"))
    {
        SIM800_HandleGprsURC(sim, line);
        return;
    }

    // ─────────────────────────────────────────────
    // 5. Standard URCs
    // ─────────────────────────────────────────────
    if (SIM800_IsURC(line))
    {
        // اگه waiting response هستیم و این URC با expected_response مطابقت داره
        if (sim->cmd_state == CMD_WAIT_RESPONSE &&
            sim->expected_response != NULL &&
            strstr(line, sim->expected_response))
        {
            SIM800_HandleCommandResponse(sim, line);
        }
        else
        {
            SIM800_HandleURC(sim, line);
        }
        // SMS هم چک کن
        SIM800_HandleSMS(sim, line);
        return;
    }

    // ─────────────────────────────────────────────
    // 6. OK / ERROR - Command responses
    // ─────────────────────────────────────────────
    if (strcmp(line, "OK") == 0 || strstr(line, "ERROR"))
    {
        if (sim->cmd_state == CMD_WAIT_RESPONSE)
            SIM800_HandleCommandResponse(sim, line);

        // OK ممکنه پایان SMS باشه
        SIM800_HandleSMS(sim, line);
        return;
    }

    // ─────────────────────────────────────────────
    // 7. سایر خطوط - ممکنه command response یا SMS body باشه
    // ─────────────────────────────────────────────
    if (sim->cmd_state == CMD_WAIT_RESPONSE)
    {
        SIM800_HandleCommandResponse(sim, line);
    }

    SIM800_HandleSMS(sim, line);
}

// ============================================================================
// GPRS API Implementation
// ============================================================================

SIM800_Result_t SIM800_GprsConnect(SIM800_t *sim, const char *apn,
                                   const char *user, const char *pass)
{
    if (!sim || !apn)
        return SIM800_ERR_INVALID_PARAM;
    if (!sim->initialized)
        return SIM800_ERR_NOT_READY;

    if (sim->gprs_state == GPRS_CONNECTED)
    {
        SIM_LOGLN("[GPRS] Already connected");
        return SIM800_OK;
    }

    if (sim->gprs_state != GPRS_IDLE)
    {
        SIM_LOGLN("[GPRS] Busy");
        return SIM800_ERR_BUSY;
    }

    strncpy(sim->apn, apn, sizeof(sim->apn) - 1);
    sim->apn[sizeof(sim->apn) - 1] = '\0';

    strncpy(sim->gprs_user, user ? user : "", sizeof(sim->gprs_user) - 1);
    sim->gprs_user[sizeof(sim->gprs_user) - 1] = '\0';

    strncpy(sim->gprs_pass, pass ? pass : "", sizeof(sim->gprs_pass) - 1);
    sim->gprs_pass[sizeof(sim->gprs_pass) - 1] = '\0';

    SIM_LOGLN("[GPRS] Starting connection...");
    sim->gprs_state = GPRS_SHUTDOWN;
    sim->gprs_next_step_time = millis();

    return SIM800_OK;
}

SIM800_Result_t SIM800_GprsDisconnect(SIM800_t *sim)
{
    if (!sim)
        return SIM800_ERR_INVALID_PARAM;
    SIM_LOGLN("[GPRS] Disconnecting...");
    sim->gprs_state = GPRS_IDLE;
    sim->tcp_state = TCP_IDLE;
    return SIM800_QueueCommand(sim, "AT+CIPSHUT", "SHUT OK", 5000);
}

bool SIM800_GprsIsConnected(SIM800_t *sim)
{
    return sim && (sim->gprs_state == GPRS_CONNECTED);
}

const char *SIM800_GprsGetIP(SIM800_t *sim)
{
    if (!sim || sim->gprs_state != GPRS_CONNECTED)
        return NULL;
    return sim->ip_address;
}

void SIM800_SetGprsCallback(SIM800_t *sim, SIM800_GprsCallback_t callback)
{
    if (sim)
    {
        sim->gprs_callback = callback;
        SIM_LOGLN("[GPRS] Callback registered");
    }
}

// ============================================================================
// ✅ GPRS State Machine - اصلاح شده
// ============================================================================
static void SIM800_ProcessGprsStateMachine(SIM800_t *sim)
{
    // فقط WAIT states و ERROR نیاز به پردازش دارن
    switch (sim->gprs_state)
    {
    case GPRS_IDLE:
    case GPRS_CONNECTED:
    case GPRS_WAIT_SHUTDOWN:
    case GPRS_WAIT_CONFIG_MODE:
    case GPRS_WAIT_APN:
    case GPRS_WAIT_BRING_UP:
    case GPRS_WAIT_IP:
        // این state‌ها توسط callback‌ها مدیریت میشن
        // فقط timeout اضطراری چک کن
        if (millis() > sim->gprs_next_step_time &&
            sim->gprs_state != GPRS_IDLE &&
            sim->gprs_state != GPRS_CONNECTED)
        {
            SIM_LOG("[GPRS] ✗ Emergency timeout in state: ");
            SIM_LOGLN(sim->gprs_state);
            sim->gprs_state = GPRS_ERROR;
            sim->gprs_next_step_time = millis();
        }
        break;

    default:
        break;
    }

    // ── Action states ──
    if (millis() < sim->gprs_next_step_time)
        return;
    if (sim->cmd_state != CMD_IDLE)
        return;

    char cmd[96];

    switch (sim->gprs_state)
    {
    case GPRS_SHUTDOWN:
        SIM_LOGLN("[GPRS] 1. Shutdown IP stack");
        if (SIM800_SendCommand(sim, "AT+CIPSHUT", "SHUT OK", 8000) == SIM800_OK)
        {
            sim->gprs_state = GPRS_WAIT_SHUTDOWN;
            sim->gprs_next_step_time = millis() + 10000; // timeout اضطراری
        }
        break;

    case GPRS_CONFIG_MODE:
        SIM_LOGLN("[GPRS] 2. Set single connection mode");
        if (SIM800_SendCommand(sim, "AT+CIPMUX=0", "OK", 3000) == SIM800_OK)
        {
            sim->gprs_state = GPRS_WAIT_CONFIG_MODE;
            sim->gprs_next_step_time = millis() + 5000;
        }
        break;

    case GPRS_CONFIG_APN:
        SIM_LOG("[GPRS] 3. Set APN: ");
        SIM_LOGLN(sim->apn);
        snprintf(cmd, sizeof(cmd), "AT+CSTT=\"%s\",\"%s\",\"%s\"",
                 sim->apn, sim->gprs_user, sim->gprs_pass);
        if (SIM800_SendCommand(sim, cmd, "OK", 5000) == SIM800_OK)
        {
            sim->gprs_state = GPRS_WAIT_APN;
            sim->gprs_next_step_time = millis() + 7000;
        }
        break;

    case GPRS_BRING_UP:
        SIM_LOGLN("[GPRS] 4. Bring up wireless connection");
        if (SIM800_SendCommand(sim, "AT+CIICR", "OK", 85000) == SIM800_OK)
        {
            sim->gprs_state = GPRS_WAIT_BRING_UP;
            sim->gprs_next_step_time = millis() + 90000;
        }
        break;

    case GPRS_GET_IP:
        SIM_LOGLN("[GPRS] 5. Get IP address");
        if (SIM800_SendCommand(sim, "AT+CIFSR", NULL, 5000) == SIM800_OK)
        {
            sim->gprs_state = GPRS_WAIT_IP;
            sim->gprs_next_step_time = millis() + 8000;
        }
        break;

    case GPRS_ERROR:
        SIM_LOGLN("[GPRS] Error - going IDLE, retry in 10s");
        sim->gprs_state = GPRS_IDLE;
        if (sim->gprs_callback)
            sim->gprs_callback(false);
        break;

    default:
        break;
    }
}

// ============================================================================
// ✅ GPRS URC Handler - اصلاح شده
// ============================================================================
static void SIM800_HandleGprsURC(SIM800_t *sim, const char *line)
{
    // SHUT OK → بعد از shutdown، برو به CONFIG_MODE
    if (strcmp(line, "SHUT OK") == 0)
    {
        SIM_LOGLN("[GPRS] ✓ Shutdown OK → CONFIG_MODE");
        sim->tcp_state = TCP_IDLE;

        if (sim->gprs_state == GPRS_WAIT_SHUTDOWN)
        {
            sim->gprs_state = GPRS_CONFIG_MODE;
            sim->gprs_next_step_time = millis() + 500;
        }
        return;
    }

    // IP Address (خروجی AT+CIFSR)
    if (sim->gprs_state == GPRS_WAIT_IP)
    {
        // ✅ چک دقیق‌تر برای IP address
        bool has_dot = strchr(line, '.') != NULL;
        bool no_error = !strstr(line, "ERROR");
        bool no_plus = (line[0] != '+');
        bool no_at = !strstr(line, "AT+");
        bool no_ok = strcmp(line, "OK") != 0;

        if (has_dot && no_error && no_plus && no_at && no_ok)
        {
            strncpy(sim->ip_address, line, sizeof(sim->ip_address) - 1);
            sim->ip_address[sizeof(sim->ip_address) - 1] = '\0';

            SIM_LOG("[GPRS] ✓ IP: ");
            SIM_LOGLN(sim->ip_address);

            sim->gprs_state = GPRS_CONNECTED;
            sim->cmd_state = CMD_IDLE;

            if (sim->gprs_callback)
                sim->gprs_callback(true);
        }
    }
}

// ============================================================================
// ✅ HandleCommandResponse - با GPRS transitions
// ============================================================================
static void SIM800_HandleCommandResponse(SIM800_t *sim, const char *line)
{
    if (sim->cmd_state != CMD_WAIT_RESPONSE)
        return;

    bool matched = false;

    // ── Expected response ──
    // ── Expected response ──
    if (sim->expected_response != NULL && strstr(line, sim->expected_response))
    {
        matched = true;
        SIM_LOG("[✓] Got: ");
        SIM_LOGLN(line);
    }
    // ── SEND OK ──
    else if (strcmp(line, "SEND OK") == 0)
    {
        // فقط اگه داشتیم منتظر SEND OK بودیم
        if (sim->expected_response != NULL &&
            strcmp(sim->expected_response, "SEND OK") == 0)
        {
            matched = true;
            SIM_LOGLN("[✓] SEND OK");
        }
    }
    // ── SHUT OK (when expected_response is "SHUT OK") ──
    else if (strcmp(line, "SHUT OK") == 0 &&
             sim->expected_response != NULL &&
             strcmp(sim->expected_response, "SHUT OK") == 0)
    {
        matched = true;
        SIM_LOGLN("[✓] SHUT OK");
    }
    // ── ERROR ──
    else if (strstr(line, "ERROR"))
    {
        SIM_LOG("[✗] ERROR: ");
        SIM_LOGLN(line);

        sim->cmd_state = CMD_IDLE;
        sim->cmd_result = false;
        sim->cmd_has_result = true;
        sim->expected_response = NULL;
        sim->sms_state = SMS_IDLE;

        // GPRS error handling
        if (sim->gprs_state == GPRS_WAIT_SHUTDOWN ||
            sim->gprs_state == GPRS_WAIT_CONFIG_MODE ||
            sim->gprs_state == GPRS_WAIT_APN ||
            sim->gprs_state == GPRS_WAIT_BRING_UP ||
            sim->gprs_state == GPRS_WAIT_IP)
        {
            SIM_LOGLN("[GPRS] ✗ Command error → ERROR state");
            sim->gprs_state = GPRS_ERROR;
            sim->gprs_next_step_time = millis();
        }

        if (sim->init_waiting_response)
        {
            sim->init_waiting_response = false;
            sim->init_state = INIT_START;
            sim->init_next_time = millis() + 2000;
        }
        return;
    }

    if (!matched)
        return;

    // ── Response matched ──
    sim->cmd_state = CMD_IDLE;
    sim->cmd_result = true;
    sim->cmd_has_result = true;
    sim->expected_response = NULL;
    sim->module_responsive = true;
    sim->last_command_success_time = millis();

    // ── GPRS State Transitions ──
    switch (sim->gprs_state)
    {
    case GPRS_WAIT_CONFIG_MODE:
        SIM_LOGLN("[GPRS] ✓ CIPMUX=0 OK → CONFIG_APN");
        sim->gprs_state = GPRS_CONFIG_APN;
        sim->gprs_next_step_time = millis() + 500;
        break;

    case GPRS_WAIT_APN:
        SIM_LOGLN("[GPRS] ✓ CSTT OK → BRING_UP");
        sim->gprs_state = GPRS_BRING_UP;
        sim->gprs_next_step_time = millis() + 500;
        break;

    case GPRS_WAIT_BRING_UP:
        SIM_LOGLN("[GPRS] ✓ CIICR OK → GET_IP");
        sim->gprs_state = GPRS_GET_IP;
        sim->gprs_next_step_time = millis() + 2000;
        break;

    case GPRS_WAIT_IP:
        // IP در HandleGprsURC پردازش میشه
        break;

    default:
        break;
    }

    // ── Init State Transitions ──
    if (sim->init_waiting_response)
    {
        sim->init_waiting_response = false;
        if (sim->init_state == INIT_COMPLETE)
        {
            sim->initialized = true;
            sim->module_responsive = true;
            sim->init_retry_count = 0;
            SIM_LOGLN("[✓✓✓] INIT DONE!");
            if (sim->init_callback)
                sim->init_callback(true);
        }
    }
}

// ============================================================================
// HandleURC
// ============================================================================
static void SIM800_HandleURC(SIM800_t *sim, const char *line)
{
    if (strstr(line, "+CREG:"))
    {
        int n, stat;
        if (sscanf(line, "+CREG: %d,%d", &n, &stat) == 2)
        {
            bool was = sim->network_registered;
            sim->network_registered = (stat == 1 || stat == 5);
            if (!was && sim->network_registered)
                SIM_LOGLN("[NET] ✓ Registered");
            else if (was && !sim->network_registered)
                SIM_LOGLN("[NET] ✗ Lost");
        }
        return;
    }

    if (strstr(line, "+CSQ:"))
    {
        int rssi, ber;
        if (sscanf(line, "+CSQ: %d,%d", &rssi, &ber) == 2)
        {
            sim->signal_rssi = rssi;
            SIM_LOG("[SIG] RSSI: ");
            SIM_LOGLN(rssi);
        }
        return;
    }

    if (strstr(line, "+CCLK:"))
    {
        int yy, MM, dd, hh, mm, ss;
        if (sscanf(line, "+CCLK: \"%2d/%2d/%2d,%2d:%2d:%2d",
                   &yy, &MM, &dd, &hh, &mm, &ss) == 6)
        {
            sim->net_year = yy;
            sim->net_month = MM;
            sim->net_day = dd;
            sim->net_hour = hh;
            sim->net_minute = mm;
            sim->net_second = ss;
            sim->net_time_valid = true;
            SIM_LOG("[TIME] 20");
            SIM_LOG(yy);
            SIM_LOG("/");
            SIM_LOG(MM);
            SIM_LOG("/");
            SIM_LOG(dd);
            SIM_LOG(" ");
            SIM_LOG(hh);
            SIM_LOG(":");
            SIM_LOG(mm);
            SIM_LOG(":");
            SIM_LOGLN(ss);
        }
        return;
    }

    if (strstr(line, "+CLIP:"))
    {
        char number[SIM800_PHONE_SIZE];
        if (SIM800_ExtractQuotedField(line, 0, number, sizeof(number)))
        {
            SIM_LOG("[CALL] From: ");
            SIM_LOGLN(number);
            if (sim->call_callback)
            {
                strncpy(sim->pending_call.number, number,
                        sizeof(sim->pending_call.number) - 1);
                sim->pending_call.pending = true;
            }
        }
        return;
    }

    SIM_LOG("[URC] ");
    SIM_LOGLN(line);
}

// ============================================================================
// SMS Functions
// ============================================================================
static void SIM800_FinishSMSCollection(SIM800_t *sim)
{
    sim->sms_collecting = false;
    sim->sms_from_cmt = false;

    if (sim->sms_callback && strlen(sim->sms_body) > 0)
    {
        strncpy(sim->pending_sms.text, sim->sms_body,
                sizeof(sim->pending_sms.text) - 1);
        strncpy(sim->pending_sms.sender, sim->sms_sender,
                sizeof(sim->pending_sms.sender) - 1);
        sim->pending_sms.pending = true;
        SIM_LOG("[SMS] Queued: ");
        SIM_LOGLN(sim->sms_body);
    }

    sim->sms_body[0] = '\0';

    if (sim->sms_index > 0)
    {
        SIM800_DeleteSMS(sim, sim->sms_index);
        sim->sms_index = -1;
    }
}

static void SIM800_HandleSMS(SIM800_t *sim, const char *line)
{
    if (strcmp(line, ">") == 0 && sim->sms_state == SMS_WAIT_PROMPT)
    {
        SIM_LOGLN("[SMS] Got prompt, sending...");
        sim->serial->write((uint8_t *)sim->sms_text, strlen(sim->sms_text));
        sim->serial->write(0x1A);
        sim->sms_state = SMS_WAIT_SEND_CONFIRM;
        sim->cmd_state = CMD_WAIT_RESPONSE;
        sim->expected_response = "+CMGS";
        sim->cmd_timeout = 30000;
        sim->cmd_start_time = millis();
        return;
    }

    if (sim->sms_state == SMS_WAIT_SEND_CONFIRM && strstr(line, "+CMGS"))
    {
        sim->sms_state = SMS_IDLE;
        SIM_LOGLN("[✓] SMS sent");
        return;
    }

    if (strstr(line, "+CMTI:"))
    {
        int idx = 0;
        if (sscanf(line, "+CMTI: \"%*[^\"]\",%d", &idx) == 1 ||
            sscanf(line, "+CMTI: %*[^,],%d", &idx) == 1)
        {
            sim->sms_index = idx;
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", idx);
            SIM800_QueueCommand(sim, cmd, "+CMGR:", 5000);
        }
        return;
    }

    if (strstr(line, "+CMGR:"))
    {
        char number[SIM800_PHONE_SIZE];
        if (SIM800_ExtractQuotedField(line, 1, number, sizeof(number)))
        {
            strncpy(sim->sms_sender, number, sizeof(sim->sms_sender) - 1);
            sim->sms_collecting = true;
            sim->sms_body[0] = '\0';
            sim->sms_from_cmt = false;
        }
        return;
    }

    if (strstr(line, "+CMT:"))
    {
        char number[SIM800_PHONE_SIZE];
        if (SIM800_ExtractQuotedField(line, 0, number, sizeof(number)))
        {
            strncpy(sim->sms_sender, number, sizeof(sim->sms_sender) - 1);
            sim->sms_collecting = true;
            sim->sms_from_cmt = true;
            sim->sms_body[0] = '\0';
            sim->sms_last_line_time = millis();
        }
        return;
    }

    if (sim->sms_collecting)
    {
        if (strcmp(line, "OK") == 0)
        {
            SIM800_FinishSMSCollection(sim);
            return;
        }
        if (strstr(line, "+CMGR:") || strstr(line, "+CMT:"))
            return;

        if (strlen(line) > 0)
        {
            sim->sms_last_line_time = millis();
            size_t left = sizeof(sim->sms_body) - strlen(sim->sms_body) - 1;
            if (left > 0)
            {
                if (strlen(sim->sms_body) > 0)
                {
                    strncat(sim->sms_body, " ", left);
                    left--;
                }
                strncat(sim->sms_body, line, left);
            }
        }
    }
}

// ============================================================================
// Init Machine
// ============================================================================
static void SIM800_SendInitCommand(SIM800_t *sim, const char *cmd,
                                   InitState_t next_state)
{
    if (SIM800_SendCommand(sim, cmd, "OK", 3000) == SIM800_OK)
    {
        sim->init_state = next_state;
        sim->init_waiting_response = true;
        sim->init_next_time = millis() + 5000;
    }
    else
    {
        sim->init_next_time = millis() + 1000;
    }
}

static void SIM800_ProcessInitMachine(SIM800_t *sim)
{
    if (sim->init_state == INIT_IDLE || sim->init_state == INIT_COMPLETE)
        return;
    if (millis() < sim->init_next_time)
        return;
    if (sim->cmd_state != CMD_IDLE)
        return;

    switch (sim->init_state)
    {
    case INIT_START:
        if (sim->init_retry_count >= SIM800_MAX_INIT_RETRIES)
        {
            SIM_LOGLN("[INIT] ✗ Max retries");
            sim->init_state = INIT_IDLE;
            sim->initialized = false;
            if (sim->init_callback)
                sim->init_callback(false);
            break;
        }
        sim->init_retry_count++;
        sim->init_state = INIT_AT;
        sim->init_next_time = millis() + 500;
        break;

    case INIT_AT:
        SIM_LOG("[INIT] Attempt ");
        SIM_LOG(sim->init_retry_count);
        SIM_LOGLN(": AT");
        SIM800_SendInitCommand(sim, "AT", INIT_ATE0);
        break;

    case INIT_ATE0:
        SIM_LOGLN("[INIT] ATE0");
        SIM800_SendInitCommand(sim, "ATE0", INIT_CMGF);
        break;

    case INIT_CMGF:
        SIM_LOGLN("[INIT] CMGF=1");
        SIM800_SendInitCommand(sim, "AT+CMGF=1", INIT_CNMI);
        break;

    case INIT_CNMI:
        SIM_LOGLN("[INIT] CNMI");
        SIM800_SendInitCommand(sim, "AT+CNMI=2,2,0,0,0", INIT_CREG);
        break;

    case INIT_CREG:
        SIM_LOGLN("[INIT] CREG=1");
        SIM800_SendInitCommand(sim, "AT+CREG=1", INIT_CLIP);
        break;

    case INIT_CLIP:
        SIM_LOGLN("[INIT] CLIP=1");
        SIM800_SendInitCommand(sim, "AT+CLIP=1", INIT_CLTS);
        break;

    case INIT_CLTS:
        SIM_LOGLN("[INIT] CLTS=1");
        SIM800_SendInitCommand(sim, "AT+CLTS=1", INIT_SAVE);
        break;

    case INIT_SAVE:
        SIM_LOGLN("[INIT] AT&W");
        SIM800_SendInitCommand(sim, "AT&W", INIT_QUERY_CREG);
        break;

    case INIT_QUERY_CREG:
        SIM_LOGLN("[INIT] Query CREG");
        if (SIM800_SendCommand(sim, "AT+CREG?", "OK", 3000) == SIM800_OK)
        {
            sim->init_state = INIT_QUERY_CSQ;
            sim->init_waiting_response = true;
            sim->init_next_time = millis() + 5000;
        }
        break;

    case INIT_QUERY_CSQ:
        SIM_LOGLN("[INIT] Query CSQ");
        if (SIM800_SendCommand(sim, "AT+CSQ", "OK", 3000) == SIM800_OK)
        {
            sim->init_state = INIT_QUERY_TIME;
            sim->init_waiting_response = true;
            sim->init_next_time = millis() + 5000;
        }
        break;

    case INIT_QUERY_TIME:
        SIM_LOGLN("[INIT] Query CCLK");
        if (SIM800_SendCommand(sim, "AT+CCLK?", "+CCLK:", 5000) == SIM800_OK)
        {
            sim->init_state = INIT_COMPLETE;
            sim->init_waiting_response = true;
            sim->init_next_time = millis() + 5000;
        }
        break;

    default:
        sim->init_state = INIT_IDLE;
        break;
    }
}

// ============================================================================
// Command Timeout
// ============================================================================
static void SIM800_ProcessCommandTimeout(SIM800_t *sim)
{
    if (sim->cmd_state != CMD_WAIT_RESPONSE)
        return;

    uint32_t elapsed = millis() - sim->cmd_start_time;
    if (elapsed > sim->cmd_timeout)
    {
        SIM_LOG("[✗] TIMEOUT (");
        SIM_LOG(elapsed);
        SIM_LOGLN("ms)");

        // GPRS timeout handling
        if (sim->gprs_state == GPRS_WAIT_SHUTDOWN ||
            sim->gprs_state == GPRS_WAIT_CONFIG_MODE ||
            sim->gprs_state == GPRS_WAIT_APN ||
            sim->gprs_state == GPRS_WAIT_BRING_UP ||
            sim->gprs_state == GPRS_WAIT_IP)
        {
            SIM_LOGLN("[GPRS] ✗ Timeout → ERROR");
            sim->gprs_state = GPRS_ERROR;
            sim->gprs_next_step_time = millis();
        }

        SIM800_ResetCommandState(sim);
        sim->sms_state = SMS_IDLE;

        if (sim->init_waiting_response)
        {
            sim->init_waiting_response = false;
            sim->init_state = INIT_START;
            sim->init_next_time = millis() + 2000;
        }
    }
}

// ============================================================================
// Command Queue
// ============================================================================
static void SIM800_ProcessCommandQueue(SIM800_t *sim)
{
    if (sim->cmd_queue_size == 0 || sim->cmd_state != CMD_IDLE)
        return;

    QueuedCommand_t *qcmd = &sim->cmd_queue[sim->cmd_queue_head];

    if (!qcmd->active)
    {
        sim->cmd_queue_head = (sim->cmd_queue_head + 1) % SIM800_CMD_QUEUE_SIZE;
        sim->cmd_queue_size--;
        return;
    }

    // ✅ اگه SMS در حال ارسال بود، صبر کن
    if (sim->sms_state != SMS_IDLE && sim->sms_state != SMS_WAIT_PROMPT)
    {
        return;
    }

    SIM_LOG("[QUEUE-] ");
    SIM_LOGLN(qcmd->cmd);

    const char *expect = qcmd->expect[0] ? qcmd->expect : NULL;

    if (SIM800_SendCommand(sim, qcmd->cmd, expect, qcmd->timeout) == SIM800_OK)
    {
        // ✅ اگه دستور SMS بود، state رو تنظیم کن
        if (strstr(qcmd->cmd, "AT+CMGS="))
        {
            sim->sms_state = SMS_WAIT_PROMPT;
        }

        qcmd->active = false;
        sim->cmd_queue_head = (sim->cmd_queue_head + 1) % SIM800_CMD_QUEUE_SIZE;
        sim->cmd_queue_size--;
    }
    else if (millis() - qcmd->timestamp > 60000)
    {
        SIM_LOGLN("[QUEUE] Expired");
        sim->cmd_queue_head = (sim->cmd_queue_head + 1) % SIM800_CMD_QUEUE_SIZE;
        sim->cmd_queue_size--;
    }
}

// ============================================================================
// Callbacks & Watchdog
// ============================================================================
static void SIM800_ProcessPendingCallbacks(SIM800_t *sim)
{
    if (sim->pending_sms.pending && sim->sms_callback)
    {
        sim->pending_sms.pending = false;
        sim->sms_callback(sim->pending_sms.text, sim->pending_sms.sender);
    }
    if (sim->pending_call.pending && sim->call_callback)
    {
        sim->pending_call.pending = false;
        sim->call_callback(sim->pending_call.number);
    }
}

static void SIM800_ProcessWatchdog(SIM800_t *sim)
{
    if (!sim->initialized)
        return;
    if (sim->init_state != INIT_COMPLETE)
        return;

    uint32_t idle = millis() - sim->last_command_success_time;
    if (idle > SIM800_WATCHDOG_TIMEOUT)
    {
        sim->last_command_success_time = millis();
        SIM_LOG("[WATCHDOG] Idle ");
        SIM_LOG(idle / 1000);
        SIM_LOGLN("s - reinit");
        SIM800_ForceReinit(sim);
    }
}

// ============================================================================
// Time API
// ============================================================================
SIM800_Result_t SIM800_RequestNetworkTime(SIM800_t *sim)
{
    if (!sim)
        return SIM800_ERR_INVALID_PARAM;
    return SIM800_QueueCommand(sim, "AT+CCLK?", "+CCLK:", 5000);
}

bool SIM800_GetTime(SIM800_t *sim, uint8_t *hour, uint8_t *minute, uint8_t *second)
{
    if (!sim || !hour || !minute || !second || !sim->net_time_valid)
        return false;
    *hour = sim->net_hour;
    *minute = sim->net_minute;
    *second = sim->net_second;
    return true;
}

bool SIM800_GetDate(SIM800_t *sim, uint8_t *day, uint8_t *month, uint8_t *year)
{
    if (!sim || !day || !month || !year || !sim->net_time_valid)
        return false;
    *day = sim->net_day;
    *month = sim->net_month;
    *year = sim->net_year;
    return true;
}

// ============================================================================
// TCP API
// ============================================================================
SIM800_Result_t SIM800_TcpConnect(SIM800_t *sim, const char *host, uint16_t port)
{
    if (!sim || !host)
        return SIM800_ERR_INVALID_PARAM;
    if (sim->gprs_state != GPRS_CONNECTED)
    {
        SIM_LOGLN("[TCP] GPRS not connected");
        return SIM800_ERR_GPRS;
    }
    if (sim->tcp_state != TCP_IDLE)
    {
        SIM_LOGLN("[TCP] Busy");
        return SIM800_ERR_BUSY;
    }

    strncpy(sim->tcp_host, host, sizeof(sim->tcp_host) - 1);
    sim->tcp_port = port;

    // ✅ اجباری: روشن کردن هدر +IPD برای دریافت ایمن دیتای باینری MQTT
    SIM800_QueueCommand(sim, "AT+CIPHEAD=1", "OK", 3000);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",\"%d\"", host, port);

    SIM_LOG("[TCP] → ");
    SIM_LOG(host);
    SIM_LOG(":");
    SIM_LOGLN(port);
    sim->tcp_state = TCP_CONNECTING;
    return SIM800_QueueCommand(sim, cmd, "CONNECT", 30000);
}

SIM800_Result_t SIM800_TcpSend(SIM800_t *sim, const uint8_t *data, uint16_t len)
{
    if (!sim || !data || len == 0)
        return SIM800_ERR_INVALID_PARAM;
    if (sim->tcp_state != TCP_CONNECTED)
    {
        SIM_LOGLN("[TCP] Not connected");
        return SIM800_ERR_TCP;
    }

    // Free قبلی اگه هنوز موجوده
    if (sim->tcp_send_buffer.data)
    {
        free(sim->tcp_send_buffer.data);
    }

    sim->tcp_send_buffer.data = (uint8_t *)malloc(len);
    if (!sim->tcp_send_buffer.data)
        return SIM800_ERR_INVALID_PARAM;

    memcpy(sim->tcp_send_buffer.data, data, len);
    sim->tcp_send_buffer.length = len;
    sim->tcp_send_buffer.ready = true;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", len);
    sim->tcp_state = TCP_WAIT_SEND_PROMPT;
    return SIM800_QueueCommand(sim, cmd, ">", 5000);
}

SIM800_Result_t SIM800_TcpClose(SIM800_t *sim)
{
    if (!sim)
        return SIM800_ERR_INVALID_PARAM;
    sim->tcp_state = TCP_CLOSING;
    return SIM800_QueueCommand(sim, "AT+CIPCLOSE", "CLOSE OK", 5000);
}

bool SIM800_TcpIsConnected(SIM800_t *sim)
{
    return sim && (sim->tcp_state == TCP_CONNECTED);
}

void SIM800_SetTcpConnectCallback(SIM800_t *sim, SIM800_TcpConnectCallback_t cb)
{
    if (sim)
        sim->tcp_connect_callback = cb;
}

void SIM800_SetTcpDataCallback(SIM800_t *sim, SIM800_TcpDataCallback_t cb)
{
    if (sim)
        sim->tcp_data_callback = cb;
}

// ============================================================================
// TCP State Machine & URC
// ============================================================================
static void SIM800_ProcessTcpStateMachine(SIM800_t *sim)
{
    // کاری نداره - همه چیز در URC handler مدیریت میشه
}

static void SIM800_HandleTcpURC(SIM800_t *sim, const char *line)
{
    if (strstr(line, "CONNECT OK"))
    {
        SIM_LOGLN("[TCP] ✓ Connected");
        sim->tcp_state = TCP_CONNECTED;
        if (sim->tcp_connect_callback)
            sim->tcp_connect_callback(true);
        return;
    }

    if (strstr(line, "CONNECT FAIL"))
    {
        SIM_LOGLN("[TCP] ✗ Failed");
        sim->tcp_state = TCP_IDLE;
        if (sim->tcp_connect_callback)
            sim->tcp_connect_callback(false);
        return;
    }

    if (strstr(line, "SEND OK"))
    {
        SIM_LOGLN("[TCP] ✓ Sent");
        if (sim->tcp_send_buffer.data)
        {
            free(sim->tcp_send_buffer.data);
            sim->tcp_send_buffer.data = NULL;
        }
        sim->tcp_send_buffer.ready = false;
        sim->tcp_state = TCP_CONNECTED;
        return;
    }

    if (strcmp(line, "CLOSED") == 0 || strstr(line, "CLOSE OK"))
    {
        SIM_LOGLN("[TCP] Closed");
        if (sim->tcp_send_buffer.data)
        {
            free(sim->tcp_send_buffer.data);
            sim->tcp_send_buffer.data = NULL;
        }
        sim->tcp_send_buffer.ready = false;
        sim->tcp_state = TCP_IDLE;
        if (sim->tcp_connect_callback)
            sim->tcp_connect_callback(false);
        return;
    }

    if (strstr(line, "+IPD,"))
    {
        int data_len = 0;
        if (sscanf(line, "+IPD,%d:", &data_len) == 1)
        {
            const char *data_start = strchr(line, ':');
            if (data_start && sim->tcp_data_callback)
            {
                data_start++;
                sim->tcp_data_callback((const uint8_t *)data_start,
                                       strlen(data_start));
            }
        }
        return;
    }
}

static void SIM800_TcpSendData(SIM800_t *sim)
{
    if (!sim->tcp_send_buffer.ready || !sim->tcp_send_buffer.data)
        return;

    SIM_LOG("[TCP] Writing ");
    SIM_LOG(sim->tcp_send_buffer.length);
    SIM_LOGLN("B");
    sim->serial->write(sim->tcp_send_buffer.data, sim->tcp_send_buffer.length);

    sim->tcp_state = TCP_SENDING;
    sim->cmd_state = CMD_WAIT_RESPONSE;
    sim->expected_response = "SEND OK";
    sim->cmd_timeout = 10000;
    sim->cmd_start_time = millis();
}

// ============================================================================
// HTTP API
// ============================================================================
SIM800_Result_t SIM800_HttpGet(SIM800_t *sim, const char *url)
{
    if (!sim || !url)
        return SIM800_ERR_INVALID_PARAM;
    if (sim->gprs_state != GPRS_CONNECTED)
        return SIM800_ERR_GPRS;
    if (sim->http_state != HTTP_IDLE)
        return SIM800_ERR_BUSY;

    strncpy(sim->http_request.url, url, sizeof(sim->http_request.url) - 1);
    sim->http_request.complete = false;
    sim->http_request.http_code = 0;
    sim->http_request.response_len = 0;

    SIM_LOG("[HTTP] GET ");
    SIM_LOGLN(url);
    sim->http_state = HTTP_INIT;
    sim->http_next_step_time = millis();
    return SIM800_OK;
}

SIM800_Result_t SIM800_HttpPost(SIM800_t *sim, const char *url, const char *data)
{
    (void)data;
    if (!sim || !url)
        return SIM800_ERR_INVALID_PARAM;
    if (sim->gprs_state != GPRS_CONNECTED)
        return SIM800_ERR_GPRS;
    SIM_LOGLN("[HTTP] POST not implemented");
    return SIM800_ERR_INVALID_PARAM;
}

void SIM800_SetHttpCallback(SIM800_t *sim, SIM800_HttpCallback_t callback)
{
    if (sim)
        sim->http_callback = callback;
}

static void SIM800_ProcessHttpStateMachine(SIM800_t *sim)
{
    if (sim->http_state == HTTP_IDLE || sim->http_state == HTTP_COMPLETE)
        return;
    if (millis() < sim->http_next_step_time)
        return;
    if (sim->cmd_state != CMD_IDLE)
        return;

    char cmd[256];

    switch (sim->http_state)
    {
    case HTTP_INIT:
        if (SIM800_SendCommand(sim, "AT+HTTPINIT", "OK", 5000) == SIM800_OK)
        {
            sim->http_state = HTTP_SET_CID;
            sim->http_next_step_time = millis() + 1000;
        }
        break;

    case HTTP_SET_CID:
        if (SIM800_SendCommand(sim, "AT+HTTPPARA=\"CID\",1", "OK", 3000) == SIM800_OK)
        {
            sim->http_state = HTTP_SET_URL;
            sim->http_next_step_time = millis() + 500;
        }
        break;

    case HTTP_SET_URL:
        snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"",
                 sim->http_request.url);
        if (SIM800_SendCommand(sim, cmd, "OK", 3000) == SIM800_OK)
        {
            sim->http_state = HTTP_ACTION;
            sim->http_next_step_time = millis() + 500;
        }
        break;

    case HTTP_ACTION:
        if (SIM800_SendCommand(sim, "AT+HTTPACTION=0", "OK", 3000) == SIM800_OK)
        {
            sim->http_state = HTTP_READ;
            sim->http_next_step_time = millis() + 30000;
        }
        break;

    case HTTP_READ:
        break; // منتظر URC

    case HTTP_TERM:
        if (SIM800_SendCommand(sim, "AT+HTTPTERM", "OK", 3000) == SIM800_OK)
        {
            sim->http_state = HTTP_COMPLETE;
            if (sim->http_callback)
                sim->http_callback(sim->http_request.http_code,
                                   sim->http_request.response,
                                   sim->http_request.response_len);
        }
        break;

    case HTTP_ERROR:
        SIM800_SendCommand(sim, "AT+HTTPTERM", "OK", 3000);
        sim->http_state = HTTP_IDLE;
        if (sim->http_callback)
            sim->http_callback(0, NULL, 0);
        break;

    default:
        break;
    }
}

static void SIM800_HandleHttpURC(SIM800_t *sim, const char *line)
{
    if (strstr(line, "+HTTPACTION:"))
    {
        int method, code, len;
        if (sscanf(line, "+HTTPACTION: %d,%d,%d", &method, &code, &len) == 3)
        {
            sim->http_request.http_code = code;
            if (code == 200 && len > 0)
                SIM800_QueueCommand(sim, "AT+HTTPREAD", "+HTTPREAD:", 5000);
            else
                sim->http_state = HTTP_TERM;
        }
        return;
    }

    if (strstr(line, "+HTTPREAD:"))
    {
        sim->http_request.response_len = 0;
        sim->http_state = HTTP_TERM;
        return;
    }
}