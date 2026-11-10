#ifndef MICROSD_DATALOGGER_H
#define MICROSD_DATALOGGER_H
/**
 * @file microsd_datalogger.h
 * @brief Driver de la capa de dispositivos para el almacenamiento clínico no volátil en tarjeta
 *        MicroSD de 2GB (formato FAT16), mediante bus SPI y gestión de reloj en tiempo real (RTC).
 * 
 * @details Administra la inicialización del bus SPI2 del ESP32-C6, el montaje del sistema de
 *          archivos FAT sobre una tarjeta MicroSD de 2 GB (FAT16) y el registro periódico
 *          protegido ante cortes de energía mediante ciclos atómicos de apertura, escritura
 *          formateada CSV y cierre con volcado inmediato (\c flush) a la tabla FAT. @n
 *          Asimismo, provee lectura selectiva de los últimos registros para su graficación
 *          en el servidor web embebido y descarga íntegra del archivo \c DATALOG.CSV. @n
 * 
 * @section sd_hw_specs Especificaciones de Hardware, Formato FAT16 y Desacople de GPIO_13 (USB_D+)
 * - <b>Tarjeta y Sistema de Archivos:</b> Tarjeta MicroSD SDSC de 2 GB comerciales (1.85 GB útiles)
 *   formateada en <b>FAT16</b> (\c FAT predeterminado), óptima por su baja sobrecarga de tabla de
 *   asignación de clústeres.
 * - <b>Etapa de Alimentación y Nivelación del Módulo Lector:</b> El módulo se alimenta con <b>+5V DC</b>
 *   en su pin \c VCC (desacoplado localmente con un capacitor electrolítico de <b>220 µF</b>),
 *   integrando en placa un regulador lineal \c AMS1117-3.3 y un buffer adaptador \c 74LVC125A que
 *   opera con niveles lógicos seguros de 3.3V compatibles con el ESP32-C6.
 * - <b>Tratamiento de GPIO_13 (Línea USB_D+):</b> El pin \c GPIO_13 corresponde por defecto a la línea
 *   diferencial \c USB_D+ del periférico interno \c USB_SERIAL_JTAG. Dado que el sistema emplea
 *   exclusivamente el puente \c USB-UART0 para alimentación y telemetría con Teleplot, en \c MicroSdInit()
 *   se invoca \c gpio_config() sobre \c GPIO_13 como salida digital pura con pull-up activo, lo cual
 *   ejecuta internamente \c usb_serial_jtag_ll_phy_enable(false) y libera el pad físico para operar
 *   como línea \c CS (Chip Select) sin interferencias.
 * 
 * @section sd_osc_study Estudio Experimental del Bus SPI con Osciloscopio (Escritura, Cierre y Lectura)
 * Para caracterizar el comportamiento temporal y eléctrico del bus SPI antes de su integración,
 * se implementó un banco de pruebas configurando el <b>Canal 1 (amarillo)</b> del osciloscopio sobre
 * la línea <b>CS (Chip Select)</b> con disparo (<i>Trigger</i>) por flanco descendente, y relevando de
 * forma secuencial con el <b>Canal 2 (verde)</b> las líneas <b>SCK</b>, <b>MOSI</b> y <b>MISO</b>:
 * 
 * 1. <b>Fase 1 - Apertura y Escritura de Datos (\c fopen / \c fprintf):</b>
 *    - <b>CS:</b> Cae de 3.3V/5V a nivel BAJO (0V), habilitando la tarjeta esclava y disparando el osciloscopio.
 *    - <b>SCK:</b> Genera ráfagas de reloj de 4 MHz (SPI Modo 0, 0V en reposo) sincronizando la transferencia.
 *    - <b>MOSI:</b> Intensa actividad transmitiendo el comando de escritura de bloque (\c CMD24), dirección de
 *      sector y la cadena ASCII de caracteres CSV.
 *    - <b>MISO:</b> Permanece mayormente en nivel ALTO con breves respuestas de confirmación (\c 0x00) e
 *      intervalos cortos de estado ocupado (<i>busy</i> en 0V). Duración medida de la ráfaga: <b>≈ 4.5 ms</b>.
 * 2. <b>Fase 2 - Cierre del Archivo y Volcado No Volátil (\c fclose / Flush a Tabla FAT):</b>
 *    - <b>CS:</b> Desciende nuevamente a 0V durante toda la transacción de cierre (<b>≈ 11 ms</b>).
 *    - <b>SCK y MOSI:</b> Emiten múltiples trenes de pulsos actualizando el sector de datos, las copias de la
 *      tabla de asignación FAT16 y la entrada de directorio con el nuevo tamaño del archivo.
 *    - <b>MISO:</b> La tarjeta MicroSD retiene la línea \c MISO en nivel BAJO (0V) mientras su controlador interno
 *      programa físicamente las celdas Flash NAND (<i>busy state</i>). Una vez que \c MISO retorna a nivel ALTO y
 *      \c CS sube a reposo, el dato queda protegido contra cualquier corte súbito de energía eléctrica.
 * 3. <b>Fase 3 - Apertura y Lectura de Verificación (\c fopen / \c fread):</b>
 *    - <b>CS:</b> Cae a 0V durante <b>≈ 2.5 ms</b>.
 *    - <b>SCK:</b> Provee el tren continuo de pulsos de reloj generados por el maestro.
 *    - <b>MOSI:</b> Presenta un único pulso inicial enviando el comando de lectura de bloque (\c CMD17) y luego
 *      permanece en ALTO continuo enviando bytes <i>dummy</i> (\c 0xFF).
 *    - <b>MISO:</b> Presenta alta densidad de conmutación devolviendo hacia el microcontrolador los bytes ASCII
 *      almacenados en el archivo \c DATALOG.CSV.
 * 
 * @section sd_media Video Experimental y Capturas del Datalogger MicroSD
 * - <b>[Registro y visualizacion de datos, y sincronización fecha y hora](https://youtu.be/hTUDn0Bl0bQ)</b> @n
 *   El ESP32 obtiene una fecha y hora actual para vincular a los datos registrados cada diez segundos. Los datos pueden descargarse de la web y visualizarse en forma de gráfica/función en la misma web también. @n
 * 
 * @see [Capacidad y Formato FAT16 de la MicroSD de 2 GB](../formateo_tarjeta_micro_sd_2GB.png)
 * @see [Pinout y Arquitectura del Módulo Lector MicroSD](../pinout_lector_micro_sd.png)
 * @see [Relevamiento en Osciloscopio de Escritura, Cierre y Lectura SPI](../escritura_cierre_y_lectura_micro_sd_por_spi.png)
 * @see [Archivo real generado por el ESP32, y descargado de la propia web - DATALOG.csv](../DATALOG.csv)
 * 
 * @section hardConn Hardware Connection
 * | Pin Módulo MicroSD | Pin ESP32-C6 | Conexión Hardware / Circuito Externo |
 * | :----------------- | :----------- | :----------------------------------- |
 * | CS (Chip Select)   | GPIO_13      | Directo a GPIO_13 (PHY USB_D+ deshabilitado por software) |
 * | SCK (Serial Clock) | GPIO_20      | Directo a GPIO_20 (Reloj SPI Maestro a 4 MHz)             |
 * | MOSI (Master Out)  | GPIO_21      | Directo a GPIO_21 (Datos Maestro -> Tarjeta MicroSD)      |
 * | MISO / MOSO        | GPIO_22      | Directo a GPIO_22 (Datos Tarjeta MicroSD -> Maestro 3.3V) |
 * | VCC                | 5V           | Alimentación +5V DC con capacitor electrolítico de 220 µF a GND |
 * | GND                | GND          | Masa común del sistema (0V)                               |
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-31 | Creación del driver de datalogging clínico CSV en MicroSD por SPI y reloj RTC |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */

/*==================[inclusions]=============================================*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "gpio_mcu.h"
#include "esp_http_server.h"

/*==================[macros and definitions]=================================*/
#define MICROSD_DEFAULT_CS_GPIO     GPIO_13 /*!< Pin CS en GPIO_13 (desacoplado de USB_D+) */
#define MICROSD_DEFAULT_SCK_GPIO    GPIO_20 /*!< Pin SCK en GPIO_20 */
#define MICROSD_DEFAULT_MOSI_GPIO   GPIO_21 /*!< Pin MOSI en GPIO_21 */
#define MICROSD_DEFAULT_MISO_GPIO   GPIO_22 /*!< Pin MISO (MOSO) en GPIO_22 */

#define MICROSD_MOUNT_POINT         "/sdcard"
#define MICROSD_CSV_FILEPATH        "/sdcard/DATALOG.CSV"
#define MICROSD_LOG_PERIOD_MS       10000   /*!< Período de grabación en tarjeta MicroSD: 10 segundos */

/*==================[typedef]================================================*/
/**
 * @brief Estructura de un registro clínico e higrotérmico para almacenamiento en DATALOG.CSV.
 */
typedef struct {
    uint16_t fc_bpm;            /*!< Frecuencia cardíaca en bpm */
    uint16_t fr_rpm;            /*!< Frecuencia respiratoria en rpm */
    int16_t temp_decimas_c;     /*!< Temperatura ambiental en décimas de °C (ej. 254 = 25.4 °C) */
    uint16_t hum_decimas_pct;   /*!< Humedad relativa en décimas de % (ej. 528 = 52.8 %) */
    bool alerta_gases;          /*!< Presencia de gases inflamables en la ventana de 10 s (0: No, 1: Sí) */
    bool movimiento_sala;       /*!< Detección de movimiento PIR en la ventana de 10 s (0: No, 1: Sí) */
} registro_clinico_sd_t;

/*==================[external function declarations]=========================*/
/**
 * @fn bool MicroSdInit(void)
 * @brief Desconecta el transceptor USB_D+ de GPIO_13, inicializa el bus SPI2 a 4 MHz
 *        y monta la partición FAT16 de la tarjeta MicroSD. Crea el encabezado CSV si el archivo no existe.
 * @return true si la tarjeta MicroSD fue montada exitosamente; false si no está insertada o falló.
 */
bool MicroSdInit(void);

/**
 * @fn bool MicroSdEscribirRegistro(const registro_clinico_sd_t *reg)
 * @brief Ejecuta un ciclo atómico de Apertura (Fase 1), Escritura CSV y Cierre con Flush FAT (Fase 2).
 *        Si la tarjeta no estaba montada, reintenta su montaje automáticamente sin bloquear el RTOS.
 * @param[in] reg Puntero a la estructura con las variables fisiológicas y ambientales a registrar.
 * @return true si el registro fue grabado y volcado físicamente a la MicroSD; false en caso de error.
 */
bool MicroSdEscribirRegistro(const registro_clinico_sd_t *reg);

/**
 * @fn bool MicroSdEstadoMontada(void)
 * @brief Consulta si la tarjeta MicroSD se encuentra operativa y con el sistema FAT montado.
 * @return true si está lista; false si hubo error o fue extraída.
 */
bool MicroSdEstadoMontada(void);

/**
 * @fn uint32_t MicroSdObtenerTotalEscritos(void)
 * @brief Devuelve la cantidad de registros grabados exitosamente desde el encendido del sistema.
 * @return Número de líneas CSV escritas en la sesión actual.
 */
uint32_t MicroSdObtenerTotalEscritos(void);

/**
 * @fn void MicroSdSincronizarRelojEpoch(int64_t epoch_sec)
 * @brief Ajusta el reloj de tiempo real (RTC) interno del ESP32-C6 a partir de un timestamp Unix (segundos)
 *        recibido desde el navegador web (ajustado a huso horario local UTC-3).
 * @param[in] epoch_sec Segundos transcurridos desde 1970-01-01 00:00:00 en hora local.
 */
void MicroSdSincronizarRelojEpoch(int64_t epoch_sec);

/**
 * @fn void MicroSdObtenerFechaHoraActual(char *out_fecha, size_t len_f, char *out_hora, size_t len_h)
 * @brief Obtiene las cadenas formateadas de fecha (\c YYYY-MM-DD) y hora (\c HH:MM:SS) actuales del RTC.
 * @param[out] out_fecha Buffer de salida para la fecha (mínimo 11 bytes).
 * @param[in] len_f Tamaño del buffer de fecha.
 * @param[out] out_hora Buffer de salida para la hora (mínimo 9 bytes).
 * @param[in] len_h Tamaño del buffer de hora.
 */
void MicroSdObtenerFechaHoraActual(char *out_fecha, size_t len_f, char *out_hora, size_t len_h);

/**
 * @fn bool MicroSdRelojSincronizado(void)
 * @brief Indica si el reloj RTC ya fue sincronizado con la fecha/hora real desde la interfaz web.
 * @return true si tiene fecha actual válida; false si corre sobre la fecha base inicial.
 */
bool MicroSdRelojSincronizado(void);

/**
 * @fn int MicroSdLeerUltimosRegistrosJson(char *json_buf, size_t max_len, uint16_t max_registros)
 * @brief Ejecuta la Fase 3 (Apertura y Lectura SPI con \c CMD17) posicionándose al final de
 *        \c DATALOG.CSV mediante \c fseek() para extraer los últimos \p max_registros en formato JSON.
 * @param[out] json_buf Buffer donde se escribe el arreglo JSON de registros históricos.
 * @param[in] max_len Capacidad máxima en bytes del buffer \p json_buf.
 * @param[in] max_registros Cantidad máxima de puntos finales a recuperar (ej. 40).
 * @return Cantidad de bytes escritos en \p json_buf.
 */
int MicroSdLeerUltimosRegistrosJson(char *json_buf, size_t max_len, uint16_t max_registros);

/**
 * @fn esp_err_t MicroSdEnviarCsvCompletoHttp(httpd_req_t *req)
 * @brief Lee el archivo \c DATALOG.CSV en bloques por SPI y lo transmite por streaming HTTP
 *        para su descarga directa en la PC o teléfono.
 * @param[in] req Puntero a la petición activa del servidor HTTP.
 * @return \c ESP_OK si la transferencia finalizó correctamente.
 */
esp_err_t MicroSdEnviarCsvCompletoHttp(httpd_req_t *req);

#endif /* MICROSD_DATALOGGER_H */
/*==================[end of file]============================================*/