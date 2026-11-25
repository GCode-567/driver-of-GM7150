/* 头文件保护宏，防止本文件被重复包含。 */
#ifndef GM7150_H
#define GM7150_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* I2CSEL为低时使用的7位I2C地址；对应手册中的写地址0xB8。 */
#define GM7150_I2C_ADDR_7BIT_I2CSEL_LOW  0x5CU
/* I2CSEL为高时使用的7位I2C地址；对应手册中的写地址0xBA。 */
#define GM7150_I2C_ADDR_7BIT_I2CSEL_HIGH 0x5DU

/* GM7150视频源选择寄存器地址。 */
#define GM7150_REG_INPUT_SELECT          0x00U
/* GM7150工作模式控制寄存器地址（bit0为软件省电）。 */
#define GM7150_REG_WORK_MODE             0x02U
/* GM7150端口信号输出控制寄存器地址。 */
#define GM7150_REG_OUTPUT_CONTROL        0x03U
/* GM7150图像调节（BCS）寄存器地址，默认值：亮度/对比度/饱和度为0x80，色调为0x00。 */
#define GM7150_REG_BRIGHTNESS            0x09U
#define GM7150_REG_SATURATION            0x0AU
#define GM7150_REG_HUE                   0x0BU
#define GM7150_REG_CONTRAST              0x0CU
/* GM7150视频输出格式选择寄存器地址。 */
#define GM7150_REG_VIDEO_OUTPUT_FORMAT   0x0DU
/* GM7150端口输出选择寄存器地址。 */
#define GM7150_REG_PORT_OUTPUT_SELECT    0x0FU
/* GM7150视频标准控制寄存器地址。 */
#define GM7150_REG_VIDEO_STANDARD        0x28U
/* GM7150芯片ID（MSB/LSB）与ROM版本只读寄存器地址。 */
#define GM7150_REG_DEVICE_ID_MSB         0x80U
#define GM7150_REG_DEVICE_ID_LSB         0x81U
#define GM7150_REG_ROM_VERSION_MAJOR     0x82U
#define GM7150_REG_ROM_VERSION_MINOR     0x83U
/* GM7150状态寄存器地址。 */
#define GM7150_REG_STATUS_1              0x88U
#define GM7150_REG_STATUS_2              0x89U
#define GM7150_REG_STATUS_5              0x8CU

/* GM7150芯片ID默认值：MSB=0x71，LSB=0x50。 */
#define GM7150_DEVICE_ID_MSB             0x71U
#define GM7150_DEVICE_ID_LSB             0x50U

/* 状态寄存器1（0x88）各bit位掩码。 */
#define GM7150_STAT1_TV_VCR              0x01U /* bit0：0=TV，1=VCR */
#define GM7150_STAT1_HLOCK               0x02U /* bit1：行同步锁定 */
#define GM7150_STAT1_VLOCK               0x04U /* bit2：场同步锁定 */
#define GM7150_STAT1_CLOCK_LOCK          0x08U /* bit3：色度副载波频率锁定 */
#define GM7150_STAT1_LOSS_OF_LOCK        0x10U /* bit4：失锁检测（0=未失锁） */
#define GM7150_STAT1_FIELD_RATE_50HZ     0x20U /* bit5：场频（0=60Hz，1=50Hz） */
#define GM7150_STAT1_INTERLACED          0x40U /* bit6：隔行状态 */
#define GM7150_STAT1_WHITE_PEAK          0x80U /* bit7：白峰检测 */
/* 判定“有效锁定”所需的位：行/场/色载波均锁定且无失锁。 */
#define GM7150_STAT1_LOCK_MASK           (GM7150_STAT1_HLOCK | GM7150_STAT1_VLOCK | GM7150_STAT1_CLOCK_LOCK)

/* 状态寄存器2（0x89）bit6：弱信号检测。 */
#define GM7150_STAT2_WEAK_SIGNAL         0x40U

/* 状态寄存器5（0x8C）bit7：自动检测标志；bits[5:3]：检测到的视频标准。 */
#define GM7150_STAT5_AUTO_DETECTED       0x80U
#define GM7150_STAT5_STANDARD_MASK       0x38U
#define GM7150_STAT5_STANDARD_SHIFT      3U

/* GM7150设备实例，保存所用的STM32 I2C句柄和HAL格式的从机地址。 */
typedef struct
{
  I2C_HandleTypeDef *I2cHandle; /* 用于访问芯片的STM32 I2C外设句柄。 */
  uint16_t I2cAddress;          /* 左移一位后的I2C地址，供STM32 HAL接口使用。 */
} GM7150_HandleTypeDef;

/* 视频源选择值，对应寄存器0x00的MODE[1:0]位（手册表10）。 */
typedef enum
{
  GM7150_INPUT_CVBS_AIP1A = 0x00U, /* 选择AIP1A作为CVBS输入（默认），MODE[1:0]=00。 */
  GM7150_INPUT_CVBS_AIP1B = 0x02U, /* 选择AIP1B作为CVBS输入，MODE[1:0]=10。 */
  GM7150_INPUT_SVIDEO     = 0x01U  /* S视频：AIP1A为Y、AIP1B为C，MODE[0]=1。 */
} GM7150_InputTypeDef;

/* 视频标准，用于强制制式或读取自动检测结果。 */
typedef enum
{
  GM7150_STD_AUTO = 0,       /* 自动检测（仅用于设置）。 */
  GM7150_STD_NTSC_MJ,        /* NTSC (M, J)。 */
  GM7150_STD_PAL_BDGHI_N,    /* PAL (B, D, G, H, I, N)。 */
  GM7150_STD_PAL_M,          /* PAL (M)。 */
  GM7150_STD_PAL_CN,         /* PAL (Combination-N)。 */
  GM7150_STD_NTSC_443        /* NTSC 4.43。 */
} GM7150_VideoStandardTypeDef;

/* 数字视频输出格式（寄存器0x0D的bits[2:0]）。 */
typedef enum
{
  GM7150_OUT_BT656 = 0, /* 8位4:2:2 YCbCr ITU-R BT.656，行/场同步内嵌于数据流。 */
  GM7150_OUT_BT601      /* 8位4:2:2 YCbCr ITU-R BT.601，行/场同步由独立引脚输出。 */
} GM7150_OutputFormatTypeDef;

/* YCbCr输出码范围（寄存器0x0D的bit6）。 */
typedef enum
{
  GM7150_RANGE_LIMITED = 0, /* ITU-R BT.601有限范围：Y 16~235，Cb/Cr 16~240。 */
  GM7150_RANGE_FULL          /* 扩展范围：Y/Cb/Cr 1~254（默认）。 */
} GM7150_CodeRangeTypeDef;

/* 软件功耗模式（寄存器0x02的bit0）。 */
typedef enum
{
  GM7150_POWER_NORMAL = 0, /* 正常工作。 */
  GM7150_POWER_SAVE        /* 省电：ADC与内部时钟关闭。 */
} GM7150_PowerModeTypeDef;

/* GM7150实时状态，由状态寄存器0x88/0x89/0x8C解析得到。 */
typedef struct
{
  uint8_t status1;                 /* 0x88原始值。 */
  uint8_t status2;                 /* 0x89原始值。 */
  uint8_t status5;                 /* 0x8C原始值。 */
  uint8_t locked;                  /* 1：行/场/色载波均锁定且未失锁。 */
  uint8_t fieldRate50Hz;           /* 1：50Hz场频（PAL类）；0：60Hz（NTSC类）。 */
  uint8_t interlaced;              /* 1：隔行。 */
  uint8_t weakSignal;              /* 1：检测到弱信号。 */
  uint8_t standardAutoDetected;    /* 1：当前制式为自动检测结果。 */
  GM7150_VideoStandardTypeDef standard; /* 检测到的视频标准。 */
} GM7150_StatusTypeDef;

/**
 * @brief 检查芯片是否应答，并配置8位4:2:2视频数据及像素时钟输出。
 * @param device     GM7150设备实例。
 * @param i2cHandle  STM32 HAL I2C外设句柄。
 * @param address7bit 芯片7位I2C地址：I2CSEL低为0x5C，高为0x5D。
 * @retval HAL_StatusTypeDef HAL操作状态；失败时返回对应错误状态。
 */
HAL_StatusTypeDef GM7150_Init(GM7150_HandleTypeDef *device,
                              I2C_HandleTypeDef *i2cHandle,
                              uint8_t address7bit);

/**
 * @brief 读取GM7150的一个8位寄存器。
 * @param device GM7150设备实例。
 * @param reg    要读取的寄存器地址。
 * @param value  用于接收寄存器值的指针。
 * @retval HAL_StatusTypeDef HAL操作状态。
 */
HAL_StatusTypeDef GM7150_ReadRegister(GM7150_HandleTypeDef *device,
                                      uint8_t reg,
                                      uint8_t *value);

/**
 * @brief 向GM7150的一个8位寄存器写入数据。
 * @param device GM7150设备实例。
 * @param reg    要写入的寄存器地址。
 * @param value  要写入的8位数据。
 * @retval HAL_StatusTypeDef HAL操作状态。
 */
HAL_StatusTypeDef GM7150_WriteRegister(GM7150_HandleTypeDef *device,
                                       uint8_t reg,
                                       uint8_t value);

/**
 * @brief 选择CVBS或S视频输入，并保留寄存器中的其他配置位。
 * @param device GM7150设备实例。
 * @param input  要选择的视频输入，参见GM7150_InputTypeDef。
 * @retval HAL_StatusTypeDef HAL操作状态；input无效时返回HAL_ERROR。
 */
HAL_StatusTypeDef GM7150_SelectInput(GM7150_HandleTypeDef *device,
                                     GM7150_InputTypeDef input);

/**
 * @brief 读取GM7150芯片ID（0x80/0x81）。
 * @param device GM7150设备实例。
 * @param msb    用于接收ID高字节（期望值0x71）的指针。
 * @param lsb    用于接收ID低字节（期望值0x50）的指针。
 * @retval HAL操作状态。
 */
HAL_StatusTypeDef GM7150_ReadDeviceID(GM7150_HandleTypeDef *device,
                                      uint8_t *msb,
                                      uint8_t *lsb);

/**
 * @brief 读取GM7150的ROM版本（0x82/0x83）。
 */
HAL_StatusTypeDef GM7150_ReadRomVersion(GM7150_HandleTypeDef *device,
                                        uint8_t *major,
                                        uint8_t *minor);

/**
 * @brief 校验总线上的设备是否为GM7150（ID等于0x7150）。
 * @param device  GM7150设备实例。
 * @param present 1表示检测到GM7150，0表示未检测到或读取失败。
 * @retval HAL操作状态。
 */
HAL_StatusTypeDef GM7150_IsDevicePresent(GM7150_HandleTypeDef *device,
                                         uint8_t *present);

/**
 * @brief 读取并解析GM7150的实时状态（锁定、场频、制式、弱信号等）。
 * @param device GM7150设备实例。
 * @param status 用于接收解析结果的结构体指针。
 * @retval HAL操作状态。
 */
HAL_StatusTypeDef GM7150_ReadStatus(GM7150_HandleTypeDef *device,
                                    GM7150_StatusTypeDef *status);

/**
 * @brief 仅查询输入视频是否已有效锁定。
 * @param device GM7150设备实例。
 * @param locked 1表示已锁定，0表示未锁定。
 * @retval HAL操作状态。
 */
HAL_StatusTypeDef GM7150_IsVideoLocked(GM7150_HandleTypeDef *device,
                                       uint8_t *locked);

/**
 * @brief 读取自动检测到的视频标准（来自状态寄存器0x8C）。
 */
HAL_StatusTypeDef GM7150_GetDetectedStandard(GM7150_HandleTypeDef *device,
                                             GM7150_VideoStandardTypeDef *standard);

/**
 * @brief 配置视频标准（寄存器0x28）：自动检测或强制某制式。
 * @param device   GM7150设备实例。
 * @param standard 目标制式，GM7150_STD_AUTO为自动检测。
 * @retval HAL操作状态；制式无效时返回HAL_ERROR。
 */
HAL_StatusTypeDef GM7150_SetVideoStandard(GM7150_HandleTypeDef *device,
                                          GM7150_VideoStandardTypeDef standard);

/**
 * @brief 配置数字视频输出格式与码范围（寄存器0x0D）。
 * @param device GM7150设备实例。
 * @param format BT.656（内嵌同步）或BT.601（独立同步）。
 * @param range  有限范围或扩展范围。
 * @retval HAL操作状态；参数无效时返回HAL_ERROR。
 */
HAL_StatusTypeDef GM7150_SetOutputFormat(GM7150_HandleTypeDef *device,
                                         GM7150_OutputFormatTypeDef format,
                                         GM7150_CodeRangeTypeDef range);

/**
 * @brief 整体开启或关闭YCbCr数据、同步信号和时钟输出（寄存器0x03）。
 * @param device GM7150设备实例。
 * @param enable 非0开启输出，0关闭（相关引脚进入高阻）。
 * @retval HAL操作状态。
 * @note  开始采集前建议先配置好STM32侧接收，再调用本函数开启输出，避免采集到乱码。
 */
HAL_StatusTypeDef GM7150_SetOutputEnable(GM7150_HandleTypeDef *device,
                                         uint8_t enable);

/**
 * @brief 设置软件功耗模式（寄存器0x02 bit0）。
 * @param device GM7150设备实例。
 * @param mode   正常或省电。
 * @retval HAL操作状态。
 */
HAL_StatusTypeDef GM7150_SetPowerMode(GM7150_HandleTypeDef *device,
                                      GM7150_PowerModeTypeDef mode);

/**
 * @brief 设置亮度（寄存器0x09，默认0x80）。
 */
HAL_StatusTypeDef GM7150_SetBrightness(GM7150_HandleTypeDef *device,
                                       uint8_t value);

/**
 * @brief 设置对比度（寄存器0x0C，默认0x80）。
 */
HAL_StatusTypeDef GM7150_SetContrast(GM7150_HandleTypeDef *device,
                                     uint8_t value);

/**
 * @brief 设置色度饱和度（寄存器0x0A，默认0x80）。
 */
HAL_StatusTypeDef GM7150_SetSaturation(GM7150_HandleTypeDef *device,
                                       uint8_t value);

/**
 * @brief 设置色度色调（寄存器0x0B，默认0x00）。
 */
HAL_StatusTypeDef GM7150_SetHue(GM7150_HandleTypeDef *device,
                                uint8_t value);

/**
 * @brief 阻塞等待输入视频锁定，常用于切换输入源或上电后确认信号有效。
 * @param device     GM7150设备实例。
 * @param timeout_ms 超时时间（毫秒）。
 * @retval HAL_OK表示在超时内锁定；HAL_TIMEOUT表示超时；其他表示I2C错误。
 */
HAL_StatusTypeDef GM7150_WaitForLock(GM7150_HandleTypeDef *device,
                                     uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* GM7150_H */
