/**
* @file tkl_jpeg_codec.h
* @brief Common process - adapter the jpeg codec api (Linux implementation)
* @version 0.1
* @date 2026-03-09
*/

#ifndef __TKL_JPEG_CODEC_H__
#define __TKL_JPEG_CODEC_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JPEG_DEC_OUT_YUV422 = 0,
    JPEG_DEC_OUT_RGB565,
    JPEG_DEC_OUT_RGB888,
} JPEG_DEC_OUT_FMT;

typedef struct {
    TUYA_FRAME_FMT_E out_fmt;
    UINT16_T         out_width;
    UINT16_T         out_height;
    UINT32_T         in_size;
} TKL_JPEG_CODEC_INFO_T;

/**
 * @brief jpeg codec init
 */
OPERATE_RET tkl_jpeg_codec_init(void);

/**
 * @brief jpeg codec deinit
 */
OPERATE_RET tkl_jpeg_codec_deinit(void);

/**
 * @brief jpeg img info get
 */
OPERATE_RET tkl_jpeg_codec_img_info_get(UINT8_T *jpeg_buf, UINT32_T jpeg_size, TKL_JPEG_CODEC_INFO_T *jpeg_info);

/**
 * @brief jpeg decode/convert
 */
OPERATE_RET tkl_jpeg_codec_convert(UINT8_T *src_buf, UINT8_T *dst_buf, TKL_JPEG_CODEC_INFO_T *jpeg_codec_info,
                                  JPEG_DEC_OUT_FMT out_fmt);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* __TKL_JPEG_CODEC_H__ */
