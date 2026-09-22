/**
 * @file tkl_spi.h
 * @brief Common process - adapter the spi api
 * @version 0.1
 * @date 2021-08-06
 *
 */
#ifndef __TKL_SPI_H__
#define __TKL_SPI_H__

#include "tuya_cloud_types.h"
#include "tkl_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET tkl_spi_init(TUYA_SPI_NUM_E port, const TUYA_SPI_BASE_CFG_T *cfg);
OPERATE_RET tkl_spi_deinit(TUYA_SPI_NUM_E port);
OPERATE_RET tkl_spi_send(TUYA_SPI_NUM_E port, void *data, uint32_t size);
OPERATE_RET tkl_spi_recv(TUYA_SPI_NUM_E port, void *data, uint32_t size);
OPERATE_RET tkl_spi_transfer(TUYA_SPI_NUM_E port, void *send_buf, void *receive_buf, uint32_t length);
OPERATE_RET tkl_spi_transfer_with_length(TUYA_SPI_NUM_E port, void *send_buf, uint32_t send_len, void *receive_buf,
                                         uint32_t receive_len);
OPERATE_RET tkl_spi_abort_transfer(TUYA_SPI_NUM_E port);
OPERATE_RET tkl_spi_get_status(TUYA_SPI_NUM_E port, TUYA_SPI_STATUS_T *status);
OPERATE_RET tkl_spi_irq_init(TUYA_SPI_NUM_E port, TUYA_SPI_IRQ_CB cb);
OPERATE_RET tkl_spi_irq_enable(TUYA_SPI_NUM_E port);
OPERATE_RET tkl_spi_irq_disable(TUYA_SPI_NUM_E port);
int32_t tkl_spi_get_data_count(TUYA_SPI_NUM_E port);
OPERATE_RET tkl_spi_ioctl(TUYA_SPI_NUM_E port, uint32_t cmd, void *args);
uint32_t tkl_spi_get_max_dma_data_length(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
