#ifndef MQ3_ALC_SENSOR_H
#define MQ3_ALC_SENSOR_H
/**
 * @file mq3_alc_sensor.h
 * @brief Driver de la capa de dispositivos para el sensor de vapor de alcohol y gases inflamables MQ-3 en GPIO_19.
 * 
 * @details Gestiona la adquisición de la salida digital DOUT gobernada por el comparador analógico LM393
 *          del módulo MQ-3, acondicionada mediante un divisor resistivo de tensión e inmunizada frente a
 *          falsos disparos durante el minuto de precalentamiento térmico del filamento semiconductor.
 * 
 * @section mq3_strat Estrategia de Funcionamiento, Divisor de Tensión y Medición Experimental
 * 1. <b>Principio Físico del Sensor de Gas:</b>
 *    El sensor MQ-3 contiene un tubo cerámico recubierto de dióxido de estaño (SnO<sub>2</sub>) con un calefactor
 *    interno de óxido de aluminio. En aire limpio, el SnO<sub>2</sub> presenta alta resistividad eléctrica. Al entrar
 *    en contacto con vapores de alcohol o gases inflamables reductores, la densidad de oxígeno adsorbido
 *    disminuye y la resistencia eléctrica del semiconductor desciende drásticamente.
 * 
 * 2. <b>Alimentación y Divisor Resistivo de Adaptación de Tensión:</b>
 *    El calefactor exige una alimentación constante de 5.0V DC. El módulo entrega su salida digital DOUT
 *    conmutando entre 0V (gas detectado) y 5.0V (aire limpio). Dado que los pines del ESP32-C6 no toleran
 *    5V, se implementa un divisor resistivo pasivo formado por R1 = 5.6 kΩ en serie y R2 = 10 kΩ a masa: @n
 *    <b>V<sub>GPIO19</sub> = V<sub>DOUT</sub> · R<sub>2</sub> / (R<sub>1</sub> + R<sub>2</sub>) = 5.0 V · (10 kΩ / 15.6 kΩ) ≈ 3.205 V</b> @n
 *    Esto garantiza que el nivel alto recibido en GPIO_19 sea perfectamente seguro para el microcontrolador.
 * 
 * 3. <b>Validación en Osciloscopio [MQ3_osciloscopio.png](../MQ3_osciloscopio.png) y [MQ3_respuesta.png](../MQ3_respuesta.png):</b>
 *    Mediante el relevamiento con osciloscopio se comprobó que, al acercar vapores de alcohol, la señal
 *    DOUT cae en escalón limpio desde 3.2V a 0V, permaneciendo en nivel bajo mientras dure la exposición
 *    y recuperando el nivel alto al disiparse el gas.
 * 
 * 4. <b>Ventana de Precalentamiento de 60 Segundos:</b>
 *    Al conectar el sistema, el filamento tarda 60 segundos en alcanzar su temperatura operativa de equilibrio
 *    (~300 °C). Durante este primer minuto, el comparador puede oscilar. La función Mq3AlcActualizarEstado()
 *    evalúa la marca temporal esp_timer_get_time() y descarta cualquier pulso bajo hasta cumplirse los 60 s.
 * 
 * @section mq3_media Video Experimental y Capturas en Osciloscopio del Sensor MQ-3
 * - <b>[Calibración sensor alcohol y gases inflamables](https://youtube.com/shorts/1Po57KtMRjY)</b> @n
 *   Se ajusta el potenciómetro incorporado al sensor para detectar la presencia de alcohol que libera una bebida alcohólica o alcohol al 70% en piel, así como gases inflamables contenidos en aerosoles. @n
 * 
 * @see [Respuesta en Escalón del Sensor MQ-3 en Osciloscopio](../MQ3_osciloscopio.png)
 * @see [Curva de Respuesta y Recuperación del Sensor MQ-3](../MQ3_respuesta.png)
 * 
 * @section hardConn Hardware Connection
 * | Pin Módulo MQ-3 | Pin ESP32-C6 / Circuito | Descripción de la Conexión Física |
 * | :-------------- | :---------------------- | :--------------------------------- |
 * | VCC             | 5V                      | Alimentación positiva de 5V DC     |
 * | GND             | GND                     | Masa común del sistema (0V)        |
 * | DOUT            | Nodo serie R1 (5.6 kΩ)  | Salida del comparador LM393        |
 * | Nodo intermedio | GPIO_19 y R2 (10 kΩ)    | Tensión nivelada a 3.20V máximos   |
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-08 | Creación del driver con ventana de precalentamiento y medición de duración de pulso |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */

/*==================[inclusions]=============================================*/
#include <stdint.h>
#include <stdbool.h>
#include "gpio_mcu.h"

/*==================[macros and definitions]=================================*/
#define MQ3_ALC_DEFAULT_GPIO        GPIO_19     /*!< Pin GPIO asignado a la salida adaptada del MQ-3 */
#define MQ3_PREHEAT_TIME_US         60000000ULL /*!< Tiempo de estabilización térmica: 60 segundos */

/*==================[external function declarations]=========================*/
/**
 * @fn void Mq3AlcInit(gpio_t pin)
 * @brief Configura el pin GPIO_19 como entrada digital y registra el instante de encendido.
 * @param[in] pin Pin GPIO conectado a la salida del divisor resistivo (por defecto GPIO_19).
 */
void Mq3AlcInit(gpio_t pin);

/**
 * @fn bool Mq3AlcPrecalentamientoListo(void)
 * @brief Indica si ya concluyó el minuto inicial de calentamiento del sensor.
 * @return true si transcurrieron 60 segundos desde la inicialización; false durante el primer minuto.
 */
bool Mq3AlcPrecalentamientoListo(void);

/**
 * @fn bool Mq3AlcActualizarEstado(void)
 * @brief Lee la entrada digital DOUT (activa en bajo) y actualiza el cronómetro del pulso de gas.
 * @return true si el sensor detecta alcohol/gases tras el precalentamiento; false en reposo.
 */
bool Mq3AlcActualizarEstado(void);

/**
 * @fn uint32_t Mq3AlcObtenerDuracionPulsoSeg(void)
 * @brief Obtiene la cantidad de segundos continuos que el sensor lleva detectando gas.
 * @return Tiempo en segundos de la detección actual (0 en estado normal de aire limpio).
 */
uint32_t Mq3AlcObtenerDuracionPulsoSeg(void);

#endif /* MQ3_ALC_SENSOR_H */
/*==================[end of file]============================================*/