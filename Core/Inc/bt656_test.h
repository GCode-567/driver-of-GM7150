/**
  ******************************************************************************
  * @file    bt656_test.h
  * @brief   BT.656 测试码流发生器：无真实视频源时用于联调上位机。
  ******************************************************************************
  */

#ifndef __BT656_TEST_H__
#define __BT656_TEST_H__

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化测试图案发生器（清状态）。 */
void BT656Test_Init(void);

/** 主循环中调用，非阻塞地推送一行 BT.656 数据。 */
void BT656Test_Poll(void);

/** 已发送的场数，可用于调试观察。 */
unsigned long BT656Test_GetFrameCount(void);

#ifdef __cplusplus
}
#endif

#endif /* __BT656_TEST_H__ */
