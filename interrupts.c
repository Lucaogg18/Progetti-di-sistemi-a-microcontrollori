#include <stdio.h>
#include "platform.h"
#include "xil_printf.h"
#include "xio.h"

// ASSEGNAZIONI REGISTRI INTERRUPT INTERNO
volatile int * gpio_0_data = (volatile int*) 0x40000000; // Output (es. LED Tasto 1, 0x1)
volatile int * IER = (volatile int*) 0x41200008; // INTC Interrupt Enable Register
volatile int * MER = (volatile int*) 0x4120001C; // INTC Master Enable Register
volatile int * IISR = (volatile int*) 0x41200000; // INTC Interrupt Status Register (usato per snapshot)
volatile int * IIAR = (volatile int*) 0x4120000C; // INTC Interrupt Acknowledge Register

// Registri GPIO Interrupt del tasto esistente (base 0x40060000)
volatile int * GGIER_1 = (volatile int*) 0x4006011C;
volatile int * GIER_1_IPIER = (volatile int*) 0x40060128; // Usato come IPIER
volatile int * GISR_1_IPISR = (volatile int*) 0x40060120; // Usato come IPISR
volatile int * GPIO1_TRI_REG = (volatile int*) (0x40060004);

// NUOVE ASSEGNAZIONI REGISTRI (TASTO ESTERNO - base 0x40050000)
volatile int * GGIER_2 = (volatile int*) 0x4005011C; // GIER per GPIO 2
volatile int * IPIER_2 = (volatile int*) 0x40050128; // IPIER per GPIO 2
volatile int * IPISR_2 = (volatile int*) 0x40050120; // IPISR per GPIO 2
volatile int * GPIO2_TRI_REG = (volatile int*) (0x40050004); // GPIO TRI Register offset 0x4




void myISR(void) __attribute__((interrupt_handler));




int main(void)
{
    // 1) Set inputs (TRI=1s)
    *GPIO1_TRI_REG = 0xFFFFFFFF; // Tasto esistente
    *GPIO2_TRI_REG = 0xFFFFFFFF; // Tasto esterno

    // 2) Enable device interrupts
    // Tasto esistente (GPIO 1)
    *GGIER_1 = 0x80000000; // Abilita GIER (bit 31)
    *GIER_1_IPIER = 0x1;  // Abilita IPIER, Canale 1 (bit 0)

    // TASTO ESTERNO (GPIO 2)
    *GGIER_2 = 0x80000000; // Abilita GIER per il nuovo GPIO
    *IPIER_2 = 0x1;        // Abilita IPIER, Canale 1 (bit 0)

    // 3) Enable INTC lines (match wiring!)
    // Abilita entrambi gli interrupt: IRQ0 (esistente) e IRQ1 (nuovo tasto)
    *IER = XPAR_BUTTON_IP2INTC_IRPT_MASK | XPAR_GPIO_IP2INTC_IRPT_MASK;

    // Abilita INTC Master Enable e Hardware Interrupt Enable
    *MER = 0x3; // 0b11 (ME | HIE)

    microblaze_enable_interrupts(); // Abilita gli interrupt globali del MicroBlaze

    while (1) {
        // Logica di attesa in loop infinito
    }
}


void myISR(void)
{
    unsigned p = *IISR; // Snapshot dello stato INTC (IRQ attivi)

    // 1. GESTIONE INTERRUPT TASTO ESISTENTE (IRQ0)
    if (p & XPAR_BUTTON_IP2INTC_IRPT_MASK) {

        // Azione: Toggle del bit 0 (LED 1)
        *gpio_0_data |= 0x1;

        // Acknowledge e Clear
        *GISR_1_IPISR = 0x1; // Clear IPISR del dispositivo (TOW)
        *IIAR = XPAR_BUTTON_IP2INTC_IRPT_MASK; // Acknowledge INTC (IRQ0)


    }

    // 2. GESTIONE INTERRUPT NUOVO TASTO ESTERNO (IRQ1)
    if (p & XPAR_GPIO_IP2INTC_IRPT_MASK) {

        // Azione: Toggle del bit 1 (LED 2)
        *gpio_0_data |= 0x1;

        // Acknowledge e Clear
        *IPISR_2 = 0x1; // Clear IPISR del nuovo GPIO (TOW)
        *IIAR = XPAR_GPIO_IP2INTC_IRPT_MASK; // Acknowledge INTC (IRQ1)


    }
}
