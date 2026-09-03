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
#include "bcd_LCD.h"       /* <= own header */
#include <stdint.h>
#include <gpio_mcu.h>      /* <= Archivo de cabecera que gestiona los GPIO */
#include "freertos/FreeRTOS.h"

/*==================[macros and definitions]=================================*/
// typedef struct
// {
// 	gpio_t pin; /* Número del pin */
// 	io_t dir;  /* Dirección 0=in 1=out */
// } gpioConf_t;

/*==================[internal data definition]===============================*/

/*==================[internal functions declaration]=========================*/
uint8_t ManejoGPIO(uint8_t bcd, gpioConf_t *config){
	for(int i = 0; i < 4; i++){
		GPIOInit(config[i].pin, config[i].dir);   // Inicializa el pin
		if(bcd & (1 << i))                        // Si el bit correspondiente en BCD está en 1
			GPIOOn(config[i].pin);                // Activa el pin
		else
			GPIOOff(config[i].pin);               // Desactiva el pin
	}
	return 0;
}

int8_t BinaryToBcd (uint32_t data, uint8_t digits, uint8_t *bcd_number){
	for(int i = digits; i > 0; i--){
		bcd_number[i - 1] = data % 10;
		data = data / 10;
	}
	return 0;
}

void ManejoLCD(uint32_t numero, uint8_t digitos, gpioConf_t* config, gpioConf_t* seleccion){
	uint8_t bcd[digitos];
	BinaryToBcd(numero, digitos, bcd);  // Convierte el número a BCD

	for(int i = 0; i < 3; i++){
		GPIOInit(seleccion[i].pin, seleccion[i].dir);  // Inicializa el pin de selección
		GPIOOn(seleccion[i].pin);                      // Activa el pin de selección
		ManejoGPIO(bcd[i], config);                    // Muestra el dígito correspondiente
		// Espera un tiempo mínimo de 50ns para latchear los datos
		for (volatile int j = 0; j < 1000; j++) {};    // Espera pequeña
		GPIOOff(seleccion[i].pin);                     // Desactiva el pin de selección
	}
}

/*==================[external functions definition]==========================*/
void app_main(void){
	/* Inicializaciones */
	gpioConf_t conf[4] = {
		{GPIO_20, GPIO_OUTPUT},   // D1 -> LCD1
		{GPIO_21, GPIO_OUTPUT},   // D2 -> LCD2
		{GPIO_22, GPIO_OUTPUT},   // D3 -> LCD3
		{GPIO_23, GPIO_OUTPUT}    // D4 -> LCD4
	};

	gpioConf_t seleccion[3] = {
		{GPIO_19, GPIO_OUTPUT},   // SEL_1 -> Dígito 1
		{GPIO_18, GPIO_OUTPUT},   // SEL_2 -> Dígito 2
		{GPIO_9, GPIO_OUTPUT}     // SEL_3 -> Dígito 3
	};

	/* Muestra el número 736 en el display LCD */
	ManejoLCD(739, 3, conf, seleccion);

    while(1){
		/* Bucle principal - Aquí el programa seguiría mostrando el número o podría incluir otras lógicas */
	}

}
/*==================[end of file]============================================*/