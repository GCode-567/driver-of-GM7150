/**
  ******************************************************************************
  * @file    bt656_test.c
  * @brief   生成可被上位机直接解码的 BT.656 测试码流（8bit 4:2:2）。

  * 码流结构（每场）：
  *     SAV(V=1) + EAV(V=1)            场消隐标志
  *     TH 条有效行：SAV(V=0) + 采样 + EAV(V=0)
  *     SAV(V=1) + EAV(V=1)            场结束标志
  *
  * 每行 TW 个像素，按 4 字节一组 Cb Y0 Cr Y1 排列。
  *
  * 为贴近真实 CCD 输出，加入了这些特征：
  *   - 4:3 画幅、隔行场标志 F 每场翻转
  *   - 镜头暗角（中心亮、四角暗）
  *   - 传感器随机噪声（亮度微小抖动）
  *   - 画面内容分三区：彩条（色度）、灰度渐变（动态范围）、
  *     棋盘格（依次 2/4/8 像素，检验分辨率与采样）
  *   - 一个白块随时间斜向移动，便于观察帧刷新与运动连续性
  ******************************************************************************
  */

#include "bt656_test.h"
#include "usart.h"

#define TW            88U                 /* 每行有效像素数 */
#define TH            66U                 /* 每场有效行数（4:3） */
#define LINE_SAMPLES  (TW * 2U)           /* 4:2:2 每行字节数 */
#define LINE_TOTAL    (4U + LINE_SAMPLES + 4U)
#define BAR_CNT       8U
#define BOX_SIZE      16U                 /* 移动白块边长 */
#define VIGNETTE      24U                 /* 暗角强度：边缘比中心暗 24/256 */

/* 彩条：{Y, Cb, Cr}，顺序为 白 黄 青 绿 品 红 蓝 黑 */
static const uint8_t kBars[BAR_CNT][3] =
{
  {255, 128, 128},
  {226,   1, 149},
  {179, 171,   0},
  {150,  44,  21},
  {105, 212, 235},
  { 76,  85, 255},
  { 29, 255, 107},
  {  0, 128, 128},
};

static uint8_t  s_line[LINE_TOTAL];
static uint8_t  s_att_x[TW];              /* 水平方向亮度衰减系数(0~255) */
static uint8_t  s_att_y[TH];              /* 垂直方向亮度衰减系数(0~255) */
static uint16_t s_line_idx;               /* 0=未开始；1..TH=第几行；TH+1=收尾 */
static uint32_t s_frame;
static uint32_t s_rng = 0x12345678U;
static unsigned long s_sent_frames;

static void put_marker(uint8_t *p, uint8_t xy)
{
  p[0] = 0xFFU;
  p[1] = 0x00U;
  p[2] = 0x00U;
  p[3] = xy;
}

static void send_bytes(const uint8_t *buf, uint16_t len)
{
  uint32_t timeout = (uint32_t)len * 10U * 1000U / huart1.Init.BaudRate + 20U;
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, timeout);
}

/* 场消隐：SAV(V=1) + EAV(V=1)，F 位随场翻转 */
static void send_blanking(void)
{
  uint8_t buf[8];
  uint8_t f = (uint8_t)((s_frame & 1U) << 5);
  put_marker(&buf[0], (uint8_t)(0xC0U | f));   /* bit6=V=1, bit4=0 -> SAV */
  put_marker(&buf[4], (uint8_t)(0xD0U | f));   /* bit6=V=1, bit4=1 -> EAV */
  send_bytes(buf, sizeof(buf));
}

static uint8_t sensor_noise(void)
{
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return (uint8_t)(s_rng & 0x07U);             /* 0~7，后面减 3 得到 -3~+4 */
}

/* 按位置生成一个像素的 YCbCr（含暗角与噪声） */
static void make_pixel(uint16_t x, uint16_t y, uint8_t *py, uint8_t *pcb, uint8_t *pcr)
{
  uint32_t yy, cb, cr;
  uint32_t sx = (TW / 2U + s_frame * 2U) % (TW - BOX_SIZE);
  uint32_t sy = (TH / 2U + s_frame * 3U) % (TH - BOX_SIZE);

  if (y < TH / 3U)                             /* 上三分之一：彩条 */
  {
    uint32_t b = (uint32_t)x * BAR_CNT / TW;
    yy = kBars[b][0];
    cb = kBars[b][1];
    cr = kBars[b][2];
  }
  else if (y < TH * 2U / 3U)                   /* 中三分之一：灰度渐变 */
  {
    yy = (uint32_t)x * 255U / (TW - 1U);
    cb = 128U;
    cr = 128U;
  }
  else                                         /* 下三分之一：棋盘格 2/4/8 像素 */
  {
    uint32_t sz = (x < TW / 3U) ? 2U : ((x < TW * 2U / 3U) ? 4U : 8U);
    yy = (((x / sz) + (y / sz)) & 1U) ? 235U : 16U;
    cb = 128U;
    cr = 128U;
  }

  if ((x >= sx) && (x < sx + BOX_SIZE) && (y >= sy) && (y < sy + BOX_SIZE))
  {
    yy = 255U;                                 /* 移动白块 */
    cb = 128U;
    cr = 128U;
  }

  yy = (yy * s_att_x[x] * s_att_y[y]) >> 16;    /* 镜头暗角 */
  if (yy > 6U)
  {
    yy += sensor_noise();                      /* 传感器噪声 */
    yy -= 3U;
  }
  if (yy > 255U)
  {
    yy = 255U;
  }

  *py = (uint8_t)yy;
  *pcb = (uint8_t)cb;
  *pcr = (uint8_t)cr;
}

static void build_line(uint16_t y)
{
  uint8_t *p = s_line;
  uint8_t f = (uint8_t)((s_frame & 1U) << 5);
  uint16_t x;

  put_marker(p, (uint8_t)(0x80U | f));          /* SAV：V=0, H=0, F 跟随场 */
  p += 4;

  for (x = 0; x < TW; x += 2U)
  {
    uint8_t y0, cb0, cr0, y1, cb1, cr1;

    make_pixel(x, y, &y0, &cb0, &cr0);
    make_pixel((uint16_t)(x + 1U), y, &y1, &cb1, &cr1);

    *p++ = (uint8_t)(((uint16_t)cb0 + cb1) >> 1);   /* Cb：两个像素共用 */
    *p++ = y0;
    *p++ = (uint8_t)(((uint16_t)cr0 + cr1) >> 1);   /* Cr */
    *p++ = y1;
  }

  put_marker(p, (uint8_t)(0x90U | f));          /* EAV：V=0, H=1 */
}

/* 预计算暗角系数：中心 255，边缘约 215 */
static void build_vignette_table(void)
{
  uint16_t i;
  uint32_t max_x = (TW - 1U) * (TW - 1U);
  uint32_t max_y = (TH - 1U) * (TH - 1U);

  for (i = 0; i < TW; i++)
  {
    uint32_t d = (i < TW / 2U) ? (TW / 2U - i) : (i - TW / 2U);
    s_att_x[i] = (uint8_t)(255U - (d * d * VIGNETTE) / (max_x / 4U + 1U));
  }
  for (i = 0; i < TH; i++)
  {
    uint32_t d = (i < TH / 2U) ? (TH / 2U - i) : (i - TH / 2U);
    s_att_y[i] = (uint8_t)(255U - (d * d * VIGNETTE) / (max_y / 4U + 1U));
  }
}

void BT656Test_Init(void)
{
  s_line_idx = 0U;
  s_frame = 0U;
  s_sent_frames = 0UL;
  build_vignette_table();
}

void BT656Test_Poll(void)
{
  if (huart1.gState != HAL_UART_STATE_READY)
  {
    return;                        /* 串口忙，下次再来 */
  }

  if (s_line_idx == 0U)            /* 场开始：先送消隐标志 */
  {
    send_blanking();
    s_line_idx = 1U;
    return;
  }

  if (s_line_idx <= TH)            /* 逐行发送有效数据 */
  {
    build_line((uint16_t)(s_line_idx - 1U));
    send_bytes(s_line, LINE_TOTAL);
    s_line_idx++;
    return;
  }

  send_blanking();                 /* 场结束标志，上位机据此出图 */
  s_line_idx = 0U;
  s_frame++;
  s_sent_frames++;
}

unsigned long BT656Test_GetFrameCount(void)
{
  return s_sent_frames;
}
