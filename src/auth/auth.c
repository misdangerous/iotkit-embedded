#include "guider_internal.h"

#define _ONLINE

#ifdef _ONLINE
    const static char *guider_host = "http://iot-auth.cn-shanghai.aliyuncs.com/auth/devicename";
#else
    const static char *guider_host = "http://iot-auth-pre.cn-shanghai.aliyuncs.com/auth/devicename";
#endif

/*
    struct {
        char            host_name[HOST_ADDRESS_LEN + 1];
        uint16_t        port;
        char            user_name[USER_NAME_LEN + 1];
        char            password[PASSWORD_LEN + 1];
        char            client_id[CLIENT_ID_LEN + 1];
        const char *    pubKey;
    }
*/

static int _hmac_md5_signature(
            char *md5_sigbuf,
            const int md5_buflen,
            const char *client_id,
            const char *device_name,
            const char *product_key,
            const char *timestamp_str,
            const char *device_secret)
{
    char                signature[40];
    char                hmac_source[512];
    int                 rc = -1;

    memset(signature, 0, sizeof(signature));
    memset(hmac_source, 0, sizeof(hmac_source));
    rc = snprintf(hmac_source,
                  sizeof(hmac_source),
                  "clientId%sdeviceName%sproductKey%stimestamp%s",
                  client_id,
                  device_name,
                  product_key,
                  timestamp_str ? timestamp_str : "2524608000000");
    assert(rc < sizeof(hmac_source));
    log_debug("| source: %s (%d)", hmac_source, strlen(hmac_source));

    utils_hmac_md5(hmac_source, strlen(hmac_source),
                   signature,
                   device_secret,
                   strlen(device_secret));
    log_debug("| signature: %s (%d)", signature, strlen(signature));

    memcpy(md5_sigbuf, signature, md5_buflen);
    return 0;
}

static int iotx_get_id_token(
            const char *auth_host,
            const char *product_key,
            const char *device_name,
            const char *device_secret,
            const char *client_id,
            const char *version,
            const char *timestamp,
            const char *resources,
            char *iot_id,
            char *iot_token,
            char *host,
            uint16_t *pport)
{
#define SIGN_SOURCE_LEN     (256)
#define HTTP_POST_MAX_LEN   (1024)
#define HTTP_RESP_MAX_LEN   (1024)

    int ret = -1, length;
    char sign[33];
    char *buf = NULL, *post_buf = NULL, *response_buf = NULL;

    httpclient_t httpclient;
    httpclient_data_t httpclient_data;


    length = strlen(client_id);
    length += strlen(product_key);
    length += strlen(device_name);
    length += strlen(timestamp);
    length += 40; //40 chars space for key strings(clientId,deviceName,productKey,timestamp)

    if (length > SIGN_SOURCE_LEN) {
        log_warning("The total length may be is too long. client_id=%s, product_key=%s, device_name=%s, timestamp= %s",
                    client_id, product_key, device_name, timestamp);
    }

    if (NULL == (buf = LITE_malloc(length))) {
        goto do_exit;
    }

    //Calculate sign
    memset(sign, 0, sizeof(sign));

    ret = snprintf(buf,
                   SIGN_SOURCE_LEN,
                   "clientId%sdeviceName%sproductKey%stimestamp%s",
                   client_id,
                   device_name,
                   product_key,
                   timestamp);
    if ((ret < 0) || (ret > SIGN_SOURCE_LEN)) {
        goto do_exit;
    }
    log_debug("sign source = %.64s ...", buf);
    utils_hmac_md5(buf, strlen(buf), sign, device_secret, strlen(device_secret));


    memset(&httpclient, 0, sizeof(httpclient_t));
    httpclient.header = "Accept: text/xml,text/javascript,text/html,application/json\r\n";

    memset(&httpclient_data, 0, sizeof(httpclient_data_t));

    post_buf = (char *) LITE_malloc(HTTP_POST_MAX_LEN);
    if (NULL == post_buf) {
        log_err("malloc http post buf failed!");
        return ERROR_MALLOC;
    }
    memset(post_buf, 0, HTTP_POST_MAX_LEN);

    ret = snprintf(post_buf,
                   HTTP_POST_MAX_LEN,
                   "productKey=%s&deviceName=%s&sign=%s&version=%s&clientId=%s&timestamp=%s&resources=%s",
                   product_key,
                   device_name,
                   sign,
                   version,
                   client_id,
                   timestamp,
                   resources);

    if ((ret < 0) || (ret >= HTTP_POST_MAX_LEN)) {
        log_err("http message body is too long");
        ret = -1;
        goto do_exit;
    }

    log_debug("http request: \r\n\r\n%s\r\n", post_buf);

    ret = strlen(post_buf);

    response_buf = (char *)LITE_malloc(HTTP_RESP_MAX_LEN);
    if (NULL == response_buf) {
        log_err("malloc http response buf failed!");
        return ERROR_MALLOC;
    }
    memset(response_buf, 0, HTTP_RESP_MAX_LEN);

    httpclient_data.post_content_type = "application/x-www-form-urlencoded;charset=utf-8";
    httpclient_data.post_buf = post_buf;
    httpclient_data.post_buf_len = ret;
    httpclient_data.response_buf = response_buf;
    httpclient_data.response_buf_len = HTTP_RESP_MAX_LEN;

#ifdef _ONLINE

    iotx_post(&httpclient,
              auth_host,
              443,
              iotx_ca_get(),
              10000,
              &httpclient_data);
#else

    iotx_post(&httpclient,
              auth_host,
              80,
              NULL,
              10000,
              &httpclient_data);
#endif

    /*
        {
            "code": 200,
            "data": {
                "iotId":"030VCbn30334364bb36997f44cMYTBAR",
                "iotToken":"e96d15a4d4734a73b13040b1878009bc",
                "resources": {
                    "mqtt": {
                            "host":"iot-as-mqtt.cn-shanghai.aliyuncs.com",
                            "port":1883
                        }
                    }
            },
            "message":"success"
        }
    */
    log_debug("http response: \r\n\r\n%s\r\n", httpclient_data.response_buf);

    //get iot-id and iot-token from response

    int type;
    const char *pvalue, *presrc;
    char port_str[6];

    //get iot-id
    pvalue = LITE_json_value_of("data.iotId", httpclient_data.response_buf);
    if (NULL == pvalue) {
        goto do_exit;
    }
    strcpy(iot_id, pvalue);
    LITE_free(pvalue);

    //get iot-token
    pvalue = LITE_json_value_of("data.iotToken", httpclient_data.response_buf);
    if (NULL == pvalue) {
        goto do_exit;
    }
    strcpy(iot_token, pvalue);
    LITE_free(pvalue);

    //get host
    pvalue = LITE_json_value_of("data.resources.mqtt.host", httpclient_data.response_buf);
    if (NULL == pvalue) {
        goto do_exit;
    }
    strcpy(host, pvalue);
    LITE_free(pvalue);

    //get port
    pvalue = LITE_json_value_of("data.resources.mqtt.port", httpclient_data.response_buf);
    if (NULL == pvalue) {
        goto do_exit;
    }
    strcpy(port_str, pvalue);
    LITE_free(pvalue);
    *pport = atoi(port_str);

    log_debug("%10s: %s", "iotId", iot_id);
    log_debug("%10s: %s", "iotToken", iot_token);
    log_debug("%10s: %s", "Host", host);
    log_debug("%10s: %d", "Port", *pport);

    ret = 0;

do_exit:
    if (NULL != buf) {
        LITE_free(buf);
    }

    if (NULL != post_buf) {
        LITE_free(post_buf);
    }

    if (NULL != response_buf) {
        LITE_free(response_buf);
    }

    return ret;
}

/*
    mqtt直接连接，域名＝ ${productkey}.iot-as-mqtt.cn-shanghai.aliyuncs.com
    sign签名和AUTH一致
    mqttClientId = clientId|securemode=0,gw=0,signmethod=hmacmd5,pid=xxx|
    mqttuserName = deviceName&productkey
    mqttPassword = sign

    其中gw＝1代表网关设备，0为普通设备； pid代表合作伙伴id，可选；
    clientId为客户端自标示id不可空，建议使用MAC、SN；
    ||内为扩展参数，域名直连模式下 securemode必须传递，不传递则默认是auth方式。
 */
typedef enum _SECURE_MODE {
    MODE_TLS_TOKEN              = -1,
    MODE_TCP_TOKEN_PLAIN        = 0,
    MODE_TCP_TOKEN_ID2_ENCRPT   = 1,
    MODE_TLS_DIRECT             = 2,
    MODE_TCP_DIRECT_PLAIN       = 3,
    MODE_TCP_DIRECT_ID2_ENCRYPT = 4,
    MODE_TLS_ID2                = 5,
} SECURE_MODE;

/*
    struct {
        char            host_name[HOST_ADDRESS_LEN + 1];
        uint16_t        port;
        char            user_name[USER_NAME_LEN + 1];
        char            password[PASSWORD_LEN + 1];
        char            client_id[CLIENT_ID_LEN + 1];
        const char *    pubKey;
    }
*/
int32_t iotx_auth(iotx_device_info_pt pdevice_info, iotx_user_info_pt puser_info)
{
    char            partner_id[16];
    int             ret = -1;

    memset(partner_id, 0, sizeof(partner_id));
    HAL_GetPartnerID(partner_id);

#ifdef DIRECT_MQTT
#define DIRECT_MQTT_DOMAIN  "iot-as-mqtt.cn-shanghai.aliyuncs.com"

    char            iotx_signature[33];
    char            iotx_hmac_source[512];
    SECURE_MODE     iotx_secmode;

#ifdef IOTX_MQTT_TCP
    iotx_secmode = MODE_TCP_DIRECT_PLAIN;
    puser_info->pubKey = NULL;
#else
    iotx_secmode = MODE_TLS_DIRECT;
    puser_info->pubKey = iotx_ca_get();
#endif

    ret = snprintf(puser_info->host_name,
                   sizeof(puser_info->host_name),
#if 0
                   "%s.%s",
                   pdevice_info->product_key,
                   DIRECT_MQTT_DOMAIN
#else
                   "%s",
                   "10.125.63.74"
#endif
                  );
    assert(ret < sizeof(puser_info->host_name));
    puser_info->port = 1883;

    ret = snprintf(puser_info->client_id,
                   sizeof(puser_info->client_id),
                   (strlen(partner_id) ?
                    "%s|securemode=%d,gw=0,signmethod=hmacmd5,partner_id=%s,timestamp=2524608000000|" :
                    "%s|securemode=%d,gw=0,signmethod=hmacmd5%s,timestamp=2524608000000|"),
                   pdevice_info->device_id,
                   iotx_secmode,
                   (strlen(partner_id) ? partner_id : "")
                  );
    assert(ret < sizeof(puser_info->client_id));

    ret = snprintf(puser_info->user_name,
                   sizeof(puser_info->user_name),
                   "%s&%s",
                   pdevice_info->device_name,
                   pdevice_info->product_key);
    assert(ret < sizeof(puser_info->user_name));

    ret = _hmac_md5_signature(iotx_signature, sizeof(iotx_signature),
                              pdevice_info->device_id,
                              pdevice_info->device_name,
                              pdevice_info->product_key,
                              0,
                              pdevice_info->device_secret);
    log_debug("iotx_signature : %s", iotx_signature);

    ret = snprintf(puser_info->password,
                   sizeof(puser_info->password),
                   "%s",
                   iotx_signature);
    assert(ret <= strlen(iotx_signature));

#else   /* #ifdef DIRECT_MQTT */

    char iot_id[GUIDER_IOT_ID_LEN + 1], iot_token[GUIDER_IOT_TOKEN_LEN + 1], host[HOST_ADDRESS_LEN + 1];
    uint16_t port;

    if (0 != iotx_get_id_token(
                    guider_host,
                    pdevice_info->product_key,
                    pdevice_info->device_name,
                    pdevice_info->device_secret,
                    pdevice_info->device_id,
                    "default",
                    "2524608000000", //01 Jan 2050
                    "mqtt",
                    iot_id,
                    iot_token,
                    host,
                    &port)) {
        return -1;
    }


    strncpy(puser_info->user_name, iot_id, USER_NAME_LEN);
    strncpy(puser_info->password, iot_token, PASSWORD_LEN);
    strncpy(puser_info->host_name, host, HOST_ADDRESS_LEN);
    puser_info->port = port;
#ifdef IOTX_MQTT_TCP
    puser_info->pubKey = NULL;
#else
    puser_info->pubKey = iotx_ca_get();
#endif
    if (NULL == puser_info->pubKey) {
        //Append string "nonesecure" if TCP connection be used.
        if (NULL == HAL_GetPartnerID(partner_id)) {
            ret = snprintf(puser_info->client_id,
                           CLIENT_ID_LEN,
                           "%s|securemode=0|",
                           pdevice_info->device_id);
        } else {
            //Append "partner_id" if we have partner_id
            ret = snprintf(puser_info->client_id,
                           CLIENT_ID_LEN,
                           "%s|securemode=0,partner_id=%s|",
                           pdevice_info->device_id,
                           partner_id);
        }
    } else {
        if (NULL == HAL_GetPartnerID(partner_id)) {
            ret = snprintf(puser_info->client_id,
                           CLIENT_ID_LEN,
                           "%s",
                           pdevice_info->device_id);
        } else {
            //Append "partner_id" if we have partner_id
            ret = snprintf(puser_info->client_id,
                           CLIENT_ID_LEN,
                           "%s|partner_id=%s|",
                           pdevice_info->device_id,
                           partner_id);
        }
    }

    if (ret >= CLIENT_ID_LEN) {
        log_err("client_id is too long");
    } else if (ret < 0) {
        return -1;
    }
#endif

    return 0;
}