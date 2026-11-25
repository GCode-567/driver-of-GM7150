/**
  ******************************************************************************
  * @file    video_link.c
  * @brief   图像传输链路层实现：USART1 发送，DMA1_Channel4 搬运。
  *
  * 采用DMA是为了让CPU在发送期间继续跑主循环（后续接入真实采集时，
  * 发送不会拖慢采集）。
  *
  * 注意：本文件自行完成了GPIO/DMA/NVIC/UART的初始化，未经过CubeMX。
  *       若以后在CubeMX中启用USART1，会与这里的DMA1_Channel4_IRQHandler、
  *       HAL_UART_TxCpltCallback产生重复定义，需要合并。
  ******************************************************************************
  */

#include "video_link.h"
#include "usart.h"

/* huart1 由 usart.c 定义与初始化，这里只使用它的发送通道。 */
DMA_HandleTypeDef hdma_usart1_tx;

/* 1：DMA正在发送；0：空闲。 */
static volatile uint8_t s_txBusy = 0U;

/**
 * @brief 配置USART1使用的GPIO：PA9复用推挽输出，PA10浮空输入。
 */
static void VideoLink_GpioInit(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_AFIO_CLK_ENABLE();

  gpio.Pin = GPIO_PIN_9;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_10;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &gpio);
}

/**
 * @brief 配置USART1_TX使用的DMA通道并挂到串口句柄上。
 */
static void VideoLink_DmaInit(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  hdma_usart1_tx.Instance = DMA1_Channel4;
  hdma_usart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_usart1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_usart1_tx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_usart1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_usart1_tx.Init.Mode = DMA_NORMAL;
  hdma_usart1_tx.Init.Priority = DMA_PRIORITY_HIGH;
  (void)HAL_DMA_Init(&hdma_usart1_tx);

  __HAL_LINKDMA(&huart1, hdmatx, hdma_usart1_tx);

  HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
}

HAL_StatusTypeDef VideoLink_Init(uint32_t baudrate)
{
  HAL_StatusTypeDef status;

  VideoLink_GpioInit();
  VideoLink_DmaInit();

  __HAL_RCC_USART1_CLK_ENABLE();

  huart1.Instance = USART1;
  huart1.Init.BaudRate = baudrate;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;

  status = HAL_UART_Init(&huart1);
  if (status != HAL_OK)
  {
    return status;
  }

  s_txBusy = 0U;
  return HAL_OK;
}

uint8_t VideoLink_IsBusy(void)
{
  return s_txBusy;
}

HAL_StatusTypeDef VideoLink_Send(const uint8_t *data, uint16_t len)
{
  HAL_StatusTypeDef status;

  if ((data == NULL) || (len == 0U))
  {
    return HAL_ERROR;
  }
  if (s_txBusy != 0U)
  {
    return HAL_BUSY;
  }

  s_txBusy = 1U;
  status = HAL_UART_Transmit_DMA(&huart1, (uint8_t *)data, len);
  if (status != HAL_OK)
  {
    s_txBusy = 0U;
  }
  return status;
}

/**
 * @brief DMA1通道4中断：USART1发送完成。
 */
void DMA1_Channel4_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_usart1_tx);
}

/**
 * @brief 串口发送完成回调，释放忙标志。
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    s_txBusy = 0U;
  }
}

/**
 * @brief 串口错误回调，避免因错误导致忙标志一直不释放而卡死发送。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    s_txBusy = 0U;
  }
}
