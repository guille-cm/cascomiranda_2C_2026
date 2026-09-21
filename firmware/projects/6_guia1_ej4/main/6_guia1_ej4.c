/**
 * @mainpage Guía de Trabajos Prácticos 1 - Ejercicio 4
 * 
 * 
 * @section genDesc General Description
 * Aplicación principal para la ESP32. @n
 * Prueba unitaria de la función convertToBcdArray, @n
 * la cual convierte un dato de 32 bits en un arreglo de dígitos BCD.
 * 
 * 
 * @file 6_guia1_ej4.c
 * @brief Aplicación principal del Ejercicio 4 de la Guía de Trabajos Prácticos 1.
 * @details 
 * Aplicación principal para la ESP32. @n
 * Prueba unitaria de la función convertToBcdArray, @n
 * la cual convierte un dato de 32 bits en un arreglo de dígitos BCD.
 * 
 * @section changelog Changelog
 * | Date | Description |
 * | :--- | :--- |
 * | 2026-09-19 | Document creation |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 * @copyright FIUNER - UNER
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include "bcd_LCD.h"

/*==================[macros and definitions]=================================*/
#define TEST_DATA 972
#define DIGITS_COUNT 3

/*==================[internal data definition]===============================*/

/*==================[internal functions declaration]=========================*/

/*==================[external functions definition]==========================*/
void app_main(void) {
    uint8_t bcd_array[DIGITS_COUNT];

    // Se invoca la función desarrollada en el módulo bcd_LCD
    if (convertToBcdArray(TEST_DATA, DIGITS_COUNT, bcd_array) == 0) {
        printf("Dato original: %d\n", TEST_DATA);
        
        // Imprime el arreglo para verificar la correcta conversión
        for (uint8_t i = 0; i < DIGITS_COUNT; i++) {
            printf("Dígito [%d]: %d\n", i, bcd_array[i]);
        }
    } else {
        printf("Error: Puntero nulo detectado al intentar convertir el arreglo.\n");
    }
}
/*==================[end of file]============================================*/