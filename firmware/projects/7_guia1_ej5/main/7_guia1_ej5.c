/**
 * @mainpage Guía de Trabajos Prácticos 1 - Ejercicio 5
 * 
 * 
 * @section genDesc General Description
 * Aplicación principal para la ESP32,
 * que prueba el mapeo de BCD a GPIO.
 * 
 * 
 * @file 7_guia1_ej5.c
 * @brief Aplicación principal del Ejercicio 5 de la Guía de Trabajos Prácticos 1.
 * @details 
 * Aplicación principal para la ESP32. @n
 * 
 * Inicializa un bus de GPIOs, y prueba el mapeo de BCD a GPIO. 
 * Id est, los estados de esos 4 dígitos BCD de un número, 
 * ponen en alto o bajo, cada 4 segundos, 
 * los estados de 4 pines GPIO de salida, específicos, de la ESP32.
 * 
 * 
 * @section changelog Changelog
 * | Date | Description |
 * | :--- | :--- |
 * | 2026-09-20 | Document creation |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 * @copyright FIUNER - UNER
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_mcu.h"
#include "bcd_LCD.h"

/*==================[macros and definitions]=================================*/
#define DELAY_MS 4000     // valor solo con fines de probar la función de este main

/*==================[internal data definition]===============================*/

/*==================[internal functions declaration]=========================*/

/*==================[external functions definition]==========================*/
void app_main(void) {
    /* 
     * Se define el vector que mapea los bits según la consigna.
     */
    gpioConf_t gpio_bcd_bus[4] = {
        {GPIO_20, GPIO_OUTPUT}, // asociado a b0 LSB
        {GPIO_21, GPIO_OUTPUT}, // asociado a b1
        {GPIO_22, GPIO_OUTPUT}, // asociado a b2
        {GPIO_23, GPIO_OUTPUT}  // asociado a b3 MSB
    };
    
    // Inicialización del hardware GPIO previo a su uso
    for (uint8_t i = 0; i < 4; i++) {
        GPIOInit(gpio_bcd_bus[i].pin, gpio_bcd_bus[i].dir);
    }

    uint8_t digito_prueba = 0;

    while (1) {
        printf("Escribiendo el dígito BCD %d en los pines GPIO...\n", digito_prueba);
        
        // Llamada a la capa de dispositivo
        bcdToGpio(digito_prueba, gpio_bcd_bus);
        
        // Verificación por software (impresión teórica de los bits)
        for (uint8_t i = 0; i < 4; i++){
            // Usamos un desplazamiento de bits para imprimir cómo debería estar cada bit
            uint8_t estado_bit = (digito_prueba & (1 << i)) ? 1 : 0;
            printf("Bus b%d (Pin %d): %d\n", i, gpio_bcd_bus[i].pin, estado_bit);
        }
        printf("\n");

        // Incrementamos el dígito y reseteamos al llegar a 10 (solo dígitos válidos BCD)
        digito_prueba++;
        if (digito_prueba > 9) {
            digito_prueba = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(DELAY_MS));
    }
}


/*==================[end of file]============================================*/