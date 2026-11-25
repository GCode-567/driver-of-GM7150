/**
 * @file    bt656_capture.c
 * @brief   GM7150 BT.601 真实采集模块（阶段 0+1 实现）
 *
 * 阶段 0：I2C 通信 + 锁定验证（上电串口打印 probe / lock 结果）。
 * 阶段 1：PCLK/HSYNC/VSYNC 时序验证（每秒打印行计数 / 每行 PCLK 数 / 锁定状态）。
 *
 * 硬件连接（见 bt656_capture.h）：
 *   YOUT0~7 -> PB8~PB15    PCLK -> PA0(TIM2_ETR)
 *   VSYNC -> PB0(EXTI0)    HSYNC -> PB1(EXTI1)    AVID -> PA4
 *   RESETB -> PA5          PDN -> PA6
 */
#include "bt656_capture.h"
#include "i2c.h"
#include "usart.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* ===================== 模块私有变量 ===================== */
static GM7150_HandleTypeDef *s_gm = NULL;   /* GM7150 句柄，Config 时赋值 */
static CaptureStats_t s_stats;              /* 阶段 1 统计 */
static char s_print_buf[160];               /* 串口打印缓冲 */

/* ===================== 内部工具 ===================== */
/**
 * @brief 通过 USART1 格式化打印（阻塞，仅调试用）。
 */
static void cap_printf(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(s_print_buf, sizeof(s_print_buf), fmt, ap);
  va_end(ap);
  if (n > 0)
  {
    if (n > (int)sizeof(s_print_buf)) n = (int)sizeof(s_print_buf);
    HAL_UART_Transmit(&huart1, (uint8_t *)s_print_buf, (uint16_t)n, 300);
  }
}

/* ===================== 公开 API ===================== */
void Capture_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* 1. 开时钟：GPIOA / GPIOB / AFIO（EXTI 端口映射需要）/ TIM2 */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_RCC_TIM2_CLK_ENABLE();

  /* 2. 数据总线 PB8~PB15：输入浮空（GM7150 推挽输出） */
  GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11
                      | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(CAP_DATA_PORT, &GPIO_InitStruct);

  /* 3. PA0 PCLK（TIM2_ETR）：输入浮空。F1 下外设时钟使能后 ETR 自动连接该引脚 */
  GPIO_InitStruct.Pin = CAP_PCLK_PIN;
  HAL_GPIO_Init(CAP_PCLK_PORT, &GPIO_InitStruct);

  /* 4. PA4 AVID：输入浮空 */
  GPIO_InitStruct.Pin = CAP_AVID_PIN;
  HAL_GPIO_Init(CAP_AVID_PORT, &GPIO_InitStruct);

  /* 5. PB0 VSYNC：外部中断上升沿触发 */
  GPIO_InitStruct.Pin = CAP_VSYNC_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(CAP_VSYNC_PORT, &GPIO_InitStruct);

  /* 6. PB1 HSYNC：外部中断上升沿触发 */
  GPIO_InitStruct.Pin = CAP_HSYNC_PIN;
  HAL_GPIO_Init(CAP_HSYNC_PORT, &GPIO_InitStruct);

  /* 7. PA5 RESETB：推挽输出，初始高（不复位） */
  GPIO_InitStruct.Pin = CAP_RESET_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_WritePin(CAP_RESET_PORT, CAP_RESET_PIN, GPIO_PIN_SET);
  HAL_GPIO_Init(CAP_RESET_PORT, &GPIO_InitStruct);

  /* 8. PA6 PDN：推挽输出，初始高（正常工作） */
  GPIO_InitStruct.Pin = CAP_PDN_PIN;
  HAL_GPIO_WritePin(CAP_PDN_PORT, CAP_PDN_PIN, GPIO_PIN_SET);
  HAL_GPIO_Init(CAP_PDN_PORT, &GPIO_InitStruct);

  /* 9. TIM2 配置为外部时钟模式 2：由 ETR(PA0/PCLK) 驱动，不分频，自由计数 */
  TIM2->PSC = 0;
  TIM2->ARR = 0xFFFF;
  /* SMCR: ECE=1(bit16, 外部时钟模式2使能), ETPS=00(不分频), ETP=0(上升沿有效) */
  TIM2->SMCR = TIM_SMCR_ECE;
  TIM2->CNT = 0;
  TIM2->CR1 |= TIM_CR1_CEN;   /* 启动计数 */

  /* 10. NVIC：EXTI0(VSYNC) / EXTI1(HSYNC)，抢占优先级高，快进快出 */
  HAL_NVIC_SetPriority(EXTI0_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);
  HAL_NVIC_SetPriority(EXTI1_IRQn, 1, 1);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  /* 11. 复位统计 */
  memset((void *)&s_stats, 0, sizeof(s_stats));
  s_stats.row_pclk_min = 0xFFFF;
}

HAL_StatusTypeDef Capture_ConfigGM7150(GM7150_HandleTypeDef *gm7150)
{
  HAL_StatusTypeDef ret;
  uint8_t present = 0;

  s_gm = gm7150;
  cap_printf("\r\n=== GM7150 Capture Init ===\r\n");

  /* ---- 硬件复位（规格书 5.8 节） ----
   * PDN=1（正常模式，RESETB 才有效）→ RESETB 拉低 ≥500us → 等 ≥20ms → 拉高。
   * 上电时电源早已稳定，这里用 HAL_Delay 满足时序。 */
  CAP_PDN_HIGH();
  CAP_RESET_LOW();
  HAL_Delay(2);        /* RESETB 低电平 ≥500us，给 2ms 留余量 */
  CAP_RESET_HIGH();
  HAL_Delay(25);       /* 内部初始化 ≥20ms */
  cap_printf("GM7150: HW reset done (PDN=1, RESETB released)\r\n");

  /* ---- I2C 探测 ---- */
  ret = GM7150_Init(gm7150, &hi2c1, GM7150_I2C_ADDR_7BIT_I2CSEL_LOW);
  if (ret != HAL_OK)
  {
    cap_printf("GM7150: I2C Init FAILED (ret=%d) — 查 SDA/SCL 上拉/接线/地址\r\n", ret);
    return ret;
  }
  GM7150_IsDevicePresent(gm7150, &present);
  cap_printf("GM7150: I2C probe %s (ID=0x7150 expected)\r\n", present ? "OK" : "FAIL");
  if (!present) return HAL_ERROR;

  /* ---- 配置视频输入与输出 ---- */
  GM7150_SelectInput(gm7150, GM7150_INPUT_CVBS_AIP1A);  /* CVBS 从 AIP1A 输入 */
  GM7150_SetVideoStandard(gm7150, GM7150_STD_AUTO);     /* 自动检测制式 */
  GM7150_SetOutputFormat(gm7150, GM7150_OUT_BT601, GM7150_RANGE_FULL); /* BT.601 + 扩展量程 */
  GM7150_SetOutputEnable(gm7150, 1);                    /* 使能数据+同步+时钟输出 */
  cap_printf("GM7150: configured CVBS/AIP1A, AUTO, BT.601, output ON\r\n");

  /* ---- 等待锁定 ---- */
  ret = GM7150_WaitForLock(gm7150, 3000);
  if (ret == HAL_OK)
  {
    cap_printf("GM7150: LOCK OK (video locked)\r\n");
  }
  else
  {
    cap_printf("GM7150: LOCK TIMEOUT (ret=%d) — 查视频源/制式/输入接线\r\n", ret);
  }
  return ret;
}

void Capture_Poll(void)
{
  static uint32_t last_tick = 0;
  uint32_t now = HAL_GetTick();
  if (now - last_tick < 1000U) return;   /* 每秒打印一次 */
  last_tick = now;

  const CaptureStats_t *st = &s_stats;
  GM7150_StatusTypeDef status;
  uint8_t locked = 0;
  uint8_t is50 = 0;
  uint8_t std = 99;

  if (s_gm != NULL)
  {
    GM7150_ReadStatus(s_gm, &status);
    locked = status.locked;
    is50 = status.fieldRate50Hz;
    std = (uint8_t)status.standard;
  }

  /* 预期正常值（PAL）：rows_last≈312，rowPCLK≈1728，50Hz，lock=1
   * 预期正常值（NTSC）：rows_last≈262，rowPCLK≈1716，60Hz，lock=1 */
  cap_printf("[CAP] vsync=%lu rows_last=%u rowPCLK=%u~%u lock=%u %sHz std=%u\r\n",
             (unsigned long)st->vsync_count,
             (unsigned)st->last_field_rows,
             (unsigned)st->row_pclk_min,
             (unsigned)st->row_pclk_max,
             (unsigned)locked,
             is50 ? "50" : "60",
             (unsigned)std);
}

void Capture_VSYNC_IRQHandler(void)
{
  /* 场同步：记录上一场行数，清零场内计数与 TIM2，便于观察每行 PCLK 数 */
  s_stats.last_field_rows = s_stats.row_in_field;
  s_stats.row_in_field = 0;
  s_stats.vsync_count++;
  s_stats.last_hsync_cnt = 0;
  s_stats.row_pclk_min = 0xFFFF;
  s_stats.row_pclk_max = 0;
  TIM2->CNT = 0;
}

void Capture_HSYNC_IRQHandler(void)
{
  /* 行同步：计算与上一行的 CNT 差值（16 位无符号减法，溢出自动回绕正确），
   * 得到每行 PCLK 数；同时累计行计数。快进快出，只做最少操作。 */
  uint16_t c = (uint16_t)TIM2->CNT;
  uint16_t diff = c - s_stats.last_hsync_cnt;
  s_stats.last_hsync_cnt = c;
  s_stats.hsync_count++;
  s_stats.row_in_field++;
  if (diff < s_stats.row_pclk_min) s_stats.row_pclk_min = diff;
  if (diff > s_stats.row_pclk_max) s_stats.row_pclk_max = diff;
}

const CaptureStats_t *Capture_GetStats(void)
{
  return &s_stats;
}
