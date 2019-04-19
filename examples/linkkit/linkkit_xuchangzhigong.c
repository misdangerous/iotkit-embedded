/*
 * Copyright (C) 2015-2018 Alibaba Group Holding Limited
 */

#include "stdio.h"
#include "iot_export_linkkit.h"
#include "cJSON.h"
#include "app_entry.h"
#include "apue.h"
#include "ipc_socket.h"
#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "serial.h"

#define	CS_OPEN "/tmp/opend.socket"	/* well-known name */

#if defined(OTA_ENABLED) && defined(BUILD_AOS)
    #include "ota_service.h"
#endif

#define USE_CUSTOME_DOMAIN      (0)

// for demo only
#define PRODUCT_KEY      "a1Yoa3d9cDn"
#define PRODUCT_SECRET   "HQGrq6fFe0Tib6zQ"
#define DEVICE_NAME      "2019030001"
#define DEVICE_SECRET    "WF89LmvwXeNz0FRUcghavAyqsWpgbqtZ"

#if USE_CUSTOME_DOMAIN
    #define CUSTOME_DOMAIN_MQTT     "iot-as-mqtt.cn-shanghai.aliyuncs.com"
    #define CUSTOME_DOMAIN_HTTP     "iot-auth.cn-shanghai.aliyuncs.com"
#endif

#define USER_EXAMPLE_YIELD_TIMEOUT_MS (200)

#define EXAMPLE_TRACE(...)                               \
    do {                                                     \
        HAL_Printf("\033[1;32;40m%s.%d: ", __func__, __LINE__);  \
        HAL_Printf(__VA_ARGS__);                                 \
        HAL_Printf("\033[0m\r\n");                                   \
    } while (0)

typedef struct {
    int master_devid;
    int cloud_connected;
    int master_initialized;
    int	 server_fd;
    char * DeviceName;
    char * DeviceSecret;
} user_example_ctx_t;

typedef struct{
    unsigned short Feed_Freq_ErrorCode;
    unsigned short Roller_Freq_ErrorCode;
    unsigned short Temp_Freq_ErrorCode;
}error_code_t;

static error_code_t g_error_code;
static error_code_t *error_code_get_ctx(void)
{
    return &g_error_code;
}

static user_example_ctx_t g_user_example_ctx;

static user_example_ctx_t *user_example_get_ctx(void)
{
    return &g_user_example_ctx;
}

void *example_malloc(size_t size)
{
    return HAL_Malloc(size);
}

void example_free(void *ptr)
{
    HAL_Free(ptr);
}

static int user_connected_event_handler(void)
{
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();

    EXAMPLE_TRACE("Cloud Connected");
    user_example_ctx->cloud_connected = 1;
#if defined(OTA_ENABLED) && defined(BUILD_AOS)
    ota_service_init(NULL);
#endif
    return 0;
}

static int user_disconnected_event_handler(void)
{
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();

    EXAMPLE_TRACE("Cloud Disconnected");

    user_example_ctx->cloud_connected = 0;

    return 0;
}

static int user_down_raw_data_arrived_event_handler(const int devid, const unsigned char *payload,
        const int payload_len)
{
    EXAMPLE_TRACE("Down Raw Message, Devid: %d, Payload Length: %d", devid, payload_len);
    return 0;
}

static int user_service_request_event_handler(const int devid, const char *serviceid, const int serviceid_len,
        const char *request, const int request_len,
        char **response, int *response_len)
{
    int contrastratio = 0, to_cloud = 0;
    cJSON *root = NULL, *item_transparency = NULL, *item_from_cloud = NULL;
    EXAMPLE_TRACE("Service Request Received, Devid: %d, Service ID: %.*s, Payload: %s", devid, serviceid_len,
                  serviceid,
                  request);

    /* Parse Root */
    root = cJSON_Parse(request);
    if (root == NULL || !cJSON_IsObject(root)) {
        EXAMPLE_TRACE("JSON Parse Error");
        return -1;
    }

    if (strlen("Custom") == serviceid_len && memcmp("Custom", serviceid, serviceid_len) == 0) {
        /* Parse Item */
        const char *response_fmt = "{\"Contrastratio\":%d}";
        item_transparency = cJSON_GetObjectItem(root, "transparency");
        if (item_transparency == NULL || !cJSON_IsNumber(item_transparency)) {
            cJSON_Delete(root);
            return -1;
        }
        EXAMPLE_TRACE("transparency: %d", item_transparency->valueint);
        contrastratio = item_transparency->valueint + 1;

        /* Send Service Response To Cloud */
        *response_len = strlen(response_fmt) + 10 + 1;
        *response = (char *)HAL_Malloc(*response_len);
        if (*response == NULL) {
            EXAMPLE_TRACE("Memory Not Enough");
            return -1;
        }
        memset(*response, 0, *response_len);
        HAL_Snprintf(*response, *response_len, response_fmt, contrastratio);
        *response_len = strlen(*response);
    } else if (strlen("SyncService") == serviceid_len && memcmp("SyncService", serviceid, serviceid_len) == 0) {
        /* Parse Item */
        const char *response_fmt = "{\"ToCloud\":%d}";
        item_from_cloud = cJSON_GetObjectItem(root, "FromCloud");
        if (item_from_cloud == NULL || !cJSON_IsNumber(item_from_cloud)) {
            cJSON_Delete(root);
            return -1;
        }
        EXAMPLE_TRACE("FromCloud: %d", item_from_cloud->valueint);
        to_cloud = item_from_cloud->valueint + 1;

        /* Send Service Response To Cloud */
        *response_len = strlen(response_fmt) + 10 + 1;
        *response = (char *)HAL_Malloc(*response_len);
        if (*response == NULL) {
            EXAMPLE_TRACE("Memory Not Enough");
            return -1;
        }
        memset(*response, 0, *response_len);
        HAL_Snprintf(*response, *response_len, response_fmt, to_cloud);
        *response_len = strlen(*response);
    }
    cJSON_Delete(root);

    return 0;
}

static int user_property_set_event_handler(const int devid, const char *request, const int request_len)
{
    int res = 0;
    char nameid[50];
    char valuestring[50];
    
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();
    EXAMPLE_TRACE("Property Set Received, Devid: %d, Request: %s", devid, request);

    sscanf(request, "{\"%[^\"]\":%[^}]}", nameid, valuestring);

    EXAMPLE_TRACE("nameid: %s, valuestring: %s", nameid, valuestring);

    if (strcmp("Feed_FreqStatus", nameid) == 0) {
        int set_value = atoi(valuestring);
        /* 正转 */
        if(set_value == 1){
            set_value = 1;
        /* 反转 */
        }else if(set_value == 2){
            set_value = 2;
        /* 停止 */
        }else if(set_value == 3){
            set_value = 5;
        }
        res = Send_Commond(user_example_ctx->server_fd, FEED_LORA_NAME, 1, FREQ_CONTROL, (unsigned short)set_value);
        if(res < 0){
            return -1;
        }
    }else if (strcmp("Feed_FreqSet", nameid) == 0) {
        double set_value = atof(valuestring);
        unsigned short value = set_value / 50 * 10000;
        res = Send_Commond(user_example_ctx->server_fd, FEED_LORA_NAME, 1, FREQ_VALUE, value);
        if(res < 0){
            return -1;
        }
    }else if (strcmp("Roller_FreqStatus", nameid) == 0) {
        int set_value = atoi(valuestring);
        /* 正转 */
        if(set_value == 1){
            set_value = 1;
        /* 反转 */
        }else if(set_value == 2){
            set_value = 2;
        /* 停止 */
        }else if(set_value == 3){
            set_value = 5;
        }
        res = Send_Commond(user_example_ctx->server_fd, FCONVE_NAME, 1, FREQ_CONTROL, (unsigned short)set_value);
        if(res < 0){
            return -1;
        }
    }else if (strcmp("Roller_FreqSet", nameid) == 0) {
        double set_value = atof(valuestring);
        unsigned short value = set_value / 60 * 10000;
        res = Send_Commond(user_example_ctx->server_fd, FCONVE_NAME, 1, FREQ_VALUE, value);
        if(res < 0){
            return -1;
        }
    }else if (strcmp("Temp_PowerSet", nameid) == 0) {
        int set_value = atoi(valuestring);
        res = Send_Commond(user_example_ctx->server_fd, TEMP_NAME, 1, TEMP_OUTPOWER, (unsigned short)set_value);
        if(res < 0){
            return -1;
        }
    }

    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                             (unsigned char *)request, request_len);
    EXAMPLE_TRACE("Post Property Message ID: %d", res);

    return 0;
}

static int user_property_get_event_handler(const int devid, const char *request, const int request_len, char **response,
        int *response_len)
{
    cJSON *request_root = NULL, *item_propertyid = NULL;
    cJSON *response_root = NULL;
    int index = 0;
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();
    unsigned short RevData[10];

    EXAMPLE_TRACE("Property Get Received, Devid: %d, Request: %s", devid, request);

    /* Parse Request */
    request_root = cJSON_Parse(request);
    if (request_root == NULL || !cJSON_IsArray(request_root)) {
        EXAMPLE_TRACE("JSON Parse Error");
        return -1;
    }

    /* Prepare Response */
    response_root = cJSON_CreateObject();
    if (response_root == NULL) {
        EXAMPLE_TRACE("No Enough Memory");
        cJSON_Delete(request_root);
        return -1;
    }

    for (index = 0; index < cJSON_GetArraySize(request_root); index++) {
        item_propertyid = cJSON_GetArrayItem(request_root, index);
        if (item_propertyid == NULL || !cJSON_IsString(item_propertyid)) {
            EXAMPLE_TRACE("JSON Parse Error");
            cJSON_Delete(request_root);
            cJSON_Delete(response_root);
            return -1;
        }

        EXAMPLE_TRACE("Property ID, index: %d, Value: %s", index, item_propertyid->valuestring);

        if (strcmp("Feed_FreqStatus", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,FEED_LORA_NAME, 1, FREQ_STATUS, 1, RevData);
            cJSON_AddNumberToObject(response_root, "Feed_FreqStatus", (int)RevData);
        } else if (strcmp("Feed_FreqSet", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,FEED_LORA_NAME, 1, FREQ_FREQSET, 1, &RevData[1]);
            cJSON_AddNumberToObject(response_root, "Feed_FreqSet", (float)RevData[1] / FREQ_FREQSET_DIV);
        } else if (strcmp("Feed_FreqRun", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,FEED_LORA_NAME, 1, FREQ_FREQRUN, 1, &RevData[2]);
            cJSON_AddNumberToObject(response_root, "Feed_FreqRun", (float)RevData[2] / FREQ_FREQRUN_DIV);
        } else if (strcmp("Feed_Hum", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,FEED_LORA_NAME, 1, FEED_HUM, 1, &RevData[3]);
            cJSON_AddNumberToObject(response_root, "Feed_Hum", (float)RevData[3] / FEED_HUM_DIV);
        } else if (strcmp("Roller_FreqStatus", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,FCONVE_NAME, 1, FREQ_STATUS, 1, &RevData[4]);
           cJSON_AddNumberToObject(response_root, "Roller_FreqStatus", RevData[4]);
        } else if (strcmp("Roller_FreqSet", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,FCONVE_NAME, 1, FREQ_FREQSET, 1, &RevData[5]);
            cJSON_AddNumberToObject(response_root, "Roller_FreqSet", (float)RevData[5] / FREQ_FREQSET_DIV);
        } else if (strcmp("Roller_FreqRun", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,FCONVE_NAME, 1, FREQ_FREQRUN, 1, &RevData[6]);
            cJSON_AddNumberToObject(response_root, "Roller_FreqRun", (float)RevData[6] / FREQ_FREQRUN_DIV);
        } else if (strcmp("Temp", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,TEMP_NAME, 1, TEMP_VALUE, 1, &RevData[7]);
            cJSON_AddNumberToObject(response_root, "Temp", (float)RevData[7] / TEMP_VALUE_DIV);
        } else if (strcmp("Temp_PowerSet", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,TEMP_NAME, 1, TEMP_OUTPOWER, 1, &RevData[8]);
            cJSON_AddNumberToObject(response_root, "Temp_PowerSet", RevData[8]);
        } else if (strcmp("Temp_Stauts", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,TEMP_NAME, 1, TEMP_ERROR, 1, &RevData[9]);
            cJSON_AddNumberToObject(response_root, "Temp_Stauts", RevData[9]);
        } else if (strcmp("Cool_Status", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,TEMP_NAME, 1, FEED_HUM, 1, &RevData[0]);
            cJSON_AddNumberToObject(response_root, "Cool_Status", 0);
        } else if (strcmp("Voltage_A", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,NORMAL_NAME, 2, VOLTAGE_A, 1, &RevData[1]);
            cJSON_AddNumberToObject(response_root, "Voltage_A", (float)RevData[1] / VOLTAGE_DIV);
        } else if (strcmp("Voltage_B", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,NORMAL_NAME, 2, VOLTAGE_B, 1, &RevData[2]);
            cJSON_AddNumberToObject(response_root, "Voltage_B", (float)RevData[2] / VOLTAGE_DIV);
        }else if (strcmp("Voltage_C", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,NORMAL_NAME, 2, VOLTAGE_C, 1, &RevData[3]);
            cJSON_AddNumberToObject(response_root, "Voltage_C", (float)RevData[3] / VOLTAGE_DIV);
        }else if (strcmp("Current_A", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,NORMAL_NAME, 2, CURRENT_A, 1, &RevData[4]);
            cJSON_AddNumberToObject(response_root, "Current_A", (float)RevData[4] / CURRENT_DIV);
        }else if (strcmp("Current_B", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,NORMAL_NAME, 2, CURRENT_B, 1, &RevData[5]);
            cJSON_AddNumberToObject(response_root, "Current_B", (float)RevData[5] / CURRENT_DIV);
        }else if (strcmp("Current_C", item_propertyid->valuestring) == 0) {
            Request_Commond(user_example_ctx->server_fd,NORMAL_NAME, 2, CURRENT_C, 1, &RevData[6]);
            cJSON_AddNumberToObject(response_root, "Current_C", (float)RevData[6] / CURRENT_DIV);
        }
    }
    cJSON_Delete(request_root);

    *response = cJSON_PrintUnformatted(response_root);
    if (*response == NULL) {
        EXAMPLE_TRACE("No Enough Memory");
        cJSON_Delete(response_root);
        return -1;
    }
    cJSON_Delete(response_root);
    *response_len = strlen(*response);

    EXAMPLE_TRACE("Property Get Response: %s", *response);

    return SUCCESS_RETURN;
}

static int user_report_reply_event_handler(const int devid, const int msgid, const int code, const char *reply,
        const int reply_len)
{
    const char *reply_value = (reply == NULL) ? ("NULL") : (reply);
    const int reply_value_len = (reply_len == 0) ? (strlen("NULL")) : (reply_len);

    EXAMPLE_TRACE("Message Post Reply Received, Devid: %d, Message ID: %d, Code: %d, Reply: %.*s", devid, msgid, code,
                  reply_value_len,
                  reply_value);
    return 0;
}

static int user_trigger_event_reply_event_handler(const int devid, const int msgid, const int code, const char *eventid,
        const int eventid_len, const char *message, const int message_len)
{
    EXAMPLE_TRACE("Trigger Event Reply Received, Devid: %d, Message ID: %d, Code: %d, EventID: %.*s, Message: %.*s", devid,
                  msgid, code,
                  eventid_len,
                  eventid, message_len, message);

    return 0;
}

static int user_timestamp_reply_event_handler(const char *timestamp)
{
    EXAMPLE_TRACE("Current Timestamp: %s", timestamp);

    return 0;
}

static int user_initialized(const int devid)
{
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();
    EXAMPLE_TRACE("Device Initialized, Devid: %d", devid);

    if (user_example_ctx->master_devid == devid) {
        user_example_ctx->master_initialized = 1;
    }

    return 0;
}

/** type:
  *
  * 0 - new firmware exist
  *
  */
static int user_fota_event_handler(int type, const char *version)
{
    char buffer[128] = {0};
    int buffer_length = 128;
    int rec;
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();

    if (type == 0) {
        EXAMPLE_TRACE("New Firmware Version: %s", version);

        rec = IOT_Linkkit_Query(user_example_ctx->master_devid, ITM_MSG_QUERY_FOTA_DATA, (unsigned char *)buffer, buffer_length);
        EXAMPLE_TRACE("IOT_Linkkit_Query: %d", rec);
    }

    return 0;
}

/** type:
  *
  * 0 - new config exist
  *
  */
static int user_cota_event_handler(int type, const char *config_id, int config_size, const char *get_type,
                                   const char *sign, const char *sign_method, const char *url)
{
    char buffer[128] = {0};
    int buffer_length = 128;
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();

    if (type == 0) {
        EXAMPLE_TRACE("New Config ID: %s", config_id);
        EXAMPLE_TRACE("New Config Size: %d", config_size);
        EXAMPLE_TRACE("New Config Type: %s", get_type);
        EXAMPLE_TRACE("New Config Sign: %s", sign);
        EXAMPLE_TRACE("New Config Sign Method: %s", sign_method);
        EXAMPLE_TRACE("New Config URL: %s", url);

        IOT_Linkkit_Query(user_example_ctx->master_devid, ITM_MSG_QUERY_COTA_DATA, (unsigned char *)buffer, buffer_length);
    }

    return 0;
}

static uint64_t user_update_sec(void)
{
    static uint64_t time_start_ms = 0;

    if (time_start_ms == 0) {
        time_start_ms = HAL_UptimeMs();
    }

    return (HAL_UptimeMs() - time_start_ms) / 1000;
}

void user_post_property(void)
{
    static int example_index = 0;
    int res = 0;
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();
    char property_payload[512];
    unsigned short RevData[10];

    if (example_index == 0) {
        /* feed lora */
        res = Request_Commond(user_example_ctx->server_fd,FEED_LORA_NAME, 1, FREQ_STATUS, 1, RevData);
        if(res == -2){
            user_example_ctx->server_fd = cli_conn(CS_OPEN);
            EXAMPLE_TRACE("new server_fd: %d,", user_example_ctx->server_fd);
        }
        Request_Commond(user_example_ctx->server_fd,FEED_LORA_NAME, 1, FREQ_FREQSET, 1, &RevData[1]);
        Request_Commond(user_example_ctx->server_fd,FEED_LORA_NAME, 1, FREQ_FREQRUN, 1, &RevData[2]);
        Request_Commond(user_example_ctx->server_fd,FEED_LORA_NAME, 1, FEED_HUM, 1, &RevData[3]);

        EXAMPLE_TRACE("Feed_FreqStatus: %d,Feed_FreqSet: %d,Feed_FreqRun: %d,Feed_Hum: %d,", RevData[0],RevData[1],RevData[2],RevData[3]);

        snprintf(property_payload, 512, "{\"Feed_FreqStatus\":%d,\"Feed_FreqSet\":%.2f,\"Feed_FreqRun\":%.2f,\"Feed_Hum\":%.2f}", \
                                    RevData[0], (float)RevData[1] / FREQ_FREQSET_DIV, (float)RevData[2] / FREQ_FREQRUN_DIV, (float)RevData[3] / FEED_HUM_DIV);
        example_index++;
    } else if (example_index == 1) {
        /* Roller freq */
        Request_Commond(user_example_ctx->server_fd,FCONVE_NAME, 1, FREQ_STATUS, 1, RevData);
        Request_Commond(user_example_ctx->server_fd,FCONVE_NAME, 1, FREQ_FREQSET, 1, &RevData[1]);
        Request_Commond(user_example_ctx->server_fd,FCONVE_NAME, 1, FREQ_FREQRUN, 1, &RevData[2]);

        EXAMPLE_TRACE("Roller_FreqStatus: %d,Roller_FreqSet: %d,Roller_FreqRun: %d", RevData[0], RevData[1], RevData[2]);
        
        snprintf(property_payload, 512, "{\"Roller_FreqStatus\":%d,\"Roller_FreqSet\":%.2f,\"Roller_FreqRun\":%.2f}", \
                RevData[0], (float)RevData[1] / FREQ_FREQSET_DIV, (float)RevData[2] / FREQ_FREQRUN_DIV);
        example_index++;
    }else if (example_index == 2) {
        /* temp freq */
        Request_Commond(user_example_ctx->server_fd,TEMP_NAME, 1, TEMP_VALUE, 1, RevData);
        Request_Commond(user_example_ctx->server_fd,TEMP_NAME, 1, TEMP_OUTPOWER, 1, &RevData[1]);
        Request_Commond(user_example_ctx->server_fd,TEMP_NAME, 1, TEMP_ERROR, 1, &RevData[2]);
        Request_Commond(user_example_ctx->server_fd,TEMP_NAME, 1, TEMP_STATUS, 1, &RevData[3]);
        Request_Commond(user_example_ctx->server_fd,TEMP_NAME, 1, COOL_STATUS, 1, &RevData[4]);

        EXAMPLE_TRACE("Temp: %d,Temp_PowerSet: %d,Temp_Stauts: %d,error: %d,cooling: %d", RevData[0], RevData[1], RevData[3],RevData[2], RevData[4]);
        snprintf(property_payload, 512, "{\"Temp\":%d,\"Temp_PowerSet\":%d,\"Temp_Stauts\":%d,\"Cool_Status\":%d}", \
                                            RevData[0] / TEMP_VALUE_DIV, RevData[1], RevData[3], RevData[4]);
        example_index++;
    }else if (example_index == 3) {
        /* normal */
        Request_Commond(user_example_ctx->server_fd,NORMAL_NAME, 2, VOLTAGE_A, 6, RevData);
        EXAMPLE_TRACE("Voltage_A: %.1f Voltage_B: %.1f Voltage_C: %.1f Current_A: %.2f Current_B: %.2f Current_C: %.2f", \
                        (float)RevData[0] / 10, (float)RevData[1] / 10, (float)RevData[2] / 10, \
                        (float)RevData[3] / 100, (float)RevData[4] / 100, (float)RevData[5] / 100);
        snprintf(property_payload, 512, "{\"Voltage_A\":%.1f,\"Voltage_B\":%.1f,\"Voltage_C\":%.1f,\"Current_A\":%.2f,\"Current_B\":%.2f,\"Current_C\":%.2f}", \
                        (float)RevData[0] / VOLTAGE_DIV, (float)RevData[1] / VOLTAGE_DIV, (float)RevData[2] / VOLTAGE_DIV, \
                        (float)RevData[3] / CURRENT_DIV, (float)RevData[4] / CURRENT_DIV, (float)RevData[5] / CURRENT_DIV);
        //example_index++;
        example_index = 0;
    }
    #if 0
    else if (example_index == 10) {
        /* Wrong Json Format */
        //property_payload = "{\"Cool_Status\":40.02}";
        Request_Commond(user_example_ctx->server_fd,TEMP_NAME, 1, FREQ_FREQRUN, 1, RevData);
        EXAMPLE_TRACE("Cool_Status: %d", RevData[0]);
        snprintf(property_payload, 512, "{\"Cool_Status\":%d}", RevData[0]);
        example_index = 0;
    }
    #endif

    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                             (unsigned char *)property_payload, strlen(property_payload));

    EXAMPLE_TRACE("Post Property Message ID: %d", res);
}

void user_post_event(void)
{
    static int example_index = 0;
    int res = 0;
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();
    error_code_t *error_code_ctx = error_code_get_ctx();
    char *event_id = "NULL";
    char event_payload[512];
    unsigned short RevData[10];

    if (example_index == 0) {
        event_id = "Feed_FreqError";
        /* Normal Example */
        Request_Commond(user_example_ctx->server_fd,FEED_LORA_NAME, 1, FREQ_ERROR, 1, RevData);
        if(RevData[0] != error_code_ctx->Feed_Freq_ErrorCode){
            error_code_ctx->Feed_Freq_ErrorCode = RevData[0];
            snprintf(event_payload, 512, "{\"Feed_FreqErrorCode\":%d}", RevData[0]);
        }else{
            EXAMPLE_TRACE("Feed_Freq_ErrorCode: %d Feed_Freq RecvCode: %d", error_code_ctx->Feed_Freq_ErrorCode, RevData[0]);
            example_index++;
            //EXAMPLE_TRACE("index : %d",example_index);
            return ;
        }
        example_index++;
    } else if (example_index == 1) {
        event_id = "Roller_Error";
        /* Wrong Property ID */
        Request_Commond(user_example_ctx->server_fd,FCONVE_NAME, 1, FREQ_ERROR, 1, RevData);
        if(RevData[0] != error_code_ctx->Roller_Freq_ErrorCode){
            error_code_ctx->Roller_Freq_ErrorCode = RevData[0];
            snprintf(event_payload, 512, "{\"Roller_ErrorCode\":%d}", RevData[0]);
        }else{
            EXAMPLE_TRACE("Roller_Freq_ErrorCode: %d Roller RecvCode: %d", error_code_ctx->Roller_Freq_ErrorCode, RevData[0]);
            example_index++;
            return ;
        }
        example_index++;
    } else if (example_index == 2) {
        event_id = "Temp_Error";
        /* Wrong Value Format */
        Request_Commond(user_example_ctx->server_fd,TEMP_NAME, 1, TEMP_ERROR, 1, RevData);
        Request_Commond(user_example_ctx->server_fd,TEMP_NAME, 1, TEMP_STATUS, 1, &RevData[1]);
        if(RevData[1] == 2)
        {
            if(RevData[0] != error_code_ctx->Temp_Freq_ErrorCode){
                error_code_ctx->Temp_Freq_ErrorCode = RevData[0];
                snprintf(event_payload, 512, "{\"Temp_ErrorCode\":%d}", RevData[0]);
            }else{
                EXAMPLE_TRACE("Temp_Freq_ErrorCode: %d Temp RecvCode: %d", error_code_ctx->Temp_Freq_ErrorCode, RevData[0]);
                example_index = 0;
                return ;
            }
        }
        else{
            example_index = 0;
            return ;
        }
        example_index = 0;
    } 

    res = IOT_Linkkit_TriggerEvent(user_example_ctx->master_devid, event_id, strlen(event_id),
                                   event_payload, strlen(event_payload));
    EXAMPLE_TRACE("Post Event Message ID: %d", res);
}
#define ICCID "AT+ICCID\n"
#define CREG2 "AT+CREG=2\n"
#define CREG_REQUEST "AT+CREG?\n"
#define QNWINFO "AT+QNWINFO\n"
#define CSQ "AT+CSQ\n"
#define AT_OK "OK"
void user_deviceinfo_update(void)
{
    int res = 0,ttyfd;
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();
    char RecvBuff[100];
    char use_datastr[50];
    int RecvLen = 0,mnc,lac,cid,sig_val;
    char *data_str;
    char device_info_update[100];

    ttyfd = open("/dev/ttyUSB2", O_RDWR | O_NOCTTY);
    if (ttyfd < 0){
        EXAMPLE_TRACE("open ec20 module false %d", ttyfd);
        return ;
    }
    res = set_port_min_time_attr (ttyfd, 10, 0 );

    /* ICCID*/
    RecvLen = write(ttyfd, ICCID, strlen(ICCID));

    RecvLen = read(ttyfd, RecvBuff, 100);
    if(RecvLen <= 0){
        return ;
    }
    RecvBuff[RecvLen] = 0;
    if(strstr(RecvBuff, AT_OK) != NULL){
        data_str = strstr(RecvBuff,"ICCID");
        sscanf(data_str, "%*[^ ] %[^\n]",use_datastr);
        EXAMPLE_TRACE("ICCID str:%s",use_datastr);
    }
    /* report ICCID data */
    snprintf(device_info_update, 100, "[{\"attrKey\":\"ICCID\",\"attrValue\":\"%s\"}]", use_datastr);
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_DEVICEINFO_UPDATE,
                             (unsigned char *)device_info_update, strlen(device_info_update));
    EXAMPLE_TRACE("Device Info Update Message ID: %d", res);

    /* QNWINFO*/
    RecvLen = write(ttyfd, QNWINFO, strlen(QNWINFO));

    RecvLen = read(ttyfd, RecvBuff, 100);
    if(RecvLen <= 0){
        return ;
    }
    RecvBuff[RecvLen] = 0;
    if(strstr(RecvBuff, AT_OK) != NULL){
        data_str = strstr(RecvBuff,"QNWINFO");
        sscanf(data_str, "%*[^ ] %[^\n]",use_datastr);
        sscanf(use_datastr, "%*[^,],\"%d\",%*[^\n]",&mnc);
        EXAMPLE_TRACE("QNWINFO str:%d",mnc);
    }
    /* CREG2*/
    RecvLen = write(ttyfd, CREG2, strlen(CREG2));

    RecvLen = read(ttyfd, RecvBuff, 100);
    if(RecvLen <= 0){
        return ;
    }
    RecvBuff[RecvLen] = 0;
    if(strstr(RecvBuff, AT_OK) != NULL){
        /* CREG_REQUEST*/
        RecvLen = write(ttyfd, CREG_REQUEST, strlen(CREG_REQUEST));

        RecvLen = read(ttyfd, RecvBuff, 100);
        if(RecvLen <= 0){
            return ;
        }
        RecvBuff[RecvLen] = 0;
        if(strstr(RecvBuff, AT_OK) != NULL){
            data_str = strstr(RecvBuff,"CREG");
            sscanf(data_str, "%*[^ ] %[^\n]",use_datastr);
            EXAMPLE_TRACE("CREG str:%s",use_datastr);
            sscanf(use_datastr, "%*[^\"]\"%X\",\"%X\"%*[^\n]", &lac, &cid);
            EXAMPLE_TRACE("CREG lac:%d cid:%d", lac, cid);
        }
    }
    /* CSQ*/
    RecvLen = write(ttyfd, CSQ, strlen(CSQ));

    RecvLen = read(ttyfd, RecvBuff, 100);
    if(RecvLen <= 0){
        return ;
    }
    RecvBuff[RecvLen] = 0;
    if(strstr(RecvBuff, AT_OK) != NULL){
        data_str = strstr(RecvBuff,"CSQ");
        sscanf(data_str, "%*[^ ] %[^\n]",use_datastr);
        EXAMPLE_TRACE("CSQ str:%s", use_datastr);
        sscanf(use_datastr, "%d,%*[^\n]", &sig_val);
        sig_val = 2 * sig_val - 113;
        EXAMPLE_TRACE("CSQ sig_val:%d", sig_val);
    }
    /* report LBS data */
    snprintf(device_info_update, 100, "[{\"attrKey\":\"LBS\",\"attrValue\":\"%d,%d,%d,%d\"}]", mnc, lac, cid, sig_val);
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_DEVICEINFO_UPDATE,
                             (unsigned char *)device_info_update, strlen(device_info_update));
    EXAMPLE_TRACE("Device Info Update Message ID: %d", res);

    close(ttyfd);
    
}


static int user_master_dev_available(void)
{
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();

    if (user_example_ctx->cloud_connected && user_example_ctx->master_initialized) {
        return 1;
    }

    return 0;
}

void set_iotx_info()
{
    user_example_ctx_t             *user_example_ctx = user_example_get_ctx();
    HAL_SetProductKey(PRODUCT_KEY);
    HAL_SetProductSecret(PRODUCT_SECRET);
    if((user_example_ctx->DeviceName == NULL) || (user_example_ctx->DeviceSecret == NULL)){
        HAL_SetDeviceName(DEVICE_NAME);
        HAL_SetDeviceSecret(DEVICE_SECRET);
    }else{
        HAL_SetDeviceName(user_example_ctx->DeviceName);
        HAL_SetDeviceSecret(user_example_ctx->DeviceSecret);
    }
}


int linkkit_main(void *paras)
{

    uint64_t                        time_prev_sec = 0, time_now_sec = 0;
    //uint64_t                        time_begin_sec = 0;
    int                             res = 0;
    iotx_linkkit_dev_meta_info_t    master_meta_info;
    user_example_ctx_t             *user_example_ctx = user_example_get_ctx();
    memset(user_example_ctx, 0, sizeof(user_example_ctx_t));
#if defined(__UBUNTU_SDK_DEMO__)
    int                             argc = ((app_main_paras_t *)paras)->argc;
    char                          **argv = ((app_main_paras_t *)paras)->argv;
    
    EXAMPLE_TRACE("argc %d\n", argc);
    if (argc == 3) {
        user_example_ctx->DeviceName = malloc(strlen(argv[1]) + 1);
        strcpy(user_example_ctx->DeviceName, argv[1]);
        user_example_ctx->DeviceSecret = malloc(strlen(argv[2]) + 1);
        strcpy(user_example_ctx->DeviceSecret, argv[2]);
        EXAMPLE_TRACE("set DeviceName %s DeviceSecret %s \n", user_example_ctx->DeviceName, user_example_ctx->DeviceSecret);
    }else
    {
        return -1;
    }
    
#endif

#if !defined(WIFI_PROVISION_ENABLED) || !defined(BUILD_AOS)
    set_iotx_info();
#endif

    IOT_SetLogLevel(IOT_LOG_INFO);

    /* Register Callback */
    IOT_RegisterCallback(ITE_CONNECT_SUCC, user_connected_event_handler);
    IOT_RegisterCallback(ITE_DISCONNECTED, user_disconnected_event_handler);
    IOT_RegisterCallback(ITE_RAWDATA_ARRIVED, user_down_raw_data_arrived_event_handler);
    IOT_RegisterCallback(ITE_SERVICE_REQUST, user_service_request_event_handler);
    IOT_RegisterCallback(ITE_PROPERTY_SET, user_property_set_event_handler);
    IOT_RegisterCallback(ITE_PROPERTY_GET, user_property_get_event_handler);
    IOT_RegisterCallback(ITE_REPORT_REPLY, user_report_reply_event_handler);
    IOT_RegisterCallback(ITE_TRIGGER_EVENT_REPLY, user_trigger_event_reply_event_handler);
    IOT_RegisterCallback(ITE_TIMESTAMP_REPLY, user_timestamp_reply_event_handler);
    IOT_RegisterCallback(ITE_INITIALIZE_COMPLETED, user_initialized);
    IOT_RegisterCallback(ITE_FOTA, user_fota_event_handler);
    IOT_RegisterCallback(ITE_COTA, user_cota_event_handler);

    memset(&master_meta_info, 0, sizeof(iotx_linkkit_dev_meta_info_t));
    memcpy(master_meta_info.product_key, PRODUCT_KEY, strlen(PRODUCT_KEY));
    memcpy(master_meta_info.product_secret, PRODUCT_SECRET, strlen(PRODUCT_SECRET));

    if((user_example_ctx->DeviceName == NULL) || (user_example_ctx->DeviceSecret == NULL)){
        memcpy(master_meta_info.device_name, DEVICE_NAME, strlen(DEVICE_NAME));
        memcpy(master_meta_info.device_secret, DEVICE_SECRET, strlen(DEVICE_SECRET));
    }else{
        memcpy(master_meta_info.device_name, user_example_ctx->DeviceName, strlen(user_example_ctx->DeviceName));
        memcpy(master_meta_info.device_secret, user_example_ctx->DeviceSecret, strlen(user_example_ctx->DeviceSecret));
    }

    /* Choose Login Server, domain should be configured before IOT_Linkkit_Open() */
#if USE_CUSTOME_DOMAIN
    IOT_Ioctl(IOTX_IOCTL_SET_MQTT_DOMAIN, (void *)CUSTOME_DOMAIN_MQTT);
    IOT_Ioctl(IOTX_IOCTL_SET_HTTP_DOMAIN, (void *)CUSTOME_DOMAIN_HTTP);
#else
    int domain_type = IOTX_CLOUD_REGION_SHANGHAI;
    IOT_Ioctl(IOTX_IOCTL_SET_DOMAIN, (void *)&domain_type);
#endif

    /* Choose Login Method */
    int dynamic_register = 0;
    IOT_Ioctl(IOTX_IOCTL_SET_DYNAMIC_REGISTER, (void *)&dynamic_register);

    /* Choose Whether You Need Post Property/Event Reply */
    int post_event_reply = 1;
    IOT_Ioctl(IOTX_IOCTL_RECV_EVENT_REPLY, (void *)&post_event_reply);

    /* Create Master Device Resources */
    user_example_ctx->master_devid = IOT_Linkkit_Open(IOTX_LINKKIT_DEV_TYPE_MASTER, &master_meta_info);
    if (user_example_ctx->master_devid < 0) {
        EXAMPLE_TRACE("IOT_Linkkit_Open Failed\n");
        return -1;
    }

    /* Start Connect Aliyun Server */
    res = IOT_Linkkit_Connect(user_example_ctx->master_devid);
    if (res < 0) {
        EXAMPLE_TRACE("IOT_Linkkit_Connect Failed\n");
        return -1;
    }
    /* unix socket connect */
    user_example_ctx->server_fd = cli_conn(CS_OPEN);
    if(user_example_ctx->server_fd < 0){
        EXAMPLE_TRACE("cli_conn error\n");
    }

    user_deviceinfo_update();

    //time_begin_sec = user_update_sec();
    while (1) {
        IOT_Linkkit_Yield(USER_EXAMPLE_YIELD_TIMEOUT_MS);

        time_now_sec = user_update_sec();
        if (time_prev_sec == time_now_sec) {
            continue;
        }
        
        /* Post Proprety Example */
        if (time_now_sec % 2 == 0 && user_master_dev_available()) {
            user_post_property();
        }
        
        /* Post Event Example */
        if (time_now_sec % 2 == 0 && user_master_dev_available()) {
            user_post_event();
        }
        #if 0
        /* Device Info Update Example */
        if (time_now_sec % 23 == 0 && user_master_dev_available()) {
            user_deviceinfo_update();
        }
        #endif

        time_prev_sec = time_now_sec;
    }

    IOT_Linkkit_Close(user_example_ctx->master_devid);

    IOT_DumpMemoryStats(IOT_LOG_INFO);
    IOT_SetLogLevel(IOT_LOG_NONE);
    close(user_example_ctx->server_fd);

    return 0;
}

