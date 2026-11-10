#ifndef HCSR501_PIR_SENSOR_H
#define HCSR501_PIR_SENSOR_H
/**
 * @file hcsr501_pir_sensor.h
 * @brief Driver de la capa de dispositivos para el sensor piroeléctrico de movimiento HC-SR501 en GPIO_12.
 * 
 * @details Este módulo gestiona la detección por flanco ascendente de los pulsos digitales generados
 *          por el circuito integrado BISS0001 del módulo HC-SR501 ante la radiación infrarroja emitida
 *          por el cuerpo humano en movimiento en la sala de internación.
 * 
 * @section pir_strat Estrategia de Funcionamiento, Filtrado de Encendido y Asignación de Pines
 * 1. <b>Alimentación y Nivel de Señal de Salida:</b>
 *    El módulo HC-SR501 se alimenta desde el riel de 5V DC del sistema. En su placa incorpora un regulador
 *    lineal de baja caída HT7133-1 que genera 3.3V internos para la etapa de detección y amplificación.
 *    La salida física OUT (pin central) proviene directamente del integrado BISS0001 y entrega un nivel
 *    lógico nativo de 3.3V en estado activo y 0V en reposo, siendo totalmente compatible con el ESP32-C6.
 * 
 * 2. <b>Supresión del Pulso de Encendido (Power-On Reset del BISS0001):</b>
 *    Al conectar la alimentación, los capacitores internos de acople del sensor piroeléctrico se cargan
 *    abruptamente, lo que provoca que el comparador de ventana del BISS0001 dispare un pulso espurio
 *    en nivel alto de aproximadamente 1.15 segundos de duración (comprobado experimentalmente en osciloscopio).
 *    Para evitar falsas alarmas al arrancar el sistema, el driver implementa una ventana temporal de
 *    estabilización (HCSR501_STARTUP_SETTLE_US) mediante esp_timer_get_time(). Durante
 *    dicho lapso, el software sincroniza la memoria de estado pero inhibe el despacho de eventos de alarma.
 * 
 * @see [Pinout y Controles del Sensor PIR HC-SR501](../HC-SR501_pinout.png)
 * 
 * @section hardConn Hardware Connection
 * | Pin Módulo HC-SR501 | Pin ESP32-C6 | Descripción del Circuito Físico |
 * | :------------------ | :----------- | :------------------------------- |
 * | Pin 1 (VCC)         | 5V           | Alimentación positiva de 5V DC   |
 * | Pin 2 (OUT)         | GPIO_12      | Salida lógica directa de 3.3V    |
 * | Pin 3 (GND)         | GND          | Masa común del sistema (0V)      |
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-08 | Creación del driver PIR con detección de flanco ascendente y contador de eventos |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */

/*==================[inclusions]=============================================*/
#include <stdint.h>
#include <stdbool.h>
#include "gpio_mcu.h"

/*==================[macros and definitions]=================================*/
#define HCSR501_PIR_DEFAULT_GPIO    GPIO_12     /*!< Pin GPIO asignado a la salida OUT del sensor PIR */
#define HCSR501_STARTUP_SETTLE_US   60000000ULL /*!< Ventana de inhibición inicial: 60 segundos (60.000.000 us) */

/*==================[external function declarations]=========================*/
/**
 * @fn void HcSr501PirInit(gpio_t pin)
 * @brief Inicializa el pin GPIO_12 como entrada digital con resistencia pull-down activa.
 * @param[in] pin Pin GPIO conectado al terminal OUT del PIR (por defecto GPIO_12).
 */
void HcSr501PirInit(gpio_t pin);

/**
 * @fn bool HcSr501PirEventoMovimiento(void)
 * @brief Evalúa la presencia de un flanco ascendente (0V -> 3.3V) tras la ventana de estabilización.
 * @return true en el instante exacto en que comienza una nueva detección; false en reposo o pulso sostenido.
 */
bool HcSr501PirEventoMovimiento(void);

/**
 * @fn bool HcSr501PirEstadoActual(void)
 * @brief Retorna el estado lógico instantáneo del sensor.
 * @return true si la salida OUT se encuentra en nivel alto; false en reposo.
 */
bool HcSr501PirEstadoActual(void);

/**
 * @fn uint32_t HcSr501PirObtenerContadorEventos(void)
 * @brief Consulta la cantidad acumulada de eventos de movimiento registrados en la sesión.
 * @return Número total de flancos ascendentes contabilizados.
 */
uint32_t HcSr501PirObtenerContadorEventos(void);

#endif /* HCSR501_PIR_SENSOR_H */
/*==================[end of file]============================================*/