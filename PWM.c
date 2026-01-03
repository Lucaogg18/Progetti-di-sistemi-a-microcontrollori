#include "xstatus.h"
#include "xtmrctr_l.h"
#include "xil_printf.h"
#include "xparameters.h"

// Configurazione indirizzo base del Timer
#ifndef SDT
#define TMRCTR_BASEADDR		XPAR_TMRCTR_0_BASEADDR
#else
#define TMRCTR_BASEADDR		XPAR_XTMRCTR_0_BASEADDR
#endif

#define TIMER_COUNTER_0	 0

// --- Memory Mapped I/O Pointers ---
// Canale 1: LED Standard (offset 0x0)
volatile int * gpio_leds_data = (volatile int *)0x40000000;
// Canale 2: RGB LEDs (offset 0x8) - Secondo il diagramma AXI GPIO
volatile int * gpio_rgb_data  = (volatile int *)0x40000008;

// Interrupt Controller Registers
volatile int * IER = (volatile int*)	0x41200008;
volatile int * MER = (volatile int*)	0x4120001C;
volatile int * IISR = (volatile int*)	0x41200000;
volatile int * IIAR = (volatile int*)	0x4120000C;

// --- PWM Global Variables ---
// Risoluzione a 8 bit (0-255)
volatile u8 pwm_counter = 0;
volatile u8 duty_R = 0;
volatile u8 duty_G = 0;
volatile u8 duty_B = 0;

// Prototipi
void myISR(void) __attribute__((interrupt_handler));
int TmrCtrLowLevelExample(UINTPTR TmrCtrBaseAddress, u8 TimerCounter);

int main(void)
{
	int Status;

	// Setup iniziali PWM (Esempio: Colore Viola)
    // Rosso al 50% (128), Blu al 50% (128), Verde spento.
    duty_R = 0;
    duty_G = 0;
    duty_B = 0;

	// Setup Interrupt Controller
    *IER = XPAR_AXI_TIMER_0_INTERRUPT_MASK;
    *MER = 0x3;
    microblaze_enable_interrupts();

	Status = TmrCtrLowLevelExample(TMRCTR_BASEADDR, TIMER_COUNTER_0);
	if (Status != XST_SUCCESS) {
		xil_printf("PWM Init Failed\r\n");
		return XST_FAILURE;
	}

    // Loop infinito: qui si possono cambiare i colori dinamicamente
	while(1) {
        // Esempio: effetto "fade" o cambio colore si potrebbe fare qui
        // modificando duty_R, duty_G, duty_B
    }

	return XST_SUCCESS;
}

int TmrCtrLowLevelExample(UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber)
{
	u32 ControlStatus;

    // 1. Imposta Control Status Register (Auto Reload + Interrupt + Down Count)
    XTmrCtr_SetControlStatusReg(TmrCtrBaseAddress, TmrCtrNumber, XTC_CSR_AUTO_RELOAD_MASK|XTC_CSR_ENABLE_INT_MASK|XTC_CSR_DOWN_COUNT_MASK);

    // 2. Calcolo del Timer Load Value per PWM
    // Formula: f_int = f_sys / LoadValue.
    // Target: Vogliamo f_pwm > 1kHz
    // Se f_sys = 100MHz e LoadValue = 400:
    // f_int = 250 kHz.
    // f_pwm = f_int / 256 (8 bit) ~= 976 Hz (circa 1kHz).
    // Valore impostato a 400 clock ticks.
    XTmrCtr_SetLoadReg(TmrCtrBaseAddress, TmrCtrNumber, 400);

    // 3. Carica il valore
	XTmrCtr_LoadTimerCounterReg(TmrCtrBaseAddress, TmrCtrNumber);

    // 4. Pulisce bit di Load
    ControlStatus = XTmrCtr_GetControlStatusReg(TmrCtrBaseAddress, TmrCtrNumber);
	XTmrCtr_SetControlStatusReg(TmrCtrBaseAddress, TmrCtrNumber, ControlStatus & (~XTC_CSR_LOAD_MASK));

    // 5. Abilita il timer
	XTmrCtr_Enable(TmrCtrBaseAddress, TmrCtrNumber);

	return XST_SUCCESS;
}

// --- ISR: Gestione PWM ---
void myISR(void)
{
    unsigned p = *IISR;
    if (p & XPAR_AXI_TIMER_0_INTERRUPT_MASK) {

        // 1. Incremento contatore globale
        pwm_counter++;
        // Il contatore è u8, quindi farà overflow da 255 a 0 automaticamente,
        // creando il ciclo periodico richiesto

        // 2. Calcolo stato LED (Active Low: 0=ON, 1=OFF)
        // Se il contatore è minore del duty cycle, il LED deve essere ACCESO (LOW).
        // Altrimenti deve essere SPENTO (HIGH).

        u32 rgb_output = 0;
        u32 r_bit, g_bit, b_bit;

        // Logica: Output 0 (LOW) se counter < duty, altrimenti 1 (HIGH)
        r_bit = (pwm_counter < duty_R) ? 0 : 1;
        g_bit = (pwm_counter < duty_G) ? 0 : 1;
        b_bit = (pwm_counter < duty_B) ? 0 : 1;

        // Assembla i bit. Assumiamo la mappatura standard Cmod A7:
        // Bit 0: Blu, Bit 1: Verde, Bit 2: Rosso (varia in base all'XDC, esempio generico)
        rgb_output = (r_bit << 2) | (g_bit << 1) | (b_bit << 0);

        // Scrive sul registro GPIO canale 2 (RGB)
        *gpio_rgb_data = rgb_output;

        // 3. Pulisce Interrupt Periferica
        int ControlStatus = XTmrCtr_GetControlStatusReg(TMRCTR_BASEADDR, 0);
        XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, 0, ControlStatus | (XTC_CSR_INT_OCCURED_MASK));

        // 4. Pulisce Interrupt Controller
        *IIAR |= XPAR_AXI_TIMER_0_INTERRUPT_MASK;
    }
}

