#ifndef CONFIG_H
#define CONFIG_H

// These are fixed constants properties of hardware
#define CONFIG_XT1_FREQ 32768
#define CONFIG_XT1_CAP 12 // pF
#define CONFIG_XT2_FREQ 12000000
// #define CONFIG_XT2_FREQ 25000000
#define CONFIG_REFO_FREQ 32768

#define MCU_ON_THRES 2607 /* 2.1 V */ // int(math.ceil(voltage * 4096 / self.VDD))
#define MCU_BOOT_LATENCY_MS 3 // measured: from Vreg = 2.2V to GPIO high at end of debug_setup()

// #define CONFIG_DCO_REF_SOURCE_REFO
// #define CONFIG_DCO_REF_CLOCK_DIV 1

#define CONFIG_DCO_REF_SOURCE_XT1
#define CONFIG_DCO_REF_CLOCK_DIV 1

// #define CONFIG_DCO_REF_SOURCE_XT2
// #define CONFIG_DCO_REF_CLOCK_DIV 4

#define CONFIG_CLOCK_SOURCE_DCO
// #define CONFIG_CLOCK_SOURCE_XT2

// #define CONFIG_DCOCLKDIV_FREQ 24000000
#define CONFIG_DCOCLKDIV_FREQ 21921792
// #define CONFIG_DCOCLKDIV_FREQ 16384000
// #define CONFIG_DCOCLKDIV_FREQ 12288000
// #define CONFIG_DCOCLKDIV_FREQ 8192000

#define CONFIG_TIMELOG_TIMER_SOURCE TASSEL__ACLK
// #define CONFIG_TIMELOG_TIMER_SOURCE TASSEL__SMCLK

#define CONFIG_TIMELOG_TIMER_DIV_BITS 0
#define CONFIG_TIMELOG_TIMER_DIV_BITS_EX 0
// #define CONFIG_TIMELOG_TIMER_DIV_BITS (ID0 | ID1)
// #define CONFIG_TIMELOG_TIMER_DIV_BITS_EX (TAIDEX_2 | TAIDEX_1 | TAIDEX_0)

// #define CONFIG_USB_UART_BAUDRATE 2000000
// #define CONFIG_USB_UART_BAUDRATE 1000000
#define CONFIG_USB_UART_BAUDRATE 921600
// #define CONFIG_USB_UART_BAUDRATE 500000
// #define CONFIG_USB_UART_BAUDRATE 115200

#define CONFIG_TARGET_UART_BAUDRATE 9600

#define CONFIG_ABORT_ON_USB_UART_ERROR // red led on, and if error is overflow, green led blinking

// #define CONFIG_CLOCK_TEST_MODE // enter a blinker loop after configuring clocks
// #define CONFIG_ROUTE_ACLK_TO_PIN // must "unplug" op amp buffers by disconnecting JP1

// Encode debugger state machine state onto pins
// #define CONFIG_STATE_PINS

/**
 * @brief Enable pull-down on the debugger<->target interrupt line.
 *
 * @details We watch for interrupts the target raises when the target requests
 *          to enter active debug mode, such as upon hitting an internal or
 *          external breakpoint. If the target is off or not present, then both
 *          ends of this line are in high impedence mode (effectively
 *          floating?). This does not seem to cause spurious interrupts, but if
 *          this problem did arise, enabling pull-down resistors should solve
 *          it.
 *
 *          NOTE: The pull-down causes energy interference (current is sourced
 *          from the target when target drives this pin high), but the target
 *          only drives this pin high for at most one cycle (signal
 *          communication is exclusively done using one-cycle-long pulses).
 */
// #define CONFIG_PULL_DOWN_ON_SIG_LINE

/**
 * @brief Enable code for decoding the RF protocol
 *
 * @details Currently, this is disabled because it causes spurious interrupts.
 *         TODO: setup RF pins only upon cmd to monitor RF because otherwise we
 *         get spurious interrupts on RX pin
 */
// #define CONFIG_ENABLE_RF_PROTOCOL_MONITORING

// The rest essentially defines the register settings that carry out the above

#define MCU_BOOT_LATENCY_CYCLES (MCU_BOOT_LATENCY_MS * CONFIG_DCOCLKDIV_FREQ / 1000)

// See MSP430F5340 datasheet p44
#if CONFIG_XT1_CAP >= 12
#define CONFIG_XT1_CAP_BITS (XCAP0 | XCAP1)
#elif CONFIG_XT1_CAP >= 8
#define CONFIG_XT1_CAP_BITS (XCAP1)
#elif CONFIG_XT1_CAP >= 5
#define CONFIG_XT1_CAP_BITS (XCAP0)
#else
#define CONFIG_XT1_CAP_BITS 0
#endif

#if defined(CONFIG_DCO_REF_SOURCE_REFO)
#define CONFIG_DCO_REF_CLOCK_FREQ CONFIG_REFO_FREQ
#elif defined(CONFIG_DCO_REF_SOURCE_XT1)
#define CONFIG_DCO_REF_CLOCK_FREQ CONFIG_XT1_FREQ
#elif defined(CONFIG_DCO_REF_SOURCE_XT2)
#define CONFIG_DCO_REF_CLOCK_FREQ CONFIG_XT2_FREQ
#else // CONFIG_DCO_FREQ_SOURCE_*
#error Invalid DCO clock reference: see DCO_REF_SOURCE_*
#endif // CONFIG_DCO_REF_SOURCE_*

#define CONFIG_DCO_REF_FREQ (CONFIG_DCO_REF_CLOCK_FREQ / CONFIG_DCO_REF_CLOCK_DIV)

// DCO config
//
// NOTE: MSP430 crashes if it runs too fast?
// This may be caused by the average frequency from DCO modulation.  If we try to use
// a faster clock, the FLL may adjust the DCO above 25MHz to produce a clock with that
// average frequency.  If this happens, even for an instant, the MSP430 can crash.

// DCOCLK freq = 2^D * (N + 1) * REF_CLOCK_FREQ/REF_CLOCK_DIV
// DCOCLKDIV = DCOCLK / 2^D
//
// R | frequency range (MHz) (datasheet MSP430F5340 page 47)
// 0:  0.20 -  0.70
// 1:  0.36 -  1.47
// 2:  0.75 -  3.17
// 3:  1.51 -  6.07
// 4:  3.20 - 12.30
// 5:  6.00 - 23.70
// 6: 10.70 - 39.00
// 7: 19.60 - 60.00

#if CONFIG_DCOCLKDIV_FREQ == 21921792 && CONFIG_DCO_REF_FREQ == 32768

// 32768 * (668 + 1)
#define CONFIG_DCO_FREQ_R 7
#define CONFIG_DCO_FREQ_N 668
#define CONFIG_DCO_FREQ_D 0

#elif CONFIG_DCOCLKDIV_FREQ == 16384000 && CONFIG_DCO_REF_FREQ == 32768

// 32768 * (499 + 1)
#define CONFIG_DCO_FREQ_R 6
#define CONFIG_DCO_FREQ_N 499
#define CONFIG_DCO_FREQ_D 1

#elif CONFIG_DCOCLKDIV_FREQ == 12288000 && CONFIG_DCO_REF_FREQ == 32768

// 32768 * (374 + 1)
#define CONFIG_DCO_FREQ_R 5
#define CONFIG_DCO_FREQ_N 374
#define CONFIG_DCO_FREQ_D 1

#elif CONFIG_DCOCLKDIV_FREQ == 8192000 && CONFIG_DCO_REF_FREQ == 32768

// 32768 * (249 + 1)
#define CONFIG_DCO_FREQ_R 5
#define CONFIG_DCO_FREQ_N 249
#define CONFIG_DCO_FREQ_D 1

#elif CONFIG_DCOCLKDIV_FREQ == 24000000 && CONFIG_DCO_REF_FREQ == 3000000

// 3000000 * (7 + 1)
#define CONFIG_DCO_FREQ_R 6
#define CONFIG_DCO_FREQ_N 7
#define CONFIG_DCO_FREQ_D 0

#endif // CONFIG_DCOCLKDIV_FREQ && CONFIG_DCO_REF_FREQ

// See MSP430F5340 datasheet (p. 177)
#if CONFIG_DCO_REF_CLOCK_DIV == 1
#define CONFIG_FLL_REF_DIV 0
#elif CONFIG_DCO_REF_CLOCK_DIV == 2
#define CONFIG_FLL_REF_DIV (FLLREFDIV0)
#elif CONFIG_DCO_REF_CLOCK_DIV == 4
#define CONFIG_FLL_REF_DIV (FLLREFDIV1)
#elif CONFIG_DCO_REF_CLOCK_DIV == 8
#define CONFIG_FLL_REF_DIV (FLLREFDIV0 | FLLREFDIV1)
#elif CONFIG_DCO_REF_CLOCK_DIV == 12
#define CONFIG_FLL_REF_DIV (FLLREFDIV2)
#elif CONFIG_DCO_REF_CLOCK_DIV == 16
#define CONFIG_FLL_REF_DIV (FLLREFDIV2 | FLLREFDIV0)
#else
#error Invalid DCO REF clock divider: see CONFIG_DCO_REF_CLOCK_DIV
#endif

// Worst-case settling time for the DCO when the DCO range bits have been changed:
// See MSP430x5xx Family User Manual (p. 165). The last fraction term is
// converting from FLL ref clock cycles to core clock cycles.
#define DCO_SETTLING_TIME \
    (1LU * CONFIG_DCO_REF_CLOCK_DIV * 32 * 32 * \
     (CONFIG_DCOCLKDIV_FREQ / CONFIG_DCO_REF_CLOCK_FREQ + 1))

#if CONFIG_DCOCLKDIV_FREQ != ((CONFIG_DCO_FREQ_N + 1) * CONFIG_DCO_REF_FREQ)
#error Inconsistent DCO freq config
#endif

// Clock source for MCLK, SMCLK
#if defined(CONFIG_CLOCK_SOURCE_DCO)
#define CONFIG_SMCLK_FREQ CONFIG_DCOCLKDIV_FREQ
#elif defined(CONFIG_CLOCK_SOURCE_XT2)
#define CONFIG_SMCLK_FREQ CONFIG_XT2_FREQ // for now, SMCLK source is not configurable
#else // CONFIG_CLOCK_SOURCE_*
#error Invalid main clock source: see CONFIG_CLOCK_SOURCE_*
#endif // CONFIG_CLOCK_SOURCE_*

#define CONFIG_UART_CLOCK_FREQ CONFIG_SMCLK_FREQ

// UART baudrate specification:
// N = SMCLK / BAUD
// BR0 = LSB(floor(N))
// BR1 = MSB(floor(N))
// BRS = (floor(N) - N) * 8

#if CONFIG_UART_CLOCK_FREQ == 21921792

#if CONFIG_USB_UART_BAUDRATE == 921600
// N = 23.786666...
#define CONFIG_USB_UART_BAUDRATE_BR0 23
#define CONFIG_USB_UART_BAUDRATE_BR1 0
#define CONFIG_USB_UART_BAUDRATE_BRS 6

#endif // CONFIG_USB_UART_BAUDRATE

#if CONFIG_TARGET_UART_BAUDRATE == 9600
// N = 2283.52
#define CONFIG_TARGET_UART_BAUDRATE_BR0 0xEB
#define CONFIG_TARGET_UART_BAUDRATE_BR1 0x08
#define CONFIG_TARGET_UART_BAUDRATE_BRS 4

#endif // CONFIG_TARGET_UART_BAUDRATE

#elif CONFIG_UART_CLOCK_FREQ == 12500000

#if CONFIG_USB_UART_BAUDRATE == 2000000
#define CONFIG_USB_UART_BAUDRATE_BR0 6
#define CONFIG_USB_UART_BAUDRATE_BR1 0
#define CONFIG_USB_UART_BAUDRATE_BRS 2

#elif CONFIG_USB_UART_BAUDRATE == 1000000
#define CONFIG_USB_UART_BAUDRATE_BR0 12
#define CONFIG_USB_UART_BAUDRATE_BR1 0
#define CONFIG_USB_UART_BAUDRATE_BRS 4

#elif CONFIG_USB_UART_BAUDRATE == 115200
#define CONFIG_USB_UART_BAUDRATE_BR0 108
#define CONFIG_USB_UART_BAUDRATE_BR1 0
#define CONFIG_USB_UART_BAUDRATE_BRS 4

#endif // CONFIG_USB_UART_BAUDRATE

#if CONFIG_TARGET_UART_BAUDRATE == 9600
#define CONFIG_TARGET_UART_BAUDRATE_BR0 0x16
#define CONFIG_TARGET_UART_BAUDRATE_BR1 0x05
#define CONFIG_TARGET_UART_BAUDRATE_BRS 1

#endif // CONFIG_TARGET_UART_BAUDRATE

#elif CONFIG_UART_CLOCK_FREQ == 6250000

#if CONFIG_USB_UART_BAUDRATE == 115200
#define CONFIG_USB_UART_BAUDRATE_BR0 54
#define CONFIG_USB_UART_BAUDRATE_BR1 0
#define CONFIG_USB_UART_BAUDRATE_BRS 2

#endif

#elif CONFIG_UART_CLOCK_FREQ == 24000000

#if CONFIG_USB_UART_BAUDRATE == 2000000
// N = 12
#define CONFIG_USB_UART_BAUDRATE_BR0 12
#define CONFIG_USB_UART_BAUDRATE_BR1 0
#define CONFIG_USB_UART_BAUDRATE_BRS 0

#elif CONFIG_USB_UART_BAUDRATE == 1000000
// N = 24
#define CONFIG_USB_UART_BAUDRATE_BR0 24
#define CONFIG_USB_UART_BAUDRATE_BR1 0
#define CONFIG_USB_UART_BAUDRATE_BRS 0

#elif CONFIG_USB_UART_BAUDRATE == 500000
// N = 48
#define CONFIG_USB_UART_BAUDRATE_BR0 48
#define CONFIG_USB_UART_BAUDRATE_BR1 0
#define CONFIG_USB_UART_BAUDRATE_BRS 0

#endif // CONFIG_USB_UART_BAUDRATE

#if CONFIG_TARGET_UART_BAUDRATE == 9600
// N = 2500
#define CONFIG_TARGET_UART_BAUDRATE_BR0 0xC4
#define CONFIG_TARGET_UART_BAUDRATE_BR1 0x09
#define CONFIG_TARGET_UART_BAUDRATE_BRS 0

#endif // CONFIG_TARGET_UART_BAUDRATE

#elif CONFIG_UART_CLOCK_FREQ == 12000000

#if CONFIG_USB_UART_BAUDRATE == 2000000
// N = 6
#define CONFIG_USB_UART_BAUDRATE_BR0 6
#define CONFIG_USB_UART_BAUDRATE_BR1 0
#define CONFIG_USB_UART_BAUDRATE_BRS 0

#endif // CONFIG_USB_UART_BAUDRATE

#if CONFIG_TARGET_UART_BAUDRATE == 9600
// N = 1250
#define CONFIG_TARGET_UART_BAUDRATE_BR0 0xE2
#define CONFIG_TARGET_UART_BAUDRATE_BR1 0x04
#define CONFIG_TARGET_UART_BAUDRATE_BRS 0

#endif // CONFIG_TARGET_UART_BAUDRATE

#endif // CONFIG_UART_CLOCK_FREQ_*

#if !defined(CONFIG_USB_UART_BAUDRATE_BR0) || !defined(CONFIG_TARGET_UART_BAUDRATE_BR0)
#error UART configuration error: no config for selected CONFIG_UART_CLOCK_FREQ
#endif

#endif // CONFIG_H
