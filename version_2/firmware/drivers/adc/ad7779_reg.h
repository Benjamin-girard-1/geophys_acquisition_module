/**
 * @file ad7779_reg.h
 * @brief Private AD7779 register map, wire fields, commands, and limits.
 *
 * Source: Analog Devices AD7779 datasheet, Rev. F. Register addresses
 * are 7-bit; the MSB of the first SPI byte is R/W (0 = write, 1 = read).
 * This header is private to the portable AD7779 driver.
 */

#ifndef AD7779_REG_H_
#define AD7779_REG_H_

#ifndef AD7779_DRIVER_INTERNAL
#error "ad7779_reg.h is private to ad7779.c"
#endif

#include <stdint.h>

/* Device and conversion limits. */
#define AD7779_NUM_CHANNELS                   UINT8_C(8)
#define AD7779_MAX_CHANNEL_INDEX              UINT8_C(7)
#define AD7779_ALL_CHANNELS_MASK              UINT8_C(0xFF)
#define AD7779_BITS_PER_SAMPLE                UINT8_C(24)
#define AD7779_SAMPLE_MIN                     (-INT32_C(8388607) - INT32_C(1))
#define AD7779_SAMPLE_MAX                     INT32_C(8388607)
#define AD7779_SAMPLE_SIGN_BIT                UINT32_C(0x00800000)
#define AD7779_SAMPLE_MODULUS                 UINT32_C(0x01000000)
#define AD7779_DRIVER_MAX_ODR_HZ              UINT32_C(16000)
#define AD7779_HR_MAX_ODR_HZ                  UINT32_C(16000)
#define AD7779_LP_MAX_ODR_HZ                  UINT32_C(8000)
#define AD7779_LP_SPECIFIED_MAX_ODR_HZ        UINT32_C(4000)
#define AD7779_HR_MCLK_DIV                     UINT8_C(4)
#define AD7779_HR_SRC_N_MIN                    UINT16_C(128)

/* SPI register transactions and commands. */
#define AD7779_SPI_ADDRESS_MASK               UINT8_C(0x7F)
#define AD7779_SPI_READ                       UINT8_C(0x80)
#define AD7779_SPI_WRITE                      UINT8_C(0x00)
#define AD7779_SPI_REGISTER_HEADER            UINT8_C(0x20)
#define AD7779_SPI_REGISTER_FRAME_BYTES       UINT8_C(2)
#define AD7779_SPI_REGISTER_CRC_FRAME_BYTES   UINT8_C(3)
#define AD7779_SPI_SOFT_RESET_BYTE            UINT8_C(0xFF)
#define AD7779_SPI_SOFT_RESET_BYTES           UINT8_C(8)
#define AD7779_SPI_SOFT_RESET_CLOCKS          UINT8_C(64)
#define AD7779_SPI_IGNORED_READ_COMMAND       UINT16_C(0x8000)
#define AD7779_SPI_IGNORED_READ_CMD_HI        UINT8_C(0x80)
#define AD7779_SPI_IGNORED_READ_CMD_LO        UINT8_C(0x00)

/* CRC x^8 + x^2 + x + 1; the input shift register is preset to ones. */
#define AD7779_CRC_POLYNOMIAL                 UINT8_C(0x07)
#define AD7779_CRC_INITIAL_VALUE              UINT8_C(0xFF)

/* Per-channel configuration registers, 0x00 through 0x07. */
#define AD7779_REG_CH_CONFIG(channel)         (UINT8_C(0x00) + (channel))
#define AD7779_CH_CONFIG_RESET_VALUE          UINT8_C(0x00)
#define AD7779_CH_CONFIG_GAIN_POS             6U
#define AD7779_CH_CONFIG_GAIN_MSK             (UINT8_C(0x03) << AD7779_CH_CONFIG_GAIN_POS)
#define AD7779_CH_CONFIG_GAIN_1               (UINT8_C(0x00) << AD7779_CH_CONFIG_GAIN_POS)
#define AD7779_CH_CONFIG_GAIN_2               (UINT8_C(0x01) << AD7779_CH_CONFIG_GAIN_POS)
#define AD7779_CH_CONFIG_GAIN_4               (UINT8_C(0x02) << AD7779_CH_CONFIG_GAIN_POS)
#define AD7779_CH_CONFIG_GAIN_8               (UINT8_C(0x03) << AD7779_CH_CONFIG_GAIN_POS)
#define AD7779_CH_CONFIG_REF_MONITOR           (UINT8_C(1) << 5)
#define AD7779_CH_CONFIG_RX                    (UINT8_C(1) << 4)

/* Channel disable and synchronization registers. */
#define AD7779_REG_CH_DISABLE                 UINT8_C(0x08)
#define AD7779_CH_DISABLE_RESET_VALUE         UINT8_C(0x00)
#define AD7779_CH_DISABLE_BIT(channel)        (UINT8_C(1) << (channel))
#define AD7779_REG_CH_SYNC_OFFSET(channel)    (UINT8_C(0x09) + (channel))
#define AD7779_CH_SYNC_OFFSET_RESET_VALUE     UINT8_C(0x00)

/* General user configuration 1. */
#define AD7779_REG_GENERAL_USER_CONFIG_1      UINT8_C(0x11)
#define AD7779_GUC1_RESET_VALUE               UINT8_C(0x24)
#define AD7779_GUC1_ALL_CH_DIS_MCLK_EN        (UINT8_C(1) << 7)
#define AD7779_GUC1_HR_MODE                   (UINT8_C(1) << 6)
#define AD7779_GUC1_PDB_VCM                   (UINT8_C(1) << 5)
#define AD7779_GUC1_PDB_REFOUT_BUF            (UINT8_C(1) << 4)
#define AD7779_GUC1_PDB_SAR                   (UINT8_C(1) << 3)
#define AD7779_GUC1_PDB_RC_OSC                (UINT8_C(1) << 2)
#define AD7779_GUC1_SOFT_RESET_MSK            UINT8_C(0x03)
#define AD7779_GUC1_SOFT_RESET_NO_EFFECT      UINT8_C(0x00)
#define AD7779_GUC1_SOFT_RESET_FIRST_WRITE    UINT8_C(0x03)
#define AD7779_GUC1_SOFT_RESET_SECOND_WRITE   UINT8_C(0x02)

/* General user configuration 2. */
#define AD7779_REG_GENERAL_USER_CONFIG_2      UINT8_C(0x12)
#define AD7779_GUC2_RESET_VALUE               UINT8_C(0x09)
#define AD7779_GUC2_SAR_DIAG_MODE_EN          (UINT8_C(1) << 5)
#define AD7779_GUC2_SDO_DRIVE_STR_POS         3U
#define AD7779_GUC2_SDO_DRIVE_STR_MSK         (UINT8_C(0x03) << AD7779_GUC2_SDO_DRIVE_STR_POS)
#define AD7779_GUC2_DOUT_DRIVE_STR_POS        1U
#define AD7779_GUC2_DOUT_DRIVE_STR_MSK        (UINT8_C(0x03) << AD7779_GUC2_DOUT_DRIVE_STR_POS)
#define AD7779_GUC2_SPI_SYNC                  (UINT8_C(1) << 0)

/* General user configuration 3. */
#define AD7779_REG_GENERAL_USER_CONFIG_3      UINT8_C(0x13)
#define AD7779_GUC3_RESET_VALUE               UINT8_C(0x80)
#define AD7779_GUC3_CONVST_DEGLITCH_POS       6U
#define AD7779_GUC3_CONVST_DEGLITCH_MSK       (UINT8_C(0x03) << AD7779_GUC3_CONVST_DEGLITCH_POS)
#define AD7779_GUC3_SPI_SUBORDINATE_MODE_EN   (UINT8_C(1) << 4)
#define AD7779_GUC3_CLK_QUAL_DIS              (UINT8_C(1) << 0)

/* Data output format. */
#define AD7779_REG_DOUT_FORMAT                UINT8_C(0x14)
#define AD7779_DOUT_RESET_VALUE               UINT8_C(0x20)
#define AD7779_DOUT_FORMAT_POS                6U
#define AD7779_DOUT_FORMAT_MSK                (UINT8_C(0x03) << AD7779_DOUT_FORMAT_POS)
#define AD7779_DOUT_FORMAT_4_LINES            (UINT8_C(0x00) << AD7779_DOUT_FORMAT_POS)
#define AD7779_DOUT_FORMAT_2_LINES            (UINT8_C(0x01) << AD7779_DOUT_FORMAT_POS)
#define AD7779_DOUT_FORMAT_1_LINE             (UINT8_C(0x02) << AD7779_DOUT_FORMAT_POS)
#define AD7779_DOUT_HEADER_FORMAT_CRC         (UINT8_C(1) << 5)
#define AD7779_DCLK_CLK_DIV_POS               1U
#define AD7779_DCLK_CLK_DIV_MSK               (UINT8_C(0x07) << AD7779_DCLK_CLK_DIV_POS)

/* ADC, diagnostic, and GPIO mux registers. */
#define AD7779_REG_ADC_MUX_CONFIG             UINT8_C(0x15)
#define AD7779_ADC_MUX_RESET_VALUE            UINT8_C(0x00)
#define AD7779_REF_MUX_CTRL_POS               6U
#define AD7779_REF_MUX_CTRL_MSK               (UINT8_C(0x03) << AD7779_REF_MUX_CTRL_POS)
#define AD7779_MTR_MUX_CTRL_POS               2U
#define AD7779_MTR_MUX_CTRL_MSK               (UINT8_C(0x0F) << AD7779_MTR_MUX_CTRL_POS)

#define AD7779_REG_GLOBAL_MUX_CONFIG          UINT8_C(0x16)
#define AD7779_GLOBAL_MUX_RESET_VALUE         UINT8_C(0x00)
#define AD7779_GLOBAL_MUX_CTRL_POS            3U
#define AD7779_GLOBAL_MUX_CTRL_MSK            (UINT8_C(0x1F) << AD7779_GLOBAL_MUX_CTRL_POS)

#define AD7779_REG_GPIO_CONFIG                UINT8_C(0x17)
#define AD7779_GPIO_CONFIG_RESET_VALUE        UINT8_C(0x00)
#define AD7779_GPIO_OP_EN_MSK                 UINT8_C(0x07)
#define AD7779_REG_GPIO_DATA                  UINT8_C(0x18)
#define AD7779_GPIO_DATA_RESET_VALUE          UINT8_C(0x00)
#define AD7779_GPIO_READ_DATA_POS             3U
#define AD7779_GPIO_READ_DATA_MSK             (UINT8_C(0x07) << AD7779_GPIO_READ_DATA_POS)
#define AD7779_GPIO_WRITE_DATA_MSK            UINT8_C(0x07)

/* Reference buffer configuration. */
#define AD7779_REG_BUFFER_CONFIG_1            UINT8_C(0x19)
#define AD7779_BC1_RESET_VALUE                UINT8_C(0x38)
#define AD7779_BC1_REF_BUF_POS_EN             (UINT8_C(1) << 4)
#define AD7779_BC1_REF_BUF_NEG_EN             (UINT8_C(1) << 3)

#define AD7779_REG_BUFFER_CONFIG_2            UINT8_C(0x1A)
#define AD7779_BC2_RESET_VALUE                UINT8_C(0xC0)
#define AD7779_BC2_REFBUFP_PRECHARGE          (UINT8_C(1) << 7)
#define AD7779_BC2_REFBUFN_PRECHARGE          (UINT8_C(1) << 6)
#define AD7779_BC2_PDB_ALDO1_OVRDRV           (UINT8_C(1) << 2)
#define AD7779_BC2_PDB_ALDO2_OVRDRV           (UINT8_C(1) << 1)
#define AD7779_BC2_PDB_DLDO_OVRDRV            (UINT8_C(1) << 0)

/* Per-channel offset and gain calibration, six consecutive bytes/channel. */
#define AD7779_REG_CH_OFFSET_UPPER(channel)   (UINT8_C(0x1C) + (UINT8_C(6) * (channel)))
#define AD7779_REG_CH_OFFSET_MID(channel)     (UINT8_C(0x1D) + (UINT8_C(6) * (channel)))
#define AD7779_REG_CH_OFFSET_LOWER(channel)   (UINT8_C(0x1E) + (UINT8_C(6) * (channel)))
#define AD7779_REG_CH_GAIN_UPPER(channel)     (UINT8_C(0x1F) + (UINT8_C(6) * (channel)))
#define AD7779_REG_CH_GAIN_MID(channel)       (UINT8_C(0x20) + (UINT8_C(6) * (channel)))
#define AD7779_REG_CH_GAIN_LOWER(channel)     (UINT8_C(0x21) + (UINT8_C(6) * (channel)))

/* Per-channel analog-input errors, 0x4C through 0x53. */
#define AD7779_REG_CH_ERR(channel)            (UINT8_C(0x4C) + (channel))
#define AD7779_CH_ERR_AINM_UV                 (UINT8_C(1) << 4)
#define AD7779_CH_ERR_AINM_OV                 (UINT8_C(1) << 3)
#define AD7779_CH_ERR_AINP_UV                 (UINT8_C(1) << 2)
#define AD7779_CH_ERR_AINP_OV                 (UINT8_C(1) << 1)
#define AD7779_CH_ERR_REF_DET                 (UINT8_C(1) << 0)

/* Paired-channel saturation errors, 0x54 through 0x57. */
#define AD7779_REG_CH_PAIR_SAT_ERR(pair)      (UINT8_C(0x54) + (pair))
#define AD7779_SAT_ERR_ODD_MOD                (UINT8_C(1) << 5)
#define AD7779_SAT_ERR_ODD_FILTER             (UINT8_C(1) << 4)
#define AD7779_SAT_ERR_ODD_OUTPUT             (UINT8_C(1) << 3)
#define AD7779_SAT_ERR_EVEN_MOD               (UINT8_C(1) << 2)
#define AD7779_SAT_ERR_EVEN_FILTER            (UINT8_C(1) << 1)
#define AD7779_SAT_ERR_EVEN_OUTPUT            (UINT8_C(1) << 0)

#define AD7779_REG_CHX_ERR_REG_EN             UINT8_C(0x58)
#define AD7779_CHX_ERR_EN_RESET_VALUE         UINT8_C(0xFE)
#define AD7779_CHX_ERR_OUTPUT_SAT_TEST_EN      (UINT8_C(1) << 7)
#define AD7779_CHX_ERR_FILTER_SAT_TEST_EN      (UINT8_C(1) << 6)
#define AD7779_CHX_ERR_MOD_SAT_TEST_EN         (UINT8_C(1) << 5)
#define AD7779_CHX_ERR_AINM_UV_TEST_EN         (UINT8_C(1) << 4)
#define AD7779_CHX_ERR_AINM_OV_TEST_EN         (UINT8_C(1) << 3)
#define AD7779_CHX_ERR_AINP_UV_TEST_EN         (UINT8_C(1) << 2)
#define AD7779_CHX_ERR_AINP_OV_TEST_EN         (UINT8_C(1) << 1)
#define AD7779_CHX_ERR_REF_DET_TEST_EN         (UINT8_C(1) << 0)

/* General errors and enables. */
#define AD7779_REG_GEN_ERR_REG_1              UINT8_C(0x59)
#define AD7779_ERR1_MEMMAP_CRC                 (UINT8_C(1) << 5)
#define AD7779_ERR1_ROM_CRC                    (UINT8_C(1) << 4)
#define AD7779_ERR1_SPI_CLK_COUNT              (UINT8_C(1) << 3)
#define AD7779_ERR1_SPI_INVALID_READ           (UINT8_C(1) << 2)
#define AD7779_ERR1_SPI_INVALID_WRITE          (UINT8_C(1) << 1)
#define AD7779_ERR1_SPI_CRC                    (UINT8_C(1) << 0)

#define AD7779_REG_GEN_ERR_REG_1_EN           UINT8_C(0x5A)
#define AD7779_ERR1_EN_RESET_VALUE             UINT8_C(0x3E)
#define AD7779_ERR1_EN_MEMMAP_CRC_TEST         (UINT8_C(1) << 5)
#define AD7779_ERR1_EN_ROM_CRC_TEST            (UINT8_C(1) << 4)
#define AD7779_ERR1_EN_SPI_CLK_COUNT_TEST      (UINT8_C(1) << 3)
#define AD7779_ERR1_EN_SPI_INVALID_READ_TEST   (UINT8_C(1) << 2)
#define AD7779_ERR1_EN_SPI_INVALID_WRITE_TEST  (UINT8_C(1) << 1)
#define AD7779_ERR1_EN_SPI_CRC_TEST            (UINT8_C(1) << 0)

#define AD7779_REG_GEN_ERR_REG_2              UINT8_C(0x5B)
#define AD7779_ERR2_RESET_DETECTED             (UINT8_C(1) << 5)
#define AD7779_ERR2_EXT_MCLK_SWITCH            (UINT8_C(1) << 4)
#define AD7779_ERR2_ALDO1_PSM                  (UINT8_C(1) << 2)
#define AD7779_ERR2_ALDO2_PSM                  (UINT8_C(1) << 1)
#define AD7779_ERR2_DLDO_PSM                   (UINT8_C(1) << 0)

#define AD7779_REG_GEN_ERR_REG_2_EN           UINT8_C(0x5C)
#define AD7779_ERR2_EN_RESET_VALUE             UINT8_C(0x3C)
#define AD7779_ERR2_EN_RESET_DETECT            (UINT8_C(1) << 5)
#define AD7779_ERR2_EN_LDO_PSM_TEST_POS        2U
#define AD7779_ERR2_EN_LDO_PSM_TEST_MSK        (UINT8_C(0x03) << AD7779_ERR2_EN_LDO_PSM_TEST_POS)
#define AD7779_ERR2_EN_LDO_TRIP_TEST_POS       0U
#define AD7779_ERR2_EN_LDO_TRIP_TEST_MSK       (UINT8_C(0x03) << AD7779_ERR2_EN_LDO_TRIP_TEST_POS)

/* Error-location status registers. */
#define AD7779_REG_STATUS_REG_1               UINT8_C(0x5D)
#define AD7779_STAT1_CHIP_ERROR                (UINT8_C(1) << 5)
#define AD7779_STAT1_ERR_LOC_CH4               (UINT8_C(1) << 4)
#define AD7779_STAT1_ERR_LOC_CH3               (UINT8_C(1) << 3)
#define AD7779_STAT1_ERR_LOC_CH2               (UINT8_C(1) << 2)
#define AD7779_STAT1_ERR_LOC_CH1               (UINT8_C(1) << 1)
#define AD7779_STAT1_ERR_LOC_CH0               (UINT8_C(1) << 0)

#define AD7779_REG_STATUS_REG_2               UINT8_C(0x5E)
#define AD7779_STAT2_CHIP_ERROR                (UINT8_C(1) << 5)
#define AD7779_STAT2_ERR_LOC_GEN2              (UINT8_C(1) << 4)
#define AD7779_STAT2_ERR_LOC_GEN1              (UINT8_C(1) << 3)
#define AD7779_STAT2_ERR_LOC_CH7               (UINT8_C(1) << 2)
#define AD7779_STAT2_ERR_LOC_CH6               (UINT8_C(1) << 1)
#define AD7779_STAT2_ERR_LOC_CH5               (UINT8_C(1) << 0)

#define AD7779_REG_STATUS_REG_3               UINT8_C(0x5F)
#define AD7779_STAT3_CHIP_ERROR                (UINT8_C(1) << 5)
#define AD7779_STAT3_INIT_COMPLETE             (UINT8_C(1) << 4)
#define AD7779_STAT3_ERR_LOC_SAT_CH6_7         (UINT8_C(1) << 3)
#define AD7779_STAT3_ERR_LOC_SAT_CH4_5         (UINT8_C(1) << 2)
#define AD7779_STAT3_ERR_LOC_SAT_CH2_3         (UINT8_C(1) << 1)
#define AD7779_STAT3_ERR_LOC_SAT_CH0_1         (UINT8_C(1) << 0)

/* Sample-rate converter fields. */
#define AD7779_REG_SRC_N_MSB                  UINT8_C(0x60)
#define AD7779_SRC_N_MSB_RESET_VALUE          UINT8_C(0x00)
#define AD7779_SRC_N_MSB_MSK                  UINT8_C(0x0F)
#define AD7779_REG_SRC_N_LSB                  UINT8_C(0x61)
#define AD7779_SRC_N_LSB_RESET_VALUE          UINT8_C(0x80)
#define AD7779_SRC_N_BITS                     UINT8_C(12)
#define AD7779_SRC_N_MAX                      UINT16_C(0x0FFF)
#define AD7779_REG_SRC_IF_MSB                 UINT8_C(0x62)
#define AD7779_REG_SRC_IF_LSB                 UINT8_C(0x63)
#define AD7779_SRC_IF_BITS                    UINT8_C(16)
#define AD7779_SRC_IF_MAX                     UINT16_C(0xFFFF)
#define AD7779_SRC_FRACTION_SCALE             UINT32_C(65536)
#define AD7779_REG_SRC_UPDATE                 UINT8_C(0x64)
#define AD7779_SRC_LOAD_SOURCE                (UINT8_C(1) << 7)
#define AD7779_SRC_LOAD_UPDATE                (UINT8_C(1) << 0)
#define AD7779_SRC_UPDATE_MIN_MCLK_CYCLES     UINT8_C(2)

/* One simultaneous sample: 8-bit header followed by signed 24-bit data. */
#define AD7779_FRAME_HEADER_BYTES             UINT8_C(1)
#define AD7779_FRAME_SAMPLE_BYTES             UINT8_C(3)
#define AD7779_FRAME_BYTES_PER_CH             UINT8_C(4)
#define AD7779_FRAME_BYTES_TOTAL              UINT8_C(32)
#define AD7779_FRAME_CHANNEL_PAIR_COUNT       UINT8_C(4)
#define AD7779_FRAME_CRC_INPUT_BYTES_PER_PAIR UINT8_C(7)
#define AD7779_FRAME_HEADER_ALERT             (UINT8_C(1) << 7)
#define AD7779_FRAME_HEADER_CHANNEL_POS       4U
#define AD7779_FRAME_HEADER_CHANNEL_MSK       (UINT8_C(0x07) << AD7779_FRAME_HEADER_CHANNEL_POS)
#define AD7779_FRAME_HEADER_LOW_NIBBLE_MSK    UINT8_C(0x0F)
#define AD7779_FRAME_STATUS_RESET_DETECTED    (UINT8_C(1) << 3)
#define AD7779_FRAME_STATUS_MODULATOR_SAT     (UINT8_C(1) << 2)
#define AD7779_FRAME_STATUS_FILTER_SAT        (UINT8_C(1) << 1)
#define AD7779_FRAME_STATUS_AIN_OV_UV         (UINT8_C(1) << 0)

#endif /* AD7779_REG_H_ */
