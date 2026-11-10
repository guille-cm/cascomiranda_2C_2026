#ifndef DISPLAY_RELOJ_3DIG_H
#define DISPLAY_RELOJ_3DIG_H
/**
 * @file display_reloj_3dig.h
 * @brief Archivo de cabecera para reloj con display de 3 dígitos.
 * 
 * 
 * @details  Se agrega control de display LCD multiplexado 
 * de manera que se muestre la "hora" por el display de 3 dígitos (y 7 segmentos cada dígito). @n
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
 * Definición de dicha función, en display_reloj_3dig.c
 * 
 * 
 * @section changelog Changelog
 * | Date | Description |
 * | :--- | :--- |
 * | 2026-09-20 | Document creation |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */


/*==================[inclusions]=============================================*/
#include <stdint.h>
#include "gpio_mcu.h"   /*!< Incluye la configuración de los GPIO */
#include "bcd_LCD.h"    /*!< Necesario para invocar lcdDisplay() */


/*==================[macros and definitions]=================================*/
#define DIGITS_COUNT 3  /*!< Cantidad de dígitos del display multiplexado */


/*==================[typedef]================================================*/


/*==================[external data declaration]==============================*/
/**
 * @brief Variable global que almacena el valor numérico del reloj.
 * Se declara extern para que main.c pueda leerla e imprimirla por consola.
 */
extern volatile uint32_t my_number;


/*==================[external function declarations]=========================*/
/**
 * @fn void DisplayRelojInit(void)
 * @brief Inicializa los GPIO del display y arranca el timer de hardware a 1 Hz.
 */
void DisplayRelojInit(void);

#endif /* DISPLAY_RELOJ_3DIG_H */


/*==================[end of file]============================================*/

