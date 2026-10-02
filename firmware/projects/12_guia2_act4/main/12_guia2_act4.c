/**
 * @file 12_guia2_act4.c
 * @brief Osciloscopio digital por puerto serie utilizando ADC, DAC y UART.
 * 
 * @details Este programa digitaliza una señal analógica conectada al canal CH1 (ADC)
 *          a una frecuencia de muestreo de 500 Hz controlada por un Timer.
 *          Simultáneamente, convierte una señal digital (arreglo ECG) en analógica 
 *          a través de la salida GPIO_0. Los datos leídos por el GPIO_1 se envían por 
 *          puerto serie (UART) utilizando un formato específico compatible con 
 *          la extensión "Teleplot" de VSCode (ej: >ecg:234\r\n).
 *          Para probar el sistema, se mide primero GPIO_0 con osciloscopio físico, 
 *          y luego se conectan apropiadamente GPIO_0 con GPIO_1.
 * 
 * 
 * ### Videos de referencia subidos a YouTube:
 * 
 * Ejecución gradual de 12_guia2_act4.c @n 
 * [Guia 2, act 4 - 00 Prueba salida GPIO_0](https://youtube.com/shorts/sI_Fc5lGRFs)
 * [Guia 2, act 4 - 01 Puente entre GPIO_0 y GPIO_1](https://youtu.be/fAqSIl8k_Ok)
 * [Guia 2, act 4 - 02 ECG entre GPIO_0 y GPIO_1](https://youtu.be/gupTlS4Yn8)
 * 
 * @section docu Documentación y Adjuntos
 * @see [Compilado](../01_compilado.png)
 * @see [Flasheado](../02_flasheado.png)
 * @see [Diagrama de Flujo](../diagrama_de_flujo_guia2_act4.png)
 * @see [Teleplot](../03_Teleplot_GPIO_0_a_GPIO_1.png)
 *
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
#include "timer_mcu.h"
#include "uart_mcu.h"
#include "analog_io_mcu.h"

/*==================[macros and definitions]=================================*/
/** @brief Período del temporizador para muestreo a 500Hz (2000 microsegundos) */
#define CONFIG_BLINK_PERIOD_500HZ 2000

/* Datos provistos en 02_ECG.txt */
#define BUFFER_SIZE 231

/*==================[internal data definition]===============================*/
TaskHandle_t main_task_handle = NULL;

const char ecg[BUFFER_SIZE] = {
    76, 77, 78, 77, 79, 86, 81, 76, 84, 93, 85, 80,
    89, 95, 89, 85, 93, 98, 94, 88, 98, 105, 96, 91,
    99, 105, 101, 96, 102, 106, 101, 96, 100, 107, 101,
    94, 100, 104, 100, 91, 99, 103, 98, 91, 96, 105, 95,
    88, 95, 100, 94, 85, 93, 99, 92, 84, 91, 96, 87, 80,
    83, 92, 86, 78, 84, 89, 79, 73, 81, 83, 78, 70, 80, 82,
    79, 69, 80, 82, 81, 70, 75, 81, 77, 74, 79, 83, 82, 72,
    80, 87, 79, 76, 85, 95, 87, 81, 88, 93, 88, 84, 87, 94,
    86, 82, 85, 94, 85, 82, 85, 95, 86, 83, 92, 99, 91, 88,
    94, 98, 95, 90, 97, 105, 104, 94, 98, 114, 117, 124, 144,
    180, 210, 236, 253, 227, 171, 99, 49, 34, 29, 43, 69, 89,
    89, 90, 98, 107, 104, 98, 104, 110, 102, 98, 103, 111, 101,
    94, 103, 108, 102, 95, 97, 106, 100, 92, 101, 103, 100, 94, 98,
    103, 96, 90, 98, 103, 97, 90, 99, 104, 95, 90, 99, 104, 100, 93,
    100, 106, 101, 93, 101, 105, 103, 96, 105, 112, 105, 99, 103, 108,
    99, 96, 102, 106, 99, 90, 92, 100, 87, 80, 82, 88, 77, 69, 75, 79,
    74, 67, 71, 78, 72, 67, 73, 81, 77, 71, 75, 84, 79, 77, 77, 76, 76,
};

/*==================[internal functions declaration]=========================*/
/**
 * @brief ISR invocada por el Timer A a 500 Hz. Notifica a la tarea principal.
 */
void FuncTimerMuestreo(void *param);

/**
 * @brief Tarea encargada de escribir en el DAC, leer el ADC y enviar por UART.
 */
static void AdcDacTask(void *pvParameter);

/*==================[internal functions definition]==========================*/
void FuncTimerMuestreo(void *param) {
    vTaskNotifyGiveFromISR(main_task_handle, pdFALSE);
}

static void AdcDacTask(void *pvParameter) {
    uint16_t valor_adc = 0;
    uint16_t indice_ecg = 0;

    while (true) {
        /* Espera la notificación del timer (500 Hz) */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* 1. Generar la señal analógica por el DAC */
        AnalogOutputWrite(ecg[indice_ecg]);
        
        indice_ecg++;
        if (indice_ecg >= BUFFER_SIZE) {
            indice_ecg = 0;
        }

        /* 2. Digitalizar la señal ingresando por ADC CH1 */
        AnalogInputReadSingle(CH1, &valor_adc);

        /* 3. Transmitir por UART en formato para Serial Plotter (>senal:valor\r\n) */
        UartSendString(UART_PC, ">ecg:");
        UartSendString(UART_PC, (char*)UartItoa(valor_adc, 10));
        UartSendString(UART_PC, "\r\n");
    }
}

/*==================[external functions definition]==========================*/
void app_main(void) {
    /* Configuración UART a 115200 baudios (suficiente para 500Hz) */
    serial_config_t mi_uart = {
        .port = UART_PC,
        .baud_rate = 115200,
        .func_p = NULL,
        .param_p = NULL
    };
    UartInit(&mi_uart);

    /* Configuración de Conversor A/D para CH1 */
    analog_input_config_t adc_config = {
        .input = CH1,
        .mode = ADC_SINGLE
    };
    AnalogInputInit(&adc_config);

    /* Configuración de Conversor D/A */
    AnalogOutputInit();

    /* Configuración de Timer A para disparar a 500 Hz (2 ms) */
    timer_config_t timer_muestreo = {
        .timer = TIMER_A,
        .period = CONFIG_BLINK_PERIOD_500HZ,
        .func_p = FuncTimerMuestreo,
        .param_p = NULL
    };
    TimerInit(&timer_muestreo);

    /* Creación de Tarea y captura del handle */
    xTaskCreate(&AdcDacTask, "AdcDacTask", 2048, NULL, 5, &main_task_handle);

    /* Iniciar el timer */
    TimerStart(timer_muestreo.timer);
}