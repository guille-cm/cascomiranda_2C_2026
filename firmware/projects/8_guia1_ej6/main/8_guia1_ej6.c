/**
 * @mainpage Guía de Trabajos Prácticos 1 - Ejercicio 6
 * 
 * 
 * @section genDesc General Description
 * Aplicación principal para la ESP32. @n
 * Muestra un valor entero incremental en un display de 3 dígitos multiplexado, @n
 * utilizando un decodificador BCD controlado por pines GPIO.
 * 
 * 
 * @file 8_guia1_ej6.c
 * @brief Aplicación principal del Ejercicio 6 de la Guía de Trabajos Prácticos 1.
 * @details 
 * Aplicación principal para la ESP32. @n
 * Muestra un valor entero incremental en un display de 3 dígitos multiplexado, @n
 * utilizando un decodificador BCD controlado por pines GPIO.
 * 
 * Para más información sobre el manejo y conexionado
 * entre los decoders (como el 74HC4543 y el CD4543BE)
 * y los displays (tanto el LCD como el LED de 7 segmentos de 3 dígitos),
 * refiérase a los PDFs e imágenes agregados en la carpeta que contiene este proyecto.
 * 
 * 
 * ### Videos de referencia subidos a YouTube:
 * 
 * Prueba manual de conteo de 0 a 9 del dígito menos significativo. @n
 * [Display LED 1 - manual 1 dígito](https://youtu.be/6-N6dmuXu8M)
 * 
 * Prueba manual de conteo de 0 a 10, para hacer uso del latch. @n
 * [Display LED 2 - manual 3 dígitos con latch](https://youtu.be/mcUrqBuPrnc)
 * 
 * Prueba de consumo de del bloque entero (display + decoders). @n
 * Consumo: no supera los 18 mA, a 3.3 V. @n
 * Condiciones medición: mostrando "888", 
 * con brillo aceptablemente legible para interiores iluminados. @n
 * [Display LED 3 - prueba consumo corriente](https://youtube.com/shorts/JrYvggtQ8cA)
 * 
 * Ejecución de 8_guia1_ej6.c @n 
 * Muestra por display y por consola, los numeros de 000 a 999, 
 * en bucle, con frecuencia de 1 Hz. @n
 * [Display LED 4 - guia 1, ej 6 - contador de 0 a 999](https://youtube.com/shorts/k55fkw2KJYM)
 *                     
 * 
 * @section hardConn Hardware Connection
 * | Periférico / Señal | Pin ESP32 | Descripción |
 * | :--- | :--- | :--- |
 * | Bus BCD b0 | GPIO_20 | Bit menos significativo del bus BCD|
 * | Bus BCD b1 | GPIO_21 | Bit 1 del bus BCD |
 * | Bus BCD b2 | GPIO_22 | Bit 2 del bus BCD |
 * | Bus BCD b3 | GPIO_23 | Bit más significativo del bus BCD |
 * | Latch Dígito 1 | GPIO_19 | Línea de habilitación (Latch) para el dígito 1 (MSB)|
 * | Latch Dígito 2 | GPIO_18 | Línea de habilitación (Latch) para el dígito 2 |
 * | Latch Dígito 3 | GPIO_9 | Línea de habilitación (Latch) para el dígito 3 (LSB)|
 * 
 * 
 * @section docu Documentación y Adjuntos
 * @see [Pinout CI 4543](../pinout_4543.jpg)
 * @see [Datasheet HEF4543B](../datasheet_HEF4543B.pdf)
 * @see [Resumen de LCD](../resumen_LCD.png)
 * @see [Señal AC para LCD](../senial_AC_para_LCD.png)
 * @see [Compilación, Flasheo y consola con LCD - 2026-09-03 a las 13:09:41](../Revision_2026-09-03_a_las_13h09m41s.jpeg)
 * @see [Compilación mismo código, pero con display LED](../01_compiled_8_guia1_ej6.png)
 * @see [Flasheo con mismo código, pero con display LED](../02_flashed_8_guia1_ej6.png)
 * @see [Consola con mismo código, pero con display LED](../03_tested_8_guia1_ej6.png)
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
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_mcu.h"
#include "bcd_LCD.h"

/*==================[macros and definitions]=================================*/
/**
 * @def DIGITS_COUNT
 * @brief Cantidad de dígitos del display multiplexado.
 */
#define DIGITS_COUNT 3

/*==================[main application]=======================================*/
/**
 * @fn void app_main(void)
 * @brief Función principal de la aplicación FreeRTOS.
 * @details Configura los pines GPIO para el bus BCD y la selección de dígitos, e inicia un bucle infinito que incrementa y muestra un contador numérico en el display.
 */
void app_main(void) {
    // Configuración del vector del bus BCD (Ejercicio 5)
    gpioConf_t gpio_bcd_bus[4] = {
        {GPIO_20, GPIO_OUTPUT}, // b0 LSB
        {GPIO_21, GPIO_OUTPUT}, // b1
        {GPIO_22, GPIO_OUTPUT}, // b2
        {GPIO_23, GPIO_OUTPUT}  // b3 MSB
    };
    
    // Configuración del vector de selección de dígitos/Latch (Ejercicio 6)
    gpioConf_t gpio_digit_select[3] = {
        {GPIO_19, GPIO_OUTPUT}, // Latch Dígito 1 LSD
        {GPIO_18, GPIO_OUTPUT}, // Latch Dígito 2
        {GPIO_9,  GPIO_OUTPUT}  // Latch Dígito 3 MSD
    };
    
    // Inicialización del hardware GPIO de datos BCD
    for (uint8_t i = 0; i < 4; i++) {
        GPIOInit(gpio_bcd_bus[i].pin, gpio_bcd_bus[i].dir);
    }
    
    // Inicialización del hardware GPIO de selección de dígitos
    for (uint8_t i = 0; i < DIGITS_COUNT; i++) {
        GPIOInit(gpio_digit_select[i].pin, gpio_digit_select[i].dir);
    }
    
    // Valor de prueba a mostrar en el display
    uint32_t my_number = 000;

    while (1) {
        // Muestra el valor reutilizando la lógica modular
        lcdDisplay(my_number, DIGITS_COUNT, gpio_bcd_bus, gpio_digit_select);
		printf("my_number: %lu\n", my_number);

		if (my_number < 999){
			my_number++;
		}
		else{
			my_number = 0;
		}
        
        // Retardo preventivo de la tarea FreeRTOS
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}