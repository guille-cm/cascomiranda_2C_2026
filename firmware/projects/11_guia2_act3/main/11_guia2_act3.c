/**
 * @file 11_guia2_act3.c
 * @brief Medidor de distancia por ultrasonido con HC-SR04, LEDs, pantalla LCD y comunicación UART.
 * 
 * @details Este programa mide la distancia utilizando un sensor HC-SR04 y la muestra en 
 *          un display LCD ITS-E0803 y una barra de LEDs de la placa ESP-EDU.
 *          Emplea interrupciones para el control de teclas (TEC1 y TEC2) y manejo 
 *          de tiempos mediante temporizadores por hardware (Timers).
 *          Se añade la funcionalidad de enviar los datos por el puerto serie UART 
 *          para ser visualizados en un Monitor Serie (como el Serial Monitor de VSCode).
 *          Además, permite controlar la medición y la función HOLD enviando las teclas 
 *          'O' y 'H' desde la PC, replicando el comportamiento de TEC1 y TEC2.
 * 
 * ### Videos de referencia subidos a YouTube:
 * 
 * Ejecución de 11_guia2_act3.c @n 
 * [Guia 2, act 3 - Medidor distancia, tasks, timers y UART](https://youtube.com/shorts/kpKebVxM9j4)
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
 * @see [Compilado](../01_compilado.png)
 * @see [Flasheado](../02_flasheado.png)
 * @see [Diagrama de Flujo](../diagrama_de_flujo_guia2_act3.png)
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
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "hc_sr04.h"
#include "lcditse0803.h"
#include "switch.h"
#include "timer_mcu.h"
#include "uart_mcu.h" /* <-- Driver UART agregado */

/*==================[macros and definitions]=================================*/
/** @brief Período del temporizador de medición en microsegundos (1 segundo) */
#define CONFIG_BLINK_PERIOD_MEDICION 1000000

/** @brief Período del temporizador de refresco del display en microsegundos (100 ms) */
#define CONFIG_BLINK_PERIOD_DISPLAY 100000

/*==================[internal data definition]===============================*/
/** @brief Estado de la medición (activada/desactivada) */
static bool medir_activado = true;

/** @brief Estado de la función de congelar pantalla HOLD (activada/desactivada) */
static bool hold_activado = false;

/** @brief Variable global para almacenar el valor de la distancia medida en centímetros */
static uint16_t distancia = 0;

/** @brief Handle para la tarea de medición de distancia */
TaskHandle_t medir_task_handle = NULL;

/** @brief Handle para la tarea de visualización en LEDs y LCD */
TaskHandle_t display_task_handle = NULL;

/*==================[internal functions declaration]=========================*/
/**
 * @brief Rutina de interrupción (ISR) invocada por el Timer A.
 */
void FuncTimerA(void *param);

/**
 * @brief Rutina de interrupción (ISR) invocada por el Timer B.
 */
void FuncTimerB(void *param);

/**
 * @brief Rutina de interrupción (ISR) invocada por el switch TEC1.
 */
void FuncTecla1(void *param);

/**
 * @brief Rutina de interrupción (ISR) invocada por el switch TEC2.
 */
void FuncTecla2(void *param);

/**
 * @brief Rutina de interrupción (ISR) invocada al recibir datos por UART.
 *        Lee la tecla enviada desde la PC y conmuta medir_activado ('O' / 'o') 
 *        o hold_activado ('H' / 'h').
 * @param param Puntero a parámetros (no utilizado)
 */
void FuncUartRecepcion(void *param);

/**
 * @brief Tarea encargada de leer el sensor de ultrasonido HC-SR04 y enviar por UART.
 */
static void MedirTask(void *pvParameter);

/**
 * @brief Tarea encargada de actualizar el estado de los LEDs y el LCD.
 */
static void DisplayTask(void *pvParameter);

/*==================[internal functions definition]==========================*/
void FuncTimerA(void *param) {
    vTaskNotifyGiveFromISR(medir_task_handle, pdFALSE);
}

void FuncTimerB(void *param) {
    vTaskNotifyGiveFromISR(display_task_handle, pdFALSE);
}

void FuncTecla1(void *param) {
    medir_activado = !medir_activado;
}

void FuncTecla2(void *param) {
    hold_activado = !hold_activado;
}

/* ISR PARA RECEPCIÓN UART */
void FuncUartRecepcion(void *param) {
    uint8_t letra;
    UartReadByte(UART_PC, &letra);
    
    if (letra == 'O' || letra == 'o') {
        medir_activado = !medir_activado;
    } else if (letra == 'H' || letra == 'h') {
        hold_activado = !hold_activado;
    }
}

static void MedirTask(void *pvParameter) {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (medir_activado) {
            distancia = HcSr04ReadDistanceInCentimeters();

            /* Envío por UART aprovechando el refresco de 1s (Timer A) de esta tarea.
               Se formatea a: 3 dígitos ascii + espacio + "cm\r\n" */
            if (!hold_activado) {
                /* Si se requiere forzar SIEMPRE 3 caracteres (ej: "005", "045", "120"), 
                   se puede usar sprintf, pero usaremos las funciones base de la cátedra */
                UartSendString(UART_PC, (char*)UartItoa(distancia, 10));
                UartSendString(UART_PC, " cm\r\n");
            }
        }
    }
}

static void DisplayTask(void *pvParameter) {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (medir_activado) {
            /* Control de la barra de LEDs según la distancia */
            if (distancia < 10) {
                LedsOffAll();
            } else if (distancia >= 10 && distancia < 20) {
                LedOn(LED_1);
                LedOff(LED_2);
                LedOff(LED_3);
            } else if (distancia >= 20 && distancia < 30) {
                LedOn(LED_1);
                LedOn(LED_2);
                LedOff(LED_3);
            } else if (distancia >= 30) {
                LedOn(LED_1);
                LedOn(LED_2);
                LedOn(LED_3);
            }

            /* Control del LCD según la función HOLD */
            if (!hold_activado) {
                LcdItsE0803Write(distancia);
            }
        } else {
            LedsOffAll();
            LcdItsE0803Off();
        }
    }
}

/*==================[external functions definition]==========================*/
void app_main(void) {
    /* Inicialización de periféricos básicos */
    LedsInit();
    HcSr04Init(GPIO_3, GPIO_2);
    LcdItsE0803Init();
    SwitchesInit();

    /* Configuración de UART */
    serial_config_t mi_uart = {
        .port = UART_PC,
        .baud_rate = 115200,
        .func_p = FuncUartRecepcion,
        .param_p = NULL
    };
    UartInit(&mi_uart);

    /* Configuración de interrupciones de teclas físicas */
    SwitchActivInt(SWITCH_1, FuncTecla1, NULL);
    SwitchActivInt(SWITCH_2, FuncTecla2, NULL);

    /* Configuración e inicialización de Timers */
    timer_config_t timer_medicion = {
        .timer = TIMER_A,
        .period = CONFIG_BLINK_PERIOD_MEDICION,
        .func_p = FuncTimerA,
        .param_p = NULL
    };
    TimerInit(&timer_medicion);

    timer_config_t timer_display = {
        .timer = TIMER_B,
        .period = CONFIG_BLINK_PERIOD_DISPLAY,
        .func_p = FuncTimerB,
        .param_p = NULL
    };
    TimerInit(&timer_display);

    /* Creación de tareas FreeRTOS */
    xTaskCreate(&MedirTask, "MedirTask", 2048, NULL, 5, &medir_task_handle);
    xTaskCreate(&DisplayTask, "DisplayTask", 2048, NULL, 5, &display_task_handle);

    /* Inicio de los temporizadores */
    TimerStart(timer_medicion.timer);
    TimerStart(timer_display.timer);
}