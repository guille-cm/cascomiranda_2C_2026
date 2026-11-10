/**
 * @file dht22_sensor.c
 * @brief Implementación del driver Single-Wire por interrupción de flancos para el sensor DHT22 (AM2302).
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-08 | Decodificación de 16 bits por variable con resolución de 0.1 |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */

/*==================[inclusions]=============================================*/
#include "dht22_sensor.h"
#include <stddef.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_attr.h"

/*==================[macros and definitions]=================================*/
#define DHT22_START_LOW_US          2000    /*!< Duración del pulso de arranque del maestro: 2.0 ms */
#define DHT22_MAX_EDGES             44      /*!< Capacidad del buffer: 1 ACK + 40 bits de datos + margen */
#define DHT22_PERIOD_THRESHOLD_US   100     /*!< Umbral discriminador: bit '0' (~76 us) vs bit '1' (~120 us) */

/*==================[internal data declaration]==============================*/
static gpio_t s_dht22_pin = DHT22_DEFAULT_GPIO;
static volatile int64_t s_edge_timestamps[DHT22_MAX_EDGES];
static volatile uint8_t s_edge_count = 0;
static volatile bool s_captura_activa = false;
static bool s_isr_registrada = false;

/*==================[internal functions declaration]=========================*/
static void dht22_gpio_negedge_isr(void *arg);

/*==================[internal functions definition]==========================*/
/**
 * @brief Rutina de interrupción (ISR) ejecutada en memoria RAM rápida (IRAM) ante cada flanco descendente.
 *        Registra la marca de tiempo de alta resolución sin bloquear el planificador ni la CPU.
 */
static void IRAM_ATTR dht22_gpio_negedge_isr(void *arg) {
    if (!s_captura_activa) {
        return;
    }
    // Lectura del temporizador de hardware de 64 bits en microsegundos (toma menos de 1 us)
    int64_t now_us = esp_timer_get_time();
    uint8_t idx = s_edge_count;
    if (idx < DHT22_MAX_EDGES) {
        s_edge_timestamps[idx] = now_us; // Guarda la marca de tiempo del flanco descendente
        s_edge_count = idx + 1;          // Avanza el puntero de flancos capturados
    }
}

/*==================[external functions definition]==========================*/
void Dht22Init(gpio_t pin) {
    s_dht22_pin = pin;

    // Configuración inicial del puerto GPIO_9 como entrada con pull-up
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_dht22_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE // Disparo por flanco descendente (High -> Low)
    };
    gpio_config(&io_conf);

    // Instalación única del servicio de interrupciones de GPIO en el sistema operativo
    if (!s_isr_registrada) {
        gpio_install_isr_service(0);
        gpio_isr_handler_add((gpio_num_t)s_dht22_pin, dht22_gpio_negedge_isr, NULL);
        s_isr_registrada = true;
    }
    // Mantiene deshabilitada la interrupción hasta que se inicie una transacción de lectura
    gpio_intr_disable((gpio_num_t)s_dht22_pin);
}

bool Dht22Read(dht22_data_t *out_data) {
    if (out_data == NULL) {
        return false;
    }

    uint8_t raw_bytes[5] = {0, 0, 0, 0, 0};

    // 1. Deshabilita la interrupción durante la generación del pulso de arranque
    s_captura_activa = false;
    gpio_intr_disable((gpio_num_t)s_dht22_pin);

    // 2. Conmuta el pin a SALIDA y fija nivel BAJO (0V) durante 2000 us (2 ms)
    //    Esto le indica al microcontrolador interno del sensor que despierte y prepare la trama
    gpio_set_direction((gpio_num_t)s_dht22_pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)s_dht22_pin, 0);
    esp_rom_delay_us(DHT22_START_LOW_US);

    // 3. Eleva la línea a 3.3V durante 25 us para generar un flanco de subida nítido
    gpio_set_level((gpio_num_t)s_dht22_pin, 1);
    esp_rom_delay_us(25);

    // 4. Conmuta inmediatamente a ENTRADA de alta impedancia con pull-up activo
    //    El sensor toma el control de la línea y responde con el pulso de presencia (ACK)
    gpio_set_direction((gpio_num_t)s_dht22_pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)s_dht22_pin, GPIO_PULLUP_ONLY);

    // 5. Reinicia el contador de flancos y habilita la captura por ISR
    s_edge_count = 0;
    s_captura_activa = true;
    gpio_intr_enable((gpio_num_t)s_dht22_pin);

    // 6. Cede la CPU a otras tareas durante 20 ms mediante FreeRTOS
    //    La trama completa tarda ~4.25 ms en transmitirse por hardware
    vTaskDelay(pdMS_TO_TICKS(20));

    // 7. Cierra la ventana de captura por interrupción
    gpio_intr_disable((gpio_num_t)s_dht22_pin);
    s_captura_activa = false;

    uint8_t total_edges = s_edge_count;
    // Se requieren como mínimo 41 flancos descendentes: 1 de ACK + 40 de datos
    if (total_edges < 41) {
        out_data->lectura_valida = false;
        return false;
    }

    // 8. Decodificación bit a bit midiendo el período entre flancos descendentes sucesivos:
    //    first_bit_edge apunta al primer flanco correspondiente a los datos
    uint8_t first_bit_edge = total_edges - 40;
    for (uint8_t i = 0; i < 40; i++) {
        // t_prev: tiempo del flanco descendente que inició el bit actual
        // t_curr: tiempo del flanco descendente que finalizó el bit actual
        int64_t t_prev = s_edge_timestamps[first_bit_edge + i - 1];
        int64_t t_curr = s_edge_timestamps[first_bit_edge + i];
        int32_t period_us = (int32_t)(t_curr - t_prev);

        // Desplaza 1 bit a la izquierda para hacer lugar al nuevo bit en el byte correspondiente
        raw_bytes[i / 8] <<= 1;

        // Si el período entre flancos superó los 100 us, el tiempo en alto fue de ~70 us -> Bit '1'
        // Si fue menor a 100 us (típicamente ~76 us), el tiempo en alto fue de ~28 us -> Bit '0'
        if (period_us > DHT22_PERIOD_THRESHOLD_US) {
            raw_bytes[i / 8] |= 0x01; // Inserta un '1' en el bit menos significativo
        }
    }

    // 9. Comprobación del byte de Checksum (Suma de los primeros 4 bytes truncada a 8 bits)
    uint8_t checksum = (uint8_t)(raw_bytes[0] + raw_bytes[1] + raw_bytes[2] + raw_bytes[3]);
    if (checksum != raw_bytes[4]) {
        out_data->lectura_valida = false;
        return false; // Error de paridad o bit corrupto por ruido
    }

    // 10. Reconstrucción de la Humedad Relativa (16 bits sin signo, resolución de 0.1 %):
    //     Byte 0: Parte alta, Byte 1: Parte baja
    out_data->hum_decimas_pct = (uint16_t)((raw_bytes[0] << 8) | raw_bytes[1]);

    // 11. Reconstrucción de la Temperatura (16 bits con signo, resolución de 0.1 °C):
    //     Byte 2: Parte alta, Byte 3: Parte baja.
    //     El bit más significativo (bit 15) es el bit de signo (1 para temperaturas bajo cero).
    uint16_t raw_temp = (uint16_t)((raw_bytes[2] << 8) | raw_bytes[3]);
    if (raw_temp & 0x8000) {
        // Temperatura negativa: Se limpia el bit de signo con máscara 0x7FFF y se aplica signo negativo
        out_data->temp_decimas_c = -(int16_t)(raw_temp & 0x7FFF);
    } else {
        // Temperatura positiva
        out_data->temp_decimas_c = (int16_t)raw_temp;
    }

    out_data->lectura_valida = true;
    return true;
}

/*==================[end of file]============================================*/