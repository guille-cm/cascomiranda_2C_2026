
#ifndef BCD_LCD_H
#define BCD_LCD_H

#include <stdint.h>
#include "gpio_mcu.h"   /*!< Incluye la configuración de los GPIO */

/*==================[macros and definitions]=================================*/

/*==================[typedefs and structures]===============================*/


typedef struct
{
	gpio_t pin; /*!< Número del pin GPIO */
	io_t dir;   /*!< Dirección del pin: 0 para entrada, 1 para salida */
} gpioConf_t;

/*==================[external function declarations]=========================*/


uint8_t ManejoGPIO(uint8_t bcd, gpioConf_t *config);


int8_t BinaryToBcd (uint32_t data, uint8_t digits, uint8_t *bcd_number);


void ManejoLCD(uint32_t numero, uint8_t digitos, gpioConf_t* config, gpioConf_t* seleccion);

#endif /* BCD_LCD_H */
