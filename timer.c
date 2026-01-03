#include "xstatus.h"
#include "xtmrctr_l.h"
#include "xil_printf.h"
#include "xparameters.h"

// Configurazione indirizzo base del Timer a seconda dell'ambiente (SDT o standard)
#ifndef SDT
#define TMRCTR_BASEADDR		XPAR_TMRCTR_0_BASEADDR
#else
#define TMRCTR_BASEADDR		XPAR_XTMRCTR_0_BASEADDR
#endif

#define TIMER_COUNTER_0	 0

// --- Memory Mapped I/O Pointers ---
// Puntatori diretti agli indirizzi fisici delle periferiche (GPIO e Interrupt Controller)
volatile int * gpio_0_data = (volatile int *)0x40000000; // Pointer to GPIO 0 Data register (e.g., LEDs)
volatile int * gpio_1_data = (volatile int *)0x40010000; // Pointer to GPIO 1 Data register

// Interrupt Controller (INTC) Registers
volatile int * IER = (volatile int*)	0x41200008; // Interrupt Enable Register
volatile int * MER = (volatile int*)	0x4120001C; // Master Enable Register

volatile int * IISR = (volatile int*)	0x41200000; // Interrupt Status Register (Read pending interrupts)
volatile int * IIAR = (volatile int*)	0x4120000C; // Interrupt Acknowledge Register

// Global Interrupt Registers for specific IP blocks (likely GPIOs based on addresses)
volatile int * GGIER_1 = (volatile int*)	0x4006011C;
volatile int * GIER_1 = (volatile int*)	0x40060128;
volatile int * GISR_1 = (volatile int*)	0x40060120;

volatile int * GGIER_2 = (volatile int*)	0x4005011C;
volatile int * GIER_2 = (volatile int*)	0x40050128;
volatile int * GISR_2 = (volatile int*)	0x40050120;

// Dichiarazione della funzione ISR con attributo specifico per MicroBlaze
// Questo dice al compilatore di usare l'istruzione di ritorno da interrupt (rtid) invece di un return normale
void myISR(void) __attribute__((interrupt_handler));

int TmrCtrLowLevelExample(UINTPTR TmrCtrBaseAddress, u8 TimerCounter);

int main(void)
{
	int Status;

	/*
	 * Setup dell'Interrupt Controller (INTC)
	 */
    // Abilita la linea di interrupt specifica per il Timer
    *IER = XPAR_AXI_TIMER_0_INTERRUPT_MASK; 

    // Abilita il Master Enable dell'Interrupt Controller
    // 0x3 = 0b00...0011 -> Imposta i bit MER (Master Enable) e HIE (Hardware Interrupt Enable)
    *MER = 0x3; 

    // Abilita gli interrupt a livello di CPU MicroBlaze (bit IE nel registro MSR)
    microblaze_enable_interrupts();

    // Configura e avvia il Timer
	Status = TmrCtrLowLevelExample(TMRCTR_BASEADDR, TIMER_COUNTER_0);
	if (Status != XST_SUCCESS) {
		xil_printf("Tmrctr lowlevel Example Failed\r\n");
		return XST_FAILURE;
	}

    // Loop infinito. Il resto dell'esecuzione avviene all'interno di myISR quando il timer scade.
	while(1);

	xil_printf("Successfully ran Tmrctr lowlevel Example\r\n");
	return XST_SUCCESS;
}

int TmrCtrLowLevelExample(UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber)
{
	u32 Value1;
	u32 Value2;
	u32 ControlStatus;

    // 1. Imposta la modalità del Timer nel Control Status Register (CSR)
    // XTC_CSR_AUTO_RELOAD_MASK: Il timer riparte automaticamente dopo aver raggiunto 0
    // XTC_CSR_ENABLE_INT_MASK: Il timer genererà un interrupt quando raggiunge 0
    // XTC_CSR_DOWN_COUNT_MASK: Il timer conta alla rovescia
    XTmrCtr_SetControlStatusReg(TmrCtrBaseAddress, TmrCtrNumber, XTC_CSR_AUTO_RELOAD_MASK|XTC_CSR_ENABLE_INT_MASK|XTC_CSR_DOWN_COUNT_MASK);

    // 2. Imposta il valore di caricamento (Load Register)
    // 100,000,000 tick. Se il clock è 100MHz, questo corrisponde a 1 secondo.
    XTmrCtr_SetLoadReg(TmrCtrBaseAddress, TmrCtrNumber, 100000000);

    // 3. Carica il valore dal Load Register al Counter Register effettivo
	XTmrCtr_LoadTimerCounterReg(TmrCtrBaseAddress, TmrCtrNumber);

    // 4. Rimuove il bit di 'Load' dal registro di controllo.
    // Se non si fa questo, il timer continuerebbe a ricaricarsi forzatamente invece di contare.
    ControlStatus = XTmrCtr_GetControlStatusReg(TmrCtrBaseAddress, TmrCtrNumber);
	XTmrCtr_SetControlStatusReg(TmrCtrBaseAddress, TmrCtrNumber, ControlStatus & (~XTC_CSR_LOAD_MASK));

    // Legge il valore corrente (opzionale, per debug)
    Value1 = XTmrCtr_GetTimerCounterReg(TmrCtrBaseAddress, TmrCtrNumber);

    // 5. Abilita il timer. Da ora inizia a contare alla rovescia.
	XTmrCtr_Enable(TmrCtrBaseAddress, TmrCtrNumber);

	return XST_SUCCESS;
}

// Routine di servizio dell'interrupt (eseguita quando scatta l'interrupt hardware)
void myISR(void)
{
    // Legge lo stato dell'Interrupt Controller per capire chi ha chiamato
    unsigned p = *IISR; 
    
    // Verifica se l'interrupt è stato causato dall'AXI Timer
    if (p & XPAR_AXI_TIMER_0_INTERRUPT_MASK) {
        
        // --- 1. Pulisce l'interrupt sulla periferica (Timer) ---
        // Bisogna dire al Timer "ho visto l'interrupt", altrimenti continua a tenerlo alto.
        int ControlStatus = XTmrCtr_GetControlStatusReg(TMRCTR_BASEADDR, 0);
        XTmrCtr_SetControlStatusReg(TMRCTR_BASEADDR, 0, ControlStatus | (XTC_CSR_INT_OCCURED_MASK));

        // --- 2. Pulisce l'interrupt sul Controller (INTC) ---
        // Scrive nel registro Acknowledge (IIAR) per confermare la gestione
        *IIAR |= XPAR_AXI_TIMER_0_INTERRUPT_MASK;

        // --- 3. Azione utente: Toggle GPIO ---
        // Legge il valore all'indirizzo 0x40000000 (GPIO LEDs), ne inverte i bit (~) e riscrive.
        // Questo fa lampeggiare i LED.
        *((int*)0x40000000) = ~(*((int*)0x40000000));
    }
}

