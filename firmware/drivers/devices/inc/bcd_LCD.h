/**
 * @file bcd_LCD.h
 * @brief Archivo de cabecera para conversión de enteros a BCD.
 * 
 * 
 * @details Declaración de función que recibe un dato de 32 bits, 
 * la cantidad de dígitos de salida, 
 * y un puntero a un arreglo donde se almacene los n dígitos. @n
 * La función convierte el dato recibido a BCD, 
 * guardando cada uno de los dígitos de salida en el arreglo pasado como puntero. @n
 * 
 * Definición de dicha función, en bcd_LCD.c
 * 
 * 
 *  * @section changelog Changelog
 * | Date | Description |
 * | :--- | :--- |
 * | 2026-09-20 | Document creation |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */

#ifndef BCD_LCD_H
#define BCD_LCD_H

#include <stdint.h>

/*==================[macros and definitions]=================================*/

/*==================[external function declarations]=========================*/


/**
 * @fn int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number)
 * @brief Convierte un número de 32 bits en un arreglo de dígitos BCD (Ejercicio 4).
 * 
 * @param[in]  data       Dato numérico a convertir.
 * @param[in]  digits     Cantidad de dígitos de salida esperados.
 * @param[out] bcd_number Puntero al arreglo donde se almacenan los dígitos BCD.
 * @return int8_t         0 si la conversión fue exitosa, -1 si el puntero es nulo.
 */
int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number);

#endif /* BCD_LCD_H */
