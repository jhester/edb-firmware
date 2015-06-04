/**
 * @file
 * @author  Graham Harvey
 * @date    13 May 2015
 * @brief   Main function for the WISP monitor.
 * @details The MSP430 on the WISP monitor has UART interrupts enabled to interface
 *          to a computer through USB.  The main loop checks flags that are set in
 *          the interrupt service routines.  This allows execution to continue
 *          without blocking to wait for peripherals.  The 12-bit ADC in use has
 *          several channels that allow four different voltages to be sampled on
 *          the WISP. These are named Vcap, Vboost, Vreg, and Vrect.  The monitor
 *          can get a single sample or log repeated samples of any of those.  It
 *          can also inject power to keep one of those voltages at a level defined
 *          by the user.  However, current is always injected on Vcap.
 */

#include <msp430.h>
#include <stdint.h>

#include "pin_assign.h"
#include "ucs.h"
#include "adc12.h"
#include "uart.h"
#include "pwm.h"
#include "timeLog.h"
#include "timer1.h"
#include "rfid.h"
#include "marker.h"
#include "minmax.h"
#include "main.h"

/**
 * @defgroup    MAIN_FLAG_DEFINES   Main loop flags
 * @brief       Flags to set in a bit mask to check in the main loop
 * @{
 */
#define FLAG_ADC12_COMPLETE         0x0001 //!< ADC12 has completed conversion
#define FLAG_UART_USB_RX            0x0002 //!< Bytes received on the USB UART
#define FLAG_UART_USB_TX            0x0004 //!< Bytes transmitted on the USB UART
#define FLAG_UART_WISP_RX           0x0008 //!< Bytes received on the WISP UART
#define FLAG_UART_WISP_TX           0x0010 //!< Bytes transmitted on the WISP UART
#define FLAG_LOGGING                0x0020 //!< Logging ADC conversion results to USB
#define FLAG_RF_DATA				0x0040 //!< RF Rx activity ready to be logged
/** @} End MAIN_FLAG_DEFINES */

/**
 * @defgroup    LOG_DEFINES     Logging flags
 * @brief       Flags to set in a bit mask to check which ADC readings to log
 * @{
 */
#define LOG_VCAP                    0x01 //!< Logging Vcap
#define LOG_VBOOST                  0x02 //!< Logging Vboost
#define LOG_VREG                    0x04 //!< Logging Vreg
#define LOG_VRECT                   0x08 //!< Logging Vrect
#define LOG_VINJ					0x10 //!< Logging Vinj
/** @} End LOG_DEFINES */

/**
 * Debugger state machine states
 */
typedef enum {
    STATE_IDLE = 0,
    STATE_ENTERING,
    STATE_DEBUG,
    STATE_EXITING,
} state_t;

static adc12_t adc12;
// indices in the adc12.config.channels and adc12.results arrays
static int8_t vcap_index = -1;
static int8_t vboost_index = -1;
static int8_t vreg_index = -1;
static int8_t vrect_index = -1;
static int8_t vinj_index = -1;

static uint16_t flags = 0; // bit mask containing bit flags to check in the main loop
static uint8_t log_flags = 0; // bit mask containing active log values to send via USB

static state_t state = STATE_IDLE;

static uint16_t adc12Target; // target ADC reading
static uint16_t saved_vcap; // energy level before entering active debug mode

static uartPkt_t wispRxPkt = { .processed = 1 };

static void set_state(state_t new_state)
{
    state = new_state;

    // Encode state onto two indicator pins
    GPIO(PORT_STATE, OUT) &= ~(BIT(PIN_STATE_0) | BIT(PIN_STATE_1));
    GPIO(PORT_STATE, OUT) |= (new_state & 0x1 ? BIT(PIN_STATE_0) : 0) |
                             (new_state & 0x2 ? BIT(PIN_STATE_1) : 0);
}

/**
 * @brief	Send an interrupt to the target device
 */
static void signal_target()
{
    // pulse the signal line

    // target signal line starts in high imedence state
    GPIO(PORT_SIG, OUT) |= BIT(PIN_SIG);		// output high
    GPIO(PORT_SIG, DIR) |= BIT(PIN_SIG);		// output enable
    GPIO(PORT_SIG, OUT) &= ~BIT(PIN_SIG);    // output low
    GPIO(PORT_SIG, DIR) &= ~BIT(PIN_SIG);    // back to high impedence state
}

/**
 * @brief	Enable interrupt line between the debugger and the target device
 */
static void unmask_target_signal()
{
    GPIO(PORT_SIG, IE) |= BIT(PIN_SIG);   // enable interrupt
    GPIO(PORT_SIG, IES) &= ~BIT(PIN_SIG); // rising edge
}

/**
 * @brief	Disable interrupt line between the debugger and the target device
 */
static void mask_target_signal()
{
    GPIO(PORT_SIG, IE) &= ~BIT(PIN_SIG); // disable interrupt
}

/**
 * @brief	Handle an interrupt from the target device
 */
static void handle_target_signal()
{
    switch (state) {
        case STATE_ENTERING:
            // WISP has entered debug main loop
            set_state(STATE_DEBUG);
            break;
        case STATE_EXITING:
            // WISP has shutdown UART and is asleep waiting for int to resume
            GPIO(PORT_CHARGE, OUT) &= ~BIT(PIN_CHARGE); // cut the power supply
            discharge_block(saved_vcap); // restore energy level
            GPIO(PORT_LED, OUT) &= ~BIT(PIN_LED_RED);
            set_state(STATE_IDLE);
            break;
        default:
            // received an unexpected signal
            break;
    }
}

static void enter_debug_mode()
{
    GPIO(PORT_LED, OUT) |= BIT(PIN_LED_RED);

    set_state(STATE_ENTERING);

    saved_vcap = adc12Read_block(ADC12INCH_VCAP); // read Vcap and set as the target for exit
    signal_target();
    unmask_target_signal();

    // Start supplying full power to the target device
    GPIO(PORT_CHARGE, OUT) |= BIT(PIN_CHARGE);
}

static void exit_debug_mode()
{
    set_state(STATE_EXITING);
    unmask_target_signal();
    UART_sendMsg(UART_INTERFACE_WISP, WISP_CMD_EXIT_ACTIVE_DEBUG, 0, 0, UART_TX_FORCE);
}

/**
 * @brief   Set up all pins.  Default to GPIO output low for unused pins.
 */
static void pin_setup()
{
    // First pass: set all pins as output low (so that unused pins stay that way)
    P1SEL = P2SEL = P3SEL = P4SEL = P5SEL = 0x00; // I/O function
    P1OUT = P2OUT = P3OUT = P4OUT = P5OUT = PJOUT = 0x00; // low
    P1DIR = P2DIR = P3DIR = P4DIR = P5DIR = PJDIR = 0xFF; // out

    // Configure pins that need to be in high-impedence/input mode
    GPIO(PORT_SIG, DIR) &= ~BIT(PIN_SIG);
    GPIO(PORT_DISCHARGE, DIR) &= ~BIT(PIN_DISCHARGE);
    GPIO(PORT_LS_ENABLE, DIR) &= ~BIT(PIN_LS_ENABLE); // level-shifter enable is pulled high
    GPIO(PORT_VSENSE, DIR) &= ~(BIT(PIN_VCAP) | BIT(PIN_VBOOST) |
                                BIT(PIN_VREG) | BIT(PIN_VRECT) | BIT(PIN_VINJ));
}

int main(void)
{
    uartPkt_t usbRxPkt = { .processed = 1 };

    // Stop watchdog timer to prevent time out reset
    WDTCTL = WDTPW + WDTHOLD;

    UCS_setup(); // set up unified clock system
    pin_setup();
    PWM_setup(1024-1, 512); // dummy default values
    UART_setup(UART_INTERFACE_USB, &flags, FLAG_UART_USB_RX, FLAG_UART_USB_TX); // USCI_A0 UART
    UART_setup(UART_INTERFACE_WISP, &flags, FLAG_UART_WISP_RX, FLAG_UART_WISP_TX); // USCI_A1 UART

    RFID_setup(&flags, FLAG_RF_DATA, FLAG_RF_DATA); // use the same flag for Rx and Tx so
    												// we only have to check one flag

    // initialize ADC12 configuration structure
    adc12.pFlags = &flags;
    adc12.flag_adc12Complete = FLAG_ADC12_COMPLETE;
    adc12.config.num_channels = 0;

    __enable_interrupt();                   // enable all interrupts

    long count = 0;
    while(1) {
        if(flags & FLAG_ADC12_COMPLETE) {
            // ADC12 has completed conversion on all active channels
            flags &= ~FLAG_ADC12_COMPLETE;

            if(flags & FLAG_LOGGING) {
                // check if we need to send Vcap
                if(log_flags & LOG_VCAP) {
                	// send ADC conversion time and conversion result
                	UART_sendMsg(UART_INTERFACE_USB, USB_RSP_TIME,
                			(uint8_t *)(&(adc12.timeComplete)), sizeof(uint32_t),
							UART_TX_DROP);
                    UART_sendMsg(UART_INTERFACE_USB, USB_RSP_VCAP,
                    	(uint8_t *)(&(adc12.results[vcap_index])), sizeof(uint16_t),
						UART_TX_DROP);
                }

                // check if we need to send Vboost
                if(log_flags & LOG_VBOOST) {
                	UART_sendMsg(UART_INTERFACE_USB, USB_RSP_TIME,
                	             (uint8_t *)(&(adc12.timeComplete)), sizeof(uint32_t),
								 UART_TX_DROP);
                    UART_sendMsg(UART_INTERFACE_USB, USB_RSP_VBOOST,
                    	(uint8_t *)(&(adc12.results[vboost_index])), sizeof(uint16_t),
						UART_TX_DROP);
                }

                // check if we need to send Vreg
                if(log_flags & LOG_VREG) {
                	UART_sendMsg(UART_INTERFACE_USB, USB_RSP_TIME,
									(uint8_t *)(&(adc12.timeComplete)), sizeof(uint32_t),
									UART_TX_DROP);
                    UART_sendMsg(UART_INTERFACE_USB, USB_RSP_VREG,
                    		(uint8_t *)(&(adc12.results[vreg_index])), sizeof(uint16_t),
							UART_TX_DROP);
                }

                // check if we need to send Vrect
                if(log_flags & LOG_VRECT) {
                	UART_sendMsg(UART_INTERFACE_USB, USB_RSP_TIME,
								 (uint8_t *)(&(adc12.timeComplete)), sizeof(uint32_t),
								 UART_TX_DROP);
                    UART_sendMsg(UART_INTERFACE_USB, USB_RSP_VRECT,
                        (uint8_t *)(&(adc12.results[vrect_index])), sizeof(uint16_t),
						UART_TX_DROP);
                }

                // check if we need to send Vinj
                if(log_flags & LOG_VINJ) {
                	UART_sendMsg(UART_INTERFACE_USB, USB_RSP_TIME,
							(uint8_t *)(&(adc12.timeComplete)), sizeof(uint32_t),
							UART_TX_DROP);
                	UART_sendMsg(UART_INTERFACE_USB, USB_RSP_VINJ,
                			(uint8_t *)(&(adc12.results[vinj_index])), sizeof(uint16_t),
							UART_TX_DROP);
                }

                ADC12_START;
                ADC12_START; // I don't know why, but for some reason this seems to
                			 // only work if the ADC12SC bit is set twice.  I hope
                			 // this doesn't indicate that something is horribly,
                			 // horribly wrong.
            }
        }

        if(flags & FLAG_UART_USB_RX) {
            // we've received a byte from USB
            if(UART_buildRxPkt(UART_INTERFACE_USB, &usbRxPkt) == 0) {
                // packet is complete
                executeUSBCmd(&usbRxPkt);
            }

            // check if we're done for now
            UART_DISABLE_USB_RX; // disable interrupt so new bytes don't come in
            if(UART_RxBufEmpty(UART_INTERFACE_USB)) {
                flags &= ~FLAG_UART_USB_RX; // clear USB Rx flag
            }
            UART_ENABLE_USB_RX; // enable interrupt
        }

/*
        if(flags & FLAG_UART_USB_TX) {
            // USB UART Tx byte
            flags &= ~FLAG_UART_USB_TX;
        }
*/
/*
        if(flags & FLAG_UART_WISP_RX) {
            // we've received a byte over UART from the WISP
            if(UART_buildRxPkt(UART_INTERFACE_WISP, &wispRxPkt) == 0) {
            	// packet is complete
            	//doStuff();
            	wispRxPkt.processed = 1;
            }

            // check if we're done for now
            UART_DISABLE_WISP_RX; // disable interrupt so new bytes don't come in
            if(UART_RxBufEmpty(UART_INTERFACE_WISP)) {
            	flags &= ~FLAG_UART_WISP_RX; // clear WISP Rx flag
            }
            UART_ENABLE_WISP_RX; // enable interrupt
        }
*/
/*
        if(flags & FLAG_UART_WISP_TX) {
            // WISP UART Tx byte
            flags &= ~FLAG_UART_WISP_TX;
        }
*/

        if(flags & FLAG_RF_DATA) {
        	flags &= ~FLAG_RF_DATA;
        	RFID_UARTSendRxData(); // send any data that we may have collected
        	RFID_UARTSendTxData();
        }

        // This LED toggle is unnecessary, and probably a huge waste of processing time.
        // The LED blinking will slow down when the monitor is performing more tasks.
        if(count++ > 500000) {
            GPIO(PORT_LED, OUT) ^= BIT(PIN_LED_GREEN);
        	count = 0;
        }
    }
}

/**
 * @brief       Pulse a designated pin for triggering an oscilloscope
 */
static void trigger_scope()
{
    GPIO(PORT_TRIGGER, OUT) |= BIT(PIN_TRIGGER);
    GPIO(PORT_TRIGGER, DIR) |= BIT(PIN_TRIGGER);
    GPIO(PORT_TRIGGER, OUT) &= ~BIT(PIN_TRIGGER);
}

/**
 * @brief       Execute a command received from the computer through the USB port
 * @param       pkt     Packet structure that contains the received message info
 */
static void executeUSBCmd(uartPkt_t *pkt)
{
    uint16_t adc12Result;

    switch(pkt->descriptor)
    {
    case USB_CMD_GET_VCAP:
        adc12Result = adc12Read_block(ADC12INCH_VCAP);
        UART_sendMsg(UART_INTERFACE_USB, USB_RSP_VCAP,
                        (uint8_t *)(&adc12Result), sizeof(uint16_t),
						UART_TX_FORCE);
        break;

    case USB_CMD_GET_VBOOST:
        adc12Result = adc12Read_block(ADC12INCH_VBOOST);
        UART_sendMsg(UART_INTERFACE_USB, USB_RSP_VBOOST,
                        (uint8_t *)(&adc12Result), sizeof(uint16_t),
						UART_TX_FORCE);
        break;

    case USB_CMD_GET_VREG:
        adc12Result = adc12Read_block(ADC12INCH_VREG);
        UART_sendMsg(UART_INTERFACE_USB, USB_RSP_VREG,
                        (uint8_t *)(&adc12Result), sizeof(uint16_t),
						UART_TX_FORCE);
        break;

    case USB_CMD_GET_VRECT:
        adc12Result = adc12Read_block(ADC12INCH_VRECT);
        UART_sendMsg(UART_INTERFACE_USB, USB_RSP_VRECT,
                        (uint8_t *)(&adc12Result), sizeof(uint16_t),
						UART_TX_FORCE);
        break;

    case USB_CMD_SET_VCAP:
        adc12Target = *((uint16_t *)(pkt->data));
        setWispVoltage_block(ADC12INCH_VCAP, &vcap_index, adc12Target);
        break;

    case USB_CMD_SET_VBOOST:
        adc12Target = *((uint16_t *)(pkt->data));
        setWispVoltage_block(ADC12INCH_VBOOST, &vboost_index, adc12Target);
        break;

    case USB_CMD_ENTER_ACTIVE_DEBUG:
    	// todo: turn off all logging?
        trigger_scope();
        enter_debug_mode();
        break;

    case USB_CMD_EXIT_ACTIVE_DEBUG:
        trigger_scope();
        exit_debug_mode();
        break;

    case USB_CMD_GET_WISP_PC:
    	UART_sendMsg(UART_INTERFACE_WISP, WISP_CMD_GET_PC, 0, 0, UART_TX_FORCE); // send request
    	while((UART_buildRxPkt(UART_INTERFACE_WISP, &wispRxPkt) != 0) ||
    			(wispRxPkt.descriptor != WISP_RSP_PC)); // wait for response
    	UART_sendMsg(UART_INTERFACE_USB, USB_RSP_WISP_PC, &(wispRxPkt.data[0]),
    					wispRxPkt.length, UART_TX_FORCE); // send PC over USB
    	wispRxPkt.processed = 1;
    	break;

    case USB_CMD_EXAMINE_MEMORY:
    	// not implemented
        break;

    case USB_CMD_LOG_VCAP_BEGIN:
        flags |= FLAG_LOGGING; // for main loop
        log_flags |= LOG_VCAP; // for main loop
        TimeLog_request(1); // request timer overflow notifications
        addAdcChannel(ADC12INCH_VCAP, &vcap_index);
        restartAdc();
        break;

    case USB_CMD_LOG_VCAP_END:
        log_flags &= ~LOG_VCAP;
        if(!log_flags) {
			flags &= ~FLAG_LOGGING; // for main loop
		}
        TimeLog_request(0);

		removeAdcChannel(&vcap_index);
        restartAdc();
        break;

    case USB_CMD_LOG_VBOOST_BEGIN:
        flags |= FLAG_LOGGING; // for main loop
        log_flags |= LOG_VBOOST; // for main loop
        TimeLog_request(1); // request timer overflow notifications
        addAdcChannel(ADC12INCH_VBOOST, &vboost_index);
        restartAdc();
        break;

    case USB_CMD_LOG_VBOOST_END:
        log_flags &= ~LOG_VBOOST;
        if(!log_flags) {
        	flags &= ~FLAG_LOGGING; // for main loop
        }
        TimeLog_request(0);

		removeAdcChannel(&vboost_index);
        restartAdc();
        break;

    case USB_CMD_LOG_VREG_BEGIN:
        flags |= FLAG_LOGGING; // for main loop
        log_flags |= LOG_VREG; // for main loop
        TimeLog_request(1); // request timer overflow notifications
        addAdcChannel(ADC12INCH_VREG, &vreg_index);
        restartAdc();
        break;

    case USB_CMD_LOG_VREG_END:
        log_flags &= ~LOG_VREG;
        if(!log_flags) {
			flags &= ~FLAG_LOGGING; // for main loop
		}
        TimeLog_request(0);
		removeAdcChannel(&vreg_index);
        restartAdc();
        break;

    case USB_CMD_LOG_VRECT_BEGIN:
        flags |= FLAG_LOGGING; // for main loop
        log_flags |= LOG_VRECT; // for main loop
        TimeLog_request(1); // request timer overflow notifications
        addAdcChannel(ADC12INCH_VRECT, &vrect_index);
        restartAdc();
        break;

    case USB_CMD_LOG_VRECT_END:
        log_flags &= ~LOG_VRECT;
        if(!log_flags) {
			flags &= ~FLAG_LOGGING; // for main loop
		}
        TimeLog_request(0);
        removeAdcChannel(&vrect_index);
        restartAdc();
        break;

    case USB_CMD_LOG_RF_RX_BEGIN:
    	TimeLog_request(1); // request timer overflow notifications
    	RFID_startRxLog();
    	break;

    case USB_CMD_LOG_RF_RX_END:
    	TimeLog_request(0);
    	RFID_stopRxLog();
    	break;

    case USB_CMD_LOG_RF_TX_BEGIN:
    	TimeLog_request(1); // request timer overflow notifications
    	RFID_startTxLog();
    	break;

    case USB_CMD_LOG_RF_TX_END:
    	TimeLog_request(0);
    	RFID_stopTxLog();
    	break;

    case USB_CMD_SEND_RF_TX_DATA:
		// not implemented
		break;

    case USB_CMD_ENABLE_PORT_INT_TAG_PWR:
    	// not implemented
    	break;

    case USB_CMD_DISABLE_PORT_INT_TAG_PWR:
    	// not implemented
    	break;

    case USB_CMD_PWM_ON:
    	PWM_start();
    	break;

    case USB_CMD_CHARGE:
        trigger_scope();
        adc12Target = *((uint16_t *)(&pkt->data[0]));
        charge_block(adc12Target);
        break;

    case USB_CMD_DISCHARGE:
        trigger_scope();
        adc12Target = *((uint16_t *)(pkt->data));
        discharge_block(adc12Target);
        break;

    case USB_CMD_TRIGGER_SCOPE:
        trigger_scope();
        break;

    case USB_CMD_RELEASE_POWER:
    case USB_CMD_PWM_OFF:
    case USB_CMD_PWM_LOW:
    	PWM_stop();
    	break;

    case USB_CMD_SET_PWM_FREQUENCY:
    	TB0CCR0 = (*((uint16_t *)(pkt->data))) - 1;
    	break;

    case USB_CMD_SET_PWM_DUTY_CYCLE:
    	TB0CCR1 = *((uint16_t *)(pkt->data));
    	break;

    case USB_CMD_LOG_VINJ_BEGIN:
    	flags |= FLAG_LOGGING; // for main loop
		log_flags |= LOG_VINJ; // for main loop
		TimeLog_request(1);
		addAdcChannel(ADC12INCH_VINJ, &vinj_index);
		restartAdc();
    	break;

    case USB_CMD_LOG_VINJ_END:
		log_flags &= ~LOG_VINJ;
		if(!log_flags) {
			flags &= ~FLAG_LOGGING; // for main loop
		}
		TimeLog_request(0);
		removeAdcChannel(&vinj_index);
		restartAdc();
    	break;

    case USB_CMD_PWM_HIGH:
    	PWM_stop();
        GPIO(PORT_CHARGE, OUT) |= BIT(PIN_CHARGE); // output high
    	break;

    // USB_CMD_PWM_LOW and USB_CMD_PWM_OFF do the same thing

    case USB_CMD_MONITOR_MARKER_BEGIN:
        marker_monitor_begin();
        break;

    case USB_CMD_MONITOR_MARKER_END:
        marker_monitor_end();
        break;

    default:
        break;
    }

    pkt->processed = 1;
}

static void addAdcChannel(uint16_t channel, int8_t *pResults_index)
{
    // note that the results index corresponds to the
    // index in the channels array

    // first check if the channel is already present
	if(*pResults_index == -1) {
        // channel isn't present in the configuration, so add it
        adc12.config.channels[adc12.config.num_channels] = channel;
        *pResults_index = adc12.config.num_channels++;
    }
}

static void removeAdcChannel(int8_t *pResults_index)
{
    // note that results_index is also the corresponding
    // index in the channels array

	if(*pResults_index != -1) {
		// the channel is present in the configuration
		adc12.config.num_channels--;

		if(*pResults_index != adc12.config.num_channels) {
			// We're removing this channel, but it wasn't the last channel
			// configured.  We need to move the other channels in the channels
			// array so there are no missing channels in the array.
			uint8_t i;
			for(i = *pResults_index + 1; i < adc12.config.num_channels + 1; i++) {
				adc12.config.channels[i - 1] = adc12.config.channels[i];
			}

			// update the results array indices
			if(vcap_index >= adc12.config.num_channels) {
				vcap_index--;
			}
			if(vboost_index >= adc12.config.num_channels) {
				vboost_index--;
			}
			if(vreg_index >= adc12.config.num_channels) {
				vreg_index--;
			}
			if(vrect_index >= adc12.config.num_channels) {
				vrect_index--;
			}
			if(vinj_index >= adc12.config.num_channels) {
				vinj_index--;
			}
		}

		*pResults_index = -1; // remove the channel
	}
}

static void restartAdc()
{
    // don't need to worry about restarting if there are no active channels
    if(adc12.config.num_channels > 0) {
        ADC12_STOP;
        ADC12_WAIT; // wait for ADC to stop (it needs to complete a conversion if multiple channels are active)
        ADC12_configure(&adc12); // reconfigure
        ADC12_START;
    }
}

static uint16_t adc12Read_block(uint16_t channel)
{
    adc12_t adc12_temp = { .pFlags = 0,
    					   .config.channels[0] = channel,
						   .config.num_channels = 1 };
    uint16_t adc12Result;

    ADC12_STOP; // stop any active conversion
    ADC12_WAIT;  // wait for conversion to complete so ADC is stopped
    ADC12_configure(&adc12_temp); // reconfigure ADC
    ADC12_DISABLE_INTERRUPT;
    ADC12_START; // start conversion
    ADC12_WAIT; // wait for conversion to complete
    adc12Result = ADC12MEM0;

    ADC12_configure(&adc12); // restore previous configuration, enable interrupt

    return adc12Result;
}

static void charge_block(uint16_t target)
{
    uint16_t chan = ADC12INCH_VCAP;
    int8_t chan_index;
    uint16_t cur_voltage;

    addAdcChannel(chan, &chan_index);
    restartAdc();

    // Output Vcc level to Vcap (through R1) */

    // Configure the pin
    GPIO(PORT_CHARGE, DS) |= BIT(PIN_CHARGE); // full drive strength
    GPIO(PORT_CHARGE, SEL) &= ~BIT(PIN_CHARGE); // I/O function
    GPIO(PORT_CHARGE, DIR) |= BIT(PIN_CHARGE); // I/O function output

    GPIO(PORT_CHARGE, OUT) |= BIT(PIN_CHARGE); // turn on the power supply

    // Wait for the cap to charge to that voltage

    /* The measured effective period of this loop is roughly 30us ~ 33kHz (out
     * of 200kHz that the ADC can theoretically do). */
    do {
        cur_voltage = adc12Read_block(chan);
    } while (cur_voltage < target);

    GPIO(PORT_CHARGE, OUT) &= ~BIT(PIN_CHARGE); // cut the power supply

    removeAdcChannel(&chan_index);
}

static void discharge_block(uint16_t target)
{
    uint16_t chan = ADC12INCH_VCAP;
    int8_t chan_index;
    uint16_t cur_voltage;

    addAdcChannel(chan, &chan_index);
    restartAdc();

    GPIO(PORT_DISCHARGE, DIR) |= BIT(PIN_DISCHARGE); // open the discharge "valve"

    /* The measured effective period of this loop is roughly 30us ~ 33kHz (out
     * of 200kHz that the ADC can theoretically do). */
    do {
        cur_voltage = adc12Read_block(chan);
    } while (cur_voltage > target);

    GPIO(PORT_DISCHARGE, DIR) &= ~BIT(PIN_DISCHARGE); // close the discharge "valve"

    removeAdcChannel(&chan_index);
}

static void setWispVoltage_block(uint16_t channel, int8_t *pResults_index, uint16_t target)
{
	uint16_t result;
	int8_t compare;
	uint8_t threshold = 1;
	addAdcChannel(channel, pResults_index);
	restartAdc();

	// Here, we want to choose a starting PWM duty cycle close to, but not above,
	// what the correct one will be.  We want it to be close because we will test
	// each duty cycle, increasing one cycle at a time, until we reach the right
	// one.  In my experiment with the oscilloscope, the WISP cap took about 60ms
	// to charge, so I'll give it 80ms for the first charge and 10ms for each
	// charge following.

	// We know the ADC target, so let's give the PWM duty cycle our best guess.
	// target (adc) * PWM_period (SMCLK cycles) / 2^12 (adc) = approx. PWM_duty_cycle (SMCLK cycles)
	// Subtract 40 from this to start.
	uint32_t duty_cycle = (uint32_t) target * (uint32_t) TB0CCR0 / 4096;
	TB0CCR1 = MAX((uint16_t) duty_cycle - 40, 0);
	PWM_start();

	// 70ms wait time
	uint8_t i;
	for(i = 0; i < 40; i++) {
		__delay_cycles(21922); // delay for 1ms
	}

	do {
		// 10ms wait time
		for(i = 0; i < 40; i++) {
			__delay_cycles(21922); // delay for 1ms
		}

		result = adc12Read_block(channel);
		compare = uint16Compare(result, target, threshold);
		if(compare < 0) {
			// result < target
			PWM_INCREASE_DUTY_CYCLE;
		} else if(compare > 0) {
			// result > target
			PWM_DECREASE_DUTY_CYCLE;
		}
	} while(compare != 0);

	// We've found the correct PWM duty cycle for the target voltage.
	// Leave PWM on, but remove this channel from the ADC configuration
	// if necessary.
	if((channel == ADC12INCH_VCAP && !(log_flags & LOG_VCAP)) ||
				(channel == ADC12INCH_VBOOST && !(log_flags & LOG_VBOOST))) {
		// We don't need this ADC channel anymore, since we're done here and
		// we're not logging this voltage right now.
		removeAdcChannel(pResults_index);
		restartAdc();
	}
}

static int8_t uint16Compare(uint16_t n1, uint16_t n2, uint16_t threshold) {
	if(n1 < n2 - threshold && n2 >= threshold) {
		return -1;
	} else if(n1 > n2 + threshold && n2 + threshold <= 0xFFFF) {
		return 1;
	}
	return 0;
}

// Port 1 ISR
#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector=PORT1_VECTOR
__interrupt void Port_1(void)
#elif defined(__GNUC__)
void __attribute__ ((interrupt(PORT1_VECTOR))) Port_1 (void)
#else
#error Compiler not supported!
#endif
{
	switch(__even_in_range(P1IV, 16))
	{
	case INTFLAG(PORT_RF, PIN_RF_TX):
		RFID_TxHandler(getTime());
		GPIO(PORT_RF, IFG) &= ~BIT(PIN_RF_TX);
		break;
	case INTFLAG(PORT_RF, PIN_RF_RX):
		RFID_RxHandler();
		break;
	case INTFLAG(PORT_SIG, PIN_SIG):
		mask_target_signal();
		handle_target_signal();
		GPIO(PORT_SIG, IFG) &= ~BIT(PIN_SIG);
		break;
	default:
		break;
	}
}
