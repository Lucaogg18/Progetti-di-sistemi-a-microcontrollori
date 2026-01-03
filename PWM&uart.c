#include "xstatus.h"
#include "platform.h"
#include "xtmrctr_l.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xuartlite_l.h"
#include "xil_io.h"

// Seleziona indirizzi base a seconda della piattaforma
#ifndef SDT
    #define TMRCTR_BASEADDR		XPAR_TMRCTR_0_BASEADDR
    #define UART_BASEADDR       XPAR_UARTLITE_0_BASEADDR
#else
    #define TMRCTR_BASEADDR		XPAR_XTMRCTR_0_BASEADDR
    #define UART_BASEADDR       XPAR_XUARTLITE_0_BASEADDR
#endif

#define TIMER_COUNTER_0	 0
#define NO_DATA          0xFFFFFFFF // Valore per indicare nessun dato ricevuto

// Puntatori diretti ai registri fisici per GPIO (Pulsanti e LED RGB)
volatile int * gpio_buttons_tri  = (volatile int *)0x40060004; 
volatile int * gpio_rgb_data     = (volatile int *)0x40000008;

// Puntatori diretti ai registri del controller delle interruzioni (INTC)
volatile int * IER = (volatile int*)0x41200008; // Abilitazione interrupt
volatile int * MER = (volatile int*)0x4120001C; // Master Enable
volatile int * IISR = (volatile int*)0x41200000; // Stato interrupt
volatile int * IIAR = (volatile int*)0x4120000C; // Acknowledge (conferma)

// Variabili globali per la gestione PWM e colori
volatile u8 pwm_counter = 0; // Contatore ciclico 0-255
volatile u32 sys_time = 0;   // Tempo di sistema per debounce

volatile u8 duty_R = 0; // Luminosità Rosso
volatile u8 duty_G = 0; // Luminosità Verde
volatile u8 duty_B = 0; // Luminosità Blu

// Prototipi delle funzioni
void myISR(void) __attribute__((interrupt_handler)); // Gestore interruzioni
int TmrCtrLowLevelExample(UINTPTR TmrCtrBaseAddress, u8 TimerCounter);
u32 my_XUartLite_RecvByte(UINTPTR BaseAddress);
void update_leds(u32 data, u8 mode);

int main(void){
	init_platform();

	int Status;
    u32 uart_input;
    u32 button_input;

    // Reset colori iniziali
    duty_R = 0; duty_G = 0; duty_B = 0;

    // Imposta direzione pulsanti come INPUT
    *gpio_buttons_tri = 0xFFFFFFFF;

    // Configurazione manuale Interrupt Controller
    *IER = XPAR_AXI_TIMER_0_INTERRUPT_MASK; // Abilita interrupt del timer
    *MER = 0x3; // Abilita uscita hardware interrupt
    microblaze_enable_interrupts(); // Abilita interrupt CPU

    // Inizializza e avvia il Timer Hardware
	Status = TmrCtrLowLevelExample(TMRCTR_BASEADDR, TIMER_COUNTER_0);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
    }

	while(1) {
        // Legge stato pulsanti
        button_input = Xil_In32(XPAR_GPIO_5_BASEADDR);
        if(button_input)
			update_leds(button_input, 0); // Modalità 0 = Pulsante

        // Legge dati dalla seriale (UART)
        uart_input = my_XUartLite_RecvByte(UART_BASEADDR);
        if (uart_input != NO_DATA) {
            update_leds(uart_input, 1); // Modalità 1 = UART
        }
    }

	return XST_SUCCESS;
}

// Funzione per aggiornare i colori dei LED
void update_leds(u32 data, u8 mode)
{
    static u32 last_debounce_time = 0;
    static int seq_index = 2; 

    u32 debounce_delay = 125000; // Ritardo per antirimbalzo

    // Se premuto pulsante (Mode 0)
    if (mode == 0) {
        // Controllo debounce temporale
        if (sys_time - last_debounce_time > debounce_delay) {
            
            // Cambia colore in sequenza
            seq_index++;
            if (seq_index > 2) {
                seq_index = 0;
            }

            if (seq_index == 0) {
                duty_R = 255; duty_G = 0; duty_B = 0; // Rosso
            }
            else if (seq_index == 1) {
                duty_R = 0; duty_G = 255; duty_B = 0; // Verde
            }
            else {
                duty_R = 0; duty_G = 0; duty_B = 255; // Blu
            }

            last_debounce_time = sys_time;
        }
    }
    // Se comando da UART (Mode 1)
    else {
        switch ((char)data) {
            case '1': duty_R = 255; duty_G = 0;   duty_B = 0;   break; // Rosso
            case '2': duty_R = 0;   duty_G = 255; duty_B = 0;   break; // Verde
            case '3': duty_R = 0;   duty_G = 0;   duty_B = 255; break; // Blu
            case '4': duty_R = 255; duty_G = 255; duty_B = 0;   break; // Giallo
            case '5': duty_R = 0;   duty_G = 255; duty_B = 255; break; // Ciano
            case '6': duty_R = 255; duty_G = 0;   duty_B = 255; break; // Magenta
            case '7': duty_R = 255; duty_G = 255; duty_B = 255; break; // Bianco
            case '8': duty_R = 255; duty_G = 128; duty_B = 0;   break; // Arancio
            case '9': duty_R = 128; duty_G = 0;   duty_B = 128; break; // Viola
            case '0': duty_R = 0;   duty_G = 0;   duty_B = 0;   break; // Spento
            default: break;
        }
    }
}

// Funzione di lettura UART a basso livello
u32 my_XUartLite_RecvByte(UINTPTR BaseAddress) 
{ 
    u32 status = XUartLite_GetStatusReg(BaseAddress);
    // Verifica se c'è un dato valido nel buffer FIFO
    if (status & XUL_SR_RX_FIFO_VALID_DATA) {
        u32 data = XUartLite_ReadReg(BaseAddress, XUL_RX_FIFO_OFFSET);
        // Ignora invio/a capo
        if ((u8)data == 0x0D || (u8)data == 0x0A) return NO_DATA;
        return data;
    }
    return NO_DATA;
} 

// Configurazione Timer Hardware
int TmrCtrLowLevelExample(UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber)
{
	u32 ControlStatus;
    // Setup: Auto-Reload, Interrupt abilitati, Conteggio a scendere
    XTmrCtr_SetControlStatusReg(TmrCtrBaseAddress, TmrCtrNumber, XTC_CSR_AUTO_RELOAD_MASK|XTC_CSR_ENABLE_INT_MASK|XTC_CSR_DOWN_COUNT_MASK);
    XTmrCtr_SetLoadReg(TmrCtrBaseAddress, TmrCtrNumber, 400); // Imposta periodo timer (frequenza PWM)
	XTmrCtr_LoadTimerCounterReg(TmrCtrBaseAddress, TmrCtrNumber);
    
    // Avvia il timer
    ControlStatus = XTmrCtr_GetControlStatusReg(TmrCtrBaseAddress, TmrCtrNumber);
    XTmrCtr_SetControlStatusReg(TmrCtrBaseAddress, TmrCtrNumber, ControlStatus & (~XTC_CSR_LOAD_MASK));
	XTmrCtr_Enable(TmrCtrBaseAddress, TmrCtrNumber);
	return XST_SUCCESS;
}

// Interrupt Service Routine (Eseguita a ogni tick del timer)
void myISR(void)
{
    unsigned p = *IISR;
    // Controlla se l'interrupt arriva dal Timer 0
    if (p & XPAR_AXI_TIMER_0_INTERRUPT_MASK) {

        pwm_counter++; // Incrementa fase PWM
        sys_time++;    // Incrementa tempo sistema

        // Calcola se accendere o spegnere ogni colore (Logica PWM)
        u32 r_bit = (pwm_counter < duty_R) ? 0 : 1;
        u32 g_bit = (pwm_counter < duty_G) ? 0 : 1;
        u32 b_bit = (pwm_counter < duty_B) ? 0 : 1;

        // Invia i bit ai LED RGB
        u32 rgb_output = (r_bit << 2) | (g_bit << 1) | (b_bit << 0);
        *gpio_rgb_data = rgb_output;

        // Pulisce il flag di interrupt per permettere il prossimo
        int ControlStatus = XTmrCtr_GetControlStatusReg(TMRCTR_BASEADDR, 0);
        XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, 0, ControlStatus | (XTC_CSR_INT_OCCURED_MASK));
        *IIAR |= XPAR_AXI_TIMER_0_INTERRUPT_MASK;
    }
}
