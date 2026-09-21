/**
 * @file bcd_LCD.c
 * @brief Implementación de las funciones de conversión BCD y manejo de display.[cite: 3]
 * @details Contiene el desarrollo de las funciones lógicas para descomponer enteros y multiplexar displays mediante decodificadores.
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

