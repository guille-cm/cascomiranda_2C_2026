/**
 * @file hcsr501_pir_sensor.c
 * @brief Implementación del driver con detección de flanco ascendente y supresión de
 *        pulso de encendido (Power-On Reset) para el sensor PIR HC-SR501.
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-08 | Implementación con detección de flanco y registro de eventos |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */

/*==================[inclusions]=============================================*/
#include "hcsr501_pir_sensor.h"
#include "driver/gpio.h"
#include "esp_timer.h"

/*==================[internal data declaration]==============================*/
static gpio_t s_pir_pin = HCSR501_PIR_DEFAULT_GPIO;
static bool s_estado_actual = false;
static bool s_estado_previo = false;
static uint32_t s_contador_detecciones = 0;
static int64_t s_init_timestamp_us = 0;

/*==================[external functions definition]==========================*/
void HcSr501PirInit(gpio_t pin) {
    s_pir_pin = pin;
    s_contador_detecciones = 0;

    // Configuración del puerto de entrada para GPIO_12:
    // - pin_bit_mask: Asigna el bit 12 en el registro de configuración (1ULL << GPIO_12).
    // - mode: Entrada digital de alta impedancia (GPIO_MODE_INPUT).
    // - pull_up_en: Deshabilitado (la salida del BISS0001 es activa, tipo push-pull).
    // - pull_down_en: Habilitado para asegurar 0V cuando el módulo se desconecta.
    // - intr_type: Desactivado para realizar muestreo sincrónico en FreeRTOS.
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_pir_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Marca temporal del instante de inicialización (en microsegundos)
    s_init_timestamp_us = esp_timer_get_time();

    // Sincronización del estado previo con el nivel real del pin al encender
    // Evita interpretar el pulso de 1.15 s del encendido como una detección de movimiento
    s_estado_previo = GPIORead(s_pir_pin);
    s_estado_actual = false;
}

bool HcSr501PirEventoMovimiento(void) {
    // Lectura del bit de estado lógico en el pin GPIO_12
    bool lectura_pin = GPIORead(s_pir_pin);

    // Evaluación de la ventana de estabilización térmica y eléctrica (30 segundos):
    // Si aún no transcurrieron 60.000.000 us, se actualiza el estado previo pero se retorna false
    if ((esp_timer_get_time() - s_init_timestamp_us) < (int64_t)HCSR501_STARTUP_SETTLE_US) {
        s_estado_previo = lectura_pin;
        s_estado_actual = false;
        return false;
    }

    s_estado_actual = lectura_pin;

    // Detección de flanco ascendente (Low -> High):
    // El pin pasa de 0V (reposo) a 3.3V (detección de calor y movimiento de personas)
    if (s_estado_actual && !s_estado_previo) {
        s_estado_previo = true;      // Bloquea re-disparos mientras el pulso continúe en alto
        s_contador_detecciones++;    // Incrementa el acumulador total de eventos
        return true;                 // Notifica el flanco ascendente al planificador
    } else if (!s_estado_actual && s_estado_previo) {
        // Flanco descendente (High -> Low): Finalizó el pulso de 1 segundo calibrado en el sensor
        s_estado_previo = false;     // Rehabilita la detección para el siguiente ingreso
    }
    return false;
}

bool HcSr501PirEstadoActual(void) {
    return s_estado_actual;
}

uint32_t HcSr501PirObtenerContadorEventos(void) {
    return s_contador_detecciones;
}

/*==================[end of file]============================================*/