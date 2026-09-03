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
#include <stdio.h>
#include <stdint.h>
/*==================[macros and definitions]=================================*/

/*==================[internal data definition]===============================*/

/*==================[internal functions declaration]=========================*/
int8_t BinaryToBcd (uint32_t data, uint8_t digits, uint8_t *bcd_number){
	unsigned int i;
	for(i=digits;i>0;i--){
		bcd_number[i]=data%10;
		data=data/10;
	}
	return 0;
}
/*==================[external functions definition]==========================*/
void app_main(void){

	
	/* initializations */
	
	uint32_t datos=1234;
	uint8_t digitos=4;
	uint8_t bcd[digitos];


	
    while(1){
		/* main loop */
    	BinaryToBcd(datos,digitos,bcd);
    	for(int i=0;i<50000;i++){};
	}
}
/*==================[end of file]============================================*/