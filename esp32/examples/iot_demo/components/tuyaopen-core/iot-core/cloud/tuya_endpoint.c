/**
 * @file tuya_endpoint.c
 * @brief Implementation of endpoint management for Tuya IoT devices.
 *
 * This source file contains the implementation of functions related to managing
 * the endpoints of Tuya IoT devices. It includes functionalities such as
 * retrieving cloud endpoints based on the device's region and environment,
 * storing and managing region and registration key information, and writing
 * these details into persistent storage for future use.
 *
 * The endpoint management is crucial for ensuring that IoT devices can
 * communicate with the correct Tuya cloud services, which may vary based on the
 * device's geographical location and the operational environment (e.g.,
 * development, staging, production). This file leverages Tuya's Abstract Layer
 * API (TAL API) for operations like key-value storage to maintain endpoint
 * information.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_endpoint.h"
#include "tal_api.h"

#include "tal_kv.h"
#include "tal_memory.h"

extern int iotdns_cloud_endpoint_get(const char *region, const char *env, tuya_endpoint_t *endpoint);

typedef struct {
    char region[MAX_LENGTH_REGION + 1];
    char regist_key[MAX_LENGTH_REGIST + 1];
    tuya_endpoint_t endpoint;
} endpoint_management_t;

static endpoint_management_t endpoint_mgr;

static int tuya_region_regist_key_write(const char *region, const char *regist_key)
{
    if (NULL == region || NULL == regist_key) {
        PR_ERR("Invalid param");
        return OPRT_INVALID_PARM;
    }

    /* Write kv storage */
    int ret = 0;
    ret = tal_kv_set("region", (const uint8_t *)region, strlen(region));
    if (ret != OPRT_OK) {
        PR_ERR("tal_kv_set region, error:0x%02x", ret);
        return OPRT_KVS_WR_FAIL;
    }

    ret = tal_kv_set("regist_key", (const uint8_t *)regist_key, strlen(regist_key));
    if (ret != OPRT_OK) {
        PR_ERR("tal_kv_set regist_key, error:0x%02x", ret);
        return OPRT_KVS_WR_FAIL;
    }

    return OPRT_OK;
}

static int tuya_region_regist_key_read(char *region, char *regist_key)
{
    if (NULL == region || NULL == regist_key) {
        PR_ERR("Invalid param");
        return OPRT_INVALID_PARM;
    }

    /* Read the region&env from kv storage */
    int ret = 0;
    size_t len = 0;
    uint8_t *value = NULL;
    ret = tal_kv_get("region", &value, &len);
    if (ret != OPRT_OK) {
        PR_ERR("tal_kv_get region fail:0x%02x", ret);
        return OPRT_KVS_RD_FAIL;
    }
    /* 按实际长度拷贝：旧实现 buf = 密文长+1 掩盖了长度不足；新引擎返回"明文长+1"，
     * 固定长 memcpy 在云端下发更短值时越界读 */
    size_t copy = (len < MAX_LENGTH_REGION) ? len : MAX_LENGTH_REGION;
    memcpy(region, value, copy);
    region[copy] = 0;
    tal_free(value);

    ret = tal_kv_get("regist_key", &value, &len);
    if (ret != OPRT_OK) {
        PR_ERR("tal_kv_get regist_key fail:0x%02x", ret);
        return OPRT_KVS_RD_FAIL;
    }
    /* 同上：regist_key 来自云端 env 字段，长度不受本地控制 */
    copy = (len < MAX_LENGTH_REGIST) ? len : MAX_LENGTH_REGIST;
    memcpy(regist_key, value, copy);
    regist_key[copy] = 0;
    tal_free(value);

    return OPRT_OK;
}

/**
 * @brief Retrieves the certificate for the specified endpoint.
 *
 * This function retrieves the certificate for the specified endpoint from the
 * key-value store.
 *
 * @param[in] endpoint Pointer to the tuya_endpoint_t structure representing the
 * endpoint.
 * @return Operation result. Returns OPRT_OK on success, or an error code on
 * failure.
 */
int tuya_endpoint_cert_get(tuya_endpoint_t *endpoint)
{
    if (NULL == endpoint) {
        PR_ERR("Invalid param");
        return OPRT_INVALID_PARM;
    }

    int ret = tal_kv_get("endpoint.cert", &endpoint->cert, &endpoint->cert_len);
    if (ret != OPRT_OK) {
        PR_ERR("tal_kv_get endpoint.cert fail:0x%02x", ret);
    }

    return ret;
}

/**
 * @brief Sets the certificate for the Tuya endpoint.
 *
 * This function sets the certificate for the Tuya endpoint by storing it in the
 * key-value store.
 *
 * @param endpoint Pointer to the Tuya endpoint structure.
 * @return Returns OPRT_OK on success, or an error code on failure.
 */
int tuya_endpoint_cert_set(tuya_endpoint_t *endpoint)
{
    if (NULL == endpoint) {
        PR_ERR("Invalid param");
        return OPRT_INVALID_PARM;
    }

    int ret = tal_kv_set("endpoint.cert", endpoint->cert, endpoint->cert_len);
    if (ret != OPRT_OK) {
        PR_ERR("tal_kv_set endpoint.cert fail:0x%02x", ret);
    }

    return ret;
}

/* 逐 key 读字符串字段：值含结尾 '\0' 一起存；超长不截断，直接报错 */
static int tuya_endpoint_kv_str_read(const char *key, char *buf, size_t buf_size)
{
    uint8_t *value = NULL;
    size_t len = 0;
    int ret = tal_kv_get(key, &value, &len);
    if (ret != OPRT_OK) {
        PR_ERR("tal_kv_get %s error:%d", key, ret);
        return ret;
    }

    /* 旧实现语义：存进去放不下就报错，不静默截断（截断后的 host/path 会被当成
     * 有效值使用）。上限为 buf_size：值自带结尾 '\0'，正好填满缓冲是合法的
     * （写侧恒为 strlen+1，与字段容量上限逐字对应）；buf_size == 0 单独判防下溢 */
    if (0 == buf_size || len > buf_size) {
        PR_ERR("tal_kv_get %s value too long: %u > %u", key, (unsigned int)len, (unsigned int)buf_size);
        tal_free(value);
        return OPRT_COM_ERROR;
    }

    if (0 == len) {
        buf[0] = 0; /* 兜底终止（port 返回空值时） */
    } else {
        memcpy(buf, value, len);
        buf[len - 1] = 0; /* 兜底终止；写侧末字节本就是 '\0'，此处幂等 */
    }
    tal_free(value);

    return OPRT_OK;
}

/* 逐 key 读 uint16 字段：值按 2 字节二进制存取 */
static int tuya_endpoint_kv_u16_read(const char *key, uint16_t *out)
{
    uint8_t *value = NULL;
    size_t len = 0;
    int ret = tal_kv_get(key, &value, &len);
    if (ret != OPRT_OK) {
        PR_ERR("tal_kv_get %s error:%d", key, ret);
        return ret;
    }
    if (len != sizeof(*out)) {
        PR_ERR("tal_kv_get %s length error:%u", key, (unsigned int)len);
        tal_free(value);
        return OPRT_COM_ERROR;
    }

    memcpy(out, value, sizeof(*out));
    tal_free(value);

    return OPRT_OK;
}

/**
 * @brief Retrieves the domain information for the endpoint.
 *
 * This function retrieves the domain information for the endpoint by reading
 * the key-value pairs from the key-value store, one key per field.
 *
 * @param[in] endpoint Pointer to the tuya_endpoint_t structure to store the
 * retrieved domain information.
 * @return Returns OPRT_OK on success, or an error code on failure.
 */
int tuya_endpoint_domain_get(tuya_endpoint_t *endpoint)
{
    if (NULL == endpoint) {
        PR_ERR("Invalid param");
        return OPRT_INVALID_PARM;
    }

    /* 逐 key 读取（key 名即字段名）：5 个字段都读，任一失败返回该错误码 */
    int ret = tuya_endpoint_kv_str_read("atop.host", endpoint->atop.host, sizeof(endpoint->atop.host));
    int rt = tuya_endpoint_kv_u16_read("atop.port", &endpoint->atop.port);
    if (OPRT_OK == ret) {
        ret = rt;
    }
    rt = tuya_endpoint_kv_str_read("atop.path", endpoint->atop.path, sizeof(endpoint->atop.path));
    if (OPRT_OK == ret) {
        ret = rt;
    }
    rt = tuya_endpoint_kv_str_read("mqtt.host", endpoint->mqtt.host, sizeof(endpoint->mqtt.host));
    if (OPRT_OK == ret) {
        ret = rt;
    }
    rt = tuya_endpoint_kv_u16_read("mqtt.port", &endpoint->mqtt.port);
    if (OPRT_OK == ret) {
        ret = rt;
    }

    if (ret != OPRT_OK) {
        PR_ERR("tal_kv_get endpoint domain error:%d", ret);
    }

    return ret;
}

/**
 * @brief Sets the domain for the Tuya endpoint.
 *
 * This function sets the domain for the Tuya endpoint by writing the endpoint
 * information into the key-value store, one key per field.
 *
 * @param endpoint Pointer to the tuya_endpoint_t structure containing the
 * endpoint information.
 * @return Returns OPRT_OK if the domain is set successfully, otherwise returns
 * an error code.
 */
int tuya_endpoint_domain_set(tuya_endpoint_t *endpoint)
{
    if (NULL == endpoint) {
        PR_ERR("Invalid param");
        return OPRT_INVALID_PARM;
    }

    /* 逐 key 写入（key 名即字段名）：字符串含结尾 '\0' 一起存，端口按 2 字节二进制存；
     * 任一失败返回该错误码 */
    int ret = tal_kv_set("atop.host", (const uint8_t *)endpoint->atop.host, strlen(endpoint->atop.host) + 1);
    if (ret != OPRT_OK) {
        PR_ERR("tal_kv_set atop.host error:%d", ret);
        return ret;
    }

    ret = tal_kv_set("atop.port", (const uint8_t *)&endpoint->atop.port, sizeof(endpoint->atop.port));
    if (ret != OPRT_OK) {
        PR_ERR("tal_kv_set atop.port error:%d", ret);
        return ret;
    }

    ret = tal_kv_set("atop.path", (const uint8_t *)endpoint->atop.path, strlen(endpoint->atop.path) + 1);
    if (ret != OPRT_OK) {
        PR_ERR("tal_kv_set atop.path error:%d", ret);
        return ret;
    }

    ret = tal_kv_set("mqtt.host", (const uint8_t *)endpoint->mqtt.host, strlen(endpoint->mqtt.host) + 1);
    if (ret != OPRT_OK) {
        PR_ERR("tal_kv_set mqtt.host error:%d", ret);
        return ret;
    }

    ret = tal_kv_set("mqtt.port", (const uint8_t *)&endpoint->mqtt.port, sizeof(endpoint->mqtt.port));
    if (ret != OPRT_OK) {
        PR_ERR("tal_kv_set mqtt.port error:%d", ret);
        return ret;
    }

    return OPRT_OK;
}

/**
 * @brief Sets the region and registration key for the Tuya endpoint.
 *
 * This function sets the region and registration key for the Tuya endpoint by
 * writing them to the appropriate variables.
 *
 * @param region The region to set for the Tuya endpoint.
 * @param regist_key The registration key to set for the Tuya endpoint.
 * @return Returns OPRT_OK if the region and registration key are set
 * successfully, or OPRT_KVS_WR_FAIL if there was an error writing the region
 * and registration key.
 */
int tuya_endpoint_region_regist_set(const char *region, const char *regist_key)
{
    if (tuya_region_regist_key_write(region, regist_key) != OPRT_OK) {
        PR_ERR("region_regist_key_write error");
        return OPRT_KVS_WR_FAIL;
    }

    strncpy(endpoint_mgr.region, region, sizeof(endpoint_mgr.region) - 1);
    endpoint_mgr.region[sizeof(endpoint_mgr.region) - 1] = '\0';
    strncpy(endpoint_mgr.regist_key, regist_key, sizeof(endpoint_mgr.regist_key) - 1);
    endpoint_mgr.regist_key[sizeof(endpoint_mgr.regist_key) - 1] = '\0';
    return OPRT_OK;
}

/**
 * @brief Removes the endpoint configuration from the device.
 *
 * This function removes the endpoint configuration from the device by deleting
 * the corresponding key-value pairs from the key-value store.
 *
 * @return OPRT_OK if the endpoint configuration is successfully removed,
 * otherwise an error code.
 */
int tuya_endpoint_remove(void)
{
    tal_kv_del("region");
    tal_kv_del("regist_key");
    tal_kv_del("endpoint.cert");
    /* domain 已拆为 5 个字段 key（见 tuya_endpoint_domain_set），逐个清理 */
    tal_kv_del("atop.host");
    tal_kv_del("atop.port");
    tal_kv_del("atop.path");
    tal_kv_del("mqtt.host");
    tal_kv_del("mqtt.port");
    /* 过渡期清理旧 key：旧固件把 domain 整体存成单 key，不删则降级回旧固件会读到陈旧数据 */
    tal_kv_del("endpoint.domain");

    return OPRT_OK;
}

/**
 * @brief Initializes the Tuya endpoint.
 *
 * This function reads the storage region and registration key, and sets the
 * default online environment if necessary.
 *
 * @return The result of the initialization process.
 */
int tuya_endpoint_init(void)
{
    int ret;

    /* Read storage region & registration key */
    ret = tuya_region_regist_key_read(endpoint_mgr.region, endpoint_mgr.regist_key);
    PR_INFO("endpoint_mgr.region:%s", endpoint_mgr.region);
    PR_INFO("endpoint_mgr.regist_key:%s", endpoint_mgr.regist_key);

    /* Default online environment */
    if (endpoint_mgr.regist_key[0] == 0) {
        strncpy(endpoint_mgr.regist_key, "pro", sizeof(endpoint_mgr.regist_key) - 1);
        endpoint_mgr.regist_key[sizeof(endpoint_mgr.regist_key) - 1] = '\0';
    }

    return ret;
}

/**
 * @brief Updates the Tuya endpoint.
 *
 * This function updates the Tuya endpoint by retrieving the iot-dns domain data
 * using the specified region and registration key. If the iotdns has already
 * been called, the allocated certificate memory will be released before
 * updating the endpoint.
 *
 * @return The result of the endpoint update operation.
 *         Returns 0 on success, or an error code on failure.
 */
int tuya_endpoint_update(void)
{
    int ret;

    /* If iotdns has already been called,
     * the allocated certificate memory needs to be released. */
    if (endpoint_mgr.endpoint.cert != NULL && endpoint_mgr.endpoint.cert_len > 0) {
        PR_TRACE("Free endpoint already exist cert.");
        tal_free(endpoint_mgr.endpoint.cert);
        endpoint_mgr.endpoint.cert = NULL;
        endpoint_mgr.endpoint.cert_len = 0;
    }
    /* Try to get the iot-dns domain data */
    ret = iotdns_cloud_endpoint_get(endpoint_mgr.region, endpoint_mgr.regist_key, &endpoint_mgr.endpoint);

    return ret;
}

/**
 * @brief Updates the auto region for the Tuya endpoint.
 *
 * This function updates the auto region for the Tuya endpoint by retrieving the
 * iot-dns domain data. If the iotdns has already been called, the allocated
 * certificate memory is released.
 *
 * @return The result of the operation. Returns 0 on success, or an error code
 * on failure.
 */
int tuya_endpoint_update_auto_region(void)
{
    int ret;

    /* If iotdns has already been called,
     * the allocated certificate memory needs to be released. */
    if (endpoint_mgr.endpoint.cert != NULL && endpoint_mgr.endpoint.cert_len > 0) {
        PR_TRACE("Free endpoint already exist cert.");
        tal_free(endpoint_mgr.endpoint.cert);
        endpoint_mgr.endpoint.cert = NULL;
        endpoint_mgr.endpoint.cert_len = 0;
    }
    /* Try to get the iot-dns domain data */
    ret = iotdns_cloud_endpoint_get(NULL, endpoint_mgr.regist_key, &endpoint_mgr.endpoint);
    return ret;
}

/**
 * @brief Retrieves the Tuya endpoint.
 *
 * This function returns a pointer to the Tuya endpoint structure.
 *
 * @return A constant pointer to the Tuya endpoint structure.
 */
const tuya_endpoint_t *tuya_endpoint_get(void)
{
    return (const tuya_endpoint_t *)&endpoint_mgr.endpoint;
}
