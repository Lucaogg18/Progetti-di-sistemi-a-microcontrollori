#include "xparameters.h"
#include "xstatus.h"
#include "xtmrctr_l.h"
#include "xil_printf.h"
#include "mb_interface.h"

// --- MAPPATURA INDIRIZZI HARDWARE ---
// Questi puntatori collegano il codice C ai pin fisici della scheda (GPIO)
volatile int *LED_DATA = (volatile int *)0x40000000; // Registro per scrivere sui LED
volatile int *LED_TRI  = (volatile int *)0x40000004; // Registro per configurare LED (In/Out)

volatile int *BTN_LEFT_DATA = (volatile int *)0x40060000; // Dati pulsante Sinistro
volatile int *BTN_LEFT_TRI  = (volatile int *)0x40060004; // Configurazione pulsante Sinistro

volatile int *BTN_RIGHT_DATA = (volatile int *)0x40050000; // Dati pulsante Destro
volatile int *BTN_RIGHT_TRI  = (volatile int *)0x40050004; // Configurazione pulsante Destro

// --- REGISTRI INTERRUZIONI (Interrupt Controller) ---
volatile int *IER  = (volatile int *)0x41200008; // Interrupt Enable Register
volatile int *MER  = (volatile int *)0x4120001C; // Master Enable Register
volatile int *IISR = (volatile int *)0x41200000; // Interrupt Status Register
volatile int *IIAR = (volatile int *)0x4120000C; // Interrupt Acknowledge Register

// --- COSTANTI DEL TIMER ---
#ifndef TMRCTR_BASEADDR
#define TMRCTR_BASEADDR XPAR_TMRCTR_0_BASEADDR
#endif
#define TIMER_COUNTER_0 0
// Valore di reset: 50 milioni di cicli. Se la CPU va a 100MHz, questo crea un ritardo di 0.5 secondi.
#define TIMER_RESET_VALUE 50000000 

// Variabile globale che cambia valore (0 o 1) ogni volta che il timer scatta (usata per il lampeggio)
volatile int blink_state = 0;

// Stati per la gestione del click del pulsante
typedef enum { STATE_IDLE, STATE_PRESSED} debounce_state_t;
// Stati del sistema "Frecce Auto": Centro (spento), Sinistra, Destra
typedef enum { CAR_CENTER, CAR_LEFT, CAR_RIGHT } car_state_t;

// Prototipi delle funzioni
void myISR(void) __attribute__((interrupt_handler)); // Funzione chiamata dall'hardware in automatico
int SetupTimer(void);
int FSM_Debounce(volatile int *port_address, debounce_state_t *current_state);

int main(void)
{
    int status;
    car_state_t carState = CAR_CENTER; // Stato iniziale: frecce spente

    // Stati indipendenti per i due pulsanti
    debounce_state_t dbStateLeft = STATE_IDLE;
    debounce_state_t dbStateRight = STATE_IDLE;

    int trigger_left = 0;
    int trigger_right = 0;

    // Configurazione GPIO: 0 = Output (LED), 1 = Input (Pulsanti)
    *LED_TRI = 0x0;
    *LED_DATA = 0x0; // Spegne tutti i LED all'avvio

    *BTN_LEFT_TRI = 0xFFFFFFFF;  // Input
    *BTN_RIGHT_TRI = 0xFFFFFFFF; // Input

    // Configura e avvia il timer hardware
    status = SetupTimer();
    if (status != XST_SUCCESS) {
        xil_printf("Errore Setup Timer\r\n");
        return XST_FAILURE;
    }

    // --- CICLO INFINITO (Loop Principale) ---
    while(1) {
        // Legge i pulsanti ed elimina i rimbalzi (debounce).
        // Restituisce 1 solo nell'istante in cui il pulsante viene rilasciato.
        trigger_left  = FSM_Debounce(BTN_LEFT_DATA, &dbStateLeft);
        trigger_right = FSM_Debounce(BTN_RIGHT_DATA, &dbStateRight);

        // Macchina a Stati per la logica delle frecce
        
        switch (carState) {
            
            // CASO 1: Nessuna freccia attiva
            case CAR_CENTER:
                *LED_DATA = 0x0; // Tutto spento

                if (trigger_left) {
                    carState = CAR_LEFT; // Passa allo stato Sinistra
                    xil_printf("Azione: FRECCIA SX\r\n");
                } else if (trigger_right) {
                    carState = CAR_RIGHT; // Passa allo stato Destra
                    xil_printf("Azione: FRECCIA DX\r\n");
                }
                break;

            // CASO 2: Freccia Sinistra attiva
            case CAR_LEFT:
                // Se premo di nuovo sinistra, spengo tutto (torno al centro)
                if (trigger_left) {
                    carState = CAR_CENTER;
                    xil_printf("Azione: OFF\r\n");
                    *LED_DATA = 0x0;
                }
                else {
                    // Gestione lampeggio: se blink_state è 1 accendo il bit 1 (0x2), altrimenti spengo
                    if (blink_state) *LED_DATA = 0x2; 
                    else             *LED_DATA = 0x0;
                }
                break;

            // CASO 3: Freccia Destra attiva
            case CAR_RIGHT:
                // Se premo di nuovo destra, spengo tutto (torno al centro)
                if (trigger_right) {
                    carState = CAR_CENTER;
                    xil_printf("Azione: OFF\r\n");
                    *LED_DATA = 0x0;
                }
                else {
                    // Gestione lampeggio: se blink_state è 1 accendo il bit 0 (0x1), altrimenti spengo
                    if (blink_state) *LED_DATA = 0x1;
                    else             *LED_DATA = 0x0;
                }
                break;
        }
    }
    return 0;
}

// --- FUNZIONE DEBOUNCE (Antirimbalzo) ---
// Serve a leggere il pulsante in modo pulito. 
// Restituisce 1 (trigger) solo quando il pulsante viene RILASCIATO.
int FSM_Debounce(volatile int *port_address, debounce_state_t *state) {
    int button_val = (*port_address & 0x1); // Legge il bit 0 del registro
    int trigger = 0;

    switch (*state) {
        case STATE_IDLE:
            // Se il pulsante viene premuto, passa allo stato PRESSED
            if (button_val != 0) {
                *state = STATE_PRESSED;
                trigger = 0;
            }
            break;

        case STATE_PRESSED:
            // Se il pulsante torna a 0 (rilasciato), torna IDLE e segnala il click (trigger = 1)
            if (button_val == 0) {
                *state = STATE_IDLE;
                trigger = 1;
            }
            break;
    }
    return trigger;
}

// --- CONFIGURAZIONE TIMER ---
int SetupTimer(void) {
    // Abilita interruzioni globali
    *IER = XPAR_AXI_TIMER_0_INTERRUPT_MASK;
    *MER = 0x3;

    // Imposta il timer: resetta, carica il valore 50.000.000
    XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_COUNTER_0, 0);
    XTmrCtr_SetLoadReg(TMRCTR_BASEADDR, TIMER_COUNTER_0, TIMER_RESET_VALUE);
    XTmrCtr_LoadTimerCounterReg(TMRCTR_BASEADDR, TIMER_COUNTER_0);

    // Avvia il timer in modalità: Auto-Reload (riparte da solo), Interrupt Abilitati, Conto alla rovescia
    XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_COUNTER_0,
        XTC_CSR_AUTO_RELOAD_MASK | XTC_CSR_ENABLE_INT_MASK | XTC_CSR_DOWN_COUNT_MASK);

    XTmrCtr_Enable(TMRCTR_BASEADDR, TIMER_COUNTER_0);
    
    // Abilita interruzioni sulla CPU MicroBlaze
    microblaze_enable_interrupts();

    return XST_SUCCESS;
}

// --- GESTORE INTERRUZIONI (ISR) ---
// Questa funzione viene eseguita ogni volta che il timer arriva a zero (ogni 0.5s circa)
void myISR(void) {
    unsigned p = *IISR; // Legge chi ha causato l'interruzione

    if (p & XPAR_AXI_TIMER_0_INTERRUPT_MASK) {
        // Pulisce il flag dell'interruzione hardware (per permettere future interruzioni)
        u32 ControlStatus = XTmrCtr_GetControlStatusReg(TMRCTR_BASEADDR, TIMER_COUNTER_0);
        XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, TIMER_COUNTER_0,
            ControlStatus | XTC_CSR_INT_OCCURED_MASK);

        // Comunica all'Interrupt Controller che l'abbiamo gestita
        *IIAR = XPAR_AXI_TIMER_0_INTERRUPT_MASK;
        
        // Inverte lo stato del lampeggio (0 -> 1 oppure 1 -> 0)
        blink_state = !blink_state;
    }
}
