/**
 * @file bcd_LCD.c
 * @brief Implementación de la función bcdToGpio
 * 
 * 
 * @details Contiene el desarrollo de la función bcdToGpio, 
 * que mapea los valores de los 4 dígitos BCD de un número cualquiera, 
 * a 4 pines GPIO de salida, específicos, de la ESP32.
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

#include "bcd_LCD.h"
#include <stddef.h>

/*==================[external functions definition]==========================*/

int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number) {
    if (bcd_number == NULL) {
        return -1; // Retorna error si el puntero no es válido
    }
    
    for (uint8_t i = 0; i < digits; i++) {
        bcd_number[i] = data % 10;
        data /= 10;
    }
    
    return 0;
}


void bcdToGpio(uint8_t bcd, gpioConf_t *gpio_conf) {
    for (uint8_t i = 0; i < 4; i++) {
        if (bcd & (1 << i)) {            /*!< Si el bit 'i' del BCD está en 1, enciende el GPIO correspondiente */
            GPIOOn(gpio_conf[i].pin);
        } else {                        /*!< Sino, si el bit 'i' del BCD está en 0, apaga el GPIO correspondiente */
            GPIOOff(gpio_conf[i].pin);
        }
    }
}
