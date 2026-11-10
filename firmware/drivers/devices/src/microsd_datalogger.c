/**
 * @file microsd_datalogger.c
 * @brief Implementación del datalogger clínico sobre tarjeta MicroSD FAT16 por SPI y reloj RTC.
 * 
 * @details Gestiona las tres fases físicas del protocolo SPI relevadas en osciloscopio:
 *          - <b>Fase 1 (Escritura):</b> \c fopen("a") y \c fprintf() bajan \c CS (GPIO_13) y envían por
 *            \c MOSI (GPIO_21) los bloques con reloj \c SCK (GPIO_20) a 4 MHz.
 *          - <b>Fase 2 (Cierre / Flush FAT):</b> \c fclose() vuelca el buffer de sector y actualiza
 *            la tabla FAT16 mientras la tarjeta retiene \c MISO (GPIO_22) en 0V (<i>busy</i>) hasta
 *            asegurar la persistencia ante cortes de energía.
 *          - <b>Fase 3 (Lectura):</b> \c fopen("r"), \c fseek() y \c fread() emiten comandos \c CMD17
 *            recibiendo los caracteres ASCII por \c MISO hacia el servidor HTTP. @n
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-31 | Implementación inicial con soporte FAT16, mutex SPI, RTC y lectura de cola CSV |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */
/*==================[inclusions]=============================================*/
#include "microsd_datalogger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

/*==================[macros and definitions]=================================*/
#define SD_SPI_HOST_ID              SPI2_HOST
#define SD_SPI_CLOCK_KHZ            4000        /*!< Frecuencia SCK de 4 MHz idéntica al ensayo en osciloscopio */
#define SD_TAIL_READ_BYTES          2560        /*!< Ventana final de bytes leída con fseek para extraer las últimas muestras */
#define EPOCH_BASE_DEFAULT          1767225600  /*!< 2026-01-01 00:00:00 como base inicial antes de sincronizar con la Web */

/*==================[internal data declaration]==============================*/
static sdmmc_card_t *s_card = NULL;
static bool s_spi_bus_inicializado = false;
static bool s_intento_montaje_hecho = false;
static volatile bool s_sd_montada = false;
static volatile bool s_reloj_sincronizado = false;
static volatile uint32_t s_total_registros_escritos = 0;
static SemaphoreHandle_t s_sd_mutex = NULL;

/** @brief Buffers estáticos para no sobrecargar la pila de las tareas de FreeRTOS */
static char s_tail_buf[SD_TAIL_READ_BYTES + 1];
static char s_csv_chunk[512];

/*==================[internal functions declaration]=========================*/
static bool montar_sistema_fat_sd(void);

/*==================[internal functions definition]==========================*/
static bool montar_sistema_fat_sd(void) {
    if (s_sd_montada && s_card != NULL) {
        return true;
    }
    if (s_intento_montaje_hecho) {
        return false; // Evita re-registrar el dispositivo SPI si la tarjeta no está insertada
    }
    s_intento_montaje_hecho = true;

    // 1. Configura GPIO_13 (CS) desconectando el transceptor USB_D+ interno
    gpio_config_t cs_conf = {
        .pin_bit_mask = (1ULL << MICROSD_DEFAULT_CS_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&cs_conf);
    gpio_set_level((gpio_num_t)MICROSD_DEFAULT_CS_GPIO, 1);

    // 2. Pull-ups internos en MISO, MOSI y SCK
    gpio_set_pull_mode((gpio_num_t)MICROSD_DEFAULT_MISO_GPIO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t)MICROSD_DEFAULT_MOSI_GPIO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t)MICROSD_DEFAULT_SCK_GPIO, GPIO_PULLUP_ONLY);

    // 3. Inicializa el bus SPI2
    if (!s_spi_bus_inicializado) {
        spi_bus_config_t bus_cfg = {
            .mosi_io_num = MICROSD_DEFAULT_MOSI_GPIO,
            .miso_io_num = MICROSD_DEFAULT_MISO_GPIO,
            .sclk_io_num = MICROSD_DEFAULT_SCK_GPIO,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096
        };
        if (spi_bus_initialize(SD_SPI_HOST_ID, &bus_cfg, SPI_DMA_CH_AUTO) != ESP_OK) {
            return false;
        }
        s_spi_bus_inicializado = true;
    }

    // 4. Monta el volumen FAT16/FAT32 sobre SPI a 4 MHz
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 3,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST_ID;
    host.max_freq_khz = SD_SPI_CLOCK_KHZ;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)MICROSD_DEFAULT_CS_GPIO;
    slot_config.host_id = SD_SPI_HOST_ID;

    if (esp_vfs_fat_sdspi_mount(MICROSD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card) != ESP_OK) {
        s_sd_montada = false;
        s_card = NULL;
        return false;
    }

    s_sd_montada = true;

    // 5. Crea el encabezado de DATALOG.CSV si el archivo no existe o está vacío
    struct stat st;
    if (stat(MICROSD_CSV_FILEPATH, &st) != 0 || st.st_size == 0) {
        FILE *f = fopen(MICROSD_CSV_FILEPATH, "w");
        if (f != NULL) {
            const char *hdr = "Fecha,Hora,FC,FR,Temp,Hum,Gases,Movimiento\n";
            fwrite(hdr, 1, strlen(hdr), f);
            fclose(f);
        }
    }

    return true;
}

/*==================[external functions definition]==========================*/
bool MicroSdInit(void) {
    if (s_sd_mutex == NULL) {
        s_sd_mutex = xSemaphoreCreateMutex();
    }

    // Inicializa el RTC interno en 2026-01-01 00:00:00 sin consumir el stack de app_main
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    if (tv_now.tv_sec < EPOCH_BASE_DEFAULT) {
        tv_now.tv_sec = EPOCH_BASE_DEFAULT;
        tv_now.tv_usec = 0;
        settimeofday(&tv_now, NULL);
    }
    return true;
}

bool MicroSdEscribirRegistro(const registro_clinico_sd_t *reg) {
    if (reg == NULL || s_sd_mutex == NULL) {
        return false;
    }

    if (xSemaphoreTake(s_sd_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return false;
    }

    if (!s_sd_montada) {
        if (!montar_sistema_fat_sd()) {
            xSemaphoreGive(s_sd_mutex);
            return false;
        }
    }

    char fecha[12];
    char hora[10];
    char linea_csv[80];
    MicroSdObtenerFechaHoraActual(fecha, sizeof(fecha), hora, sizeof(hora));

    // Fase 1: Apertura y escritura de la línea CSV
    FILE *f = fopen(MICROSD_CSV_FILEPATH, "a");
    if (f == NULL) {
        esp_vfs_fat_sdcard_unmount(MICROSD_MOUNT_POINT, s_card);
        s_card = NULL;
        s_sd_montada = false;
        s_intento_montaje_hecho = false;
        xSemaphoreGive(s_sd_mutex);
        return false;
    }

    int16_t t_ent = reg->temp_decimas_c / 10;
    int16_t t_dec = abs(reg->temp_decimas_c % 10);
    uint16_t h_ent = reg->hum_decimas_pct / 10;
    uint16_t h_dec = reg->hum_decimas_pct % 10;

    int len = snprintf(linea_csv, sizeof(linea_csv), "%s,%s,%u,%u,%d.%d,%u.%u,%u,%u\n",
                       fecha,
                       hora,
                       (unsigned int)reg->fc_bpm,
                       (unsigned int)reg->fr_rpm,
                       (int)t_ent, (int)t_dec,
                       (unsigned int)h_ent, (unsigned int)h_dec,
                       reg->alerta_gases ? 1U : 0U,
                       reg->movimiento_sala ? 1U : 0U);
    if (len > 0) {
        fwrite(linea_csv, 1, (size_t)len, f);
    }

    // Fase 2: Cierre del archivo y Flush inmediato a la tabla FAT16
    fclose(f);

    s_total_registros_escritos++;
    xSemaphoreGive(s_sd_mutex);
    return true;
}

bool MicroSdEstadoMontada(void) {
    return s_sd_montada;
}

uint32_t MicroSdObtenerTotalEscritos(void) {
    return s_total_registros_escritos;
}

void MicroSdSincronizarRelojEpoch(int64_t epoch_sec) {
    if (epoch_sec > 1700000000LL) {
        struct timeval tv = {
            .tv_sec = (time_t)epoch_sec,
            .tv_usec = 0
        };
        settimeofday(&tv, NULL);
        s_reloj_sincronizado = true;
    }
}

void MicroSdObtenerFechaHoraActual(char *out_fecha, size_t len_f, char *out_hora, size_t len_h) {
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    gmtime_r(&now, &timeinfo);

    if (out_fecha != NULL && len_f >= 11) {
        snprintf(out_fecha, len_f, "%04d-%02d-%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    }
    if (out_hora != NULL && len_h >= 9) {
        snprintf(out_hora, len_h, "%02d:%02d:%02d",
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
}

bool MicroSdRelojSincronizado(void) {
    return s_reloj_sincronizado;
}

int MicroSdLeerUltimosRegistrosJson(char *json_buf, size_t max_len, uint16_t max_registros) {
    if (json_buf == NULL || max_len < 64) {
        return 0;
    }

    if (s_sd_mutex == NULL || xSemaphoreTake(s_sd_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return snprintf(json_buf, max_len, "{\"sd_ok\":false,\"rows\":[]}");
    }

    if (!s_sd_montada && !montar_sistema_fat_sd()) {
        xSemaphoreGive(s_sd_mutex);
        return snprintf(json_buf, max_len, "{\"sd_ok\":false,\"rows\":[]}");
    }

    // Fase 3: Apertura y lectura SPI desde el final del archivo con fseek
    FILE *f = fopen(MICROSD_CSV_FILEPATH, "r");
    if (f == NULL) {
        xSemaphoreGive(s_sd_mutex);
        return snprintf(json_buf, max_len, "{\"sd_ok\":false,\"rows\":[]}");
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    long read_offset = 0;
    if (file_size > SD_TAIL_READ_BYTES) {
        read_offset = file_size - SD_TAIL_READ_BYTES;
    }
    fseek(f, read_offset, SEEK_SET);

    size_t bytes_leidos = fread(s_tail_buf, 1, SD_TAIL_READ_BYTES, f);
    s_tail_buf[bytes_leidos] = '\0';
    fclose(f);

    char *lineas[60];
    uint16_t total_lineas = 0;
    char *saveptr = NULL;
    char *linea = strtok_r(s_tail_buf, "\r\n", &saveptr);

    if (read_offset > 0 && linea != NULL) {
        linea = strtok_r(NULL, "\r\n", &saveptr);
    }

    while (linea != NULL) {
        if (strncmp(linea, "Fecha", 5) != 0 && strlen(linea) > 15) {
            if (total_lineas < 60) {
                lineas[total_lineas++] = linea;
            } else {
                memmove(&lineas[0], &lineas[1], 59 * sizeof(char *));
                lineas[59] = linea;
            }
        }
        linea = strtok_r(NULL, "\r\n", &saveptr);
    }

    uint16_t inicio = (total_lineas > max_registros) ? (total_lineas - max_registros) : 0;
    size_t pos = snprintf(json_buf, max_len, "{\"sd_ok\":true,\"total\":%lu,\"rows\":[",
                          (unsigned long)s_total_registros_escritos);

    bool primero = true;
    for (uint16_t i = inicio; i < total_lineas; i++) {
        char *p_save = NULL;
        char *c_fecha = strtok_r(lineas[i], ",", &p_save);
        char *c_hora  = strtok_r(NULL, ",", &p_save);
        char *c_fc    = strtok_r(NULL, ",", &p_save);
        char *c_fr    = strtok_r(NULL, ",", &p_save);
        char *c_temp  = strtok_r(NULL, ",", &p_save);
        char *c_hum   = strtok_r(NULL, ",", &p_save);
        char *c_gas   = strtok_r(NULL, ",", &p_save);
        char *c_mov   = strtok_r(NULL, ",", &p_save);

        if (c_fecha && c_hora && c_fc && c_fr && c_temp && c_hum && c_gas && c_mov) {
            int escritos = snprintf(json_buf + pos, max_len - pos,
                                    "%s[\"%s\",\"%s\",%s,%s,%s,%s,%s,%s]",
                                    primero ? "" : ",",
                                    c_fecha, c_hora, c_fc, c_fr, c_temp, c_hum, c_gas, c_mov);
            if (escritos > 0 && (pos + (size_t)escritos + 4) < max_len) {
                pos += (size_t)escritos;
                primero = false;
            } else {
                break;
            }
        }
    }

    pos += snprintf(json_buf + pos, max_len - pos, "]}");
    xSemaphoreGive(s_sd_mutex);
    return (int)pos;
}

esp_err_t MicroSdEnviarCsvCompletoHttp(httpd_req_t *req) {
    if (s_sd_mutex == NULL || xSemaphoreTake(s_sd_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Bus SPI ocupado");
        return ESP_FAIL;
    }

    FILE *f = fopen(MICROSD_CSV_FILEPATH, "r");
    if (f == NULL) {
        xSemaphoreGive(s_sd_mutex);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "DATALOG.CSV no encontrado");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"DATALOG.CSV\"");

    size_t leidos = 0;
    while ((leidos = fread(s_csv_chunk, 1, sizeof(s_csv_chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, s_csv_chunk, leidos) != ESP_OK) {
            break;
        }
    }
    fclose(f);
    xSemaphoreGive(s_sd_mutex);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/*==================[end of file]============================================*/