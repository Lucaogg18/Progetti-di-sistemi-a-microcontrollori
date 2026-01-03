#include "xparameters.h"
#include "xstatus.h"
#include "xtmrctr_l.h"
#include "xil_printf.h"
#include "mb_interface.h"

volatile int *LED_DATA = (volatile int *)0x40000000;
volatile int *LED_TRI  = (volatile int *)0x40000004;

volatile int *BTN_LEFT_DATA = (volatile int *)0x40060000;
volatile int *BTN_LEFT_TRI  = (volatile int *)0x40060004;

volatile int *BTN_RIGHT_DATA = (volatile int *)0x40050000;
volatile int *BTN_RIGHT_TRI  = (volatile int *)0x40050004;

volatile int *IER  = (volatile int *)0x41200008;
volatile int *MER  = (volatile int *)0x4120001C;
volatile int *IISR = (volatile int *)0x41200000;
volatile int *IIAR = (volatile int *)0x4120000C;

#ifndef TMRCTR_BASEADDR
#define TMRCTR_BASEADDR XPAR_TMRCTR_0_BASEADDR
#endif
#define TIMER_COUNTER_0 0
#define TIMER_RESET_VALUE 50000000

volatile int blink_state = 0;

typedef enum { STATE_IDLE, STATE_PRESSED} debounce_state_t;
typedef enum { CAR_CENTER, CAR_LEFT, CAR_RIGHT } car_state_t;

void myISR(void) __attribute__((interrupt_handler));
int SetupTimer(void);
int FSM_Debounce(volatile int *port_address, debounce_state_t *current_state);

int main(void)
{
    int status;
    car_state_t carState = CAR_CENTER;

    debounce_state_t dbStateLeft = STATE_IDLE;
    debounce_state_t dbStateRight = STATE_IDLE;

    int trigger_left = 0;
    int trigger_right = 0;

    *LED_TRI = 0x0;
    *LED_DATA = 0x0;

    *BTN_LEFT_TRI = 0xFFFFFFFF;
    *BTN_RIGHT_TRI = 0xFFFFFFFF;

    status = SetupTimer();
    if (status != XST_SUCCESS) {
        xil_printf("Errore Setup Timer\r\n");
        return XST_FAILURE;
    }

    while(1) {
        trigger_left  = FSM_Debounce(BTN_LEFT_DATA, &dbStateLeft);
        trigger_right = FSM_Debounce(BTN_RIGHT_DATA, &dbStateRight);

        switch (carState) {
            case CAR_CENTER:
                *LED_DATA = 0x0;

                if (trigger_left) {
                    carState = CAR_LEFT;
                    xil_printf("Azione: FRECCIA SX\r\n");
                } else if (trigger_right) {
                    carState = CAR_RIGHT;
                    xil_printf("Azione: FRECCIA DX\r\n");
                }
                break;

            case CAR_LEFT:
                if (trigger_left) {
                    carState = CAR_CENTER;
                    xil_printf("Azione: OFF\r\n");
                    *LED_DATA = 0x0;
                }
                else {
                    if (blink_state) *LED_DATA = 0x2;
                    else             *LED_DATA = 0x0;
                }
                break;

            case CAR_RIGHT:
                if (trigger_right) {
                    carState = CAR_CENTER;
                    xil_printf("Azione: OFF\r\n");
                    *LED_DATA = 0x0;
                }
                else {
                    if (blink_state) *LED_DATA = 0x1;
                    else             *LED_DATA = 0x0;
                }
                break;
        }
    }
    return 0;
}

int FSM_Debounce(volatile int *port_address, debounce_state_t *state) {
    int button_val = (*port_address & 0x1);
    int trigger = 0;

    switch (*state) {
        case STATE_IDLE:
            if (button_val != 0) {
                *state = STATE_PRESSED;
                trigger = 0;
            }
            break;

        case STATE_PRESSED:
            if (button_val == 0) {
                *state = STATE_IDLE;
                trigger = 1;
            }
            break;
    }
    return trigger;
}

int SetupTimer(void) {
    *IER = XPAR_AXI_TIMER_0_INTERRUPT_MASK;
    *MER = 0x3;

    XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_COUNTER_0, 0);
    XTmrCtr_SetLoadReg(TMRCTR_BASEADDR, TIMER_COUNTER_0, TIMER_RESET_VALUE);
    XTmrCtr_LoadTimerCounterReg(TMRCTR_BASEADDR, TIMER_COUNTER_0);

    XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_COUNTER_0,
        XTC_CSR_AUTO_RELOAD_MASK | XTC_CSR_ENABLE_INT_MASK | XTC_CSR_DOWN_COUNT_MASK);

    XTmrCtr_Enable(TMRCTR_BASEADDR, TIMER_COUNTER_0);
    microblaze_enable_interrupts();

    return XST_SUCCESS;
}

void myISR(void) {
    unsigned p = *IISR;

    if (p & XPAR_AXI_TIMER_0_INTERRUPT_MASK) {
        u32 ControlStatus = XTmrCtr_GetControlStatusReg(TMRCTR_BASEADDR, TIMER_COUNTER_0);
        XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_COUNTER_0,
            ControlStatus | XTC_CSR_INT_OCCURED_MASK);

        *IIAR = XPAR_AXI_TIMER_0_INTERRUPT_MASK;
        blink_state = !blink_state;
    }
}
