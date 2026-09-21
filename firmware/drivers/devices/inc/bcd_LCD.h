/**
 * @file bcd_LCD.h
 * @brief Archivo de cabecera para conversión de BCD a GPIO
 * 
 * 
 * @details Declaración de función que recibe como parámetro un número BCD, 
 * y un vector de estructuras del tipo gpioConf_t. @n
 * 
 * Esta función, asigna el estado (alto o bajo) de cada dígito de un número, expresado en BCD, 
 * a un estado de un pin GPIO particular de la ESP32.
 * 
 * LLa correspondencia entre los bits de un número BCD y los pines GPIO es de la siguiente manera: @n @n
 * 
 * b0 --> GPIO_20 @n
 * b1 --> GPIO_21 @n
 * b2 --> GPIO_22 @n
 * b3 --> GPIO_23 @n @n
 * 
 * De esta manera, cada GPIO (configurado como pin de salida),
 * se pondrá en alto o bajo según el estado de su dígito BCD correspondiente. @n
 * 
 * Esta cabecera contiene, además, la definición de gpioConf_t @n
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
#include "gpio_mcu.h"   /*!< Incluye la configuración de los GPIO */

/*==================[macros and definitions]=================================*/
/**
 * @brief Estructura de configuración para pines GPIO. @n
 * Aquí se definine esta estructura, y se crea un alias de tipo mediante typedef
 * estructura forma parte de la interfaz pública (API) de tu driver. 
 * Dado que funciones como bcdToGpio y lcdDisplay reciben punteros a gpioConf_t como argumentos, 
 * cualquier archivo main.c que consuma el módulo necesita conocer la estructura
 * para poder declarar e inicializar los vectores de pines. FIN
 */
typedef struct {
    gpio_t pin; /*!< Número de pin GPIO asignado */
    io_t dir;   /*!< Dirección del puerto: GPIO_INPUT (0) o GPIO_OUTPUT (1) */
} gpioConf_t;

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


/**
 * @fn void bcdToGpio(uint8_t bcd, gpioConf_t *gpio_conf)
 * @brief Cambia el estado de los GPIO del bus BCD según un dígito recibido (Ejercicio 5).
 * 
 * @param[in] bcd       Dígito numérico BCD (0-9).
 * @param[in] gpio_conf Vector de estructuras con la asignación de pines b0..b3.
 */
void bcdToGpio(uint8_t bcd, gpioConf_t *gpio_conf);

#endif /* BCD_LCD_H */
