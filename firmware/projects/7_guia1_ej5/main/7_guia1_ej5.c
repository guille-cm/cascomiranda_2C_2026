/*! @mainpage Template
 *
 * @section genDesc General Description
 *
 * This section describes how the program works.
 *
 * <a href="https://drive.google.com/...">Operation Example</a>
 *
 * @section hardConn Hardware Connection
 *
 * |    Peripheral  |   ESP32   	|
 * |:--------------:|:--------------|
 * | 	PIN_X	 	| 	GPIO_X		|
 *
 *
 * @section changelog Changelog
 *
 * |   Date	    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 12/09/2023 | Document creation		                         |
 *
 * @author Albano Peñalva (albano.penalva@uner.edu.ar)
 *
 */

/*==================[inclusions]=============================================*/
#include "gpio_mcu.h"  /*!< Incluye la cabecera correspondiente */
#include <stdint.h>
/*==================[macros and definitions]=================================*/

/*==================[internal data definition]===============================*/
typedef struct
{
    gpio_t pin;  /*!< Número de pin GPIO */
    io_t dir;    /*!< Dirección del GPIO ('0' IN, '1' OUT) */
} gpioConf_t;

/*==================[internal functions declaration]=========================*/
uint8_t ManejoGPIO(uint8_t bcd, gpioConf_t *config) {
    for (int i = 0; i < 4; i++) {
        GPIOInit(config[i].pin, config[i].dir);  /*!< Inicializa el GPIO con la configuración adecuada */
        
        if (bcd & (1 << i)) {  /*!< Si el bit 'i' del BCD está en 1, enciende el GPIO correspondiente */
            GPIOOn(config[i].pin);
        } else {               /*!< Si el bit 'i' del BCD está en 0, apaga el GPIO correspondiente */
            GPIOOff(config[i].pin);
        }
    }
    return 0;  /*!< Retorna 0 indicando que no hubo errores */
}
/*==================[external functions definition]==========================*/
void app_main(void){


	/* Definición del vector de configuración, mapeando los bits del BCD a los GPIO correspondientes */
    gpioConf_t conf[4] = {
        {GPIO_20, GPIO_OUTPUT},  /*!< b0 -> GPIO_20 */
        {GPIO_21, GPIO_OUTPUT},  /*!< b1 -> GPIO_21 */
        {GPIO_22, GPIO_OUTPUT},  /*!< b2 -> GPIO_22 */
        {GPIO_23, GPIO_OUTPUT}   /*!< b3 -> GPIO_23 */
    };

    /* Manejo de diferentes valores de BCD para establecer el estado de los GPIO */
    ManejoGPIO(4, conf);  /*!< Establece el estado del BCD 4 */
    ManejoGPIO(6, conf);  /*!< Establece el estado del BCD 6 */
    ManejoGPIO(9, conf);  /*!< Establece el estado del BCD 9 */

    while (1) {
        /* Bucle infinito */
    }
}
/*==================[end of file]============================================*/