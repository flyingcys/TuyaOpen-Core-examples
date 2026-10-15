/**
 * @file ota_app.c
 * @brief 应用层 OTA 实现（esp_ota）：3 个回调 + 注册注入
 *
 * 原为 port/src/tkl_ota.c（ops 卡模式：port 组装 tal_ota_ops_platform 全局
 * 实例，core 的 tal_api.c 经 extern 强引用——不需要 OTA 的平台也必须给空
 * 桩）。2026-10 OTA 改为 core 回调注入制（tal_ota_register_cbs，见
 * tal_ota.h）：实现移到应用层，本 demo 需要 OTA 故在此提供并注册；不需要
 * OTA 的应用什么都不用做（tal_ota_* 返回 OPRT_NOT_SUPPORTED）。
 *
 * 升级包经 esp_ota 写入下一个更新分区（ota_0/ota_1 轮换），end_notify 时
 * 切换启动分区并按 reset 参数复位。注意：分区表必须包含 otadata 与
 * ota_0/ota_1 分区，否则 start_notify 取分区失败（返回错误码，不崩）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#include "tal_api.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "ota_app.h"

typedef enum {
    OTA_STATE_INIT,
    OTA_STATE_BEGIN,
    OTA_STATE_IN_PROGRESS,
    OTA_STATE_SUCCESS,
} OTA_STATE_T;

typedef struct {
    OTA_STATE_T  state;
    esp_ota_handle_t handle;
    const esp_partition_t *partition;
    unsigned int     image_size;
    unsigned int     offset_image_size;
} OTA_HANDLE_S;

#define DBG_TAG "OTA_APP"

#define OTA_HANDLE_INIT(h) do { \
     (h).state = OTA_STATE_INIT; \
     (h).handle = 0; \
     (h).partition = NULL; \
     (h).image_size = 0; \
     (h).offset_image_size = 0; \
} while (0)

static OTA_HANDLE_S s_ota_handle = {
    .state = OTA_STATE_INIT,
    .handle = 0,
    .partition = NULL,
    .image_size = 0,
    .offset_image_size = 0
};

/**
 * @brief 失败清理：丢弃未完成的升级（abort 释放 handle 与分区 entry），状态归 INIT
 *
 * 两个调用入口：本文件 data_process 的写失败分支（core 对 tal_ota_data_process
 * 的返回值不检查——tuya_ota.c:81——实现必须自清理，否则 state 停在
 * IN_PROGRESS、start_notify 被永久拒绝，设备不重启则再无 OTA 能力，PK H1）；
 * core 下载失败路径的 tal_ota_end_notify(FALSE)（DL_EVENT_FAULT / hmac 校验
 * 失败分支）。幂等：handle 已释放时 abort 跳过。
 */
static void __ota_fail_cleanup(void)
{
    if (s_ota_handle.handle) {
        (void)esp_ota_abort(s_ota_handle.handle); /* 失败可忽略（可能已 end/abort） */
    }
    OTA_HANDLE_INIT(s_ota_handle);
}

/**
 * @brief ota start notify（注册回调：TAL_OTA_CBS_T.start_notify）
 *
 * @param[in] image_size:  image size
 * @param[in] type:        ota type
 * @param[in] path:        ota path
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET ota_start_notify(uint32_t image_size, TUYA_OTA_TYPE_E type, TUYA_OTA_PATH_E path)
{
    (void)type;
    (void)path;

    if (0 == image_size) {
        ESP_LOGE(DBG_TAG, "%s: invalid ota image size %u", __func__, (unsigned)image_size);
        return OPRT_INVALID_PARM;
    }

    /* 入口自愈——**H1 的必需修复**：两个失败入口（写失败停 IN_PROGRESS、
     * hmac 失败停 SUCCESS）最终都靠这里恢复（PK 变异验证：去掉本处全部失败
     * 场景复现）。对齐原 linux port 实现的入口语义——旧 linux 版不看历史
     * 状态、残留句柄在入口直接清掉重开；esp32 版引入状态机时须保留该语义。
     * core 的 OTA 流程单线程串行（DL_EVENT_* 顺序处理），不存在并发会话，
     * 清残留不会打断进行中的升级。 */
    if (OTA_STATE_INIT != s_ota_handle.state) {
        ESP_LOGW(DBG_TAG, "%s: stale ota session (state=%d), cleanup and restart", __func__,
                 s_ota_handle.state);
        __ota_fail_cleanup();
        /* 自愈时对目标分区做整段擦：esp_ota_begin 传非 0 image_size 时只擦
         * aligned_erase_size（esp_ota_ops.c:147-158），若本次 image_size 小于
         * 上次失败残留的范围，尾部残留未擦会让 esp_ota_write 失败（NOR 无法
         * 把 0 写回 1）。自愈是低频异常路径，一次整段擦（分区 1536KB，数百 ms
         * 级）换"无条件正确"（PK H1 边界，1c 提供）。 */
        const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
        if (p) {
            (void)esp_partition_erase_range(p, 0, p->size);
        }
    }

    OTA_HANDLE_INIT(s_ota_handle);

    s_ota_handle.partition = esp_ota_get_next_update_partition(NULL);
    if (NULL == s_ota_handle.partition) {
        ESP_LOGE(DBG_TAG, "%s: get next update partition failed (partitions.csv needs ota_0/ota_1?)", __func__);
        return OPRT_COM_ERROR;
    }

    if (ESP_OK != esp_ota_begin(s_ota_handle.partition, image_size, &s_ota_handle.handle)) {
        s_ota_handle.handle = 0;
        s_ota_handle.partition = NULL;
        ESP_LOGE(DBG_TAG, "%s: call esp_ota_begin failed", __func__);
        return OPRT_COM_ERROR;
    }

    s_ota_handle.image_size = image_size;
    s_ota_handle.state = OTA_STATE_BEGIN;

    return OPRT_OK;
}

/**
 * @brief ota data process（注册回调：TAL_OTA_CBS_T.data_process）
 *
 * @param[in] pack:       point to ota pack
 * @param[out] remain_len: ota pack remain len
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET ota_data_process(TUYA_OTA_DATA_T *pack, uint32_t *remain_len)
{
    if (NULL == pack || NULL == remain_len) {
        ESP_LOGE(DBG_TAG, "%s: input invalid params", __func__);
        return OPRT_INVALID_PARM;
    }

    if (OTA_STATE_INIT == s_ota_handle.state) {
        ESP_LOGE(DBG_TAG, "%s: ota upgrade is not init", __func__);
        return OPRT_COM_ERROR;
    }

    if (OTA_STATE_SUCCESS == s_ota_handle.state) {
        *remain_len = 0;
        ESP_LOGI(DBG_TAG, "%s: ota process is successful", __func__);
        return OPRT_OK;
    }

    if (ESP_OK != esp_ota_write(s_ota_handle.handle, (const void *)pack->data, pack->len)) {
        ESP_LOGE(DBG_TAG, "%s: call esp_ota_write failed", __func__);
        /* core 忽略本接口返回值（tuya_ota.c:81），此自清理有两层价值：
         * 1) 正确性：state 归 INIT 后，后续数据被本函数开头的 INIT 门挡下
         *    报 "not init"；若无本处（state 停 IN_PROGRESS），core 继续送来
         *    的数据会走 esp_ota_write 写一个**已 abort 的 handle**（PK 复核
         *    补核，比"延迟到入口自愈"更糟）；
         * 2) 即时性：stale handle 更早释放，不必等下次 start_notify 的入口
         *    自愈——那是另一个独立必需路径（变异验证：去掉本处，入口自愈
         *    仍可兜住全部失败场景）。
         * 另防御 core 未来改成检查返回值并中断流程的契约。 */
        __ota_fail_cleanup();
        return OPRT_COM_ERROR;
    }

    s_ota_handle.offset_image_size += pack->len;
    if (OTA_STATE_BEGIN == s_ota_handle.state) {
        s_ota_handle.state = OTA_STATE_IN_PROGRESS;
    }

    *remain_len = 0;
    if ((OTA_STATE_IN_PROGRESS == s_ota_handle.state) &&
        (s_ota_handle.offset_image_size >= s_ota_handle.image_size)) {
        s_ota_handle.state = OTA_STATE_SUCCESS;
    }

    return OPRT_OK;
}

/**
 * @brief ota end notify（注册回调：TAL_OTA_CBS_T.end_notify）
 *
 * @param[in] reset:  ota reset
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET ota_end_notify(BOOL_T reset)
{
    /* 注：core 的失败路径不调用本接口——全 core 唯一调用点是成功路径的
     * end_notify(TRUE)（tuya_ota.c:118），DL_EVENT_FAULT / hmac 校验失败
     * 分支无通知。esp32 的失败恢复由 start_notify 的入口自愈 +
     * data_process 写失败自清理兜底（PK H1）。
     *
     * reset 参数当前未使用（到达本函数的调用恒为 TRUE），按"成功结束"无条件
     * 重启是当前 core 契约下的正确行为（PK 复核 ad20a66 声明项）。若 core
     * 将来补失败通知（reset=FALSE），**必须先恢复参数分流**：reset=FALSE 时
     * 走 __ota_fail_cleanup()（不提交、不重启）；旧 linux port 实现在该分支
     * 打印 "Download Fail" 并清理句柄（已随 OTA 回调注入改造移出 port）。 */
    (void)reset;

    if (OTA_STATE_SUCCESS != s_ota_handle.state) {
        ESP_LOGE(DBG_TAG, "%s: ota upgrade state is not success", __func__);
        goto err_exit;
    }

    if (ESP_OK != esp_ota_end(s_ota_handle.handle)) {
        ESP_LOGE(DBG_TAG, "%s: call esp_ota_end failed", __func__);
        goto err_exit;
    }

    if (ESP_OK != esp_ota_set_boot_partition(s_ota_handle.partition)) {
        ESP_LOGE(DBG_TAG, "%s: call esp_ota_set_boot_partition failed", __func__);
        goto err_exit;
    }

    OTA_HANDLE_INIT(s_ota_handle);

    ESP_LOGI(DBG_TAG, "%s: ota upgrade successful, system reboot now...", __func__);
    esp_restart();
    return OPRT_OK;

err_exit:
    __ota_fail_cleanup();
    return OPRT_COM_ERROR;
}
