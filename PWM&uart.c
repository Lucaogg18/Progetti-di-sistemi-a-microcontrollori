#include "xstatus.h"
#include "platform.h"
#include "xtmrctr_l.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xuartlite_l.h"
#include "xil_io.h"

#ifndef SDT
    #define TMRCTR_BASEADDR		XPAR_TMRCTR_0_BASEADDR
    #define UART_BASEADDR       XPAR_UARTLITE_0_BASEADDR
#else
    #define TMRCTR_BASEADDR		XPAR_XTMRCTR_0_BASEADDR
    #define UART_BASEADDR       XPAR_XUARTLITE_0_BASEADDR
#endif

#define TIMER_COUNTER_0	 0
#define NO_DATA          0xFFFFFFFF

volatile int * gpio_buttons_tri  = (volatile int *)0x40060004; 
volatile int * gpio_rgb_data     = (volatile int *)0x40000008;

volatile int * IER = (volatile int*)0x41200008;
volatile int * MER = (volatile int*)0x4120001C;
volatile int * IISR = (volatile int*)0x41200000;
volatile int * IIAR = (volatile int*)0x4120000C;

volatile u8 pwm_counter = 0;
volatile u32 sys_time = 0; 

volatile u8 duty_R = 0;
volatile u8 duty_G = 0;
volatile u8 duty_B = 0;

void myISR(void) __attribute__((interrupt_handler));
int TmrCtrLowLevelExample(UINTPTR TmrCtrBaseAddress, u8 TimerCounter);
u32 my_XUartLite_RecvByte(UINTPTR BaseAddress);
void update_leds(u32 data, u8 mode);

int main(void){
	init_platform();

	int Status;
    u32 uart_input;
    u32 button_input;

    duty_R = 0; duty_G = 0; duty_B = 0;

    *gpio_buttons_tri = 0xFFFFFFFF;

    *IER = XPAR_AXI_TIMER_0_INTERRUPT_MASK;
    *MER = 0x3;
    microblaze_enable_interrupts();

	Status = TmrCtrLowLevelExample(TMRCTR_BASEADDR, TIMER_COUNTER_0);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
    }

	while(1) {
        button_input = Xil_In32(XPAR_GPIO_5_BASEADDR);
        if(button_input)
			update_leds(button_input, 0);

        uart_input = my_XUartLite_RecvByte(UART_BASEADDR);
        if (uart_input != NO_DATA) {
            update_leds(uart_input, 1);
        }
    }

	return XST_SUCCESS;
}

void update_leds(u32 data, u8 mode)
{
    static u32 last_debounce_time = 0;
    static int seq_index = 2; 

    u32 debounce_delay = 125000; 

    if (mode == 0) {
        if (sys_time - last_debounce_time > debounce_delay) {
            
            seq_index++;
            if (seq_index > 2) {
                seq_index = 0;
            }

            if (seq_index == 0) {
                duty_R = 255; duty_G = 0; duty_B = 0;
            }
            else if (seq_index == 1) {
                duty_R = 0; duty_G = 255; duty_B = 0;
            }
            else {
                duty_R = 0; duty_G = 0; duty_B = 255;
            }

            last_debounce_time = sys_time;
        }
    }
    else {
        switch ((char)data) {
            case '1': duty_R = 255; duty_G = 0;   duty_B = 0;   break; 
            case '2': duty_R = 0;   duty_G = 255; duty_B = 0;   break; 
            case '3': duty_R = 0;   duty_G = 0;   duty_B = 255; break; 
            case '4': duty_R = 255; duty_G = 255; duty_B = 0;   break; 
            case '5': duty_R = 0;   duty_G = 255; duty_B = 255; break; 
            case '6': duty_R = 255; duty_G = 0;   duty_B = 255; break; 
            case '7': duty_R = 255; duty_G = 255; duty_B = 255; break; 
            case '8': duty_R = 255; duty_G = 128; duty_B = 0;   break; 
            case '9': duty_R = 128; duty_G = 0;   duty_B = 128; break; 
            case '0': duty_R = 0;   duty_G = 0;   duty_B = 0;   break; 
            default: break;
        }
    }
}

u32 my_XUartLite_RecvByte(UINTPTR BaseAddress) 
{ 
    u32 status = XUartLite_GetStatusReg(BaseAddress);
    if (status & XUL_SR_RX_FIFO_VALID_DATA) {
        u32 data = XUartLite_ReadReg(BaseAddress, XUL_RX_FIFO_OFFSET);
        if ((u8)data == 0x0D || (u8)data == 0x0A) return NO_DATA;
        return data;
    }
    return NO_DATA;
} 

int TmrCtrLowLevelExample(UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber)
{
	u32 ControlStatus;
    XTmrCtr_SetControlStatusReg(TmrCtrBaseAddress, TmrCtrNumber, XTC_CSR_AUTO_RELOAD_MASK|XTC_CSR_ENABLE_INT_MASK|XTC_CSR_DOWN_COUNT_MASK);
    XTmrCtr_SetLoadReg(TmrCtrBaseAddress, TmrCtrNumber, 400); 
	XTmrCtr_LoadTimerCounterReg(TmrCtrBaseAddress, TmrCtrNumber);
    ControlStatus = XTmrCtr_GetControlStatusReg(TmrCtrBaseAddress, TmrCtrNumber);
    XTmrCtr_SetControlStatusReg(TmrCtrBaseAddress, TmrCtrNumber, ControlStatus & (~XTC_CSR_LOAD_MASK));
	XTmrCtr_Enable(TmrCtrBaseAddress, TmrCtrNumber);
	return XST_SUCCESS;
}

void myISR(void)
{
    unsigned p = *IISR;
    if (p & XPAR_AXI_TIMER_0_INTERRUPT_MASK) {

        pwm_counter++;
        sys_time++; 

        u32 r_bit = (pwm_counter < duty_R) ? 0 : 1;
        u32 g_bit = (pwm_counter < duty_G) ? 0 : 1;
        u32 b_bit = (pwm_counter < duty_B) ? 0 : 1;

        u32 rgb_output = (r_bit << 2) | (g_bit << 1) | (b_bit << 0);
        *gpio_rgb_data = rgb_output;

        int ControlStatus = XTmrCtr_GetControlStatusReg(TMRCTR_BASEADDR, 0);
        XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, 0, ControlStatus | (XTC_CSR_INT_OCCURED_MASK));
        *IIAR |= XPAR_AXI_TIMER_0_INTERRUPT_MASK;
    }
}