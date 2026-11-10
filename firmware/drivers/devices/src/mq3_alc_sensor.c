/**
 * @file mq3_alc_sensor.c
 * @brief Implementación del driver para el sensor de alcohol y gases MQ-3 en GPIO_19.
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-08 | Implementación con ventana de calentamiento de 60 s y medición de tiempo en bajo |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */

/*==================[inclusions]=============================================*/
#include "mq3_alc_sensor.h"
#include "driver/gpio.h"
#include "esp_timer.h"

/*==================[internal data declaration]==============================*/
static gpio_t s_mq3_pin = MQ3_ALC_DEFAULT_GPIO;
static int64_t s_init_timestamp_us = 0;
static int64_t s_low_start_timestamp_us = 0;
static bool s_en_estado_bajo = false;

/*==================[external functions definition]==========================*/
void Mq3AlcInit(gpio_t pin) {
    s_mq3_pin = pin;

    // Registra el instante de arranque en microsegundos para la temporización de 60 s
    s_init_timestamp_us = esp_timer_get_time();
    s_en_estado_bajo = false;
    s_low_start_timestamp_us = 0;

    // Configuración del pin de entrada digital GPIO_19:
    // El divisor resistivo fija 3.2V en reposo. Se activa el pull-up interno como protección
    // para evitar lecturas flotantes en caso de desconexión del cable de señal.
    GPIOInit(s_mq3_pin, GPIO_INPUT);
    gpio_set_pull_mode((gpio_num_t)s_mq3_pin, GPIO_PULLUP_ONLY);
}

bool Mq3AlcPrecalentamientoListo(void) {
    // Calcula la diferencia temporal respecto al arranque:
    // 60.000.000 microsegundos = 60 segundos
    int64_t transcurrido_us = esp_timer_get_time() - s_init_timestamp_us;
    return (transcurrido_us >= (int64_t)MQ3_PREHEAT_TIME_US);
}

bool Mq3AlcActualizarEstado(void) {
    // 1. Verificación de seguridad térmica: Si está en el primer minuto, ignora cualquier señal
    if (!Mq3AlcPrecalentamientoListo()) {
        s_en_estado_bajo = false;
        return false;
    }

    // 2. Muestreo lógico del pin GPIO_19:
    // La salida del comparador LM393 conmuta a 0V (nivel bajo) al superar el umbral de gas.
    // Invertir la lectura (!GPIORead) hace que dout_bajo sea true cuando hay presencia de gas.
    bool dout_bajo = !GPIORead(s_mq3_pin);

    if (dout_bajo) {
        if (!s_en_estado_bajo) {
            // Flanco de bajada físico: Inicia un nuevo evento de gas
            s_en_estado_bajo = true;
            s_low_start_timestamp_us = esp_timer_get_time(); // Guarda la marca de inicio
        }
        return true;
    } else {
        // Retorno a nivel alto (aire limpio): Restablece el cronómetro de detección
        s_en_estado_bajo = false;
        s_low_start_timestamp_us = 0;
        return false;
    }
}

uint32_t Mq3AlcObtenerDuracionPulsoSeg(void) {
    if (!s_en_estado_bajo) {
        return 0; // En reposo no hay duración acumulada
    }

    // Calcula el tiempo transcurrido desde el flanco de bajada y convierte de microsegundos a segundos
    int64_t duracion_us = esp_timer_get_time() - s_low_start_timestamp_us;
    if (duracion_us < 0) {
        return 0;
    }
    return (uint32_t)(duracion_us / 1000000ULL);
}

/*==================[end of file]============================================*/