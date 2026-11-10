/**
 * @file display_reloj_3dig.c
 * @brief Implementación para mostrar los 3 últimos dígitos de la hora.
 * 
 * 
 * @details Agrega el desarrollo de la función DisplayRelojInit, 
 * que permite mostrar los tres últimos dígitos de la hora en un display de 3 dígitos, de 7 segmentos LED cada dígito. @n
 * 
 * Para más información sobre el conexionado e implementaciones, refiérase a bcd_LCD.h
 * 
 * @version 1.0
 * 
 * 
 *  * @section changelog Changelog
 * | Date | Description |
 * | :--- | :--- |
 * | 2026-09-20 | Document creation |
 * 
 * @author Guillermo Casco Miranda
 */
/*==================[inclusions]=============================================*/
#include "display_reloj_3dig.h"
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "timer_mcu.h"   // Se reemplaza esp_timer.h por el driver de la cátedra
/*==================[macros and definitions]=================================*/

/*==================[internal data declaration]==============================*/

/*==================[internal functions declaration]=========================*/
/**
 * @brief Función de callback ejecutada por el timer de hardware cada 1 segundo.
 */
static void timer_1hz_callback(void* arg);


/*==================[internal data definition]===============================*/
// Variables de tiempo internas (encapsuladas en la librería)
static volatile uint32_t segundosTotales = 0;
static volatile uint32_t minutosTotales = 0;

// Configuración de los pines del bus BCD
static gpioConf_t gpio_bcd_bus[4] = {
    {GPIO_20, GPIO_OUTPUT}, // b0 LSB
    {GPIO_21, GPIO_OUTPUT}, // b1
    {GPIO_22, GPIO_OUTPUT}, // b2
    {GPIO_23, GPIO_OUTPUT}  // b3 MSB
};

// Configuración de los pines de selección (Latch)
static gpioConf_t gpio_digit_select[3] = {
    {GPIO_19, GPIO_OUTPUT}, // Latch Dígito 1 LSD
    {GPIO_18, GPIO_OUTPUT}, // Latch Dígito 2
    {GPIO_9,  GPIO_OUTPUT}  // Latch Dígito 3 MSD
};


/*==================[external data definition]===============================*/
// Variable global accesible desde main
volatile uint32_t my_number = 0;


/*==================[internal functions definition]==========================*/
static void timer_1hz_callback(void* arg) {
    segundosTotales++;
    if (segundosTotales == 60) {
        segundosTotales = 0;
        minutosTotales++;
    }
    
    // Calcula el número a mostrar uniendo los minutos (centenas) y los segundos
    uint16_t min_unidades = minutosTotales % 10;
    my_number = (min_unidades * 100) + segundosTotales;
    
    // Refresca el display
    lcdDisplay(my_number, DIGITS_COUNT, gpio_bcd_bus, gpio_digit_select);
}


/*==================[external functions definition]==========================*/
void DisplayRelojInit(void) {
    // 1. Inicialización del hardware GPIO de datos BCD
    for (uint8_t i = 0; i < 4; i++) {
        GPIOInit(gpio_bcd_bus[i].pin, gpio_bcd_bus[i].dir);
    }
    
    // 2. Inicialización del hardware GPIO de selección de dígitos
    for (uint8_t i = 0; i < DIGITS_COUNT; i++) {
        GPIOInit(gpio_digit_select[i].pin, gpio_digit_select[i].dir);
    }

    // 3. Configuración y arranque del Timer de hardware usando la abstracción de la cátedra
    timer_config_t timer_reloj = {
        .timer = TIMER_A,
        .period = 1000000, // 1.000.000 microsegundos = 1 Hz
        .func_p = timer_1hz_callback,
        .param_p = NULL
    };
    
    TimerInit(&timer_reloj);
    TimerStart(timer_reloj.timer);
}


/*==================[end of file]============================================*/
