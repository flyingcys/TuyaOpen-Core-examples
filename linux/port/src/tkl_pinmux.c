#include "tkl_pinmux.h"

#include "tkl_output.h"
#include "tuya_error_code.h"

OPERATE_RET tkl_io_pinmux_config(TUYA_PIN_NAME_E pin, TUYA_PIN_FUNC_E pin_func)
{
    // On Linux (e.g. Raspberry Pi), pin muxing is typically controlled by the
    // device-tree / overlays and kernel drivers, not from userspace.
    // Keep this as a no-op so higher-level examples can run.
    (void)pin;
    (void)pin_func;
    return OPRT_OK;
}

OPERATE_RET tkl_multi_io_pinmux_config(TUYA_MUL_PIN_CFG_T *cfg, uint16_t num)
{
    if (cfg == NULL && num != 0) {
        return OPRT_INVALID_PARM;
    }

    for (uint16_t i = 0; i < num; i++) {
        (void)tkl_io_pinmux_config(cfg[i].pin, cfg[i].pin_func);
    }

    return OPRT_OK;
}

int32_t tkl_io_pin_to_func(uint32_t pin, TUYA_PIN_TYPE_E pin_type)
{
    (void)pin;
    (void)pin_type;

    // Not supported on Linux adapter.
    return -1;
}
