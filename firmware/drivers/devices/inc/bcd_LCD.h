/**
 * @file bcd_LCD.h
 * @brief Archivo de cabecera para conversión de enteros a BCD, BCD a GPIO, y control de display LCD multiplexado.
 * 
 * 
 * @details  Se agrega control de display LCD multiplexado 
 * de manera que se integran los objetivos de los ejercicios 4, 5 y 6. @n
 * 
 * Declaración de función que recibe un dato de 32 bits, 
 * la cantidad de dígitos de salida, 
 * y un puntero a un arreglo donde se almacene los n dígitos. @n
 * 
 * La función convierte el dato recibido a BCD, 
 * guardando cada uno de los dígitos de salida en el arreglo pasado como puntero. @n
 * 
 * Declaración de función que recibe como parámetro un número BCD, 
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
 * Una vez mapeados los dígitos BCD a los pines GPIO, 
 * esta cabecera declara la función que permite conectar la ESP32 al mundo exterior
 * mediante 6 cables que manejan un display LCD o LED de 3 dígitos, y 7 segmentos cada uno. @n
 * 
 * Para ver la correspondencia entre variables y pines, refiérase a la sección Hardware Connection.
 * 
 * @section hardConn Hardware Connection
 * | Periférico / Señal | Pin ESP32 | Descripción |
 * | :--- | :--- | :--- |
 * | Bus BCD b0 | GPIO_20 | Bit menos significativo del bus BCD|
 * | Bus BCD b1 | GPIO_21 | Bit 1 del bus BCD |
 * | Bus BCD b2 | GPIO_22 | Bit 2 del bus BCD |
 * | Bus BCD b3 | GPIO_23 | Bit más significativo del bus BCD |
 * | Latch Dígito 1 | GPIO_19 | Línea de habilitación (Latch) para el dígito 1 (MSD) |
 * | Latch Dígito 2 | GPIO_18 | Línea de habilitación (Latch) para el dígito 2 |
 * | Latch Dígito 3 | GPIO_9 | Línea de habilitación (Latch) para el dígito 3 (LSD)|
 *
 * Para más información sobre el manejo y conexionado
 * entre los decoders (como el 74HC4543 y el CD4543B)
 * y los displays (tanto LCD como display de 7 segmentos de 3 dígitos),
 * refiérase a los PDFs e imágenes agregados en la carpeta que contiene este proyecto.
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


/**
 * @fn void lcdDisplay(uint32_t data, uint8_t digits, gpioConf_t *gpio_bcd, gpioConf_t *gpio_sel)
 * @brief Muestra un valor entero multiplexado en el display enviando datos y latches (Ejercicio 6).
 * 
 * @param[in] data     Dato numérico a mostrar en el display.
 * @param[in] digits   Cantidad de dígitos del display.
 * @param[in] gpio_bcd Vector de estructuras que define el bus BCD (b0-b3).
 * @param[in] gpio_sel Vector de estructuras que define las líneas de selección/latch.
 */
void lcdDisplay(uint32_t data, uint8_t digits, gpioConf_t *gpio_bcd, gpioConf_t *gpio_sel);

#endif /* BCD_LCD_H */
