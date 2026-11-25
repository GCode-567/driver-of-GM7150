/**
 * @file    bt656_capture.h
 * @brief   GM7150 BT.601 真实采集模块（阶段 0+1：硬件时序验证）
 *
 * 方案：GM7150 切 BT.601 输出独立同步引脚；PCLK 驱动 TIM2 外部时钟自由计数；
 *       VSYNC/HSYNC 经 EXTI 中断同步行场；主循环按 TIM2 计数偏移抽行抽点。
 *
 * 阶段 0：I2C 通信 + GM7150 锁定验证。
 * 阶段 1：PCLK/HSYNC/VSYNC 时序验证（打印行计数 / CNT 范围 / 锁定状态）。
 * 阶段 2+：抽行抽点采样 + 打包 BT.656 上发（后续迭代）。
 */
#ifndef BT656_CAPTURE_H
#define BT656_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "gm7150.h"

/* ========================================================================
 * 切换开关：1 = 使用真实 GM7150 采集；0 = 回退 bt656_test 模拟码流
 * 在 main.c 中据此选择调用 Capture_* 还是 BT656Test_*。
 * ====================================================================== */
#ifndef USE_REAL_CAPTURE
#define USE_REAL_CAPTURE 1
#endif

/* ========================================================================
 * 采集目标参数（阶段 2+ 抽行抽点用，阶段 0+1 仅占位声明）
 * ====================================================================== */
#define CAP_W             88     /* 目标宽度（像素） */
#define CAP_H             66     /* 目标高度（行）   */
#define CAP_ROW_STEP      4      /* 抽行步长：每 N 行采 1 行 */
#define CAP_PIX_STEP      16     /* 抽点字节步长：4:2:2 中 Y 位于奇数字节位 */

/* ========================================================================
 * 引脚定义（F103C8T6，LQFP48）
 * ====================================================================== */
/* 数据总线：YOUT0~YOUT7 -> PB8~PB15，读取 GPIOB->IDR 高 8 位 */
#define CAP_DATA_PORT       GPIOB
#define CAP_DATA_GET()      ((uint8_t)((CAP_DATA_PORT->IDR >> 8) & 0xFFU))

/* PCLK -> PA0（TIM2_ETR，外部时钟输入） */
#define CAP_PCLK_PORT       GPIOA
#define CAP_PCLK_PIN        GPIO_PIN_0

/* VSYNC -> PB0（EXTI0，场同步中断） */
#define CAP_VSYNC_PORT      GPIOB
#define CAP_VSYNC_PIN       GPIO_PIN_0

/* HSYNC -> PB1（EXTI1，行同步中断） */
#define CAP_HSYNC_PORT      GPIOB
#define CAP_HSYNC_PIN       GPIO_PIN_1

/* AVID -> PA4（有效视频区指示，输入） */
#define CAP_AVID_PORT       GPIOA
#define CAP_AVID_PIN        GPIO_PIN_4
#define CAP_AVID_GET()      ((CAP_AVID_PORT->IDR & CAP_AVID_PIN) != 0U)

/* RESETB -> PA5（推挽输出，低电平复位 GM7150） */
#define CAP_RESET_PORT      GPIOA
#define CAP_RESET_PIN       GPIO_PIN_5
#define CAP_RESET_HIGH()    (CAP_RESET_PORT->BSRR = CAP_RESET_PIN)
#define CAP_RESET_LOW()     (CAP_RESET_PORT->BSRR = ((uint32_t)CAP_RESET_PIN << 16U))

/* PDN -> PA6（推挽输出，高电平正常 / 低电平掉电） */
#define CAP_PDN_PORT        GPIOA
#define CAP_PDN_PIN         GPIO_PIN_6
#define CAP_PDN_HIGH()      (CAP_PDN_PORT->BSRR = CAP_PDN_PIN)
#define CAP_PDN_LOW()       (CAP_PDN_PORT->BSRR = ((uint32_t)CAP_PDN_PIN << 16U))

/* ========================================================================
 * 阶段 1 调试统计
 * ====================================================================== */
typedef struct
{
  volatile uint32_t vsync_count;     /* 累计场中断次数（上电以来） */
  volatile uint32_t hsync_count;     /* 累计行中断次数（上电以来） */
  volatile uint16_t last_field_rows; /* 上一场内的行数（含消隐，PAL≈312） */
  volatile uint16_t row_in_field;    /* 当前场内已计行数 */
  volatile uint16_t row_pclk_min;    /* 单场内观测到的每行 PCLK 数最小值（PAL≈1728） */
  volatile uint16_t row_pclk_max;    /* 单场内观测到的每行 PCLK 数最大值 */
  volatile uint16_t last_hsync_cnt;  /* 上次 HSYNC 时的 TIM2->CNT，用于算每行差值 */
} CaptureStats_t;

/* ========================================================================
 * 公开 API
 * ====================================================================== */
/**
 * @brief 初始化采集相关硬件：GPIO、TIM2 外部时钟、EXTI、NVIC。
 *        必须在 GM7150 输出使能之前调用。
 */
void Capture_Init(void);

/**
 * @brief 配置 GM7150：硬件复位 → I2C 初始化 → 选源 → AUTO 制式 → 切 BT.601 → 开输出 → 等锁。
 * @param gm7150  传入已定义的 GM7150 句柄（由调用方持有）。
 * @retval HAL_OK 表示锁定成功；HAL_TIMEOUT 表示超时未锁定；其他为 I2C 错误。
 */
HAL_StatusTypeDef Capture_ConfigGM7150(GM7150_HandleTypeDef *gm7150);

/**
 * @brief 主循环轮询。阶段 1 每秒通过 USART1 打印一次时序统计与锁定状态。
 */
void Capture_Poll(void);

/**
 * @brief VSYNC 中断回调（在 EXTI0_IRQHandler 中调用）。
 */
void Capture_VSYNC_IRQHandler(void);

/**
 * @brief HSYNC 中断回调（在 EXTI1_IRQHandler 中调用）。
 */
void Capture_HSYNC_IRQHandler(void);

/**
 * @brief 获取统计指针（只读）。
 */
const CaptureStats_t *Capture_GetStats(void);

#ifdef __cplusplus
}
#endif

#endif /* BT656_CAPTURE_H */
