/**
 * @mainpage Proyecto 2 - Actividad 1: Medidor de distancia por ultrasonido
 * 
 * 
 * @section genDesc General Description
 * Aplicación principal para la ESP32-EDU, con el uso de tareas de FreeRTOS. @n
 * Diseña e implementa un medidor de distancia por ultrasonido utilizando el sensor HC-SR04, @n
 * con visualización de distancia en cm mediante un display LCD y barra de LEDs, @n
 * además de control mediante botones/teclas para encendido/apagado y función HOLD.
 * 
 * 
 * @file 9_guia2_act1.c
 * @brief Aplicación principal de la Actividad 1 del Proyecto 2 (Medidor de distancia por ultrasonido).
 * @details 
 * Aplicación principal para la ESP32 en el marco de la materia Electrónica Programable (FIUNER). @n
 * 
 * Funcionalidades implementadas:
 * - **Medición de distancia:** Adquisición periódica cada 1 segundo mediante el sensor HC-SR04.
 * - **Indicación por LEDs:**
 *   - Distancia < 10 cm: Todos los LEDs apagados.
 *   - 10 cm <= Distancia < 20 cm: Encendido de LED_1.
 *   - 20 cm <= Distancia < 30 cm: Encendido de LED_1 y LED_2.
 *   - Distancia >= 30 cm: Encendido de LED_1, LED_2 y LED_3.
 * - **Visualización en LCD:** Muestra el valor medido en centímetros sobre el display LCD (LCDitse0803).
 * - **Control por Teclas (Switches):**
 *   - TEC1 (SWITCH_1): Activa o detiene la medición (ON/OFF).
 *   - TEC2 (SWITCH_2): Activa/desactiva la función "HOLD" para congelar la lectura en el display.
 * 
 * ### Videos de referencia subidos a YouTube:
 * 
 * Prueba funciones de la aplicación, con TASKS, y refresco a 1 Hz. @n
 * [Guia 2 - act 1 - Distancia y osciloscopio con TASKS](https://youtube.com/shorts/QGzCfP2jkSA)
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
 * | HC-SR04 VCC | +5V | Alimentación del sensor ultrasonido (5V) |
 * | HC-SR04 ECHO | GPIO_3 | Entrada del pulso de Eco |
 * | HC-SR04 TRIGGER | GPIO_2 | Salida del pulso de Disparo (Trigger) |
 * | HC-SR04 GND | GND | Tierra / Masa |
 * 
 * 
 * 
 * @section docu Documentación y Adjuntos
 * @see [Diagrama en bloque](../diagrama_en_bloques_9_guia2_act1.png)
 * @see [Diagrama de flujo](../diagrama_de_flujo_9_guia2_act1.png)
 * @see [Medición 1 con osciloscopio para 10 cm: 632 us/58 = 10,89 cm](../DSOBMP0036.bmp)
 * @see [Medición 2 con osciloscopio otras distancias](../DSOBMP0037.bmp)
 * @see [Medición 3 con osciloscopio otras distancias](../DSOBMP0039.bmp)
 * 
 * 
 * 
 * @section changelog Changelog
 * | Date | Description |
 * | :--- | :--- |
 * | 2026-09-24 | Document creation and Doxygen documentation |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */
/*==================[inclusions]=============================================*/
#include "stdio.h"
#include "stdint.h"
#include "stdbool.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gpio_mcu.h"
#include "switch.h"
#include "led.h"
#include "hc_sr04.h"
#include "lcditse0803.h"

/*==================[macros and definitions]=================================*/
/**
 * @def GPIO_ECHO
 * @brief Pin GPIO asignado a la señal ECHO del sensor HC-SR04.
 */
#define GPIO_ECHO GPIO_3

/**
 * @def GPIO_TRIGGER
 * @brief Pin GPIO asignado a la señal TRIGGER del sensor HC-SR04.
 */
#define GPIO_TRIGGER GPIO_2
/*==================[internal data definition]===============================*/
/**
 * @brief Flag global que habilita o deshabilita la medición de distancia (ON/OFF).
 */
volatile bool medir_activado = true;

/**
 * @brief Flag global que activa o congela la actualización de la pantalla (HOLD).
 */
volatile bool hold_activado = false;

/**
 * @brief Variable global que almacena el último valor de distancia medido en centímetros.
 */
volatile uint16_t distancia = 0;

/**
 * @brief Variable global que guarda la captura de la distancia cuando se activa el modo HOLD.
 */
volatile uint16_t distancia_display = 0;

/*==================[tasks definitions]======================================*/

/**
 * @fn void medir_distancia_task(void *pvParameter)
 * @brief Tarea de FreeRTOS encargada de medir la distancia mediante el sensor ultrasónico.
 * @details Realiza la lectura periódica del sensor HC-SR04 cada 1 segundo (1000 ms) siempre que `medir_activado` sea verdadero.
 * @param[in] pvParameter Parámetro de entrada a la tarea FreeRTOS (no utilizado).
 */
void medir_distancia_task(void *pvParameter) {
    while (true) {
        if (medir_activado) {
            // Solo se encarga de adquirir el dato del sensor
            distancia = HcSr04ReadDistanceInCentimeters();
        }
        // Se ejecuta cada 1 segundo exacto (1000 ms), cumpliendo la consigna
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/**
 * @fn void mostrar_display_task(void *pvParameter)
 * @brief Tarea de FreeRTOS encargada del control de LEDs y actualización del display LCD.
 * @details Según el valor de la distancia medida, enciende la combinación correspondiente de LEDs (LED_1, LED_2, LED_3) @n
 * y envía el valor al display LCD considerando el estado de retención (HOLD). Se refresca cada 100 ms.
 * @param[in] pvParameter Parámetro de entrada a la tarea FreeRTOS (no utilizado).
 */
void mostrar_display_task(void *pvParameter) {
    while (true) {
        if (medir_activado) {
            // --- Lógica de LEDs ---
            if (distancia < 10) {
                LedsOffAll();
            } else if (distancia >= 10 && distancia < 20) {
                LedOn(LED_1); LedOff(LED_2); LedOff(LED_3);
            } else if (distancia >= 20 && distancia < 30) {
                LedOn(LED_1); LedOn(LED_2); LedOff(LED_3);
            } else {
                LedOn(LED_1); LedOn(LED_2); LedOn(LED_3);
            }

            // --- Lógica de Display LCD ---
            if (hold_activado) {
                LcdItsE0803Write(distancia_display); 
            } else {
                LcdItsE0803Write(distancia);
            }
        } else {
            LedsOffAll();
            LcdItsE0803Off(); 
        }
        
        // Se refresca cada 100ms para responder rápido a los cambios de estado (teclas)
        vTaskDelay(100 / portTICK_PERIOD_MS); 
    }
}

/**
 * @fn void teclas_task(void *pvParameter)
 * @brief Tarea de FreeRTOS encargada de leer el estado de las teclas por polling.
 * @details Detecta flancos de subida en las teclas para alternar el estado de medición (TEC1) y la función HOLD (TEC2).
 * @param[in] pvParameter Parámetro de entrada a la tarea FreeRTOS (no utilizado).
 */
void teclas_task(void *pvParameter) {
    bool tec1_anterior = false;
    bool tec2_anterior = false;

    while(true) {
        uint8_t teclas = SwitchesRead();
        bool tec1_actual = (teclas == SWITCH_1 || teclas == (SWITCH_1 | SWITCH_2));
        bool tec2_actual = (teclas == SWITCH_2 || teclas == (SWITCH_1 | SWITCH_2));

        // Detección flanco TEC1 (ON/OFF de la medición)
        if (tec1_actual && !tec1_anterior) {
            medir_activado = !medir_activado; 
        }
        tec1_anterior = tec1_actual;

        // Detección flanco TEC2 (HOLD del display)
        if (tec2_actual && !tec2_anterior) {
            hold_activado = !hold_activado;
            if (hold_activado) {
                distancia_display = distancia; // Toma una "foto" del valor actual
            }
        }
        tec2_anterior = tec2_actual;
        
        // Polling de teclas rápido para no perder pulsaciones
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

/*==================[external functions definition]==========================*/
/**
 * @fn void app_main(void)
 * @brief Función principal de la aplicación FreeRTOS.
 * @details Inicializa los periféricos (LEDs, Switches, LCD, HC-SR04) y crea las 3 tareas encargadas de la medición, actualización del display/LEDs y lectura de teclas.
 */
void app_main(void) {
    // Inicialización de periféricos
    LedsInit();
    SwitchesInit();
    LcdItsE0803Init();
    HcSr04Init(GPIO_ECHO, GPIO_TRIGGER);

    // Creación de las 3 tareas
    xTaskCreate(&medir_distancia_task, "medir_distancia_task", 2048, NULL, 5, NULL);
    xTaskCreate(&mostrar_display_task, "mostrar_display_task", 2048, NULL, 5, NULL);
    xTaskCreate(&teclas_task, "teclas_task", 1024, NULL, 5, NULL);
}