/**
  ******************************************************************************
  * @file    video_link.h
  * @brief   图像传输的链路层：USART1 + DMA 单向发送。
  *
  * 本层只负责"把一段字节发出去"，不关心内容格式。
  * 后续若改用 USB CDC、SPI+以太网等，只需替换本层的实现，
  * 上层 video_stream 与画面内容均无需改动。
  ******************************************************************************
  */

#ifndef __VIDEO_LINK_H
#define __VIDEO_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32f1xx_hal.h"

/**
 * @brief 初始化链路（PA9=TX，PA10=RX，DMA1_Channel4）。
 * @param baudrate 波特率。72MHz 下可选 2000000/3000000/4000000/4500000。
 * @retval HAL操作状态。
 */
HAL_StatusTypeDef VideoLink_Init(uint32_t baudrate);

/**
 * @brief 查询是否有发送正在进行。
 * @retval 1表示忙，0表示空闲。
 */
uint8_t VideoLink_IsBusy(void);

/**
 * @brief 以DMA方式发送一段数据（非阻塞）。
 * @param data 待发送数据指针，调用者需保证在发送完成前数据有效。
 * @param len  字节数（不超过65535）。
 * @retval HAL_OK已启动；HAL_BUSY上一笔未完成；HAL_ERROR参数错误。
 */
HAL_StatusTypeDef VideoLink_Send(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __VIDEO_LINK_H */
