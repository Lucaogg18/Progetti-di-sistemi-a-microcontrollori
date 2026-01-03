#include "xparameters.h"
#include "xstatus.h"
#include "xtmrctr_l.h"
#include "xil_printf.h"
#include "xuartlite_l.h"
#include "xil_io.h"

// -----------------------------------------------------------------------------
// CONFIGURAZIONE INDIRIZZI HARDWARE
// -----------------------------------------------------------------------------
#ifndef SDT
    #define TMRCTR_BASEADDR     XPAR_TMRCTR_0_BASEADDR
    #define UART_BASEADDR       XPAR_UARTLITE_0_BASEADDR

    // Sostituisci con indirizzo reale GPIO Motori (es. XPAR_AXI_GPIO_0_BASEADDR)
    #define GPIO_MOTORS_BASE    XPAR_GPIO_MOTORS_BASEADDR
    #define GPIO_LEDS_BASE      0x40000000
#else
    #define TMRCTR_BASEADDR     XPAR_XTMRCTR_0_BASEADDR
    #define UART_BASEADDR       XPAR_XUARTLITE_0_BASEADDR
#endif

#define NO_DATA             0xFFFFFFFF

// --- CONFIGURAZIONE TIMER ---
#define TIMER_PWM           0  // Canale 0 per PWM Veloce
#define TIMER_BLINK         1  // Canale 1 per Lampeggio Lento

// Valori di caricamento (Assumendo Clock AXI a 100 MHz)
// PWM: 400 ticks -> 250 kHz interrupt base -> ~1 kHz PWM resolution
#define PWM_PERIOD          400
// BLINK: 50.000.000 ticks -> 0.5 secondi (2 Hz)
#define BLINK_PERIOD        50000000

// --- MAPPATURA PIN GPIO ---
volatile int * leds_data = (volatile int *)(GPIO_LEDS_BASE + 0x00);
volatile int * leds_tri  = (volatile int *)(GPIO_LEDS_BASE + 0x04);

volatile int * motors_speed_dir_data = (volatile int *)(GPIO_MOTORS_BASE + 0x00);
volatile int * motors_speed_dir_tri  = (volatile int *)(GPIO_MOTORS_BASE + 0x04);
volatile int * motors_enable_data    = (volatile int *)(GPIO_MOTORS_BASE + 0x08);
volatile int * motors_enable_tri     = (volatile int *)(GPIO_MOTORS_BASE + 0x0C);

// Interrupt Controller
volatile int * IER  = (volatile int *)0x41200008;
volatile int * MER  = (volatile int *)0x4120001C;
volatile int * IISR = (volatile int *)0x41200000;
volatile int * IIAR = (volatile int *)0x4120000C;

// -----------------------------------------------------------------------------
// VARIABILI GLOBALI
// -----------------------------------------------------------------------------
volatile u8 pwm_counter = 0;
volatile u8 speed_R = 0;
volatile u8 speed_L = 0;
volatile u8 dir_R = 0;
volatile u8 dir_L = 0;

// Variabili Lampeggio
volatile int blink_state = 0;
volatile int turn_mode = 0;   // 0=Off, 1=Left, 2=Right

// -----------------------------------------------------------------------------
// PROTOTIPI
// -----------------------------------------------------------------------------
void myISR(void) __attribute__((interrupt_handler));
int SetupTimer(void);
u32 UART_RecvByte(UINTPTR BaseAddress);
void ProcessCommand(char cmd);
void UpdateTurnSignals(void); // Funzione helper per aggiornare i LED

// -----------------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------------
int main(void) {
    int Status;
    u32 uart_input;

    xil_printf("--- Smart Car: Dual Timer System ---\r\n");
    xil_printf("Comandi: f,b,s,l,r + q,e (Curve Larghe) + z,c (Curve Strette)\r\n");

    // 1. Configurazione Output
    *leds_tri = 0x00;
    *motors_speed_dir_tri = 0x00;
    *motors_enable_tri = 0x00;

    // 2. Reset (Tutto Spento)
    *leds_data = 0x00;
    *motors_speed_dir_data = 0x00;

    // 3. Abilita Driver (Standby = 1)
    *motors_enable_data = 0x01;

    Status = SetupTimer();
    if (Status != XST_SUCCESS) return XST_FAILURE;

    while (1) {
        uart_input = UART_RecvByte(UART_BASEADDR);
        if (uart_input != NO_DATA) {
            ProcessCommand((char)uart_input);
        }
    }
    return XST_SUCCESS;
}

// -----------------------------------------------------------------------------
// LOGICA COMANDI
// -----------------------------------------------------------------------------
void ProcessCommand(char cmd) {
    const u8 SPD_MAX = 150;
    const u8 SPD_MED = 100;
    const u8 SPD_LOW = 50;

    switch (cmd) {
        // --- BASE ---
        case 'f': // Avanti
            dir_R = 1; dir_L = 1; speed_R = SPD_MAX; speed_L = SPD_MAX;
            turn_mode = 0; *leds_data = 0x0;
            xil_printf("Avanti\r\n");
            break;

        case 'b': // Indietro
            dir_R = 0; dir_L = 0; speed_R = SPD_MAX; speed_L = SPD_MAX;
            turn_mode = 0; *leds_data = 0x0;
            xil_printf("Indietro\r\n");
            break;

        case 's': // Stop
            speed_R = 0; speed_L = 0; dir_R = 0; dir_L = 0;
            turn_mode = 0; *leds_data = 0x0;
            xil_printf("Stop\r\n");
            break;

        // --- PIVOT ---
        case 'l': // Ruota SX
            dir_R = 1; dir_L = 0; speed_R = SPD_MAX; speed_L = SPD_MAX;
            turn_mode = 1; xil_printf("Ruota SX\r\n");
            break;

        case 'r': // Ruota DX
            dir_R = 0; dir_L = 1; speed_R = SPD_MAX; speed_L = SPD_MAX;
            turn_mode = 2; xil_printf("Ruota DX\r\n");
            break;

        // --- CURVE AVANZATE ---
        case 'q': // Wide Left
            dir_R = 1; dir_L = 1; speed_R = SPD_MAX; speed_L = SPD_MED;
            turn_mode = 1; xil_printf("Curva Larga SX\r\n");
            break;

        case 'e': // Wide Right
            dir_R = 1; dir_L = 1; speed_R = SPD_MED; speed_L = SPD_MAX;
            turn_mode = 2; xil_printf("Curva Larga DX\r\n");
            break;

        case 'z': // Sharp Left
            dir_R = 1; dir_L = 1; speed_R = SPD_MAX; speed_L = SPD_LOW;
            turn_mode = 1; xil_printf("Curva Stretta SX\r\n");
            break;

        case 'c': // Sharp Right
            dir_R = 1; dir_L = 1; speed_R = SPD_LOW; speed_L = SPD_MAX;
            turn_mode = 2; xil_printf("Curva Stretta DX\r\n");
            break;
    }
}

// -----------------------------------------------------------------------------
// ISR: DUAL TIMER HANDLER
// -----------------------------------------------------------------------------
void myISR(void) {
    // Leggi quale interrupt è scattato dal Controller
    unsigned p = *IISR;

    if (p & XPAR_AXI_TIMER_0_INTERRUPT_MASK) {

        // --------------------------------
        // GESTIONE TIMER 0: PWM (Veloce)
        // --------------------------------
        // Verifica se Timer 0 ha generato interrupt
        u32 csr_pwm = XTmrCtr_GetControlStatusReg(TMRCTR_BASEADDR, TIMER_PWM);
        if (csr_pwm & XTC_CSR_INT_OCCURED_MASK) {

            pwm_counter++;

            u32 pwm_bit_R = (pwm_counter < speed_R) ? 1 : 0;
            u32 pwm_bit_L = (pwm_counter < speed_L) ? 1 : 0;

            u32 motor_output = (pwm_bit_R << 0) | (dir_R << 1) |
                               (pwm_bit_L << 2) | (dir_L << 3);

            *motors_speed_dir_data = motor_output;

            // Pulisci flag interrupt SOLO del Timer PWM
            XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_PWM, csr_pwm | XTC_CSR_INT_OCCURED_MASK);
        }

        // --------------------------------
        // GESTIONE TIMER 1: BLINK (Lento)
        // --------------------------------
        // Verifica se Timer 1 ha generato interrupt
        u32 csr_blink = XTmrCtr_GetControlStatusReg(TMRCTR_BASEADDR, TIMER_BLINK);
        if (csr_blink & XTC_CSR_INT_OCCURED_MASK) {

            // Qui entriamo ogni 0.5 secondi (non serve divisore software)
            blink_state = !blink_state;
            UpdateTurnSignals();

            // Pulisci flag interrupt SOLO del Timer Blink
            XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_BLINK, csr_blink | XTC_CSR_INT_OCCURED_MASK);
        }

        // ACK al Controller Interrupt Generale
        *IIAR |= XPAR_AXI_TIMER_0_INTERRUPT_MASK;
    }
}

// Funzione helper per aggiornare i LED frecce
void UpdateTurnSignals(void) {
    if (turn_mode == 1) {
        // SX: Lampeggia Bit 0
        *leds_data = (blink_state) ? 0x1 : 0x0;
    }
    else if (turn_mode == 2) {
        // DX: Lampeggia Bit 1
        *leds_data = (blink_state) ? 0x2 : 0x0;
    }
    else {
        // Spento
        *leds_data = 0x0;
    }
}

// -----------------------------------------------------------------------------
// SETUP TIMER (DUAL CHANNEL)
// -----------------------------------------------------------------------------
int SetupTimer(void) {
    // 1. Setup Interrupt Controller
    *IER = XPAR_AXI_TIMER_0_INTERRUPT_MASK;
    *MER = 0x3;

    // 2. SETUP TIMER 0 (PWM - VELOCE)
    // ------------------------------------------
    // Reset e Configurazione Auto-Reload + IRQ + DownCount
    XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_PWM, 0);
    XTmrCtr_SetLoadReg(TMRCTR_BASEADDR, TIMER_PWM, PWM_PERIOD);
    XTmrCtr_LoadTimerCounterReg(TMRCTR_BASEADDR, TIMER_PWM);

    XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_PWM,
        XTC_CSR_AUTO_RELOAD_MASK | XTC_CSR_ENABLE_INT_MASK | XTC_CSR_DOWN_COUNT_MASK);

    // 3. SETUP TIMER 1 (BLINK - LENTO)
    // ------------------------------------------
    // Stessa configurazione, ma con Periodo molto più lungo
    XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_BLINK, 0);
    XTmrCtr_SetLoadReg(TMRCTR_BASEADDR, TIMER_BLINK, BLINK_PERIOD);
    XTmrCtr_LoadTimerCounterReg(TMRCTR_BASEADDR, TIMER_BLINK);

    XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_BLINK,
        XTC_CSR_AUTO_RELOAD_MASK | XTC_CSR_ENABLE_INT_MASK | XTC_CSR_DOWN_COUNT_MASK);

    // 4. Avvia entrambi i Timer
    XTmrCtr_Enable(TMRCTR_BASEADDR, TIMER_PWM);
    XTmrCtr_Enable(TMRCTR_BASEADDR, TIMER_BLINK);

    microblaze_enable_interrupts();
    return XST_SUCCESS;
}

// --- UART Helper ---
u32 UART_RecvByte(UINTPTR BaseAddress) {
    u32 status = XUartLite_GetStatusReg(BaseAddress);
    if (status & XUL_SR_RX_FIFO_VALID_DATA) {
        u32 data = XUartLite_ReadReg(BaseAddress, XUL_RX_FIFO_OFFSET);
        if ((u8)data == 0x0D || (u8)data == 0x0A) return NO_DATA;
        return data;
    }
    return NO_DATA;
}
