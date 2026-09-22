/**
 * @file tkl_camera_v4l2.h
 * @brief Linux V4L2 camera TKL interface (USB/UVC).
 */

#ifndef __TKL_CAMERA_V4L2_H__
#define __TKL_CAMERA_V4L2_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TKL_CAMERA_V4L2_PIXFMT_YUYV = 0,
    TKL_CAMERA_V4L2_PIXFMT_MJPEG,
} TKL_CAMERA_V4L2_PIXFMT_E;

typedef struct {
    const char *devnode;   /* e.g. "/dev/video0" */
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t buffer_count; /* request buffer count, typical 4 */
    TKL_CAMERA_V4L2_PIXFMT_E pixfmt;
} TKL_CAMERA_V4L2_CFG_T;

typedef void *TKL_CAMERA_V4L2_HANDLE_T;

OPERATE_RET tkl_camera_v4l2_open(TKL_CAMERA_V4L2_HANDLE_T *hdl, const TKL_CAMERA_V4L2_CFG_T *cfg);
OPERATE_RET tkl_camera_v4l2_start(TKL_CAMERA_V4L2_HANDLE_T hdl);
OPERATE_RET tkl_camera_v4l2_stop(TKL_CAMERA_V4L2_HANDLE_T hdl);
OPERATE_RET tkl_camera_v4l2_close(TKL_CAMERA_V4L2_HANDLE_T hdl);

/**
 * @brief Dequeue one captured buffer.
 *
 * @param hdl     camera handle
 * @param data    output pointer to buffer data (mmap'ed)
 * @param len     output bytes used
 * @param index   output buffer index (must be passed to queue)
 */
OPERATE_RET tkl_camera_v4l2_dequeue(TKL_CAMERA_V4L2_HANDLE_T hdl, uint8_t **data, uint32_t *len, uint32_t *index);
OPERATE_RET tkl_camera_v4l2_queue(TKL_CAMERA_V4L2_HANDLE_T hdl, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* __TKL_CAMERA_V4L2_H__ */
