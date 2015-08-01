#ifndef ERROR_H
#define ERROR_H

/* @brief A number that identifies an error condition
 * @details The handling of each error is defined in the source file.
 */
typedef enum {
    ERROR_NONE = 0,
    ERROR_UNEXPECTED_CODEPOINT,
    ERROR_UNEXPECTED_INTERRUPT,
    ERROR_INVALID_VALUE,
} error_t;

/**
 * @brief IDs for assert statements reported as a power-of-two frequenc on green LED
 * @details If red led is continuously on, the green LED should be a square wave
 *          at a frequency 2^(assser_id).
 */
typedef enum {
    ASSERT_SET_CORE_VOLTAGE_FAILED                = 0, // won't show up as 1 Hz (freq not yet set)
    ASSERT_RF_EVENTS_BUF_OVERFLOW                 = 1,
    ASSERT_HOST_MSG_BUF_OVERFLOW                  = 2,
    ASSERT_INVALID_RFID_CMD_HANDLER               = 3,
    ASSERT_UART_FAULT                             = 4,
    ASSERT_UART_ERROR_CIRC_BUF_OVERFLOW           = 5,
    ASSERT_UART_ERROR_RX_PKT_OVERFLOW             = 6,
    ASSERT_INVALID_STREAM_BUF_HEADER              = 7,
    ASSERT_UART_TX_BUSY                           = 8,
    ASSERT_UNHANDLED_INTERRUPT                    = 9,
    ASSERT_UNEXPECTED_INTERRUPT                   = 10,
    ASSERT_CORRUPT_STATE                          = 11,
    ASSERT_VOLTAGE_SAMPLES_BUF_OVERFLOW           = 12,
    ASSERT_UNEXPECTED_ADC_DMA_STATE               = 13,
    ASSERT_ADC_BUFFER_OVERFLOW                    = 14,
    ASSERT_ADC_FAULT                              = 15,
    ASSERT_INVALID_SIG_CMD                        = 16,
} assert_t;

/* @brief Blink led at a given rate indefinitely
 * @details This has to be a macro because arg to __delay_cycles intrinsic must be a
 *          constant.
 */
#define BLINK_LOOP(led_pin, delay_cycles) while (1) { \
        GPIO(PORT_LED, OUT) ^= BIT(led_pin); \
        __delay_cycles(delay_cycles); \
    }

/* @brief Report and handle error */
void error(error_t num);

#define ASSERT(idx, cond) \
    if (!(cond)) { \
        GPIO(PORT_LED, OUT) |= BIT(PIN_LED_RED); \
        BLINK_LOOP(PIN_LED_GREEN, (CONFIG_MCLK_FREQ >> 1) >> (idx)); \
    }

#endif
