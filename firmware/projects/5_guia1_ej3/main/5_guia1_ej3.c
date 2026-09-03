/*! @mainpage Template
 *
 * @section genDesc General Description
 *
 * Ejercicio 3 - Control de LEDs mediante estructura y puntero.
 *
 * @section hardConn Hardware Connection
 *
 * |    Peripheral  |   ESP32       |
 * |:--------------:|:--------------|
 * |    LED_1       |   GPIO_X      |
 * |    LED_2       |   GPIO_X      |
 * |    LED_3       |   GPIO_X      |
 *
 * @section changelog Changelog
 *
 * |    Date     | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 12/09/2023 | Document creation                              |
 *
 * @author Albano Peñalva (albano.penalva@uner.edu.ar)
 *
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "switch.h"

/*==================[macros and definitions]=================================*/
#define CONFIG_BLINK_PERIOD 100 // Base de tiempo de 100 ms

#define ON     1
#define OFF    2
#define TOGGLE 3

/*==================[internal data definition]===============================*/
struct leds
{
    uint8_t mode;       // ON, OFF, TOGGLE.
    uint8_t n_leds;     // número de leds.
    uint8_t n_ciclos;   // cantidad de encendido/apagado.
    uint16_t periodo;   // tiempo para cada ciclo en ms.
} my_leds;

/*==================[internal functions declaration]=========================*/
void ControlLeds(struct leds *ptr_led);

/*==================[external functions definition]==========================*/

/**
 * @brief Función que controla los LEDs de acuerdo a la configuración pasándole un puntero a la estructura.
 * @param ptr_led Puntero a la estructura de configuración tipo struct leds.
 */
void ControlLeds(struct leds *ptr_led)
{
    if (ptr_led == NULL) return;

    if (ptr_led->mode == ON)
    {
        if (ptr_led->n_leds == 1) {
            LedOn(LED_1);
        } else if (ptr_led->n_leds == 2) {
            LedOn(LED_2);
        } else if (ptr_led->n_leds == 3) {
            LedOn(LED_3);
        }
    }
    else if (ptr_led->mode == OFF)
    {
        if (ptr_led->n_leds == 1) {
            LedOff(LED_1);
        } else if (ptr_led->n_leds == 2) {
            LedOff(LED_2);
        } else if (ptr_led->n_leds == 3) {
            LedOff(LED_3);
        }
    }
    else if (ptr_led->mode == TOGGLE)
    {
        // Calcula la cantidad de bloques de retardo de 100ms necesarios
        uint16_t retardo = ptr_led->periodo / CONFIG_BLINK_PERIOD;

        for (uint8_t i = 0; i < ptr_led->n_ciclos; i++)
        {
            // Conmuta el LED correspondiente
            if (ptr_led->n_leds == 1) {
                LedToggle(LED_1);
            } else if (ptr_led->n_leds == 2) {
                LedToggle(LED_2);
            } else if (ptr_led->n_leds == 3) {
                LedToggle(LED_3);
            }

            // Sub-bucle de retardo acumulando bloques de 100 ms
            for (uint16_t j = 0; j < retardo; j++) {
                vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);
            }
        }
    }
}

void app_main(void)
{
    LedsInit();
    SwitchesInit();

    /* Configuración: Modo TOGGLE, 10 ciclos, 500 ms */
    my_leds.mode = TOGGLE;
    my_leds.n_leds = 1;       // LED_1
    my_leds.n_ciclos = 10;    // 10 ciclos
    my_leds.periodo = 500;    // 500 ms

    /* Ejecución de la función */
    ControlLeds(&my_leds);

    while(1)
    {
        vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);
    }
}

/*==================[end of file]============================================*/