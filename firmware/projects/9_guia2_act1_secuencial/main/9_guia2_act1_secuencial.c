/**
 * @mainpage Proyecto 2 - Actividad 1 (versión EXTRA): Medidor de distancia por ultrasonido, versión SECUENCIAL 
 * 
 * 
 * @section genDesc General Description
 * Aplicación principal para la ESP32-EDU, versión EXTRA, SECUENCIAL (la hecha con Tasks de FreeRTOS ya subida). @n
 * Diseña e implementa un medidor de distancia por ultrasonido utilizando el sensor HC-SR04, @n
 * con visualización de distancia en cm mediante un display LCD y barra de LEDs, @n
 * además de control mediante botones/teclas para encendido/apagado y función HOLD.
 * 
 * 
 * @file 9_guia2_act1_secuencial.c
 * @brief Aplicación principal de la Actividad 1 del Proyecto 2, versión EXTRA (Medidor de distancia por ultrasonido, SECUENCIAL).
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
 * [Guia 2 - act 1 - Distancia y osciloscopio con TASKS](https://youtu.be/UBXX3C8eSug)
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
 * @see [Diagrama en bloque, versión SECUENCIAL](../diagrama_en_bloque_04_guia2_act1_SECUENCIAL.png)
 * @see [Diagrama de flujo, versión SECUENCIAL](../diagrama_de_flujo_04_guia2_act1_SECUENCIAL.png)
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
#define GPIO_ECHO GPIO_3
#define GPIO_TRIGGER GPIO_2
#define REFRESCO_MEDICION_MS 100	// original consigna: 1000 ms = 1 s
#define REFRESCO_TECLAS_MS 200

/*==================[internal data definition]===============================*/
// Variables globales para interactuar entre tareas
volatile bool medir_activado = true;
volatile bool hold_activado = false;

volatile uint16_t distancia = 0;
volatile uint16_t distancia_display = 0;

/*==================[internal functions declaration]=========================*/

/*==================[external functions definition]==========================*/


void app_main(void) {	// versión SECUENCIAL de 09_guia2_act1.c    
    // Inicialización
    LedsInit();
    SwitchesInit();
    LcdItsE0803Init();
    HcSr04Init(GPIO_ECHO, GPIO_TRIGGER);

    // Variables de estado del sistema
    bool medir_activado = true;
    bool hold_activado = false;
    uint16_t distancia = 0;
    uint16_t distancia_display = 0;

    // Variables para detección de flancos de las teclas
    bool tec1_anterior = false;
    bool tec2_anterior = false;

    // Contador de tiempo
    uint16_t timer_medicion = 0;

    while (true) {
        // --- LECTURA DE TECLAS (Se ejecuta rápido, cada 50ms) ---
        uint8_t teclas = SwitchesRead();
        bool tec1_actual = (teclas == SWITCH_1 || teclas == (SWITCH_1 | SWITCH_2));
        bool tec2_actual = (teclas == SWITCH_2 || teclas == (SWITCH_1 | SWITCH_2));

        // Detección de flanco para TEC1
        if (tec1_actual && !tec1_anterior) {
            medir_activado = !medir_activado; // Invierte estado solo al apretar
        }
        tec1_anterior = tec1_actual;

        // Detección de flanco para TEC2
        if (tec2_actual && !tec2_anterior) {
            hold_activado = !hold_activado;
            if (hold_activado) distancia_display = distancia; // Congela el valor actual
        }
        tec2_anterior = tec2_actual;


        // --- LÓGICA DE MEDICIÓN (Se ejecuta solo 1 vez por segundo) ---
        if (timer_medicion >= 1000) {
            timer_medicion = 0; // Reiniciamos el cronómetro

            if (medir_activado) {
                distancia = HcSr04ReadDistanceInCentimeters();

                // Lógica de LEDs según consigna
                if (distancia < 10) {
                    LedsOffAll();
                } else if (distancia >= 10 && distancia < 20) {
                    LedOn(LED_1); LedOff(LED_2); LedOff(LED_3);
                } else if (distancia >= 20 && distancia < 30) {
                    LedOn(LED_1); LedOn(LED_2); LedOff(LED_3);
                } else {
                    LedOn(LED_1); LedOn(LED_2); LedOn(LED_3);
                }

                // Lógica de Display
                if (hold_activado) {
                    LcdItsE0803Write(distancia_display);
                } else {
                    LcdItsE0803Write(distancia);
                }
            } else {
                LedsOffAll();
                LcdItsE0803Off();
            }
        }

        // Retardo base del bucle. El sistema reacciona a los botones cada 50ms.
        vTaskDelay(50 / portTICK_PERIOD_MS);
        timer_medicion += 50; 
    }
}

/*==================[end of file]============================================*/