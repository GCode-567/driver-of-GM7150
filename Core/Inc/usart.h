/**
  ******************************************************************************
  * @file    usart.h
  * @brief   USART1 配置（PA9=TX，PA10=RX）。
  ******************************************************************************
  */

#ifndef __USART_H__
#define __USART_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* USART1句柄，供其他模块引用。 */
extern UART_HandleTypeDef huart1;

/**
 * @brief 初始化USART1：115200 8N1，仅收发，无硬件流控。
 */
void MX_USART1_UART_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __USART_H__ */
