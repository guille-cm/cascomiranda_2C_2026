#ifndef WEB_UNIDAD_PACIENTE_H
#define WEB_UNIDAD_PACIENTE_H
/**
 * @file web_unidad_paciente.h
 * @brief Módulo de conectividad Wi-Fi (Modo Estación) y Servidor Web HTTP embebido para la
 *        supervisión clínica en tiempo real, control remoto de alarmas, sincronización de reloj
 *        RTC y graficación histórica desde tarjeta MicroSD mediante dirección IPv4.
 *
 * @details Gestiona la conexión automática a la red Wi-Fi de 2.4 GHz del centro de salud u hogar
 *          y aloja un servidor HTTP ligero en el puerto TCP 80. Mientras el operador no ingrese un
 *          nombre de paciente ni un mensaje personalizado desde la página web, la función
 *          \c WebUnidadPacienteObtenerTextoFila0() suministra automáticamente la dirección IPv4
 *          asignada por el router para que permanezca visible en la Fila 0 del display LCD 16x2. @n
 * 
 * @section web_comm_strategy Arquitectura de Conectividad Wi-Fi STA, Protocolo HTTP/REST, Interfaz Gráfica e Integración con RTC y MicroSD
 * 
 * ### 1. Estrategia de Conexión Wi-Fi (Modo Estación 802.11 b/g/n) e Intercambio con el Router
 * El microcontrolador ESP32-C6 se configura como cliente inalámbrico (<b>Modo Estación / \c WIFI_MODE_STA</b>)
 * para integrarse a la infraestructura de red local existente del centro de salud. El proceso de enlace
 * sigue una máquina de estados asincrónica gobernada por el bucle de eventos del sistema (\c esp_event):
 * 1. <b>Inicialización de Memoria No Volátil (NVS) y Pila TCP/IP (\c lwIP):</b> En \c WebUnidadPacienteInit()
 *    se inicializa la partición Flash NVS (donde el transceptor de radiofrecuencia almacena los parámetros de
 *    calibración PHY y claves de red) y se crea la interfaz de red virtual (\c esp_netif_create_default_wifi_sta())
 *    vinculada a la pila ligera TCP/IP <b>lwIP</b>.
 * 2. <b>Barrido, Autenticación y Asociación IEEE 802.11:</b> Al dispararse el evento \c WIFI_EVENT_STA_START,
 *    el driver invoca \c esp_wifi_connect(). El radio busca las tramas <i>Beacon</i> del punto de acceso (router)
 *    cuyo identificador coincide con \c WIFI_SSID_DEFAULT, intercambia las tramas de Autenticación Abierta
 *    y Solicitud de Asociación (<i>Association Request / Response</i>), y ejecuta el protocolo criptográfico
 *    <b>4-Way Handshake WPA2-PSK (AES-CCMP)</b> empleando la clave \c WIFI_PASS_DEFAULT.
 * 3. <b>Negociación DHCP (<i>Dynamic Host Configuration Protocol</i>):</b> Una vez establecido el enlace de capa 2,
 *    el cliente DHCP de la pila lwIP intercambia con el router los cuatro paquetes UDP de configuración de red:
 *    <b>DHCP Discover → DHCP Offer → DHCP Request → DHCP ACK</b>.
 * 4. <b>Obtención de Dirección IPv4 y Reconexión Automática:</b> Al completarse el protocolo DHCP, el router
 *    otorga la dirección IPv4 (por ejemplo, \c 192.168.1.7), disparando el evento \c IP_EVENT_STA_GOT_IP.
 *    El manejador convierte los 4 octetos binarios de la dirección a cadena ASCII mediante la macro \c IP2STR
 *    y la almacena en \c s_ip_asignada, mostrándola inmediatamente en la Fila 0 del display LCD 16x2 para que
 *    el personal médico conozca a qué dirección conectarse desde cualquier teléfono celular, tablet o PC.
 *    Si el enlace cae temporalmente (\c WIFI_EVENT_STA_DISCONNECTED), el manejador reintenta la conexión de
 *    manera automática sin bloquear las tareas de monitoreo fisiológico.
 * 
 * ### 2. Estructura Gráfica del Dashboard Web (HTML5 + CSS3 + SVG + Canvas en Memoria Flash)
 * El documento completo de la aplicación web de página única (<i>Single-Page Application</i>, \c INDEX_HTML[])
 * reside en la sección \c .rodata de la memoria Flash y está estructurado visualmente en tarjetas (\c .card)
 * responsivas adaptables a pantallas de teléfonos móviles, tablets y computadoras de escritorio:
 * - <b>Encabezado Global (\c .header):</b> Muestra el título de la unidad, la dirección IPv4 activa, el estado
 *   actual de las teclas locales de simulación (\c SWITCH_1 para FC y \c SWITCH_2 para FR) y un indicador
 *   maestro superior (\c SISTEMA NORMAL en verde o \c ALERTA ACTIVA (N) con animación de parpadeo rojo).
 * - <b>Tarjeta Unificada de Signos Vitales y Estado del Paciente:</b> Agrupa dentro de un mismo contenedor
 *   visual dividido por bordes horizontales (\c .patient-sub-block) los tres indicadores clínicos esenciales:
 *   1. <b>Frecuencia Cardíaca (ECG):</b> Valor numérico en \c bpm y etiqueta de estado a su derecha acompañada de
 *      un ícono de corazón cuya animación CSS (\c @keyframes beat) ajusta dinámicamente su período
 *      de latido a <b>T = 60 / FC segundos</b> para latir en sincronía exacta con el pulso del paciente.
 *   2. <b>Frecuencia Respiratoria:</b> Valor numérico en \c rpm y etiqueta de estado a su derecha con un gráfico
 *      vectorial <b>SVG de pulmones</b> cuya animación de expansión y contracción (\c @keyframes breathe) ajusta su
 *      período en tiempo real a <b>T = 60 / FR segundos</b>.
 *   3. <b>Estado del Paciente:</b> Informa la condición general con un ícono dinámico (rostro sonriente
 *      con etiqueta verde \c NORMAL en reposo, que conmuta a rostro afligido con etiqueta roja
 *      parpadeante \c ATENCIÓN y leyenda <i>"Paciente requiere atención!"</i> cuando el paciente acciona el control
 *      remoto inalámbrico de 433 MHz sobre \c GPIO_8).
 * - <b>Tarjeta Ambiente:</b> Presenta en filas apiladas verticalmente la <b>Temperatura</b> (rango normal <b>15 a 35 °C</b>),
 *   la <b>Humedad Relativa</b> (rango normal <b>30 a 80 %</b>), el estado de <b>Gases inflamables</b> (indicando el
 *   minuto de calentamiento inicial o la duración en segundos del pulso de detección) y el <b>Movimiento de personas</b>
 *   junto con su contador histórico de eventos PIR. Cada magnitud posee su propio recuadro (\c .tag-amb) que pasa de
 *   verde a rojo intermitente cuando la variable sale de rango o permanece encolada en la FIFO.
 * - <b>Tarjeta de Identificación de Paciente y Mensajes al LCD 16x2:</b> Permite visualizar en vivo el texto de la
 *   Fila 0 del display físico, ingresar el nombre del paciente o enviar mensajes predefinidos a la sala (incluyendo
 *   <i>"AYUDA EN CAMINO"</i>, que dispara simultáneamente la locución hablada LPCM en el parlante).
 * - <b>Tarjeta de Cola FIFO de Alarmas y Aviso Sonoro:</b> Muestra la cantidad de alertas pendientes, destaca centrada
 *   horizontalmente la primera alerta a atender (frente de la cola FIFO) y ofrece tres botones de comando remoto:
 *   desactivar la alerta más antigua, disparar la melodía de advertencia (<i>Pacman</i>) o reproducir la voz LPCM.
 * - <b>Tarjeta de Registro en MicroSD y Gráficas Históricas:</b> Presenta en líneas independientes el estado físico de
 *   la tarjeta (\c MONTADA (FAT16) o \c NO DETECTADA), el total de registros grabados y la fecha/hora actual del
 *   reloj RTC del ESP32-C6, junto con controles para sincronizar la hora, descargar el archivo \c DATALOG.CSV
 *   completo o desplegar <b>5 gráficas históricas de puntos y líneas apiladas verticalmente</b> dibujadas mediante
 *   la API 2D de \c canvas (1. FC con bandas límite en 50-130 bpm; 2. FR con bandas en 12-20 rpm; 3. Temperatura
 *   con bandas en 15-35 °C; 4. Humedad con bandas en 30-80 %; 5. Eventos discretos 0/1 de Gases y Movimiento).
 * 
 * ### 3. Mecanismo de Comunicación Cliente-Servidor (Polling JSON cada 1 s y Comandos POST REST)
 * La interacción entre el navegador web y el servidor HTTP del ESP32-C6 se realiza de forma asíncrona mediante
 * la API \c fetch() de JavaScript sin recargar la página:
 * - <b>Actualización de Telemetría (\c GET /api/status):</b> Un temporizador JavaScript (\c setInterval(upd, 1000))
 *   solicita cada 1 segundo el documento JSON de estado. El servidor toma una copia atómica de la estructura
 *   \c s_datos_actuales protegida por un <i>spinlock</i> de hardware (\c portENTER_CRITICAL(&s_web_spinlock)), formatea
 *   las décimas de temperatura y humedad en punto fijo y responde utilizando el buffer estático \c s_json_status_buf
 *   alojado en la sección \c .bss para no sobrecargar la pila de la tarea \c httpd.
 * - <b>Envío de Órdenes Remotas (\c POST):</b> Cuando el usuario presiona un botón en la interfaz, el navegador emite
 *   una petición \c POST hacia el endpoint correspondiente (\c /api/patient, \c /api/msg, \c /api/clear_alert,
 *   \c /api/sound_warn, \c /api/play_voice). Los manejadores HTTP en el ESP32-C6 activan banderas booleanas
 *   volátiles protegidas por sección crítica (\c s_req_clear_alert, \c s_req_warn_3s, \c s_req_play_voice), las
 *   cuales son consumidas cada 20 ms por \c TaskPotenciometro_LED mediante las funciones \c WebUnidadPacienteConsumir...().
 * 
 * ### 4. Sincronización de Fecha/Hora (RTC) e Interacción con la Tarjeta MicroSD
 * - <b>Sincronización Transparente y Manual del Reloj RTC (\c POST /api/time):</b> En la primera respuesta de
 *   \c /api/status, el navegador inspecciona el campo booleano \c d.rtc_ok (provisto por \c MicroSdRelojSincronizado()).
 *   Si es \c false (el microcontrolador acaba de encenderse y corre sobre su fecha base), el script de la página
 *   obtiene automáticamente la hora del teléfono o PC (\c new Date()), compensa el huso horario local restando
 *   \c getTimezoneOffset() * 60000, convierte el valor a segundos Unix y lo envía en segundo plano a \c /api/time.
 *   El manejador \c post_time_handler convierte la cadena a entero de 64 bits (\c atoll) e invoca
 *   \c MicroSdSincronizarRelojEpoch() para poner en hora el RTC del ESP32-C6 sin intervención humana. Adicionalmente,
 *   el usuario puede fijar cualquier fecha y hora manualmente con el selector \c datetime-local.
 * - <b>Supervisión, Graficación de los Últimos Registros (\c GET /api/sd_history) y Descarga (\c GET /api/sd_csv):</b>
 *   El estado de conexión de la tarjeta y el número de líneas escritas viajan cada segundo en \c /api/status (\c sd_ok
 *   y \c sd_n). Cuando el usuario pulsa <i>"Consultar / Actualizar Gráficas desde MicroSD"</i>, el navegador solicita
 *   \c GET /api/sd_history; el servidor invoca \c MicroSdLeerUltimosRegistrosJson() sobre el buffer estático
 *   \c s_json_history_buf (3072 bytes en \c .bss), que lee mediante \c fseek() el tramo final de \c DATALOG.CSV
 *   en el bus SPI (bajo protección de Mutex) y devuelve las últimas muestras en formato de matriz JSON para que
 *   \c drawPlot() y \c drawEvents() tracen las 5 curvas apiladas, coloreando en rojo los puntos que excedan los
 *   umbrales clínicos. Si el usuario pulsa <i>"Descargar DATALOG.CSV"</i>, el endpoint \c GET /api/sd_csv invoca
 *   \c MicroSdEnviarCsvCompletoHttp(), transmitiendo el archivo completo en fragmentos de 512 bytes.
 * 
 * @section web_operation Flujo Operacional del Texto en la Fila 0 del Display LCD 16x2
 * - <b>Arranque inicial:</b> Antes de obtener IP del router, el texto base de la Fila 0 es \c "BIENVENIDO!".
 * - <b>Conexión establecida:</b> Al recibir el evento \c IP_EVENT_STA_GOT_IP, el texto base de la Fila 0
 *   pasa a mostrar permanentemente la dirección IPv4 asignada (ej. \c "192.168.1.7").
 * - <b>Personalización remota:</b> Cuando el operador guarda un nombre de paciente o selecciona un mensaje
 *   desde el navegador web, dicho texto reemplaza a la dirección IP en la Fila 0 (siempre que no
 *   existan alertas activas en la cola FIFO ni una ventana de simulación local abierta).
 * 
 * @section web_media Videos Experimentales de Conectividad Wi-Fi, Control Remoto y Diseño Web
 * - <b>[Velocidad de enlace a la red local en 1 segundo](https://youtu.be/DE7KnygfEy8)</b> @n
 *   Tiempo de enlace a la red local inferior a 2 segundos (observado desde el arranque en frío tras el flasheo). Para la conexión mDNS y DNS público, inferior a 3 segundos. @n
 * - <b>[Conexion WiFi LAN y visualizacion web](https://youtu.be/K3T0wrf6PqI)</b> @n
 *   Se comprueba la conexión esperada entre el ESP32 y la red WiFi LAN creada para tal fin y se visualizan las variables registradas por la unidad paciente. @n
 * - <b>[Ejecución de comandos vía web](https://youtube.com/shorts/NUhS4IJ7fCM)</b> @n
 *   Comunicación WiFi LAN desde notebook hacia ESP32 y ejecución remota de órdenes vía WiFi. @n
 * - <b>[Ejecución de comandos celular/tablet](https://youtu.be/ey6NkgmBr6s)</b> @n
 *   Comunicación desde el celular o tablet hacia el monitor unidad paciente y lectura inversa de las variables que el monitor maneja. @n
 * - <b>[Registro y visualizacion de datos, y sincronización fecha y hora](https://youtu.be/hTUDn0Bl0bQ)</b> @n
 *   Sincronización de fecha y hora actual con el navegador para vincular a los datos registrados cada diez segundos, descarga de CSV y visualización de gráficas en la web. @n
 * - <b>[Diseño web](https://youtu.be/uvIX7M69ZE0)</b> @n
 *   Diseño web online en tiempo real desde phcode.dev/tools/es/html-viewer.html. @n
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-08 | Creación del módulo de conectividad Wi-Fi STA y servidor HTTP |
 * | 2026-10-09 | Simplificación sin mDNS, IP persistente en Fila 0 hasta cambio web y estilo FIFO |
 * | 2026-10-09 | Reorganización de los elementos, y agregado de funcionalidades como decarga y visualización de datos en microSD |
 * 
 * @version 1.2
 * @author Guillermo Casco Miranda
 */
/*==================[inclusions]=============================================*/
#include <stdint.h>
#include <stdbool.h>

/*==================[macros and definitions]=================================*/
/** @brief Nombre de la red Wi-Fi (SSID) de 2.4 GHz a la que se conecta la estación (reemplazar antes de compilar) */
#define WIFI_SSID_DEFAULT       "Mi_humilde_red"

/** @brief Contraseña WPA2-PSK de la red Wi-Fi (reemplazar antes de compilar) */
#define WIFI_PASS_DEFAULT       "contraseniasencilla1234"

/** @brief Longitud máxima de caracteres visibles en una fila del display LCD 16x2 */
#define WEB_LCD_TEXT_MAX_LEN    16

/*==================[typedef]================================================*/
/**
 * @brief Estructura integral de datos del sistema publicada hacia el Dashboard Web cada 200 ms por \c TaskDisplayLCD.
 */
typedef struct {
    uint16_t fc_bpm;                 /*!< Frecuencia cardíaca actual en bpm (rango 40 a 140) */
    uint16_t fr_rpm;                 /*!< Frecuencia respiratoria actual en rpm (rango 8 a 24) */
    bool alerta_fc;                  /*!< \c true si FC está fuera de [50, 130] bpm o permanece en la cola FIFO */
    bool alerta_fr;                  /*!< \c true si FR está fuera de [12, 20] rpm o permanece en la cola FIFO */
    uint8_t modo_sim_local;          /*!< 0: Ajuste inactivo, 1: Simulando FC (Tecla 1), 2: Simulando FR (Tecla 2) */
    int16_t temp_decimas_c;          /*!< Temperatura medida por el DHT22 en décimas de °C (ej. 254 = 25.4 °C) */
    uint16_t hum_decimas_pct;        /*!< Humedad relativa medida por el DHT22 en décimas de % (ej. 528 = 52.8 %) */
    bool mq3_precalentado;           /*!< \c true cuando se cumplieron los 60 s iniciales de calentamiento del MQ-3 */
    bool mq3_alcohol_detectado;      /*!< \c true si hay presencia activa de alcohol/gas o alerta en la cola FIFO */
    uint32_t mq3_segundos_bajo;      /*!< Duración continua en segundos del pulso actual de detección de gases */
    bool pir_movimiento;             /*!< \c true si hay detección activa en el sensor PIR o alerta en la cola FIFO */
    uint32_t pir_contador_eventos;   /*!< Cantidad acumulada de eventos de movimiento detectados desde el arranque */
    uint8_t alertas_fifo_cant;       /*!< Cantidad total de alertas alojadas actualmente en la cola FIFO */
    char alerta_fifo_actual[17];     /*!< Cadena de texto de la alerta más antigua ubicada en el frente de la FIFO */
    bool sd_montada;                 /*!< \c true si la tarjeta MicroSD FAT16 se encuentra montada y operativa */
    uint32_t sd_registros_totales;   /*!< Cantidad total de registros grabados en DATALOG.CSV durante la sesión */
    char fecha_actual[12];           /*!< Fecha actual del reloj RTC en formato YYYY-MM-DD */
    char hora_actual[10];            /*!< Hora actual del reloj RTC en formato HH:MM:SS */
    bool paciente_requiere_atencion; /*!< \c true si se activó el llamado de paciente mediante el relé en GPIO_8 */
    int16_t temp_min_c;              /*!< Umbral mínimo de temperatura en °C enteros configurado en main */
    int16_t temp_max_c;              /*!< Umbral máximo de temperatura en °C enteros configurado en main */
    uint16_t hum_min_pct;            /*!< Umbral mínimo de humedad en % entero configurado en main */
    uint16_t hum_max_pct;            /*!< Umbral máximo de humedad en % entero configurado en main */
} estado_unidad_paciente_t;

/*==================[external function declarations]=========================*/
/**
 * @fn void WebUnidadPacienteInit(void)
 * @brief Inicializa la partición Flash NVS, la pila TCP/IP lwIP, el bucle de eventos, el cliente Wi-Fi
 *        en Modo Estación (STA) y pone en marcha el servidor HTTP en el puerto 80 registrando sus 10 URI handlers.
 */
void WebUnidadPacienteInit(void);

/**
 * @fn void WebUnidadPacienteActualizarDatos(const estado_unidad_paciente_t *nuevo_estado)
 * @brief Actualiza de forma atómica (mediante spinlock de sección crítica) la estructura interna de telemetría
 *        que es servida en formato JSON a través del endpoint \c GET /api/status.
 * @param[in] nuevo_estado Puntero constante a la estructura con los valores actuales recopilados en el sistema.
 */
void WebUnidadPacienteActualizarDatos(const estado_unidad_paciente_t *nuevo_estado);

/**
 * @fn void WebUnidadPacienteObtenerTextoFila0(char *buffer_17_bytes)
 * @brief Obtiene de forma segura el texto normalizado de hasta 16 caracteres para la Fila 0 del LCD en condición normal:
 *        1. Si aún no conectó al Wi-Fi, devuelve \c "BIENVENIDO!".
 *        2. Si el operador seleccionó un mensaje predefinido desde la web, devuelve dicho mensaje.
 *        3. Si el operador guardó un nombre de paciente desde la web, devuelve el nombre del paciente.
 *        4. Si conectó al Wi-Fi y ambos campos web están vacíos, devuelve la dirección IPv4 asignada (ej. \c "192.168.1.7").
 * @param[out] buffer_17_bytes Búfer de salida de al menos 17 bytes donde se copia la cadena terminada en \c '\0'.
 */
void WebUnidadPacienteObtenerTextoFila0(char *buffer_17_bytes);

/**
 * @fn bool WebUnidadPacienteConsumirSolicitudClearAlert(void)
 * @brief Verifica y consume atómicamente la bandera activada por el botón web "Desactivar Alerta más Antigua (FIFO)".
 * @return \c true una única vez por cada pulsación del botón en el navegador web; \c false si no hubo solicitud.
 */
bool WebUnidadPacienteConsumirSolicitudClearAlert(void);

/**
 * @fn bool WebUnidadPacienteConsumirSolicitudAdvertencia3s(void)
 * @brief Verifica y consume atómicamente la bandera activada por el botón web "Tono Advertencia (3s)".
 * @return \c true una única vez por cada pulsación del botón en el navegador web; \c false si no hubo solicitud.
 */
bool WebUnidadPacienteConsumirSolicitudAdvertencia3s(void);

/**
 * @fn bool WebUnidadPacienteConsumirSolicitudVozAyuda(void)
 * @brief Verifica y consume atómicamente la solicitud de reproducción del mensaje de voz LPCM ("Ayuda en camino").
 * @return \c true si se solicitó reproducir el mensaje de voz desde la web; \c false en caso contrario.
 */
bool WebUnidadPacienteConsumirSolicitudVozAyuda(void);

#endif /* WEB_UNIDAD_PACIENTE_H */
/*==================[end of file]============================================*/