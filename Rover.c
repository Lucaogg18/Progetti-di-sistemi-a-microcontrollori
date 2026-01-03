#include "xparameters.h"
#include "xstatus.h"
#include "xtmrctr_l.h"
#include "xil_printf.h"
#include "xuartlite_l.h"
#include "xil_io.h"

// --- INDIRIZZI HARDWARE ---
// Qui diciamo al programma dove trovare le periferiche nella memoria della scheda
#ifndef SDT
    #define TMRCTR_BASEADDR     XPAR_TMRCTR_0_BASEADDR
    #define UART_BASEADDR       XPAR_UARTLITE_0_BASEADDR
    #define GPIO_MOTORS_BASE    XPAR_GPIO_MOTORS_BASEADDR
    #define GPIO_LEDS_BASE      0x40000000
#else
    #define TMRCTR_BASEADDR     XPAR_XTMRCTR_0_BASEADDR
    #define UART_BASEADDR       XPAR_XUARTLITE_0_BASEADDR
#endif

#define NO_DATA             0xFFFFFFFF

// --- CONFIGURAZIONE DEI DUE TIMER ---
#define TIMER_PWM           0  // Timer 0: Controlla la velocità dei motori (veloce)
#define TIMER_BLINK         1  // Timer 1: Controlla il lampeggio delle frecce (lento)

#define PWM_PERIOD          400       // Durata breve per dare potenza fluida ai motori
#define BLINK_PERIOD        50000000  // Durata lunga (mezzo secondo) per le frecce

// --- PUNTATORI AI PIN (GPIO) ---
// Variabili speciali che scrivono direttamente sui cavi fisici di LED e Motori
volatile int * leds_data = (volatile int *)(GPIO_LEDS_BASE + 0x00);
volatile int * leds_tri  = (volatile int *)(GPIO_LEDS_BASE + 0x04);

volatile int * motors_speed_dir_data = (volatile int *)(GPIO_MOTORS_BASE + 0x00);
volatile int * motors_speed_dir_tri  = (volatile int *)(GPIO_MOTORS_BASE + 0x04);
volatile int * motors_enable_data    = (volatile int *)(GPIO_MOTORS_BASE + 0x08);
volatile int * motors_enable_tri     = (volatile int *)(GPIO_MOTORS_BASE + 0x0C);

// Registri per gestire le interruzioni (il "campanello" del processore)
volatile int * IER  = (volatile int *)0x41200008;
volatile int * MER  = (volatile int *)0x4120001C;
volatile int * IISR = (volatile int *)0x41200000;
volatile int * IIAR = (volatile int *)0x4120000C;

// --- MEMORIA DI SISTEMA ---
volatile u8 pwm_counter = 0; // Conta ciclicamente per generare l'onda PWM
volatile u8 speed_R = 0;     // Velocità destra
volatile u8 speed_L = 0;     // Velocità sinistra
volatile u8 dir_R = 0;       // Direzione destra
volatile u8 dir_L = 0;       // Direzione sinistra

// Variabili per le frecce
volatile int blink_state = 0; // Stato della luce (accesa/spenta)
volatile int turn_mode = 0;   // Dove stiamo girando (0=dritto, 1=SX, 2=DX)

// Elenco delle funzioni usate
void myISR(void) __attribute__((interrupt_handler));
int SetupTimer(void);
u32 UART_RecvByte(UINTPTR BaseAddress);
void ProcessCommand(char cmd);
void UpdateTurnSignals(void);

// --- PROGRAMMA PRINCIPALE ---
int main(void) {
    int Status;
    u32 uart_input;

    xil_printf("Sistema Avviato. In attesa di comandi...\r\n");

    // Configura i pin come USCITA
    *leds_tri = 0x00;
    *motors_speed_dir_tri = 0x00;
    *motors_enable_tri = 0x00;

    // Spegne tutto all'inizio
    *leds_data = 0x00;
    *motors_speed_dir_data = 0x00;

    // Accende il chip dei motori
    *motors_enable_data = 0x01;

    // Prepara i timer (motori e frecce)
    Status = SetupTimer();
    if (Status != XST_SUCCESS) return XST_FAILURE;

    // Ciclo infinito: controlla sempre se arrivano comandi dalla tastiera
    while (1) {
        uart_input = UART_RecvByte(UART_BASEADDR);
        if (uart_input != NO_DATA) {
            ProcessCommand((char)uart_input);
        }
    }
    return XST_SUCCESS;
}

// --- GESTIONE DEI COMANDI ---
// Legge il tasto premuto e imposta velocità e direzione
void ProcessCommand(char cmd) {
    const u8 SPD_MAX = 150;
    const u8 SPD_MED = 100;
    const u8 SPD_LOW = 50;

    switch (cmd) {
        // Movimenti dritti
        case 'f': // Avanti
            dir_R = 1; dir_L = 1; speed_R = SPD_MAX; speed_L = SPD_MAX;
            turn_mode = 0; *leds_data = 0x0;
            break;

        case 'b': // Indietro
            dir_R = 0; dir_L = 0; speed_R = SPD_MAX; speed_L = SPD_MAX;
            turn_mode = 0; *leds_data = 0x0;
            break;

        case 's': // Stop
            speed_R = 0; speed_L = 0; dir_R = 0; dir_L = 0;
            turn_mode = 0; *leds_data = 0x0;
            break;

        // Rotazioni su se stesso (Pivot)
        case 'l': // Ruota a Sinistra
            dir_R = 1; dir_L = 0; speed_R = SPD_MAX; speed_L = SPD_MAX;
            turn_mode = 1; // Attiva freccia SX
            break;

        case 'r': // Ruota a Destra
            dir_R = 0; dir_L = 1; speed_R = SPD_MAX; speed_L = SPD_MAX;
            turn_mode = 2; // Attiva freccia DX
            break;

        // Curve Larghe (un motore veloce, uno medio)
        case 'q': 
            dir_R = 1; dir_L = 1; speed_R = SPD_MAX; speed_L = SPD_MED;
            turn_mode = 1; 
            break;

        case 'e': 
            dir_R = 1; dir_L = 1; speed_R = SPD_MED; speed_L = SPD_MAX;
            turn_mode = 2; 
            break;

        // Curve Strette (un motore veloce, uno lento)
        case 'z': 
            dir_R = 1; dir_L = 1; speed_R = SPD_MAX; speed_L = SPD_LOW;
            turn_mode = 1; 
            break;

        case 'c': 
            dir_R = 1; dir_L = 1; speed_R = SPD_LOW; speed_L = SPD_MAX;
            turn_mode = 2; 
            break;
    }
}

// --- GESTORE DELLE INTERRUZIONI (IL CUORE DEL SISTEMA) ---
// Questa funzione viene chiamata automaticamente dall'hardware
void myISR(void) {
    unsigned p = *IISR; // Controlla chi ha suonato il campanello

    if (p & XPAR_AXI_TIMER_0_INTERRUPT_MASK) {

        // --- CASO 1: È IL TIMER DEI MOTORI? (Veloce) ---
        u32 csr_pwm = XTmrCtr_GetControlStatusReg(TMRCTR_BASEADDR, TIMER_PWM);
        if (csr_pwm & XTC_CSR_INT_OCCURED_MASK) {

            pwm_counter++; // Incrementa il contatore

            // Decide se dare corrente al motore in questo istante
            u32 pwm_bit_R = (pwm_counter < speed_R) ? 1 : 0;
            u32 pwm_bit_L = (pwm_counter < speed_L) ? 1 : 0;

            // Invia il segnale ai motori
            u32 motor_output = (pwm_bit_R << 0) | (dir_R << 1) |
                               (pwm_bit_L << 2) | (dir_L << 3);
            *motors_speed_dir_data = motor_output;

            // Resetta l'avviso di questo timer
            XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_PWM, csr_pwm | XTC_CSR_INT_OCCURED_MASK);
        }

        // --- CASO 2: È IL TIMER DELLE FRECCE? (Lento) ---
        u32 csr_blink = XTmrCtr_GetControlStatusReg(TMRCTR_BASEADDR, TIMER_BLINK);
        if (csr_blink & XTC_CSR_INT_OCCURED_MASK) {

            // Inverte lo stato (se acceso spegne, se spento accende)
            blink_state = !blink_state;
            UpdateTurnSignals(); // Aggiorna i LED

            // Resetta l'avviso di questo timer
            XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_BLINK, csr_blink | XTC_CSR_INT_OCCURED_MASK);
        }

        // Conferma al processore di aver gestito l'evento
        *IIAR |= XPAR_AXI_TIMER_0_INTERRUPT_MASK;
    }
}

// Funzione ausiliaria per accendere il LED giusto
void UpdateTurnSignals(void) {
    if (turn_mode == 1) {
        *leds_data = (blink_state) ? 0x1 : 0x0; // Freccia SX
    }
    else if (turn_mode == 2) {
        *leds_data = (blink_state) ? 0x2 : 0x0; // Freccia DX
    }
    else {
        *leds_data = 0x0; // Tutto spento
    }
}

// --- SETUP INIZIALE DEI TIMER ---
int SetupTimer(void) {
    // Abilita il controller delle interruzioni
    *IER = XPAR_AXI_TIMER_0_INTERRUPT_MASK;
    *MER = 0x3;

    // Configura TIMER 0 (Motori)
    XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_PWM, 0);
    XTmrCtr_SetLoadReg(TMRCTR_BASEADDR, TIMER_PWM, PWM_PERIOD); // Imposta velocità alta
    XTmrCtr_LoadTimerCounterReg(TMRCTR_BASEADDR, TIMER_PWM);
    // Imposta modalità automatica e conto alla rovescia
    XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_PWM,
        XTC_CSR_AUTO_RELOAD_MASK | XTC_CSR_ENABLE_INT_MASK | XTC_CSR_DOWN_COUNT_MASK);

    // Configura TIMER 1 (Frecce)
    XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_BLINK, 0);
    XTmrCtr_SetLoadReg(TMRCTR_BASEADDR, TIMER_BLINK, BLINK_PERIOD); // Imposta velocità bassa
    XTmrCtr_LoadTimerCounterReg(TMRCTR_BASEADDR, TIMER_BLINK);
    // Imposta modalità automatica e conto alla rovescia
    XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_BLINK,
        XTC_CSR_AUTO_RELOAD_MASK | XTC_CSR_ENABLE_INT_MASK | XTC_CSR_DOWN_COUNT_MASK);

    // Avvia entrambi i timer
    XTmrCtr_Enable(TMRCTR_BASEADDR, TIMER_PWM);
    XTmrCtr_Enable(TMRCTR_BASEADDR, TIMER_BLINK);

    // Dà il via libera al processore
    microblaze_enable_interrupts();
    return XST_SUCCESS;
}

// --- LETTURA SERIALE ---
u32 UART_RecvByte(UINTPTR BaseAddress) {
    u32 status = XUartLite_GetStatusReg(BaseAddress);
    // Se c'è un dato valido nella coda...
    if (status & XUL_SR_RX_FIFO_VALID_DATA) {
        u32 data = XUartLite_ReadReg(BaseAddress, XUL_RX_FIFO_OFFSET);
        // Ignora i tasti "Invio"
        if ((u8)data == 0x0D || (u8)data == 0x0A) return NO_DATA;
        return data;
    }
    return NO_DATA;
}
