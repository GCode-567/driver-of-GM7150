/**
  ******************************************************************************
  * @file    video_stream.h
  * @brief   图像传输的协议/流控层，以及可替换的"数据源"入口。
  *
  * 分层结构（便于后续替换任意一层）：
  *   数据源层  SimSource_Pixel()   —— 当前为模拟图案，将来换成BT.656/FPGA采集
  *   协议层    video_stream.c      —— 帧头+分块+CRC16+丢帧背压
  *   链路层    video_link.c        —— USART1+DMA（将来可换USB）
  *
  * 帧格式（小端）：
  *   偏移  长度  内容
  *   0     2     同步字 0xA5 0x5A
  *   2     1     类型：0x01=灰度8位
  *   3     1     标志位（保留，0）
  *   4     2     帧序号（循环递增）
  *   6     2     宽（像素）
  *   8     2     高（行）
  *   10    4     负载长度（字节），= 宽 × 高
  *   14    2     帧头CRC16（覆盖偏移2~13）
  *   16    N     负载：逐行像素，每行"宽"个字节
  *   16+N  2     负载CRC16（CCITT-FALSE，初值0xFFFF）
  ******************************************************************************
  */

#ifndef __VIDEO_STREAM_H
#define __VIDEO_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ======================= 可调参数 ======================= */

/* 图像宽度（像素）。 */
#define VIDEO_STREAM_WIDTH        240U
/* 图像高度（行）。 */
#define VIDEO_STREAM_HEIGHT       180U
/* 目标帧率。实际受链路带宽限制，见下表：
 *   2 Mbps -> 约4fps   3 Mbps -> 约6fps
 *   4 Mbps -> 约8fps   4.5Mbps -> 约10fps
 * 计算依据：UART每字节10位，240x180每帧43200字节。
 * 15fps@240x180需要约648KB/s，超出UART能力，必须换USB链路。 */
#define VIDEO_STREAM_FPS          4U

/* 链路波特率：72MHz下可选 2000000/3000000/4000000/4500000。
 * CH340类USB转串口通常最高2Mbps，FT232/CH343可到3~4.5Mbps。 */
#define VIDEO_LINK_BAUD           2000000U

/* ======================= 接口 ======================= */

/**
 * @brief 初始化协议层状态（链路层需已由VideoLink_Init初始化）。
 */
void VideoStream_Init(void);

/**
 * @brief 协议层轮询：应在主循环中尽可能频繁地调用。
 *        非阻塞，链路忙时自动让出CPU，不会影响其他任务。
 */
void VideoStream_Poll(void);

/**
 * @brief 已完整发送的帧数。
 */
uint32_t VideoStream_GetFrameCount(void);

/**
 * @brief 因链路来不及发送而丢弃的帧数（背压统计）。
 */
uint32_t VideoStream_GetDropCount(void);

#ifdef __cplusplus
}
#endif

#endif /* __VIDEO_STREAM_H */
