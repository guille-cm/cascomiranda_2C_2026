/**
 * @file bcd_LCD.c
 * @brief Implementación para conversión de enteros a BCD, BCD a GPIO, y control de display LCD multiplexado.
 * 
 * 
 * @details Agrega el desarrollo de la función lcdDisplay, 
 * que permite mostrar números en un display. @n
 * 
 * Contiene e integra el desarrollo de las funciones lógicas para descomponer enteros 
 * y multiplexar displays mediante un decodificador/latch BCD a 7 segmentos, como el CD4543B. @n
 * 
 * Para más información sobre el conexionado, refiérase a bcd_LCD.h
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


void lcdDisplay(uint32_t data, uint8_t digits, gpioConf_t *gpio_bcd, gpioConf_t *gpio_sel) {
    uint8_t bcd_array[digits];
    
    // 1. Ejercicio 4: Convertir el entero en arreglo BCD
    if (convertToBcdArray(data, digits, bcd_array) == 0) {
        
        // 2. Recorrer cada dígito para actualizar los CD4543BE
        for (uint8_t i = 0; i < digits; i++) {
            
            // Ejercicio 5: Setear el bus de datos BCD (b0-b3)
            bcdToGpio(bcd_array[digits-1-i], gpio_bcd);
            
            // Ejercicio 6: Pulso en el pin de Latch Disable para fijar el número en el integrado
            GPIOOn(gpio_sel[i].pin);
            GPIOOff(gpio_sel[i].pin);
        }
    }
}