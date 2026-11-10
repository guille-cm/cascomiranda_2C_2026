#ifndef DHT22_SENSOR_H
#define DHT22_SENSOR_H
/**
 * @file dht22_sensor.h
 * @brief Driver de la capa de dispositivos para el sensor digital de temperatura y humedad
 *        DHT22 (ASAIR AM2302) utilizando captura de flancos por interrupción en GPIO_9.
 * 
 * @details Este módulo implementa el protocolo propietario bidireccional Single-Wire del sensor
 *          DHT22 (AM2302) mediante una rutina de servicio de interrupción (ISR) de muy baja latencia
 *          y el contador de 64 bits esp_timer_get_time(). Este diseño asegura inmunidad determinística
 *          frente a las interrupciones periódicas de 500 Hz del ECG, Respirograma y telemetría UART.
 * 
 * @section dht22_strat Protocolo Single-Wire, Acondicionamiento en GPIO_9 y Análisis Temporal
 * 1. <b>Alimentación y Asignación al Pin de Strapping GPIO_9:</b>
 *    El sensor se alimenta con 3.3V DC desde la placa. La línea bidireccional DATA se conecta a GPIO_9
 *    con un resistor de pull-up externo de 5.6 kΩ conectado a 3.3V. Dado que GPIO_9 es un pin de strapping
 *    del ESP32-C6 (que debe permanecer en nivel ALTO durante el arranque para bootear desde la memoria
 *    Flash SPI interna), la presencia del pull-up garantiza el inicio normal del sistema sin inhibir el flasheo.
 * 
 * 2. <b>Comportamiento Temporal Relevado en Osciloscopio (dht22_tren_de_pulsos.png):</b>
 *    En la captura de osciloscopio se observan las siguientes fases del protocolo:
 *    - <b>Pulso de Inicio del Maestro:</b> El ESP32 fuerza la línea a nivel BAJO (0V) durante 2.0 ms
 *      (DHT22_START_LOW_US) y luego conmuta a entrada de alta impedancia con un pulso de subida de 25 µs.
 *    - <b>Respuesta de Presencia (ACK):</b> El sensor toma el control de la línea, manteniéndola 80 µs en
 *      BAJO seguida de 80 µs en ALTO.
 *    - <b>Transmisión de 40 Bits de Datos:</b> Cada bit comienza con un intervalo fijo de 50 µs en BAJO.
 *      La información lógica reside en el ancho del nivel ALTO subsiguiente:
 *      - Bit '0': Nivel ALTO de 26 a 28 µs. Intervalo entre dos flancos descendentes consecutivos: ~76 µs.
 *      - Bit '1': Nivel ALTO de 70 µs. Intervalo entre dos flancos descendentes consecutivos: ~120 µs.
 *    - <b>Umbral de Decodificación:</b> Midiendo el tiempo entre flancos descendentes sucesivos mediante la
 *      ISR, un umbral de 100 µs (DHT22_PERIOD_THRESHOLD_US) separa inequívocamente los bits '0' de los '1'.
 * 
 * 3. <b>Integración Asíncrona en FreeRTOS:</b>
 *    El sensor se consulta cada 1.5 s desde la tarea TaskSensoresAmbientales. Tras enviar el pulso de inicio,
 *    la tarea cede la CPU mediante vTaskDelay(20 ms) mientras la interrupción colecta los 41 flancos
 *    en segundo plano sin consumir ciclos de CPU.
 * 
 * @section dht22_media Video Experimental y Capturas del Sensor DHT22
 * - <b>[DHT22 sensor temp. y hum. comunicación](https://youtu.be/vintphlcF2E)</b> @n
 *   El sensor de temperatura y humedad DHT22 se comunica mediante un protocolo en donde la duración de sus pulsos determinan los ceros y unos que codifican las variables sensadas. @n
 * 
 * @see [Pinout y Conexión del Sensor DHT22](../DHT22_pinout.png)
 * @see [Tren de Pulsos Single-Wire del Sensor DHT22 en Osciloscopio](../dht22_tren_de_pulsos.png)
 * 
 * @section hardConn Hardware Connection
 * | Pin Sensor DHT22 (AM2302) | Pin ESP32-C6 | Conexión Hardware / Circuito Externo |
 * | :------------------------ | :----------- | :----------------------------------- |
 * | Pin 1 (VCC)               | 3V3          | Alimentación regulada de 3.3V DC     |
 * | Pin 2 (DATA)              | GPIO_9       | Línea Single-Wire con pull-up de 5.6 kΩ a 3.3V |
 * | Pin 3 (NC)                | -            | Sin conexión interna                 |
 * | Pin 4 (GND)               | GND          | Masa común del sistema (0V)          |
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-08 | Creación del driver inicial para DHT22 |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */


/*==================[inclusions]=============================================*/
#include <stdint.h>
#include <stdbool.h>
#include "gpio_mcu.h"

/*==================[macros and definitions]=================================*/
#define DHT22_DEFAULT_GPIO      GPIO_9 /*!< Pin GPIO_9 asignado a la línea DATA del DHT22 */

/*==================[typedef]================================================*/
/**
 * @brief Estructura de datos con las magnitudes decodificadas del sensor DHT22.
 */
typedef struct {
    int16_t temp_decimas_c;     /*!< Temperatura ambiental en décimas de °C (ej. 254 = 25.4 °C) */
    uint16_t hum_decimas_pct;   /*!< Humedad relativa en décimas de % (ej. 528 = 52.8 %) */
    bool lectura_valida;        /*!< true si la trama de 40 bits superó la verificación por Checksum */
} dht22_data_t;

/*==================[external function declarations]=========================*/
/**
 * @fn void Dht22Init(gpio_t pin)
 * @brief Inicializa el puerto GPIO_9 e instala el servicio de interrupción por flanco descendente.
 * @param[in] pin Pin GPIO conectado a la línea DATA del sensor (por defecto GPIO_9).
 */
void Dht22Init(gpio_t pin);

/**
 * @fn bool Dht22Read(dht22_data_t *out_data)
 * @brief Dispara la secuencia de inicio, captura los flancos en segundo plano y decodifica los datos.
 * @param[out] out_data Puntero a la estructura donde se almacenarán las variables físicas calculadas.
 * @return true si la trama de 40 bits fue íntegra y el checksum coincidió; false en caso contrario.
 */
bool Dht22Read(dht22_data_t *out_data);

#endif /* DHT22_SENSOR_H */
/*==================[end of file]============================================*/