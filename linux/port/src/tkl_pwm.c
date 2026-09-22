#include "tkl_pwm.h"
#include "tuya_error_code.h"
#include "tuya_kconfig.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PWM_SYSFS_CHIP
#define PWM_SYSFS_CHIP 0
#endif

#ifndef PWM_SYSFS_CHANNEL_BASE
#define PWM_SYSFS_CHANNEL_BASE 0
#endif

#ifndef TUYA_PWM_DUTY_SCALE
// Tuya duty is commonly 0..10000
#define TUYA_PWM_DUTY_SCALE 10000U
#endif

typedef struct {
    bool inited;
    bool exported;
    bool enabled;
    TUYA_PWM_BASE_CFG_T cfg;
} pwm_ctx_t;

static pthread_mutex_t g_pwm_lock = PTHREAD_MUTEX_INITIALIZER;
static pwm_ctx_t g_pwm_ctx[TUYA_PWM_NUM_MAX];

static bool path_exists(const char *path)
{
    struct stat st;
    return (path != NULL) && (stat(path, &st) == 0);
}

static int write_text_file(const char *path, const char *value)
{
    if (path == NULL || value == NULL) {
        return -EINVAL;
    }

    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }

    const size_t len = strlen(value);
    ssize_t wr = write(fd, value, len);
    int saved = errno;
    close(fd);

    if (wr < 0) {
        return -saved;
    }
    if ((size_t)wr != len) {
        return -EIO;
    }
    return 0;
}

static void pwm_sysfs_paths(int chip, int channel, char *chip_dir, size_t chip_dir_len, char *pwm_dir,
                           size_t pwm_dir_len)
{
    if (chip_dir && chip_dir_len > 0) {
        snprintf(chip_dir, chip_dir_len, "/sys/class/pwm/pwmchip%d", chip);
    }
    if (pwm_dir && pwm_dir_len > 0) {
        snprintf(pwm_dir, pwm_dir_len, "/sys/class/pwm/pwmchip%d/pwm%d", chip, channel);
    }
}

static uint64_t period_ns_from_frequency(uint32_t frequency)
{
    if (frequency == 0) {
        return 0;
    }
    return 1000000000ULL / (uint64_t)frequency;
}

static uint64_t duty_ns_from_cfg(uint64_t period_ns, const TUYA_PWM_BASE_CFG_T *cfg)
{
    if (cfg == NULL) {
        return 0;
    }

    if (period_ns == 0) {
        return 0;
    }

    // Prefer (duty/cycle) if cycle is set and sane.
    if (cfg->cycle != 0 && cfg->duty <= cfg->cycle) {
        return (period_ns * (uint64_t)cfg->duty) / (uint64_t)cfg->cycle;
    }

    // Fallback: duty is 0..10000
    uint32_t duty = cfg->duty;
    if (duty > TUYA_PWM_DUTY_SCALE) {
        duty = TUYA_PWM_DUTY_SCALE;
    }
    return (period_ns * (uint64_t)duty) / (uint64_t)TUYA_PWM_DUTY_SCALE;
}

static int ensure_exported_locked(int chip, int channel)
{
    char chip_dir[96];
    char pwm_dir[128];
    pwm_sysfs_paths(chip, channel, chip_dir, sizeof(chip_dir), pwm_dir, sizeof(pwm_dir));

    if (!path_exists(chip_dir)) {
        return -ENOENT;
    }

    if (path_exists(pwm_dir)) {
        return 0;
    }

    char export_path[128];
    snprintf(export_path, sizeof(export_path), "%s/export", chip_dir);

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", channel);
    int rt = write_text_file(export_path, buf);
    if (rt < 0 && rt != -EBUSY) {
        // -EBUSY can happen if already exported.
        return rt;
    }

    // sysfs creation may be async; wait a little.
    for (int i = 0; i < 50; i++) {
        if (path_exists(pwm_dir)) {
            return 0;
        }
        usleep(10 * 1000);
    }

    return -ETIMEDOUT;
}

static int unexport_locked(int chip, int channel)
{
    char chip_dir[96];
    char pwm_dir[128];
    pwm_sysfs_paths(chip, channel, chip_dir, sizeof(chip_dir), pwm_dir, sizeof(pwm_dir));

    if (!path_exists(chip_dir)) {
        return -ENOENT;
    }

    if (!path_exists(pwm_dir)) {
        return 0;
    }

    char unexport_path[128];
    snprintf(unexport_path, sizeof(unexport_path), "%s/unexport", chip_dir);

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", channel);
    return write_text_file(unexport_path, buf);
}

static int set_enable_locked(int chip, int channel, bool enable)
{
    char pwm_dir[128];
    pwm_sysfs_paths(chip, channel, NULL, 0, pwm_dir, sizeof(pwm_dir));

    char enable_path[160];
    snprintf(enable_path, sizeof(enable_path), "%s/enable", pwm_dir);

    return write_text_file(enable_path, enable ? "1" : "0");
}

static int set_period_locked(int chip, int channel, uint64_t period_ns)
{
    char pwm_dir[128];
    pwm_sysfs_paths(chip, channel, NULL, 0, pwm_dir, sizeof(pwm_dir));

    char period_path[160];
    snprintf(period_path, sizeof(period_path), "%s/period", pwm_dir);

    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)period_ns);
    return write_text_file(period_path, buf);
}

static int set_duty_locked(int chip, int channel, uint64_t duty_ns)
{
    char pwm_dir[128];
    pwm_sysfs_paths(chip, channel, NULL, 0, pwm_dir, sizeof(pwm_dir));

    char duty_path[160];
    snprintf(duty_path, sizeof(duty_path), "%s/duty_cycle", pwm_dir);

    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)duty_ns);
    return write_text_file(duty_path, buf);
}

static int set_polarity_locked(int chip, int channel, TUYA_PWM_POLARITY_E polarity)
{
    char pwm_dir[128];
    pwm_sysfs_paths(chip, channel, NULL, 0, pwm_dir, sizeof(pwm_dir));

    char pol_path[160];
    snprintf(pol_path, sizeof(pol_path), "%s/polarity", pwm_dir);

    // Sysfs values: "normal" or "inversed"
    const char *v = (polarity == TUYA_PWM_NEGATIVE) ? "inversed" : "normal";
    return write_text_file(pol_path, v);
}

static OPERATE_RET map_errno_to_oprt(int err)
{
    if (err >= 0) {
        return OPRT_OK;
    }

    switch (-err) {
    case EINVAL:
        return OPRT_INVALID_PARM;
    case ENOENT:
    case ENODEV:
        return OPRT_NOT_FOUND;
    case EPERM:
    case EACCES:
        return OPRT_COM_ERROR;
    default:
        return OPRT_COM_ERROR;
    }
}

static bool ch_valid(TUYA_PWM_NUM_E ch_id)
{
    return (ch_id >= TUYA_PWM_NUM_0) && (ch_id < TUYA_PWM_NUM_MAX);
}

OPERATE_RET tkl_pwm_init(TUYA_PWM_NUM_E ch_id, const TUYA_PWM_BASE_CFG_T *cfg)
{
    if (!ch_valid(ch_id) || cfg == NULL) {
        return OPRT_INVALID_PARM;
    }

    const int chip = (int)PWM_SYSFS_CHIP;
    const int channel = (int)PWM_SYSFS_CHANNEL_BASE + (int)ch_id;

    pthread_mutex_lock(&g_pwm_lock);

    int err = ensure_exported_locked(chip, channel);
    if (err < 0) {
        pthread_mutex_unlock(&g_pwm_lock);
        return map_errno_to_oprt(err);
    }

    // Apply polarity first (kernel may require disabled for polarity change)
    (void)set_enable_locked(chip, channel, false);
    err = set_polarity_locked(chip, channel, cfg->polarity);
    if (err < 0) {
        pthread_mutex_unlock(&g_pwm_lock);
        return map_errno_to_oprt(err);
    }

    uint64_t period_ns = period_ns_from_frequency(cfg->frequency);
    if (period_ns == 0) {
        pthread_mutex_unlock(&g_pwm_lock);
        return OPRT_INVALID_PARM;
    }

    uint64_t duty_ns = duty_ns_from_cfg(period_ns, cfg);
    if (duty_ns > period_ns) {
        duty_ns = period_ns;
    }

    // Set period first, then duty.
    err = set_period_locked(chip, channel, period_ns);
    if (err < 0) {
        pthread_mutex_unlock(&g_pwm_lock);
        return map_errno_to_oprt(err);
    }
    err = set_duty_locked(chip, channel, duty_ns);
    if (err < 0) {
        pthread_mutex_unlock(&g_pwm_lock);
        return map_errno_to_oprt(err);
    }

    g_pwm_ctx[ch_id].cfg = *cfg;
    g_pwm_ctx[ch_id].inited = true;
    g_pwm_ctx[ch_id].exported = true;
    g_pwm_ctx[ch_id].enabled = false;

    pthread_mutex_unlock(&g_pwm_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_deinit(TUYA_PWM_NUM_E ch_id)
{
    if (!ch_valid(ch_id)) {
        return OPRT_INVALID_PARM;
    }

    const int chip = (int)PWM_SYSFS_CHIP;
    const int channel = (int)PWM_SYSFS_CHANNEL_BASE + (int)ch_id;

    pthread_mutex_lock(&g_pwm_lock);

    if (g_pwm_ctx[ch_id].enabled) {
        (void)set_enable_locked(chip, channel, false);
        g_pwm_ctx[ch_id].enabled = false;
    }

    (void)unexport_locked(chip, channel);

    memset(&g_pwm_ctx[ch_id], 0, sizeof(g_pwm_ctx[ch_id]));

    pthread_mutex_unlock(&g_pwm_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_start(TUYA_PWM_NUM_E ch_id)
{
    if (!ch_valid(ch_id)) {
        return OPRT_INVALID_PARM;
    }

    const int chip = (int)PWM_SYSFS_CHIP;
    const int channel = (int)PWM_SYSFS_CHANNEL_BASE + (int)ch_id;

    pthread_mutex_lock(&g_pwm_lock);

    if (!g_pwm_ctx[ch_id].inited) {
        pthread_mutex_unlock(&g_pwm_lock);
        return OPRT_INVALID_PARM;
    }

    int err = set_enable_locked(chip, channel, true);
    if (err < 0) {
        pthread_mutex_unlock(&g_pwm_lock);
        return map_errno_to_oprt(err);
    }

    g_pwm_ctx[ch_id].enabled = true;

    pthread_mutex_unlock(&g_pwm_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_stop(TUYA_PWM_NUM_E ch_id)
{
    if (!ch_valid(ch_id)) {
        return OPRT_INVALID_PARM;
    }

    const int chip = (int)PWM_SYSFS_CHIP;
    const int channel = (int)PWM_SYSFS_CHANNEL_BASE + (int)ch_id;

    pthread_mutex_lock(&g_pwm_lock);

    if (!g_pwm_ctx[ch_id].inited) {
        pthread_mutex_unlock(&g_pwm_lock);
        return OPRT_INVALID_PARM;
    }

    int err = set_enable_locked(chip, channel, false);
    if (err < 0) {
        pthread_mutex_unlock(&g_pwm_lock);
        return map_errno_to_oprt(err);
    }

    g_pwm_ctx[ch_id].enabled = false;

    pthread_mutex_unlock(&g_pwm_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_multichannel_start(TUYA_PWM_NUM_E *ch_id, uint8_t num)
{
    if (ch_id == NULL || num == 0) {
        return OPRT_INVALID_PARM;
    }

    for (uint8_t i = 0; i < num; i++) {
        OPERATE_RET rt = tkl_pwm_start(ch_id[i]);
        if (rt != OPRT_OK) {
            return rt;
        }
    }
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_multichannel_stop(TUYA_PWM_NUM_E *ch_id, uint8_t num)
{
    if (ch_id == NULL || num == 0) {
        return OPRT_INVALID_PARM;
    }

    for (uint8_t i = 0; i < num; i++) {
        OPERATE_RET rt = tkl_pwm_stop(ch_id[i]);
        if (rt != OPRT_OK) {
            return rt;
        }
    }
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_duty_set(TUYA_PWM_NUM_E ch_id, uint32_t duty)
{
    if (!ch_valid(ch_id)) {
        return OPRT_INVALID_PARM;
    }

    const int chip = (int)PWM_SYSFS_CHIP;
    const int channel = (int)PWM_SYSFS_CHANNEL_BASE + (int)ch_id;

    pthread_mutex_lock(&g_pwm_lock);

    if (!g_pwm_ctx[ch_id].inited) {
        pthread_mutex_unlock(&g_pwm_lock);
        return OPRT_INVALID_PARM;
    }

    TUYA_PWM_BASE_CFG_T cfg = g_pwm_ctx[ch_id].cfg;
    cfg.duty = duty;

    uint64_t period_ns = period_ns_from_frequency(cfg.frequency);
    uint64_t duty_ns = duty_ns_from_cfg(period_ns, &cfg);
    if (duty_ns > period_ns) {
        duty_ns = period_ns;
    }

    int err = set_duty_locked(chip, channel, duty_ns);
    if (err < 0) {
        pthread_mutex_unlock(&g_pwm_lock);
        return map_errno_to_oprt(err);
    }

    g_pwm_ctx[ch_id].cfg = cfg;

    pthread_mutex_unlock(&g_pwm_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_frequency_set(TUYA_PWM_NUM_E ch_id, uint32_t frequency)
{
    if (!ch_valid(ch_id) || frequency == 0) {
        return OPRT_INVALID_PARM;
    }

    const int chip = (int)PWM_SYSFS_CHIP;
    const int channel = (int)PWM_SYSFS_CHANNEL_BASE + (int)ch_id;

    pthread_mutex_lock(&g_pwm_lock);

    if (!g_pwm_ctx[ch_id].inited) {
        pthread_mutex_unlock(&g_pwm_lock);
        return OPRT_INVALID_PARM;
    }

    bool was_enabled = g_pwm_ctx[ch_id].enabled;
    (void)set_enable_locked(chip, channel, false);

    TUYA_PWM_BASE_CFG_T cfg = g_pwm_ctx[ch_id].cfg;
    cfg.frequency = frequency;

    uint64_t period_ns = period_ns_from_frequency(cfg.frequency);
    uint64_t duty_ns = duty_ns_from_cfg(period_ns, &cfg);
    if (duty_ns > period_ns) {
        duty_ns = period_ns;
    }

    int err = set_period_locked(chip, channel, period_ns);
    if (err < 0) {
        pthread_mutex_unlock(&g_pwm_lock);
        return map_errno_to_oprt(err);
    }
    err = set_duty_locked(chip, channel, duty_ns);
    if (err < 0) {
        pthread_mutex_unlock(&g_pwm_lock);
        return map_errno_to_oprt(err);
    }

    if (was_enabled) {
        err = set_enable_locked(chip, channel, true);
        if (err < 0) {
            pthread_mutex_unlock(&g_pwm_lock);
            return map_errno_to_oprt(err);
        }
    }

    g_pwm_ctx[ch_id].cfg = cfg;
    g_pwm_ctx[ch_id].enabled = was_enabled;

    pthread_mutex_unlock(&g_pwm_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_polarity_set(TUYA_PWM_NUM_E ch_id, TUYA_PWM_POLARITY_E polarity)
{
    if (!ch_valid(ch_id)) {
        return OPRT_INVALID_PARM;
    }

    const int chip = (int)PWM_SYSFS_CHIP;
    const int channel = (int)PWM_SYSFS_CHANNEL_BASE + (int)ch_id;

    pthread_mutex_lock(&g_pwm_lock);

    if (!g_pwm_ctx[ch_id].inited) {
        pthread_mutex_unlock(&g_pwm_lock);
        return OPRT_INVALID_PARM;
    }

    bool was_enabled = g_pwm_ctx[ch_id].enabled;
    (void)set_enable_locked(chip, channel, false);

    int err = set_polarity_locked(chip, channel, polarity);
    if (err < 0) {
        pthread_mutex_unlock(&g_pwm_lock);
        return map_errno_to_oprt(err);
    }

    if (was_enabled) {
        err = set_enable_locked(chip, channel, true);
        if (err < 0) {
            pthread_mutex_unlock(&g_pwm_lock);
            return map_errno_to_oprt(err);
        }
    }

    g_pwm_ctx[ch_id].cfg.polarity = polarity;
    g_pwm_ctx[ch_id].enabled = was_enabled;

    pthread_mutex_unlock(&g_pwm_lock);
    return OPRT_OK;
}

OPERATE_RET tkl_pwm_info_set(TUYA_PWM_NUM_E ch_id, const TUYA_PWM_BASE_CFG_T *info)
{
    if (!ch_valid(ch_id) || info == NULL) {
        return OPRT_INVALID_PARM;
    }

    // Apply as full init/update while keeping enable state.
    pthread_mutex_lock(&g_pwm_lock);
    bool was_inited = g_pwm_ctx[ch_id].inited;
    bool was_enabled = g_pwm_ctx[ch_id].enabled;
    pthread_mutex_unlock(&g_pwm_lock);

    if (!was_inited) {
        OPERATE_RET rt = tkl_pwm_init(ch_id, info);
        if (rt != OPRT_OK) {
            return rt;
        }
        return was_enabled ? tkl_pwm_start(ch_id) : OPRT_OK;
    }

    // Update fields in order: stop -> polarity -> period/duty -> restart (if needed)
    if (was_enabled) {
        (void)tkl_pwm_stop(ch_id);
    }

    OPERATE_RET rt = tkl_pwm_polarity_set(ch_id, info->polarity);
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = tkl_pwm_frequency_set(ch_id, info->frequency);
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = tkl_pwm_duty_set(ch_id, info->duty);
    if (rt != OPRT_OK) {
        return rt;
    }

    pthread_mutex_lock(&g_pwm_lock);
    g_pwm_ctx[ch_id].cfg = *info;
    pthread_mutex_unlock(&g_pwm_lock);

    return was_enabled ? tkl_pwm_start(ch_id) : OPRT_OK;
}

OPERATE_RET tkl_pwm_info_get(TUYA_PWM_NUM_E ch_id, TUYA_PWM_BASE_CFG_T *info)
{
    if (!ch_valid(ch_id) || info == NULL) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&g_pwm_lock);
    if (!g_pwm_ctx[ch_id].inited) {
        pthread_mutex_unlock(&g_pwm_lock);
        return OPRT_INVALID_PARM;
    }
    *info = g_pwm_ctx[ch_id].cfg;
    pthread_mutex_unlock(&g_pwm_lock);

    return OPRT_OK;
}

OPERATE_RET tkl_pwm_cap_start(TUYA_PWM_NUM_E ch_id, const TUYA_PWM_CAP_IRQ_T *cfg)
{
    (void)ch_id;
    (void)cfg;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_pwm_cap_stop(TUYA_PWM_NUM_E ch_id)
{
    (void)ch_id;
    return OPRT_NOT_SUPPORTED;
}
