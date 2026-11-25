#include "gm7150.h"

/* I2C阻塞读写操作允许使用的最长等待时间，单位为毫秒。 */
#define GM7150_I2C_TIMEOUT_MS             100U
/* 探测芯片I2C应答时，HAL最多尝试的次数。 */
#define GM7150_I2C_READY_TRIALS           3U

/* 寄存器0x03的bit3：使能YCbCr视频数据输出。 */
#define GM7150_OUTPUT_DATA_ENABLE         0x08U
/* 寄存器0x03的bit0：使能时钟输出。 */
#define GM7150_OUTPUT_CLOCK_ENABLE        0x01U
/* 数据输出和时钟输出使能位的组合掩码。 */
#define GM7150_OUTPUT_ENABLE_MASK         (GM7150_OUTPUT_DATA_ENABLE | GM7150_OUTPUT_CLOCK_ENABLE)
/* 寄存器0x0F的bit0：选择PCLK像素时钟，而非SCLK系统时钟。 */
#define GM7150_PORT_PCLK_SELECT           0x01U

/* 寄存器0x00的bit[1:0]：视频输入源选择位掩码。 */
#define GM7150_INPUT_SELECT_MASK          0x03U

/* 寄存器0x03的bit2：使能HSYNC/VSYNC/AVID/FID同步信号输出。 */
#define GM7150_OUTPUT_SYNC_ENABLE         0x04U
/* 数据+同步+时钟输出使能位的总掩码，用于整体开关视频输出。 */
#define GM7150_OUTPUT_ALL_ENABLE_MASK     (GM7150_OUTPUT_DATA_ENABLE | GM7150_OUTPUT_SYNC_ENABLE | GM7150_OUTPUT_CLOCK_ENABLE)

/* 寄存器0x02的bit0：软件省电模式位。 */
#define GM7150_WORK_MODE_PWRDN            0x01U

/* 寄存器0x0D：bit6为输出码范围选择，bits[2:0]为输出格式。 */
#define GM7150_OFMT_CODE_RANGE_FULL       0x40U
#define GM7150_OFMT_FORMAT_MASK           0x07U
#define GM7150_OFMT_BT656                 0x07U
#define GM7150_OFMT_BT601                 0x00U
#define GM7150_OFMT_CONFIG_MASK           (GM7150_OFMT_CODE_RANGE_FULL | GM7150_OFMT_FORMAT_MASK)

/* 寄存器0x28的bits[3:0]：视频标准选择掩码。 */
#define GM7150_VIDEO_STANDARD_MASK        0x0FU

/* GM7150_WaitForLock 轮询锁定状态的间隔（毫秒）。 */
#define GM7150_LOCK_POLL_INTERVAL_MS      10U

/* 0x28控制寄存器的视频标准编码（bits[3:0]），索引与GM7150_VideoStandardTypeDef一致。 */
static const uint8_t GM7150_StdCtrlValue[] =
{
  0x00U, /* GM7150_STD_AUTO       自动检测 */
  0x02U, /* GM7150_STD_NTSC_MJ    NTSC (M, J) */
  0x04U, /* GM7150_STD_PAL_BDGHI_N PAL (B,D,G,H,I,N) */
  0x06U, /* GM7150_STD_PAL_M      PAL (M) */
  0x08U, /* GM7150_STD_PAL_CN     PAL (Combination-N) */
  0x0AU  /* GM7150_STD_NTSC_443   NTSC 4.43 */
};

/* 0x8C状态寄存器的视频标准编码（bits[5:3]）到枚举的映射，索引为寄存器值。 */
static const GM7150_VideoStandardTypeDef GM7150_StdFromStatus[] =
{
  GM7150_STD_NTSC_MJ,     /* 000 */
  GM7150_STD_PAL_BDGHI_N, /* 001 */
  GM7150_STD_PAL_M,       /* 010 */
  GM7150_STD_PAL_CN,      /* 011 */
  GM7150_STD_NTSC_443     /* 100 */
};

/**
 * @brief 按掩码更新寄存器中的指定位，保留其余位不变。
 * @param device GM7150设备实例。
 * @param reg    要更新的寄存器地址。
 * @param mask   要修改的位掩码。
 * @param value  新位值；仅mask指定的位会被写入。
 * @retval HAL操作状态；读取或写入失败时返回HAL错误状态。
 */
static HAL_StatusTypeDef GM7150_UpdateRegister(GM7150_HandleTypeDef *device,
                                               uint8_t reg,
                                               uint8_t mask,
                                               uint8_t value)
{
  HAL_StatusTypeDef status;
  uint8_t currentValue;
  uint8_t newValue;

  status = GM7150_ReadRegister(device, reg, &currentValue);
  if (status != HAL_OK)
  {
    return status;
  }

  newValue = (uint8_t)((currentValue & (uint8_t)~mask) | (value & mask));
  if (newValue == currentValue)
  {
    return HAL_OK;
  }

  return GM7150_WriteRegister(device, reg, newValue);
}

/**
 * @brief 初始化GM7150的I2C访问和数字视频输出。
 *
 * 检查芯片应答，将视频输出格式寄存器设为数据手册默认值0x47，
 * 选择PCLK，并使能YCbCr数据和时钟输出。自动制式检测及视频输入
 * 源配置保持芯片当前值。
 * @param device     GM7150设备实例。
 * @param i2cHandle  STM32 HAL I2C外设句柄。
 * @param address7bit 芯片7位I2C地址：I2CSEL低为0x5C，高为0x5D。
 * @retval HAL操作状态。
 */
HAL_StatusTypeDef GM7150_Init(GM7150_HandleTypeDef *device,
                              I2C_HandleTypeDef *i2cHandle,
                              uint8_t address7bit)
{
  HAL_StatusTypeDef status;

  if ((device == NULL) || (i2cHandle == NULL) || (address7bit > 0x7FU))
  {
    return HAL_ERROR;
  }

  device->I2cHandle = i2cHandle;
  device->I2cAddress = (uint16_t)(address7bit << 1);

  status = HAL_I2C_IsDeviceReady(device->I2cHandle,
                                 device->I2cAddress,
                                 GM7150_I2C_READY_TRIALS,
                                 GM7150_I2C_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    return status;
  }

  /* 0x47是数据手册给出的8位4:2:2 YCbCr输出格式默认值。 */
  status = GM7150_WriteRegister(device, GM7150_REG_VIDEO_OUTPUT_FORMAT, 0x47U);
  if (status != HAL_OK)
  {
    return status;
  }

  /* 选择像素时钟PCLK，并使能YCbCr数据及其时钟输出。 */
  status = GM7150_UpdateRegister(device, GM7150_REG_PORT_OUTPUT_SELECT,
                                 GM7150_PORT_PCLK_SELECT,
                                 GM7150_PORT_PCLK_SELECT);
  if (status != HAL_OK)
  {
    return status;
  }

  return GM7150_UpdateRegister(device, GM7150_REG_OUTPUT_CONTROL,
                               GM7150_OUTPUT_ENABLE_MASK,
                               GM7150_OUTPUT_ENABLE_MASK);
}

/**
 * @brief 通过I2C读取GM7150的一个8位寄存器。
 * @param device GM7150设备实例。
 * @param reg    要读取的寄存器地址。
 * @param value  用于接收寄存器值的指针。
 * @retval HAL操作状态；参数无效时返回HAL_ERROR。
 */
HAL_StatusTypeDef GM7150_ReadRegister(GM7150_HandleTypeDef *device,
                                      uint8_t reg,
                                      uint8_t *value)
{
  if ((device == NULL) || (device->I2cHandle == NULL) || (value == NULL))
  {
    return HAL_ERROR;
  }

  return HAL_I2C_Mem_Read(device->I2cHandle,
                          device->I2cAddress,
                          reg,
                          I2C_MEMADD_SIZE_8BIT,
                          value,
                          1U,
                          GM7150_I2C_TIMEOUT_MS);
}

/**
 * @brief 通过I2C向GM7150的一个8位寄存器写入数据。
 * @param device GM7150设备实例。
 * @param reg    要写入的寄存器地址。
 * @param value  要写入的8位数据。
 * @retval HAL操作状态；参数无效时返回HAL_ERROR。
 */
HAL_StatusTypeDef GM7150_WriteRegister(GM7150_HandleTypeDef *device,
                                       uint8_t reg,
                                       uint8_t value)
{
  if ((device == NULL) || (device->I2cHandle == NULL))
  {
    return HAL_ERROR;
  }

  return HAL_I2C_Mem_Write(device->I2cHandle,
                           device->I2cAddress,
                           reg,
                           I2C_MEMADD_SIZE_8BIT,
                           &value,
                           1U,
                           GM7150_I2C_TIMEOUT_MS);
}

/**
 * @brief 选择CVBS或S视频输入源，并保留寄存器0x00的其他位。
 * @param device GM7150设备实例。
 * @param input  要选择的视频输入，参见GM7150_InputTypeDef。
 * @retval HAL操作状态；输入类型无效时返回HAL_ERROR。
 */
HAL_StatusTypeDef GM7150_SelectInput(GM7150_HandleTypeDef *device,
                                     GM7150_InputTypeDef input)
{
  if ((input != GM7150_INPUT_CVBS_AIP1A) &&
      (input != GM7150_INPUT_CVBS_AIP1B) &&
      (input != GM7150_INPUT_SVIDEO))
  {
    return HAL_ERROR;
  }

  return GM7150_UpdateRegister(device,
                               GM7150_REG_INPUT_SELECT,
                               GM7150_INPUT_SELECT_MASK,
                               (uint8_t)input);
}

/**
 * @brief 解析0x8C状态寄存器中的视频标准字段。
 * @param raw5 0x8C寄存器原始值。
 * @retval 对应的视频标准枚举；字段值越界时返回GM7150_STD_AUTO。
 */
static GM7150_VideoStandardTypeDef GM7150_ParseStandard(uint8_t raw5)
{
  uint8_t idx = (uint8_t)((raw5 & GM7150_STAT5_STANDARD_MASK) >> GM7150_STAT5_STANDARD_SHIFT);
  if (idx >= (sizeof(GM7150_StdFromStatus) / sizeof(GM7150_StdFromStatus[0])))
  {
    return GM7150_STD_AUTO;
  }
  return GM7150_StdFromStatus[idx];
}

HAL_StatusTypeDef GM7150_ReadDeviceID(GM7150_HandleTypeDef *device,
                                      uint8_t *msb,
                                      uint8_t *lsb)
{
  HAL_StatusTypeDef status;

  if ((device == NULL) || (msb == NULL) || (lsb == NULL))
  {
    return HAL_ERROR;
  }

  status = GM7150_ReadRegister(device, GM7150_REG_DEVICE_ID_MSB, msb);
  if (status != HAL_OK)
  {
    return status;
  }

  return GM7150_ReadRegister(device, GM7150_REG_DEVICE_ID_LSB, lsb);
}

HAL_StatusTypeDef GM7150_ReadRomVersion(GM7150_HandleTypeDef *device,
                                        uint8_t *major,
                                        uint8_t *minor)
{
  HAL_StatusTypeDef status;

  if ((device == NULL) || (major == NULL) || (minor == NULL))
  {
    return HAL_ERROR;
  }

  status = GM7150_ReadRegister(device, GM7150_REG_ROM_VERSION_MAJOR, major);
  if (status != HAL_OK)
  {
    return status;
  }

  return GM7150_ReadRegister(device, GM7150_REG_ROM_VERSION_MINOR, minor);
}

HAL_StatusTypeDef GM7150_IsDevicePresent(GM7150_HandleTypeDef *device,
                                         uint8_t *present)
{
  uint8_t msb;
  uint8_t lsb;
  HAL_StatusTypeDef status;

  if ((device == NULL) || (present == NULL))
  {
    return HAL_ERROR;
  }

  *present = 0U;
  status = GM7150_ReadDeviceID(device, &msb, &lsb);
  if (status != HAL_OK)
  {
    return status;
  }

  if ((msb == GM7150_DEVICE_ID_MSB) && (lsb == GM7150_DEVICE_ID_LSB))
  {
    *present = 1U;
  }

  return HAL_OK;
}

HAL_StatusTypeDef GM7150_ReadStatus(GM7150_HandleTypeDef *device,
                                    GM7150_StatusTypeDef *status)
{
  uint8_t raw1;
  uint8_t raw2;
  uint8_t raw5;
  HAL_StatusTypeDef st;

  if ((device == NULL) || (status == NULL))
  {
    return HAL_ERROR;
  }

  st = GM7150_ReadRegister(device, GM7150_REG_STATUS_1, &raw1);
  if (st != HAL_OK)
  {
    return st;
  }
  st = GM7150_ReadRegister(device, GM7150_REG_STATUS_2, &raw2);
  if (st != HAL_OK)
  {
    return st;
  }
  st = GM7150_ReadRegister(device, GM7150_REG_STATUS_5, &raw5);
  if (st != HAL_OK)
  {
    return st;
  }

  status->status1 = raw1;
  status->status2 = raw2;
  status->status5 = raw5;
  status->locked = (((raw1 & GM7150_STAT1_LOCK_MASK) == GM7150_STAT1_LOCK_MASK) &&
                    ((raw1 & GM7150_STAT1_LOSS_OF_LOCK) == 0U)) ? 1U : 0U;
  status->fieldRate50Hz = ((raw1 & GM7150_STAT1_FIELD_RATE_50HZ) != 0U) ? 1U : 0U;
  status->interlaced = ((raw1 & GM7150_STAT1_INTERLACED) != 0U) ? 1U : 0U;
  status->weakSignal = ((raw2 & GM7150_STAT2_WEAK_SIGNAL) != 0U) ? 1U : 0U;
  status->standardAutoDetected = ((raw5 & GM7150_STAT5_AUTO_DETECTED) != 0U) ? 1U : 0U;
  status->standard = GM7150_ParseStandard(raw5);

  return HAL_OK;
}

HAL_StatusTypeDef GM7150_IsVideoLocked(GM7150_HandleTypeDef *device,
                                       uint8_t *locked)
{
  uint8_t raw1;
  HAL_StatusTypeDef st;

  if ((device == NULL) || (locked == NULL))
  {
    return HAL_ERROR;
  }

  st = GM7150_ReadRegister(device, GM7150_REG_STATUS_1, &raw1);
  if (st != HAL_OK)
  {
    return st;
  }

  *locked = (((raw1 & GM7150_STAT1_LOCK_MASK) == GM7150_STAT1_LOCK_MASK) &&
             ((raw1 & GM7150_STAT1_LOSS_OF_LOCK) == 0U)) ? 1U : 0U;
  return HAL_OK;
}

HAL_StatusTypeDef GM7150_GetDetectedStandard(GM7150_HandleTypeDef *device,
                                             GM7150_VideoStandardTypeDef *standard)
{
  uint8_t raw5;
  HAL_StatusTypeDef st;

  if ((device == NULL) || (standard == NULL))
  {
    return HAL_ERROR;
  }

  st = GM7150_ReadRegister(device, GM7150_REG_STATUS_5, &raw5);
  if (st != HAL_OK)
  {
    return st;
  }

  *standard = GM7150_ParseStandard(raw5);
  return HAL_OK;
}

HAL_StatusTypeDef GM7150_SetVideoStandard(GM7150_HandleTypeDef *device,
                                          GM7150_VideoStandardTypeDef standard)
{
  if (device == NULL)
  {
    return HAL_ERROR;
  }
  if ((uint8_t)standard >= (sizeof(GM7150_StdCtrlValue) / sizeof(GM7150_StdCtrlValue[0])))
  {
    return HAL_ERROR;
  }

  return GM7150_UpdateRegister(device,
                               GM7150_REG_VIDEO_STANDARD,
                               GM7150_VIDEO_STANDARD_MASK,
                               GM7150_StdCtrlValue[(uint8_t)standard]);
}

HAL_StatusTypeDef GM7150_SetOutputFormat(GM7150_HandleTypeDef *device,
                                         GM7150_OutputFormatTypeDef format,
                                         GM7150_CodeRangeTypeDef range)
{
  uint8_t value;

  if (device == NULL)
  {
    return HAL_ERROR;
  }
  if ((format != GM7150_OUT_BT656) && (format != GM7150_OUT_BT601))
  {
    return HAL_ERROR;
  }
  if ((range != GM7150_RANGE_LIMITED) && (range != GM7150_RANGE_FULL))
  {
    return HAL_ERROR;
  }

  value = (format == GM7150_OUT_BT656) ? GM7150_OFMT_BT656 : GM7150_OFMT_BT601;
  if (range == GM7150_RANGE_FULL)
  {
    value |= GM7150_OFMT_CODE_RANGE_FULL;
  }

  return GM7150_UpdateRegister(device,
                               GM7150_REG_VIDEO_OUTPUT_FORMAT,
                               GM7150_OFMT_CONFIG_MASK,
                               value);
}

HAL_StatusTypeDef GM7150_SetOutputEnable(GM7150_HandleTypeDef *device,
                                         uint8_t enable)
{
  if (device == NULL)
  {
    return HAL_ERROR;
  }

  return GM7150_UpdateRegister(device,
                               GM7150_REG_OUTPUT_CONTROL,
                               GM7150_OUTPUT_ALL_ENABLE_MASK,
                               (enable != 0U) ? GM7150_OUTPUT_ALL_ENABLE_MASK : 0U);
}

HAL_StatusTypeDef GM7150_SetPowerMode(GM7150_HandleTypeDef *device,
                                      GM7150_PowerModeTypeDef mode)
{
  if (device == NULL)
  {
    return HAL_ERROR;
  }
  if ((mode != GM7150_POWER_NORMAL) && (mode != GM7150_POWER_SAVE))
  {
    return HAL_ERROR;
  }

  return GM7150_UpdateRegister(device,
                               GM7150_REG_WORK_MODE,
                               GM7150_WORK_MODE_PWRDN,
                               (mode == GM7150_POWER_SAVE) ? GM7150_WORK_MODE_PWRDN : 0U);
}

HAL_StatusTypeDef GM7150_SetBrightness(GM7150_HandleTypeDef *device,
                                       uint8_t value)
{
  if (device == NULL)
  {
    return HAL_ERROR;
  }
  return GM7150_WriteRegister(device, GM7150_REG_BRIGHTNESS, value);
}

HAL_StatusTypeDef GM7150_SetContrast(GM7150_HandleTypeDef *device,
                                     uint8_t value)
{
  if (device == NULL)
  {
    return HAL_ERROR;
  }
  return GM7150_WriteRegister(device, GM7150_REG_CONTRAST, value);
}

HAL_StatusTypeDef GM7150_SetSaturation(GM7150_HandleTypeDef *device,
                                       uint8_t value)
{
  if (device == NULL)
  {
    return HAL_ERROR;
  }
  return GM7150_WriteRegister(device, GM7150_REG_SATURATION, value);
}

HAL_StatusTypeDef GM7150_SetHue(GM7150_HandleTypeDef *device,
                                uint8_t value)
{
  if (device == NULL)
  {
    return HAL_ERROR;
  }
  return GM7150_WriteRegister(device, GM7150_REG_HUE, value);
}

HAL_StatusTypeDef GM7150_WaitForLock(GM7150_HandleTypeDef *device,
                                     uint32_t timeout_ms)
{
  uint32_t start;
  uint8_t locked;
  HAL_StatusTypeDef st;

  if (device == NULL)
  {
    return HAL_ERROR;
  }

  start = HAL_GetTick();
  do
  {
    st = GM7150_IsVideoLocked(device, &locked);
    if (st != HAL_OK)
    {
      return st;
    }
    if (locked != 0U)
    {
      return HAL_OK;
    }
    HAL_Delay(GM7150_LOCK_POLL_INTERVAL_MS);
  } while ((HAL_GetTick() - start) < timeout_ms);

  return HAL_TIMEOUT;
}
