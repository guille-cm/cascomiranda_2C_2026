 /**
 * @file monitor_unidad_paciente.c
 * @brief Sistema embebido de monitoreo clínico, supervisión ambiental, registro no volátil
 *        y asistencia remota para una unidad de paciente basado en FreeRTOS sobre ESP32-C6.
 * 
 * @details El sistema constituye un prototipo integral de una unidad de monitoreo hospitalario
 *          para sala de internación, combinando la generación y adquisición en tiempo real de
 *          señales fisiológicas (electrocardiograma y respirograma), el relevamiento continuo de
 *          variables higrotérmicas y de seguridad ambiental, el almacenamiento no volátil en
 *          tarjeta MicroSD, la gestión priorizada de alarmas audiovisuales (incluyendo síntesis
 *          de voz digital LPCM y melodías RTTTL) y la telemetría bidireccional mediante un
 *          servidor web HTTP embebido. @n 
 *          
 *          Su arquitectura modular permite proyectar la incorporación futura de transductores
 *          biomédicos analógicos o digitales adicionales sin alterar el núcleo del sistema.
 *          Asimismo, al operar como cliente inalámbrico (Modo Estación) dentro de la red local
 *          del centro de salud, múltiples unidades de monitoreo pueden coexistir simultáneamente,
 *          siendo identificadas de forma individual a través de la dirección IPv4 exhibida en su
 *          pantalla LCD local al establecer enlace con el router. @n 
 * 
 * @section arq_modular Arquitectura de Software en Capas, Comunicaciones e Integración en FreeRTOS
 * 
 * @subsection cap_diseno 1. Programación Modular y Diseño en Capas (Layered Architecture)
 * El firmware ha sido estructurado siguiendo un modelo estricto de abstracción por capas que
 * desacopla la lógica clínica de alto nivel de los registros físicos del microcontrolador ESP32-C6:
 * - <b>Capa de Aplicación (\c monitor_unidad_paciente.c):</b> Actúa como director de orquesta del
 *   sistema. Define las políticas clínicas (umbrales de bradicardia/taquicardia, bradipnea/taquipnea
 *   y rangos higrotérmicos), administra la cola FIFO circular de alertas, ejecuta el procesamiento
 *   digital de señales (acumuladores de fase DDS y filtrado exponencial IIR) y coordina las tareas
 *   concurrentes del sistema operativo FreeRTOS.
 * - <b>Capa de Dispositivos (\c drivers/devices/):</b> Encapsula el protocolo específico de cada
 *   periférico externo exponiendo interfaces de programación (APIs) limpias y orientadas a acciones:
 *   - \c display_16x2_i2c.h / \c .c : Controlador de pantalla alfanumérica HD44780 en modo de 4 bits
 *     a través del expansor de E/S I2C PCF8574 (dirección \c 0x27).
 *   - \c dht22_sensor.h / \c .c : Decodificador Single-Wire no bloqueante por captura de flancos
 *     descendentes mediante interrupción (ISR) en \c GPIO_9.
 *   - \c mq3_alc_sensor.h / \c .c : Supervisor de gases inflamables y vapores de alcohol en \c GPIO_19
 *     con ventana de inhibición térmica inicial de 60 segundos y cronometraje de pulso activo.
 *   - \c hcsr501_pir_sensor.h / \c .c : Detector piroeléctrico de movimiento en \c GPIO_12 con
 *     supresión de pulso espurio de encendido (<i>Power-On Reset</i>) durante los primeros 3 segundos.
 *   - \c relevador_remoto.h / \c .c : Gestor de entrada digital acondicionada en \c GPIO_8 vinculada
 *     al contacto seco Normalmente Abierto (NO) del receptor RF de 433 MHz (\c RX1C).
 *   - \c aviso_sonoro.h / \c .c + \c sonidos_lpcm/ayuda_en_camino.h + \c buzzer_melodies.h : Motor
 *     acústico asíncrono en \c GPIO_23 capaz de sintetizar voz hablada LPCM a 8 kHz sobre portadora
 *     PWM Clase D de 32 kHz y ejecutar partituras musicales RTTTL en segundo plano.
 *   - \c microsd_datalogger.h / \c .c : Controlador de almacenamiento masivo FAT16 sobre bus SPI2
 *     (\c GPIO_13, \c GPIO_20, \c GPIO_21, \c GPIO_22) y administrador del reloj de tiempo real (RTC).
 *   - \c web_unidad_paciente.h / \c .c : Cliente Wi-Fi IEEE 802.11 STA y servidor HTTP REST con
 *     interfaz gráfica HTML5/Canvas alojada en memoria Flash (\c .rodata).
 *   - \c led.h y \c switch.h : Controladores de señalización luminosa y teclas de la placa base.
 * - <b>Capa de Abstracción de Hardware y Microcontrolador (\c drivers/microcontroller/ y ESP-IDF):</b>
 *   Gestiona los periféricos internos del SoC RISC-V (\c analog_io_mcu, \c timer_mcu, \c uart_mcu,
 *   \c i2c_mcu, \c gpio_mcu, \c esp_driver_ledc, \c esp_driver_spi, \c esp_timer, \c fatfs, \c lwip).
 * 
 * @subsection com_buses 2. Síntesis de Protocolos de Comunicación e Interfaces Físicas
 * El módulo principal coordina de manera simultánea ocho tecnologías de interfaz hardware:
 * 1. <b>Modulación Sigma-Delta (DAC) y Conversión SAR (ADC) en Lazo Cerrado (500 Hz):</b>
 *    Los canales Sigma-Delta en \c GPIO_0 (ECG) y \c GPIO_18 (Respirograma) generan trenes de pulsos
 *    de alta frecuencia cuyo valor medio es reconstruido por sendos filtros pasabajos pasivos RC
 *    externos (<b>R = 100 kΩ</b>, <b>C = 100 nF</b>, frecuencia de corte <b>f<sub>c</sub> ≈ 15.9 Hz</b>).
 *    Las tensiones analógicas suavizadas reingresan a los canales del conversor analógico-digital de
 *    aproximaciones sucesivas (SAR ADC) \c CH1 (\c GPIO_1) y \c CH3 (\c GPIO_3), mientras que el canal
 *    \c CH2 (\c GPIO_2) digitaliza la posición del potenciómetro de ajuste manual.
 * 2. <b>Comunicación Serie Asincrónica UART (115200 bps, 8N1):</b>
 *    Vincula el microcontrolador con la herramienta gráfica Teleplot en la PC a través del puente
 *    USB-UART0. Para impedir que la preempción del sistema operativo fragmente o mezcle los textos de
 *    ambas señales, \c TaskLeerECG_UART construye paquetes atómicos unificados mediante \c snprintf()
 *    y los despacha en una única invocación a \c UartSendString().
 * 3. <b>Bus Serie Sincrónico I2C (100 kHz, Open-Drain con Conversión de Nivel 3.3V a 5V):</b>
 *    A través de las líneas \c SDA (\c GPIO_6) y \c SCL (\c GPIO_7), el maestro envía tramas de 27 bits
 *    hacia la dirección \c 0x27 (\c 0x4E en escritura) del expansor PCF8574 para gobernar en modo de
 *    4 bits (\c D7..D4) y líneas de control (\c RS, \c RW, \c EN, \c BL) al display LCD 16x2.
 * 4. <b>Protocolo Bidireccional Single-Wire por Captura de Tiempo en ISR (DHT22 en GPIO_9):</b>
 *    La tarea ambiental inicia la lectura con un pulso bajo de <b>2 ms</b> y cede la CPU en estado
 *    \c Blocked durante <b>20 ms</b>; entretanto, una rutina de interrupción alojada en memoria IRAM
 *    estampa con \c esp_timer_get_time() cada flanco descendente de los 40 bits de datos, discriminando
 *    ceros (<b>≈ 76 µs</b>) y unos (<b>≈ 120 µs</b>) con inmunidad absoluta frente
 *    a las interrupciones de <b>500 Hz</b>.
 * 5. <b>Bus Serie Sincrónico Full-Duplex SPI2 (4 MHz, Modo 0) y Sistema FAT16:</b>
 *    Conecta el lector de tarjeta MicroSD de 2 GB mediante las líneas \c CS (\c GPIO_13, con el
 *    transceptor \c USB_D+ desacoplado por software), \c SCK (\c GPIO_20), \c MOSI (\c GPIO_21) y
 *    \c MISO (\c GPIO_22). Cada 10 segundos ejecuta las Fases 1 y 2 relevadas en osciloscopio
 *    (apertura en modo <i>append</i>, escritura de línea CSV y cierre \c fclose() con <i>flush</i> inmediato a la
 *    tabla FAT16 mientras \c MISO indica estado <i>busy</i>), resguardando el historial clínico ante cortes
 *    de suministro eléctrico.
 * 6. <b>Modulación PWM Clase D (32 kHz) y Etapa de Potencia Push-Pull Aislada (GPIO_23):</b>
 *    El periférico \c LEDC y el temporizador \c SYSTIMER sintetizan tanto ondas cuadradas tonales
 *    (melodías RTTTL) como audio digital LPCM de 8 bits a <b>8000 Hz</b> (<b>125 µs</b> por
 *    muestra). La señal en \c GPIO_23 excita a través de un resistor de <b>220 Ω</b> las bases del par
 *    complementario NPN/PNP (\c C1815 / \c A1015), cuyo colector NPN está desacoplado del bus de <b>+5V</b>
 *    mediante un diodo \c 1N4007 y un capacitor tanque electrolítico de <b>1000 µF</b> a masa,
 *    atacando al parlante dinámico de <b>4 Ω</b> mediante un capacitor de acople de <b>220 µF</b> y
 *    un resistor serie de <b>10 Ω</b>.
 * 7. <b>Elevación Conmutada Step-Up (5V a 12V) y Acondicionamiento Antirrebote RC en GPIO_8:</b>
 *    A partir de los <b>+5V</b> del puerto USB (filtrados a la entrada con <b>100 µF</b> electrolítico
 *    y <b>100 nF</b> cerámico), un convertidor elevador \c MT3608 genera <b>+12.0V</b> (filtrados a la
 *    salida con <b>220 µF / 35V</b> y <b>100 nF</b>) para alimentar al módulo receptor de
 *    radiofrecuencia de <b>433 MHz</b> (\c RX1C). Sus contactos secos aislados (\c COM a \c GND y \c NO
 *    al nodo de señal) gobiernan el pin \c GPIO_8 mediante una red de acondicionamiento idéntica a la de
 *    las teclas del sistema: resistor <i>pull-up</i> de <b>10 kΩ</b> a <b>+3.3V</b>, capacitor cerámico
 *    lenteja \c 104 (<b>100 nF</b>) a masa para supresión física de rebotes mecánicos, y resistor serie de
 *    protección de <b>1 kΩ</b> hacia el pin \c GPIO_8.
 * 8. <b>Conectividad Inalámbrica Wi-Fi (IEEE 802.11 b/g/n) y Servidor HTTP REST:</b>
 *    Provee telemetría JSON cada <b>1 s</b> (\c /api/status), sincronización automática y manual del
 *    reloj RTC (\c /api/time), consulta de las últimas 40 muestras de la MicroSD para graficación en
 *    5 lienzos Canvas apilados (\c /api/sd_history), descarga del archivo \c DATALOG.CSV completo
 *    (\c /api/sd_csv) y recepción de comandos remotos de control.
 * 
 * @subsection rtos_arch 3. Arquitectura Multitarea en FreeRTOS: Prioridades, Estados y Sincronización
 * El diseño explota el planificador apropiativo (<i>preemptive scheduler</i>) por prioridades de FreeRTOS,
 * organizando la carga computacional en cinco tareas de aplicación más los servicios de sistema:
 * 
 * | Tarea FreeRTOS / ISR       | Prioridad | Stack (Bytes) | Activación / Período | Rol Funcional y Transición de Estados (Running / Ready / Blocked) |
 * | :------------------------- | :-------: | :-----------: | :------------------- | :---------------------------------------------------------------- |
 * | \c FuncTimerA / \c B (ISR) | Hardware  | Contexto ISR  | Cada 2000 µs (500 Hz) | Interrupciones de hardware (\c GPTimer). Envían notificación directa (\c vTaskNotifyGiveFromISR) y despiertan a las tareas críticas. |
 * | \c TaskGenerarECG          | <b>5 (Máx)</b> | 2048     | Notificación 500 Hz  | Permanece en <b>Blocked</b> sobre \c ulTaskNotifyTake(). Al recibir la notificación pasa a <b>Running</b>, actualiza acumuladores DDS y escribe en ambos DACs. |
 * | \c TaskLeerECG_UART        | <b>4 (Alta)</b>| 2048     | Notificación 500 Hz  | Permanece en <b>Blocked</b> sobre \c ulTaskNotifyTake(). Al ejecutarse muestrea ADC (\c CH1, \c CH3 a 50 Hz, \c CH2 a 20 Hz) y transmite por UART. |
 * | \c TaskPotenciometro_LED   | <b>2 (Media)</b>| 2048    | \c vTaskDelay(20 ms) | Ejecuta filtrado IIR de potenciómetro, máquina de estados de teclas, lógica dual de \c GPIO_8, umbrales con histéresis y destellos de LED 1 y LED 2. |
 * | \c TaskDisplayLCD          | <b>2 (Media)</b>| 4096    | \c vTaskDelay(200 ms)| Publica estado hacia el servidor web cada 200 ms y cada 1 s (5 ciclos) refresca las 2 filas del LCD I2C, la baliza LED 3 y la melodía de alerta. |
 * | \c TaskSensoresAmbientales | <b>2 (Media)</b>| 8192    | \c vTaskDelay(100 ms)| Muestrea PIR y MQ-3 cada 100 ms (activando <i>latches</i>), lee el DHT22 cada 1.5 s (15 ciclos) y vuelca el registro a la MicroSD cada 10 s (100 ciclos). |
 * 
 * <b>Mecanismos de Sincronización y Protección de Recursos Compartidos:</b>
 * - <b>Notificaciones Directas a Tarea (\c vTaskNotifyGiveFromISR / \c ulTaskNotifyTake):</b> Empleadas en
 *   lugar de semáforos binarios convencionales para vincular \c TIMER_A y \c TIMER_B con las tareas de
 *   <b>500 Hz</b>, logrando una conmutación de contexto hasta un 45% más veloz y sin sobrecarga de RAM.
 * - <b>Muestreo Unificado del Periférico SAR ADC (\c pot_raw_compartido):</b> Dado que el convertidor
 *   ADC1 del ESP32-C6 es un recurso físico único compartido por \c CH1, \c CH2 y \c CH3, si dos tareas de
 *   distinta prioridad invocaran \c AnalogInputReadSingle() de forma asincrónica se produciría una colisión
 *   en los registros del convertidor. Para evitarlo por diseño, \c TaskLeerECG_UART centraliza la totalidad
 *   de las lecturas ADC (muestreando el potenciómetro \c CH2 cada 25 ciclos = <b>50 ms</b>) y deposita el
 *   resultado en la variable volátil \c pot_raw_compartido, la cual es leída sin bloqueos por \c TaskPotenciometro_LED.
 * - <b>Secciones Críticas con Spinlock de Hardware (\c portMUX_TYPE \c s_fifo_spinlock):</b> Protegen las
 *   operaciones de encolado, desencolado y búsqueda sobre la cola circular \c s_cola_fifo[], garantizando
 *   atomicidad cuando múltiples tareas (\c TaskPotenciometro_LED, \c TaskSensoresAmbientales y \c TaskDisplayLCD)
 *   acceden concurrentemente a la estructura de alarmas.
 * - <b>Banderas de Retención Temporal (<i>Latches</i> \c s_latch_gases_10s y \c s_latch_movimiento_10s):</b>
 *   Dado que el período de muestreo de los sensores de seguridad (<b>100 ms</b>) es cien veces más rápido
 *   que el período de escritura en la tarjeta MicroSD (<b>10.000 ms</b>), estas banderas actúan como
 *   memorias biestables que capturan cualquier evento fugaz de gas o movimiento ocurrido dentro de la
 *   ventana de 10 segundos y solo se limpian una vez confirmado el cierre exitoso (\c fclose) del archivo CSV.
 * 
 * @section diseño Explicación del diseño y funcionamiento de la aplicación
 *
 * El sistema monitorea de forma continua la unidad del paciente combinando la lectura de variables 
 * ambientales con la simulación en tiempo real de signos vitales. Dos interrupciones de hardware 
 * configuradas a 500 Hz aseguran el determinismo del sistema, actualizando las salidas analógicas
 * Sigma-Delta que representan la morfología del electrocardiograma (ECG en GPIO_0) y el respirograma
 * (en GPIO_18). Estas señales pasan por filtros RC externos para suavizarse y son leídas de nuevo
 * mediante los canales ADC del microcontrolador (CH1 en GPIO_1 y CH3 en GPIO_3). Durante el mismo
 * ritmo de muestreo, estos valores analógicos capturados se transmiten por el puerto UART hacia el
 * complemento Teleplot de Visual Studio Code, garantizando el trazado ininterrumpido de las curvas
 * fisiológicas. En paralelo, un potenciómetro local en GPIO_2 (CH2) habilitado de manera excluyente
 * mediante las teclas 1 y 2 permite al operador modificar de forma independiente la frecuencia
 * cardíaca (40 a 140 bpm, rango normal de 50 a 130 bpm) y la frecuencia respiratoria (8 a 24 rpm,
 * rango normal de 12 a 20 rpm), disparando alertas cuando estas variables salen del rango normal.
 *
 * Simultáneamente, el sistema operativo FreeRTOS administra tareas de prioridad media para la 
 * supervisión de vapores de alcohol y gases inflamables (MQ-3 en GPIO_19 con espera de precalentamiento
 * de 1 minuto), detección de movimiento por flanco ascendente (PIR HC-SR501 en GPIO_12), lectura de
 * temperatura y humedad cada 1.5 s (DHT22 / AM2302 en GPIO_9 por captura de flancos con ISR) y
 * almacenamiento clínico no volátil cada 10 s en tarjeta MicroSD (bus SPI en GPIO_13, GPIO_20, GPIO_21
 * y GPIO_22) sin bloquear la CPU. Asimismo, la entrada acondicionada en GPIO_8 (accionada mediante el
 * relevador inalámbrico de 433 MHz alimentado a 12V con un elevador MT3608) cumple una función dual:
 * si la cola FIFO está vacía, permite al paciente solicitar atención (encendiendo fijo el LED 3 y
 * reproduciendo la melodía Drama); si existen alertas en la cola FIFO, cada pulsación desencola la
 * alerta más antigua. Toda anomalía fisiológica o ambiental ingresa a la cola FIFO de alarmas,
 * gobernando la interfaz hombre-máquina local (pantalla LCD I2C en GPIO_6/GPIO_7, LED 3 y aviso sonoro
 * RTTTL / síntesis de voz LPCM en Clase D por GPIO_23) y actualizando el servidor web HTTP integrado.
 * Al iniciar el sistema el LCD exhibe "BIENVENIDO!" y, tras conectarse al router Wi-Fi, mantiene
 * visible la dirección IP asignada hasta que el personal médico configure el nombre del paciente o un
 * mensaje desde el navegador web, o bien se presente una alerta activa.
 *
 * ---
 *
 * @section requerimientos Requerimientos de diseño
 *
 * @subsection req_funcionales 1. Requerimientos Funcionales y de Adquisición
 * - <b>Muestreo y Generación Determinística:</b> Uso de temporizadores de hardware a 500 Hz para la 
 *   actualización de las salidas Sigma-Delta de señales fisiológicas, lectura del ADC en configuración
 *   de lazo cerrado (<i>loopback</i>) y transmisión inmediata de la trama de datos por puerto serie (UART).
 * - <b>Adquisición Asíncrona por ISR:</b> Implementación de decodificación Single-Wire mediante interrupción 
 *   de flanco descendente en GPIO_9 para el sensor DHT22 (AM2302), evitando pérdidas de bits durante las 
 *   preempciones de 500 Hz del ECG y la UART.
 * - <b>Sensores Secundarios y Registro No Volátil:</b> Lectura digital con divisor resistivo y espera de
 *   calentamiento (60 s) para alcohol/gases (MQ-3 en GPIO_19), detección por flanco en accesos (PIR HC-SR501
 *   en GPIO_12), regulación manual de frecuencias mediante potenciómetro y teclas de selección, y guardado
 *   periódico cada 10 s en archivo CSV sobre tarjeta MicroSD (FAT16 vía SPI) con estampa de fecha y hora RTC.
 *
 * @subsection req_alarmas 2. Gestión de Alarmas e Interfaz Hombre-Máquina (HMI)
 * - <b>Procesamiento de Eventos:</b> Estructura de datos en cola FIFO capaz de retener múltiples 
 *   alertas concurrentes (llamado de paciente, signos vitales o variables ambientales fuera de rango) sin
 *   pérdida de información, desencolables mediante el relevador remoto en GPIO_8 o vía Web.
 * - <b>Interfaz Local:</b> Visualización del estado, dirección IP y la alerta más prioritaria en una pantalla 
 *   LCD 16x2 vía bus I2C adaptado mediante conversor de nivel lógico bidireccional (3.3V a 5V).
 * - <b>Señalización Audiovisual:</b> LEDs indicadores de pulso cardíaco (LED 1) y respiratorio (LED 2),
 *   LED 3 baliza de alerta (fijo en llamado de paciente o intermitente 1s/1s en alertas), melodías polifónicas
 *   monocanales RTTTL (Mario Hurry Up, Drama, Pacman) y locución de voz LPCM a 8 kHz en GPIO_23 sobre etapa
 *   de potencia Push-Pull C1815/A1015 con aislamiento de fuente y tanque capacitivo de 1000 uF.
 *
 * @subsection req_conectividad 3. Conectividad, Software y Arquitectura Web
 * - <b>Sistema Operativo en Tiempo Real (RTOS):</b> Arquitectura basada en FreeRTOS para el manejo seguro 
 *   de múltiples tareas (síntesis, telemetría, sensores, datalogger SD, UI local y servidor HTTP) protegiendo
 *   los recursos compartidos mediante notificaciones directas, spinlocks y semáforos mutex.
 * - <b>Dashboard Web con Graficación Histórica:</b> Servidor HTTP embebido en modo estación (STA) accesible
 *   directamente mediante la dirección IPv4 mostrada en el display LCD. La interfaz web muestra en tiempo
 *   real las variables clínicas y ambientales con animaciones sincronizadas, sincroniza el reloj RTC,
 *   permite descargar el archivo \c DATALOG.CSV completo y grafica en 5 lienzos Canvas apilados los
 *   últimos registros almacenados en la tarjeta MicroSD.
 * 
 * @section hardConn Hardware Connection
 * | Periférico / Señal           | Pin ESP32-C6 | Conexión Hardware / Circuito Externo (función) |
 * | :--------------------------- | :----------- | :--------------------------------------------- |
 * | Tecla 1 (SWITCH_1)           | GPIO_4       | Integrado en placa ESP-EDU (Habilita/deshabilita el ajuste de FC) |
 * | Tecla 2 (SWITCH_2)           | GPIO_15      | Integrado en placa ESP-EDU (Habilita/deshabilita el ajuste de FR) |
 * | LED 1 (Pulso / Alerta ECG)   | GPIO_11      | Integrado en placa ESP-EDU (Destella 60 ms en pico R o fijo en alerta FC) |
 * | LED 2 (Pulso / Alerta Resp)  | GPIO_10      | Integrado en placa ESP-EDU (Destella 120 ms en pico inspiratorio o fijo en alerta FR) |
 * | LED 3 (Alerta General FIFO)  | GPIO_5       | Integrado en placa ESP-EDU (Fijo en llamado paciente o baliza 1s ON / 1s OFF) |
 * | Salida DAC ECG (Sigma-Delta) | GPIO_0       | A extremo de resistencia R1 = 100 kΩ del filtro RC pasabajos |
 * | Entrada ADC (ECG)            | GPIO_1 (CH1) | Al nodo central entre R1 (100 kΩ) y C1 (100 nF a GND) |
 * | Salida DAC Resp(Sigma-Delta) | GPIO_18      | A extremo de resistencia R2 = 100 kΩ del filtro RC pasabajos |
 * | Entrada ADC (Respirograma)   | GPIO_3 (CH3) | Al nodo central entre R2 (100 kΩ) y C2 (100 nF a GND) |
 * | Entrada ADC (Potenciómetro)  | GPIO_2 (CH2) | Al cursor central del potenciómetro entre 3.3V y GND (ajusta FC y FR) |
 * | Puerto UART PC               | USB-UART0    | 115200 baudios, 8N1 (Envío de tramas atómicas a Teleplot) |
 * | Bus I2C Datos (SDA 3.3V)     | GPIO_6       | Pin TX1LCH1 del conv. nivel -> TX0HCH1 a SDA PCF8574 (0x27) |
 * | Bus I2C Reloj (SCL 3.3V)     | GPIO_7       | Pin TX1LCH2 del conv. nivel -> TX0HCH2 a SCL PCF8574 (0x27) |
 * | Referencia Baja Tensión      | 3V3          | Pin LV del conv. de nivel, pot., pull-up GPIO_8 y VCC de DHT22 |
 * | Alimentación Alta Tensión    | 5V           | Pin HV conv. nivel, VCC PCF8574 (+100 uF/100 nF), PIR, MQ-3, MicroSD, MT3608 y Audio |
 * | Masa común (GND)             | GND          | Plano de referencia común de 0V para todos los subsistemas |
 * | Relevador Remoto RF 433 MHz  | GPIO_8       | Nodo con 10 kΩ a 3.3V, cap. 104 (100 nF) a GND, 1 kΩ serie a GPIO_8 y contacto NO a COM (GND) |
 * | Elevador Step-Up MT3608      | 5V -> 12.0V  | Entrada 5V (con 100 uF + 100 nF a GND) -> Salida 12.0V (con 220 uF/35V + 100 nF) a módulo RX1C |
 * | Sensor Movimiento HC-SR501   | GPIO_12      | Salida OUT directa a GPIO_12 (VCC=5V, salida lógica nativa de 3.3V) |
 * | Sensor DHT22 (AM2302)        | GPIO_9       | Pin 2 DATA directo a GPIO_9 (VCC=3.3V con pull-up externo de 5.6 kΩ a 3.3V) |
 * | Sensor Alcohol/Gas MQ-3      | GPIO_19      | Salida DOUT vía divisor resistivo: 5.6 kΩ en serie y 10 kΩ a GND (VCC=5V) |
 * | Bus SPI Lector MicroSD (CS)  | GPIO_13      | Salida digital CS (Transceptor USB_D+ interno deshabilitado por software) |
 * | Bus SPI Lector MicroSD (SCK) | GPIO_20      | Reloj SPI Maestro a 4 MHz hacia pin SCK del módulo MicroSD |
 * | Bus SPI Lector MicroSD (MOSI)| GPIO_21      | Datos Maestro -> Esclavo hacia pin MOSI del módulo MicroSD |
 * | Bus SPI Lector MicroSD (MISO)| GPIO_22      | Datos Esclavo -> Maestro (3.3V nativos) desde pin MISO/MOSO del módulo MicroSD |
 * | Bus SPI Lector MicroSD (VCC) | 5V           | Alimentación +5V DC desacoplada con 220 uF electrolítico y 100 nF cerámico a GND |
 * | Aviso Sonoro (PWM / LPCM)    | GPIO_23      | A 220 Ω -> Bases C1815/A1015 (Colector NPN con diodo + 1000 uF a GND) -> 220 uF -> 10 Ω -> Parlante 4 Ω |
 * 
 * ### Implementaciones de hardware y software en videos de prueba subidos a YouTube:
 * 
 * #### 1. Proyecto Finalizado (Demostración Integral en Ambas Placas):
 * - <b>[Monitor Unidad Paciente (mi ESP32)](https://youtu.be/qIbY7SjvQnA)</b> @n
 *   Monitor Unidad Paciente finalizado, corriendo sobre mi ESP32-C6-WROOM-1 V1.2, Single-Core, RISC-V 32-bit, 160 MHz, kit de desarrollo: Muse Lab. @n
 * - <b>[Monitor Unidad Paciente (ESP-EDU)](https://youtu.be/X56GXF_a68k)</b> @n
 *   Monitor Unidad Paciente finalizado, corriendo sobre ESP-EDU. @n
 * 
 * #### 2. Salida Digital-Analógica (DAC Sigma-Delta), Filtro RC, ECG y Respirograma:
 * - <b>[Prueba salida GPIO_0](https://youtube.com/shorts/sI_Fc5lGRFs)</b> @n
 *   Verificación inicial en osciloscopio de la señal modulada entre 0 y 3.3 V en GPIO_0 con GPIO_1 flotando. @n
 * - <b>[Guia 2 - Act 4 - 01 puente entre GPIO_0 y GPIO_1](https://youtu.be/fAqSIl8k_Ok)</b> @n
 *   Se realiza un puente entre GPIO_0 y GPIO_1 comprobando en osciloscopio físico y por puerto serie (COM11 a 115200 baudios) la señal capturada por el canal 1 del ADC1. @n
 * - <b>[Guia 2, act 4 - 02 ECG entre GPIO_0 y GPIO_1](https://youtu.be/91VUybTmg7U)</b> @n
 *   Conexión entre el pin GPIO_0 y GPIO_1 mediante un divisor de tensión RC, cuyo punto medio filtra y suaviza la señal sigma-delta reconstruyendo la forma de onda de un ECG. @n
 * - <b>[Mi DAC - prueba variante sencilla de ESP-EDU](https://youtu.be/Rb2sv9CD0vM)</b> @n
 *   Mejora del aspecto visual de la señal ECG generada por la salida sigma-delta en GPIO_0 del ESP32-C6 mediante el ajuste del filtro RC pasabajos. @n
 * - <b>[Mi DAC para ECG final](https://youtu.be/w0FN1_etG9A)</b> @n
 *   Resultado final de la implementación del filtro RC a partir de lo experimentado con la resistencia variable. @n
 * - <b>[ECG frecuencia variable, con pulsos y alarma](https://youtu.be/pI7VuOgxYBg)</b> @n
 *   Prueba del bloque ECG con frecuencia variable visualizando la señal, frecuencia cardíaca, pulsaciones y alertas en Teleplot (UART), osciloscopio y LED 1. @n
 * - <b>[Respirograma y ECG, frecuencias variables, pulsos, alarmas, independientes, display 16x2](https://youtu.be/MppDgt1LKZQ)</b> @n
 *   Ajuste independiente de frecuencias cardíaca y respiratoria mediante ventanas de programación con potenciómetro, indicación en display 16x2, encendido continuo de LEDs en alerta y trazado en Teleplot. @n
 * - <b>[Respirograma y ECG - comprobación con osciloscopio](https://youtu.be/-Qa2UPxSq94)</b> @n
 *   Comprobación en osciloscopio en tiempo real del modelado, generación y captura simultánea del ECG y el respirograma por parte del ESP32-C6. @n
 * 
 * #### 3. Sensores Ambientales (DHT22 y MQ-3) y Display I2C:
 * - <b>[Conexión I2C ESP32, conversor de nivel, conversor I2C, y display LCD 16x2](https://youtube.com/shorts/58nJPL-9mYg)</b> @n
 *   Interposición de un conversor de nivel lógico bidireccional (3.3 V a 5 V) entre el ESP32-C6 y el expansor I2C para lograr un funcionamiento adecuado en brillo, contraste y comunicación con el display LCD 16x2. @n
 * - <b>[DHT22 sensor temp. y hum. comunicación](https://youtu.be/vintphlcF2E)</b> @n
 *   El sensor de temperatura y humedad DHT22 se comunica mediante un protocolo Single-Wire en donde la duración de sus pulsos determina los ceros y unos que codifican las variables sensadas. @n
 * - <b>[Calibración sensor alcohol y gases inflamables](https://youtube.com/shorts/1Po57KtMRjY)</b> @n
 *   Ajuste del potenciómetro del sensor MQ-3 para detectar la presencia de vapores de alcohol (bebidas alcohólicas o alcohol al 70% en piel) y gases inflamables contenidos en aerosoles. @n
 * 
 * #### 4. Audio LPCM / PWM, Datalogger MicroSD y Conectividad Web Wi-Fi:
 * - <b>[Etapa sonido para avisos LPCM y PWM](https://youtu.be/Lmkvz8ZtWx4)</b> @n
 *   Mediante la modulación LPCM sobre portadora PWM pueden enviarse sonidos elaborados como mensajes hablados hacia el monitor unidad paciente. @n
 * - <b>[Registro y visualizacion de datos, y sincronización fecha y hora](https://youtu.be/hTUDn0Bl0bQ)</b> @n
 *   El ESP32 obtiene una fecha y hora actual para vincular a los datos registrados cada diez segundos en la MicroSD. Los datos pueden descargarse de la web y visualizarse en forma de gráfica/función en la misma web. @n
 * - <b>[Conexion WiFi LAN y visualizacion web](https://youtu.be/K3T0wrf6PqI)</b> @n
 *   Comprobación del enlace entre el ESP32 y la red WiFi LAN, visualizando en tiempo real las variables registradas por la unidad paciente en la página web alojada en el microcontrolador. @n
 * - <b>[Ejecución de comandos vía web](https://youtube.com/shorts/NUhS4IJ7fCM)</b> @n
 *   Comunicación WiFi LAN desde notebook hacia el ESP32 para la ejecución remota de órdenes sobre la unidad paciente. @n
 * - <b>[Ejecución de comandos celular/tablet](https://youtu.be/ey6NkgmBr6s)</b> @n
 *   Comunicación bidireccional desde el celular o tablet hacia el monitor unidad paciente y visualización del estado del monitor en la web. @n
 * - <b>[Diseño web](https://youtu.be/uvIX7M69ZE0)</b> @n
 *   Diseño web online en tiempo real desde phcode.dev/tools/es/html-viewer.html. @n
 * - <b>[Velocidad de enlace a la red local en 1 segundo](https://youtu.be/DE7KnygfEy8)</b> @n
 *   Observación del tiempo de arranque desde cero tras el flasheo: tiempo de enlace a la red local inferior a 2 segundos, y conexión mDNS/DNS público inferior a 3 segundos. @n
 * 
 * @section docu Documentación Experimental, Capturas de Osciloscopio y Adjuntos
 * @see [Análisis de Especificaciones, Portabilidad, Pinout y Esquemático (Mi ESP32 vs. ESP-EDU)](../00_Analisis_de_Especificaciones_y_Portabilidad_-_mi_ESP32_y_ESP32_facultad_PINOUT_ESQUEMATICO.pdf)
 * @see [Ver Anteproyecto original](../Tarea_Anteproyecto_Proyecto_3_Monitoreo_unidad_paciente.pdf)
 * @see [Anteproyecto entregado](../anteproyecto_entregado.png)
 * @see [Diagrama en bloques inicial](../diagrama_en_bloques_monitor_unidad_paciente.png)
 * @see [Diagrama en Bloques Final del Sistema](../diagrama_de_bloques_final.png)
 * @see [Diagrama de Flujo Final y Arquitectura FreeRTOS](../diagrama_de_flujo_final.png)
 * @see [Prototipo Final Monitor Unidad de Paciente - Vista 1](../Monitor_unidad_paciente_01.jpg)
 * @see [Prototipo Final Monitor Unidad de Paciente - Vista 2](../Monitor_unidad_paciente_02.jpg)
 * @see [Acondicionamiento de Mi ESP32 en Protoboard - Vista 1](../esp32_mia_1.jpg)
 * @see [Acondicionamiento de Mi ESP32 en Protoboard - Vista 2](../esp32_mia_2.jpg)
 * @see [Reconstrucción Analógica RC de Mi DAC para ECG](../mi_DAC.png)
 * @see [Señal DAC ECG con Frecuencia Variable en Osciloscopio](../DAC_ECG_frecuencia_variable.png)
 * @see [Señales DAC de ECG y Respirograma de Frecuencia Variable en Osciloscopio](../DAC_ECG_y_respirograma_frecuencia_variable.png)
 * @see [Registro de Prueba del Conversor I2C para Display 16x2](../01_conversor_I2C_para_display_16x2.txt)
 * @see [Pinout del Conversor I2C PCF8574T](../i2c_conversor_I2C_PCF8574T_pinout.png)
 * @see [Tren de Pulsos de Prueba del Bus I2C en Osciloscopio](../i2c_todos_los_pulsos_de_prueba.png)
 * @see [Módulo Display LCD 16x2 y Expansor I2C](../display_y_expansor_i2c.jpg)
 * @see [Pinout y Conexión del Sensor DHT22](../DHT22_pinout.png)
 * @see [Tren de Pulsos Single-Wire del Sensor DHT22 en Osciloscopio](../dht22_tren_de_pulsos.png)
 * @see [Respuesta en Escalón del Sensor MQ-3 en Osciloscopio](../MQ3_osciloscopio.png)
 * @see [Curva de Respuesta y Recuperación del Sensor MQ-3](../MQ3_respuesta.png)
 * @see [Pinout y Controles del Sensor PIR HC-SR501](../HC-SR501_pinout.png)
 * @see [Esquema de la Etapa Amplificadora Push-Pull de Audio](../amplificador.jpg)
 * @see [Capacidad y Formato FAT16 de la Tarjeta MicroSD de 2 GB](../formateo_tarjeta_micro_sd_2GB.png)
 * @see [Pinout y Arquitectura del Módulo Lector MicroSD](../pinout_lector_micro_sd.png)
 * @see [Relevamiento en Osciloscopio de Escritura, Cierre y Lectura SPI](../escritura_cierre_y_lectura_micro_sd_por_spi.png)
 * @see [Recursos de Memoria Flash y SRAM Consumidos (Versión 1 MB)](../recursos_consumidos_version_1MB.png)
 * 
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-09-30 | Creación del documento base e implementación de síntesis y muestreo ECG a 500 Hz |
 * | 2026-10-07 | Integración de visualización en display LCD 16x2 I2C sin bloqueo de temporizadores |
 * | 2026-10-08 | Agregado de generación y adquisición de Respirograma, control de LED 2 y alertas FR |
 * | 2026-10-08 | Selección excluyente de ajuste FC/FR con SWITCH_1 y SWITCH_2, y tramas UART atómicas |
 * | 2026-10-08 | Integración del módulo web_unidad_paciente (Wi-Fi STA y servidor HTTP) |
 * | 2026-10-08 | Integración modular de dht22_sensor, mq3_alc_sensor, hcsr501_pir_sensor, relevador_remoto, aviso_sonoro y Cola FIFO |
 * | 2026-10-09 | Incorporación de datalogger clínico en tarjeta MicroSD (FAT16 por SPI), reloj RTC, melodías RTTTL y lógica dual en GPIO_8 |
 * | 2026-10-09 | Documentación integral de arquitectura en capas, buses de comunicación y planificación en FreeRTOS |
 * 
 * @version 2.0
 * @author Guillermo Casco Miranda
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "led.h"
#include "switch.h"
#include "timer_mcu.h"
#include "uart_mcu.h"
#include "analog_io_mcu.h"

#include "display_16x2_i2c.h"
#include "web_unidad_paciente.h"
#include "dht22_sensor.h"
#include "mq3_alc_sensor.h"
#include "hcsr501_pir_sensor.h"
#include "relevador_remoto.h"
#include "aviso_sonoro.h"
#include "buzzer_melodies.h"
#include "microsd_datalogger.h"

/*==================[macros and definitions]=================================*/
#define BUFFER_SIZE              231    /*!< Cantidad total de muestras discretas que conforman un ciclo completo de las tablas de ECG y Respirograma */
#define R_PEAK_INDEX             133    /*!< Índice dentro del vector ecg[] donde se localiza el máximo del complejo QRS (pico R = 253) para disparo del LED 1*/
#define RESP_PEAK_INDEX          92     /*!< Índice dentro del vector respirograma[] correspondiente al máximo de inspiración para disparo del LED 2 */

#define CONFIG_PERIOD_TIMER_A    2000   /*!< Período de interrupción del Temporizador A en microsegundos (2000 us = 500 Hz) para síntesis analógica DA */
#define CONFIG_PERIOD_TIMER_B    2000   /*!< Período de interrupción del Temporizador B en microsegundos (2000 us = 500 Hz) para adquisición AD y envío UART */

#define FC_MIN                   40     /*!< Frecuencia cardíaca mínima seleccionable mediante el potenciómetro en latidos por minuto (bpm) */
#define FC_MAX                   140    /*!< Frecuencia cardíaca máxima seleccionable mediante el potenciómetro en latidos por minuto (bpm) */
#define FC_NORMAL_MIN            50     /*!< Umbral clínico inferior normal de frecuencia cardíaca en bpm (valores menores disparan alerta de bradicardia) */
#define FC_NORMAL_MAX            130    /*!< Umbral clínico superior normal de frecuencia cardíaca en bpm (valores mayores disparan alerta de taquicardia) */

#define FR_MIN                   8      /*!< Frecuencia respiratoria mínima seleccionable mediante el potenciómetro en respiraciones por minuto (rpm) */
#define FR_MAX                   24     /*!< Frecuencia respiratoria máxima seleccionable mediante el potenciómetro en respiraciones por minuto (rpm) */
#define FR_NORMAL_MIN            12     /*!< Umbral clínico inferior normal de frecuencia respiratoria en rpm (valores menores disparan alerta de bradipnea) */
#define FR_NORMAL_MAX            20     /*!< Umbral clínico superior normal de frecuencia respiratoria en rpm (valores mayores disparan alerta de taquipnea) */

#define TEMP_MIN_DECIMAS_C       150    /*!< Umbral inferior normal de temperatura ambiental expresado en décimas de °C (150 décimas = 15.0 °C) */
#define TEMP_MAX_DECIMAS_C       400    /*!< Umbral superior normal de temperatura ambiental expresado en décimas de °C (400 décimas = 35.0 °C), intencionalmente alta para poner a prueba la estabilidad del sistema. Se fuerza con aire caliente para probar la alerta fuera de rango */

#define HUM_MIN_DECIMAS_PCT      300    /*!< Umbral inferior normal de humedad relativa ambiente expresado en décimas de % (300 décimas = 30.0 %) */
#define HUM_MAX_DECIMAS_PCT      800    /*!< Umbral superior normal de humedad relativa ambiente expresado en décimas de % (800 décimas = 80.0 %), intencionalmente alta para poner a prueba la estabilidad del sistema. Se fuerza respirando en frente del sensor para probar la alerta fuera de rango  */

#define FIFO_MAX_ALERTAS         8      /*!< Capacidad máxima de almacenamiento de eventos concurrentes en la cola FIFO circular de alarmas */

#define ADC_MAX_VAL              3300   /*!< Fondo de escala de tensión del conversor analógico-digital (ADC) expresado en milivoltios (3300 mV) */
#define PERIODO_CICLO_LCD_MS     200    /*!< Período base de bloqueo de la tarea TaskDisplayLCD en milisegundos (200 ms; 5 ciclos conforman 1 segundo) */
#define PERIODO_TAREA_AMB_MS     100    /*!< Período base de bloqueo de la tarea TaskSensoresAmbientales en milisegundos (100 ms) */

/*==================[typedef]================================================*/
/**
 * @brief Máquina de estados de habilitación excluyente del potenciómetro gobernada por SWITCH_1 y SWITCH_2.
 */
typedef enum {
    MODO_SIM_NINGUNO = 0,   /*!< Ajuste manual desactivado: FC y FR conservan su último valor configurado */
    MODO_SIM_FC,            /*!< Ventana de simulación cardíaca activa (SWITCH_1): el potenciómetro regula FC */
    MODO_SIM_FR             /*!< Ventana de simulación respiratoria activa (SWITCH_2): el potenciómetro regula FR */
} modo_simulacion_t;

/**
 * @brief Identificadores unívocos de eventos de alarma para la Cola FIFO circular del sistema.
 */
typedef enum {
    ALERTA_ID_NINGUNA = 0,      /*!< Valor nulo: indica ausencia de alertas en la cola FIFO */
    ALERTA_ID_LLAMADO_PACIENTE, /*!< Texto LCD/Web: "LLAMADO PACIENTE" - Disparado por el paciente desde GPIO_8 */
    ALERTA_ID_ECG,              /*!< Texto LCD/Web: "ALERTA! ECG" - FC fuera del rango [50, 130] bpm */
    ALERTA_ID_RESPIRAT,         /*!< Texto LCD/Web: "ALERTA! RESPIRAT" - FR fuera del rango [12, 20] rpm */
    ALERTA_ID_TEMP_BAJA,        /*!< Texto LCD/Web: "ALERTA: Temp<15C" - Temperatura ambiental menor a 15.0 °C */
    ALERTA_ID_TEMP_ALTA,        /*!< Texto LCD/Web: "ALERTA: Temp>35C" - Temperatura ambiental mayor a 35.0 °C */
    ALERTA_ID_HUMEDAD_BAJA,     /*!< Texto LCD/Web: "ALERTA: Hum<30p" - Humedad relativa menor a 30.0 % */
    ALERTA_ID_HUMEDAD_ALTA,     /*!< Texto LCD/Web: "ALERTA: Hum>80p" - Humedad relativa mayor a 80.0 % */
    ALERTA_ID_ALCOHOL,          /*!< Texto LCD/Web: "ALERTA: GASES !" - Detección en sensor MQ-3 (GPIO_19) */
    ALERTA_ID_MOVIMIENTO        /*!< Texto LCD/Web: "! MOVIMIENTO !" - Detección en sensor PIR HC-SR501 (GPIO_12) */
} tipo_alerta_id_t;

/*==================[internal data declaration]==============================*/
/** @brief Descriptor (Task Handle) de TaskGenerarECG utilizado por la ISR FuncTimerA para enviar notificaciones directas */
static TaskHandle_t task_generar_ecg_handle = NULL;

/** @brief Descriptor (Task Handle) de TaskLeerECG_UART utilizado por la ISR FuncTimerB para enviar notificaciones directas */
static TaskHandle_t task_leer_ecg_handle = NULL;

/** @brief Valor vigente de Frecuencia Cardíaca en bpm (compartido entre tareas; inicializado en 60 bpm fisiológicos) */
static volatile uint16_t fc_actual = 60;

/** @brief Valor vigente de Frecuencia Respiratoria en rpm (compartido entre tareas; inicializado en 15 rpm fisiológicas) */
static volatile uint16_t fr_actual = 15;

/** @brief Última lectura en mV del potenciómetro (CH2) adquirida por TaskLeerECG_UART para evitar colisiones de ADC */
static volatile uint16_t pot_raw_compartido = 1650;

/** @brief Bandera de sincronismo activada por TaskGenerarECG al pasar por R_PEAK_INDEX y consumida por TaskPotenciometro_LED */
static volatile bool pulso_detectado = false;

/** @brief Bandera de sincronismo activada por TaskGenerarECG al pasar por RESP_PEAK_INDEX y consumida por TaskPotenciometro_LED */
static volatile bool pulso_resp_detectado = false;

/** @brief Estado instantáneo de condición fuera de rango en Frecuencia Cardíaca (con histéresis de 2 bpm) */
static volatile bool alerta_fc_activa = false;

/** @brief Estado instantáneo de condición fuera de rango en Frecuencia Respiratoria (con histéresis de 1 rpm) */
static volatile bool alerta_fr_activa = false;

/** @brief Bandera que permanece en true mientras existe un llamado de paciente (GPIO_8) pendiente de atención en la FIFO */
static volatile bool s_llamado_paciente_activo = false;

/** @brief Última medición válida de temperatura del sensor DHT22 en décimas de °C (valor inicial por defecto: 22.0 °C) */
static volatile int16_t temp_decimas_actual = 220;

/** @brief Última medición válida de humedad relativa del sensor DHT22 en décimas de % (valor inicial por defecto: 50.0 %) */
static volatile uint16_t hum_decimas_actual = 500;

/** @brief Indica si el calefactor del sensor MQ-3 ya completó los 60 segundos iniciales de estabilización térmica */
static volatile bool mq3_listo_calentado = false;

/** @brief Estado instantáneo de detección de gases inflamables / vapores de alcohol en el sensor MQ-3 */
static volatile bool mq3_alcohol_activo = false;

/** @brief Cantidad de segundos continuos que la salida del sensor MQ-3 permanece en estado bajo de alerta */
static volatile uint32_t mq3_seg_en_bajo = 0;

/** @brief Estado instantáneo del pin de salida del sensor piroeléctrico PIR HC-SR501 */
static volatile bool pir_estado_instantaneo = false;

/** @brief Contador acumulado de eventos de movimiento detectados por flanco ascendente desde el encendido */
static volatile uint32_t pir_total_eventos = 0;

/** @brief Memoria biestable (latch) que retiene cualquier detección de gases ocurrida durante la ventana de 10 s previa al guardado SD */
static volatile bool s_latch_gases_10s = false;

/** @brief Memoria biestable (latch) que retiene cualquier detección de movimiento ocurrida durante la ventana de 10 s previa al guardado SD */
static volatile bool s_latch_movimiento_10s = false;

/** @brief Estado actual de la máquina de selección manual de ajuste por potenciómetro */
static volatile modo_simulacion_t modo_simulacion = MODO_SIM_NINGUNO;

/** @brief Arreglo estático que aloja los elementos de la cola FIFO circular de alarmas */
static tipo_alerta_id_t s_cola_fifo[FIFO_MAX_ALERTAS];

/** @brief Índice de lectura (frente) de la cola FIFO circular: apunta siempre a la alerta más antigua */
static volatile uint8_t s_fifo_head = 0;

/** @brief Índice de escritura (final) de la cola FIFO circular: apunta a la siguiente posición libre */
static volatile uint8_t s_fifo_tail = 0;

/** @brief Cantidad actual de alertas almacenadas en la cola FIFO circular (0 a FIFO_MAX_ALERTAS) */
static volatile uint8_t s_fifo_count = 0;

/** @brief Spinlock de exclusión mutua para proteger las secciones críticas de acceso a la cola FIFO en FreeRTOS */
static portMUX_TYPE s_fifo_spinlock = portMUX_INITIALIZER_UNLOCKED;

/** @brief Variable de compatibilidad para temporización de advertencia */
static volatile uint8_t s_ticks_advertencia_3s = 0;

/**
 * @brief Tabla de búsqueda (Look-Up Table) con 231 muestras cuantizadas en 8 bits sin signo (0 a 255)
 *        correspondientes a un ciclo cardíaco P-Q-R-S-T completo de electrocardiograma.
 *        Alojada en memoria Flash (.rodata) mediante el calificador const.
 */
static const uint8_t ecg[BUFFER_SIZE] = {
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
    94, 103, 108, 102, 95, 97, 106, 100, 92, 101, 103, 100, 94,
    98, 103, 96, 90, 98, 103, 97, 90, 99, 104, 95, 90, 99, 104,
    100, 93, 100, 106, 101, 93, 101, 105, 103, 96, 105, 112, 105,
    99, 103, 108, 99, 96, 102, 106, 99, 90, 92, 100, 87, 80, 82,
    88, 77, 69, 75, 79, 74, 67, 71, 78, 72, 67, 73, 81, 77, 71,
    75, 84, 79, 77, 77, 76, 76
};

/** 
 * @brief Tabla de búsqueda (Look-Up Table) con 231 muestras cuantizadas en 8 bits sin signo (40 a 235)
 *        que modelan la morfología fisiológica de un ciclo ventilatorio (inspiración activa,
 *        espiración pasiva exponencial y pausa tele-espiratoria). Alojada en Flash (.rodata).
 */
static const uint8_t respirograma[BUFFER_SIZE] = {
    40,  40,  41,  41,  42,  43,  44,  45,  46,  48,  50,  52,
    54,  56,  59,  61,  64,  67,  70,  73,  77,  80,  84,  88,
    92,  96, 100, 104, 108, 113, 117, 122, 126, 131, 135, 140,
   145, 149, 154, 158, 163, 167, 172, 176, 180, 184, 188, 192,
   196, 200, 203, 207, 210, 213, 216, 218, 221, 223, 225, 227,
   229, 230, 231, 232, 233, 234, 234, 235, 235, 235, 235, 235,
   234, 234, 233, 232, 231, 230, 228, 227, 225, 223, 221, 219,
   216, 214, 211, 208, 205, 202, 199, 196, 193, 189, 186, 182,
   179, 175, 171, 168, 164, 160, 156, 153, 149, 145, 141, 138,
   134, 130, 127, 123, 120, 116, 113, 109, 106, 103, 100,  97,
    94,  91,  88,  85,  82,  80,  77,  75,  72,  70,  68,  66,
    64,  62,  60,  58,  57,  55,  54,  52,  51,  50,  49,  48,
    47,  46,  45,  45,  44,  43,  43,  42,  42,  42,  41,  41,
    41,  41,  40,  40,  40,  40,  40,  40,  40,  40,  40,  40,
    40,  40,  40,  40,  40,  40,  40,  40,  40,  40,  40,  40,
    40,  40,  40,  40,  40,  40,  40,  40,  40,  40,  40,  40,
    40,  40,  40,  40,  40,  40,  40,  40,  40,  40,  40,  40,
    40,  40,  40,  40,  40,  40,  40,  40,  40,  40,  40,  40,
    40,  40,  40,  40,  40,  40,  40,  40,  40,  40,  40,  40,
    40,  40,  40
};

/*==================[internal functions declaration]=========================*/
static const char* FifoObtenerTextoAlerta(tipo_alerta_id_t id);
static void FifoEncolarAlerta(tipo_alerta_id_t id);
static void FifoDesencolarMasAntigua(void);
static tipo_alerta_id_t FifoConsultarFrente(uint8_t *out_cantidad);

static void FuncTimerA(void* param);
static void FuncTimerB(void* param);
static void TaskGenerarECG(void *pvParameter);
static void TaskLeerECG_UART(void *pvParameter);
static void TaskPotenciometro_LED(void *pvParameter);
static void TaskDisplayLCD(void *pvParameter);
static void TaskSensoresAmbientales(void *pvParameter);

static bool FifoContieneAlerta(tipo_alerta_id_t id);

/*==================[internal functions definition]==========================*/
/**
 * @brief Traduce un identificador enumerado de alerta a su representación textual normalizada
 *        de hasta 16 caracteres para visualización en la Fila 0 del LCD 16x2 y en el Dashboard Web.
 * @param[in] id Identificador de la alerta a traducir.
 * @return Puntero constante a la cadena literal en memoria Flash (.rodata).
 */
static const char* FifoObtenerTextoAlerta(tipo_alerta_id_t id) {
    switch (id) {
        case ALERTA_ID_LLAMADO_PACIENTE: return "LLAMADO PACIENTE";
        case ALERTA_ID_ECG:              return "ALERTA! ECG";
        case ALERTA_ID_RESPIRAT:         return "ALERTA! RESPIRAT";
        case ALERTA_ID_TEMP_BAJA:        return "ALERTA: Temp<15C";
        case ALERTA_ID_TEMP_ALTA:        return "ALERTA: Temp>35C";
        case ALERTA_ID_HUMEDAD_BAJA:     return "ALERTA: Hum<30p";
        case ALERTA_ID_HUMEDAD_ALTA:     return "ALERTA: Hum>80p";
        case ALERTA_ID_ALCOHOL:          return "ALERTA: GASES !";
        case ALERTA_ID_MOVIMIENTO:       return "! MOVIMIENTO !";
        default:                         return "SIN ALERTAS";
    }
}


/**
 * @brief Inserta una nueva alerta al final (tail) de la cola FIFO circular si aún no se encuentra presente.
 * @details Emplea una sección crítica de FreeRTOS (\c portENTER_CRITICAL) para recorrer los elementos
 *          activos entre \c s_fifo_head y \c s_fifo_tail mediante aritmética modular (\c % FIFO_MAX_ALERTAS).
 *          Si el identificador ya está encolado, ignora la inserción para evitar saturar la cola con
 *          eventos repetidos de una misma anomalía sostenida en el tiempo.
 * @param[in] id Identificador de la alerta a encolar.
 */
static void FifoEncolarAlerta(tipo_alerta_id_t id) {
    // Descarta inserciones del identificador nulo
    if (id == ALERTA_ID_NINGUNA) return;

    // Ingresa a sección crítica deshabilitando interrupciones para acceso atómico a la cola compartida
    portENTER_CRITICAL(&s_fifo_spinlock);
    bool ya_existe = false;
    // Recorre las 's_fifo_count' posiciones ocupadas a partir del índice de cabeza (s_fifo_head)
    for (uint8_t i = 0; i < s_fifo_count; i++) {
        uint8_t idx = (s_fifo_head + i) % FIFO_MAX_ALERTAS;
        if (s_cola_fifo[idx] == id) {
            ya_existe = true;
            break;
        }
    }
    // Si la alerta no estaba duplicada y existe espacio libre en el buffer circular, la inserta en s_fifo_tail
    if (!ya_existe && s_fifo_count < FIFO_MAX_ALERTAS) {
        s_cola_fifo[s_fifo_tail] = id;
        s_fifo_tail = (s_fifo_tail + 1) % FIFO_MAX_ALERTAS;
        s_fifo_count++;
    }
    // Libera el spinlock y restaura el estado de las interrupciones del sistema
    portEXIT_CRITICAL(&s_fifo_spinlock);
}

/**
 * @brief Extrae (desencola) la alerta más antigua alojada en el frente (\c s_fifo_head) de la cola FIFO.
 * @details Es invocada cuando el operador o enfermero presiona el control remoto sobre \c GPIO_8
 *          (con cola no vacía) o el botón de desactivación en la interfaz web. Avanza el índice
 *          \c s_fifo_head en aritmética modular y decrementa \c s_fifo_count bajo sección crítica.
 */
static void FifoDesencolarMasAntigua(void) {
    portENTER_CRITICAL(&s_fifo_spinlock);
    if (s_fifo_count > 0) {
        // Avanza el puntero de cabeza al siguiente elemento más antiguo envolviendo en FIFO_MAX_ALERTAS (8)
        s_fifo_head = (s_fifo_head + 1) % FIFO_MAX_ALERTAS;
        s_fifo_count--;
    }
    portEXIT_CRITICAL(&s_fifo_spinlock);
}

/**
 * @brief Consulta de forma atómica el elemento ubicado en el frente de la cola FIFO sin extraerlo.
 * @param[out] out_cantidad Puntero opcional donde se escribe la cantidad total de alertas activas.
 * @return Identificador de la alerta más antigua en el frente de la cola (o \c ALERTA_ID_NINGUNA si está vacía).
 */
static tipo_alerta_id_t FifoConsultarFrente(uint8_t *out_cantidad) {
    tipo_alerta_id_t frente = ALERTA_ID_NINGUNA;
    portENTER_CRITICAL(&s_fifo_spinlock);
    if (out_cantidad != NULL) {
        *out_cantidad = s_fifo_count;
    }
    if (s_fifo_count > 0) {
        frente = s_cola_fifo[s_fifo_head];
    }
    portEXIT_CRITICAL(&s_fifo_spinlock);
    return frente;
}

/**
 * @brief Comprueba de forma atómica si un identificador de alerta específico permanece dentro de la cola FIFO.
 * @details Permite que las tarjetas del Dashboard Web (FC, FR, Gases, Movimiento y Estado del Paciente)
 *          continúen parpadeando en rojo mientras su alerta no haya sido reconocida y desencolada por el
 *          personal, aun cuando la magnitud física instantánea ya haya retornado a su valor normal.
 * @param[in] id Identificador de la alerta buscada.
 * @return \c true si la alerta se encuentra en cualquier posición activa de la cola; \c false en caso contrario.
 */
static bool FifoContieneAlerta(tipo_alerta_id_t id) {
    bool encontrada = false;
    portENTER_CRITICAL(&s_fifo_spinlock);
    for (uint8_t i = 0; i < s_fifo_count; i++) {
        uint8_t idx = (s_fifo_head + i) % FIFO_MAX_ALERTAS;
        if (s_cola_fifo[idx] == id) {
            encontrada = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_fifo_spinlock);
    return encontrada;
}

/**
 * @brief Rutina de servicio de interrupción (ISR) del Temporizador de Hardware A (disparada cada 2000 us = 500 Hz).
 * @details Envía una notificación directa desde contexto de interrupción (\c vTaskNotifyGiveFromISR) hacia
 *          la tarea de máxima prioridad \c TaskGenerarECG, pasándola instantáneamente del estado \c Blocked
 *          al estado \c Ready para que el planificador de FreeRTOS le ceda el procesador.
 * @param[in] param Puntero a parámetros de usuario (no utilizado).
 */
static void FuncTimerA(void* param) {
    vTaskNotifyGiveFromISR(task_generar_ecg_handle, pdFALSE);
}

/**
 * @brief Rutina de servicio de interrupción (ISR) del Temporizador de Hardware B (disparada cada 2000 us = 500 Hz).
 * @details Envía una notificación directa desde contexto de interrupción (\c vTaskNotifyGiveFromISR) hacia
 *          la tarea \c TaskLeerECG_UART, pasándola del estado \c Blocked al estado \c Ready para iniciar
 *          el muestreo del conversor ADC y la transmisión hacia Teleplot.
 * @param[in] param Puntero a parámetros de usuario (no utilizado).
 */
static void FuncTimerB(void* param) {
    vTaskNotifyGiveFromISR(task_leer_ecg_handle, pdFALSE);
}

/**
 * @brief Tarea de tiempo real estricto (Prioridad 5 - Máxima) encargada de sintetizar las señales de
 *        ECG (\c GPIO_0) y Respirograma (\c GPIO_18) mediante acumuladores de fase DDS a 500 Hz.
 * @details Algoritmo de Síntesis Digital Directa (DDS) en punto fijo:
 *          Con una frecuencia de interrupción <b>f<sub>ISR</sub> = 500 Hz</b> y una tabla de <b>N = 231</b> muestras,
 *          para reproducir una frecuencia fisiológica <b>F</b> (en ciclos por minuto, es decir, <b>F / 60 Hz</b>),
 *          la velocidad de avance de índice requerida por cada interrupción de <b>2 ms</b> es: @n
 *          <b>Δíndice = (N · F) / (60 · f<sub>ISR</sub>) = (231 · F) / 30000 = (77 · F) / 10000</b> @n
 *          Para evitar operaciones en punto flotante, en cada ciclo de <b>2 ms</b> se suma el entero
 *          <b>(F · 77)</b> a un acumulador de fase; cada vez que el acumulador alcanza o supera el módulo
 *          <b>10.000</b>, se le resta <b>10.000</b> y se avanza una posición en el arreglo de muestras.
 * @param[in] pvParameter Puntero a parámetros de creación de la tarea (no utilizado).
 */
static void TaskGenerarECG(void *pvParameter) {
    // Acumuladores de fase en punto fijo (módulo 10.000) e índices de recorrido de tabla (0 a 230)
    static uint32_t acumulador_fase_ecg = 0;
    static uint16_t indice_ecg = 0;

    static uint32_t acumulador_fase_resp = 0;
    static uint16_t indice_resp = 0;

    while (1) {
        /*
         * Bloqueo por notificación directa de tarea:
         * La tarea pasa a estado Blocked (consumo 0% de CPU) con espera infinita (portMAX_DELAY)
         * hasta que la ISR FuncTimerA incrementa su valor de notificación cada 2000 us.
         * El argumento pdTRUE limpia el contador de notificación a 0 al despertar (comportamiento de semáforo binario).
         */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 1. Síntesis DDS y actualización del modulador Sigma-Delta de ECG en GPIO_0
        acumulador_fase_ecg += (fc_actual * 77);
        while (acumulador_fase_ecg >= 10000) {
            acumulador_fase_ecg -= 10000;
            indice_ecg++;
            // Si alcanza el final de las 231 muestras, envuelve al inicio del ciclo cardíaco
            if (indice_ecg >= BUFFER_SIZE) {
                indice_ecg = 0;
            }
            // Al pasar exactamente por el índice del pico R (133), levanta la bandera para destellar el LED 1
            if (indice_ecg == R_PEAK_INDEX) {
                pulso_detectado = true;
            }
        }
        // Envía la muestra actual (0 a 255) al modulador Sigma-Delta 0 (conectado al filtro RC en GPIO_0)
        AnalogOutputWrite(ecg[indice_ecg]);

        // 2. Síntesis DDS y actualización del modulador Sigma-Delta de Respirograma en GPIO_18
        acumulador_fase_resp += (fr_actual * 77);
        while (acumulador_fase_resp >= 10000) {
            acumulador_fase_resp -= 10000;
            indice_resp++;
            // Si alcanza el final de las 231 muestras, envuelve al inicio del ciclo ventilatorio
            if (indice_resp >= BUFFER_SIZE) {
                indice_resp = 0;
            }
            // Al pasar por el pico de máxima inspiración (92), levanta la bandera para destellar el LED 2
            if (indice_resp == RESP_PEAK_INDEX) {
                pulso_resp_detectado = true;
            }
        }
        // Envía la muestra actual (40 a 235) al modulador Sigma-Delta 1 (conectado al filtro RC en GPIO_18)
        AnalogOutput2Write(respirograma[indice_resp]);
    }
}

/**
 * @brief Tarea de tiempo real (Prioridad 4) encargada de muestrear de forma centralizada los tres
 *        canales del conversor SAR ADC (\c CH1: ECG, \c CH3: Respirograma, \c CH2: Potenciómetro)
 *        y transmitir tramas serie atómicas hacia Teleplot por \c UART_PC a 115200 baudios.
 * @details Implementa un esquema de diezmado temporal (<i>decimation</i>) para optimizar el ancho de banda
 *          del canal serie UART:
 *          - El ECG (\c CH1 en \c GPIO_1) se muestrea y transmite a la tasa máxima de <b>500 Hz</b> (cada <b>2 ms</b>)
 *            para preservar los componentes de alta frecuencia del complejo QRS.
 *          - El Respirograma (\c CH3 en \c GPIO_3), cuya frecuencia máxima es de <b>24 rpm</b> (<b>0.4 Hz</b>),
 *            se muestrea y anexa a la misma trama cada 10 ciclos (<b>50 Hz</b>, cada <b>20 ms</b>).
 *          - El Potenciómetro (\c CH2 en \c GPIO_2) se muestrea cada 25 ciclos (<b>20 Hz</b>, cada <b>50 ms</b>)
 *            y su valor en milivoltios se deposita en \c pot_raw_compartido para su filtrado en \c TaskPotenciometro_LED.
 * @param[in] pvParameter Puntero a parámetros de creación de la tarea (no utilizado).
 */
static void TaskLeerECG_UART(void *pvParameter) {
    uint32_t valor_adc_ecg = 0;
    uint32_t valor_adc_resp = 0;
    uint32_t valor_pot = 0;
    uint8_t divisor_ciclos = 0;
    char trama_uart[40];

    while (1) {
        // Permanece en estado Blocked hasta recibir la notificación directa desde la ISR FuncTimerB (cada 2 ms)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 1. Conversión analógico-digital de la señal ECG filtrada por hardware en CH1 (GPIO_1)
        AnalogInputReadSingle(CH1, (uint16_t*)&valor_adc_ecg);

        divisor_ciclos++;

        // 2. Cada 10 interrupciones (20 ms = 50 Hz) digitaliza CH3 (GPIO_3) y unifica ECG + Resp en una sola escritura UART
        if ((divisor_ciclos % 10) == 0) {
            AnalogInputReadSingle(CH3, (uint16_t*)&valor_adc_resp);
            snprintf(trama_uart, sizeof(trama_uart), "\r\n>ecg:%u\r\n>resp:%u\r\n",
                     (uint16_t)valor_adc_ecg, (uint16_t)valor_adc_resp);
            UartSendString(UART_PC, trama_uart);
        } else {
            // En los 9 ciclos intermedios transmite únicamente la muestra de alta velocidad del ECG
            snprintf(trama_uart, sizeof(trama_uart), "\r\n>ecg:%u\r\n", (uint16_t)valor_adc_ecg);
            UartSendString(UART_PC, trama_uart);
        }

        // 3. Cada 25 interrupciones (50 ms = 20 Hz) digitaliza el potenciómetro en CH2 (GPIO_2) sin colisión de periférico ADC
        if (divisor_ciclos >= 25) {
            divisor_ciclos = 0;
            AnalogInputReadSingle(CH2, (uint16_t*)&valor_pot);
            pot_raw_compartido = (uint16_t)valor_pot;
        }
    }
}

/**
 * @brief Tarea de supervisión de interfaz física y control fisiológico (Prioridad 2, período de 20 ms).
 * @details Ejecuta de forma determinística cada <b>20 ms</b> (<b>50 Hz</b>) nueve etapas de control:
 *          1. Detección de flanco ascendente con exclusión mutua en \c SWITCH_1 (\c GPIO_4) y \c SWITCH_2 (\c GPIO_15).
 *          2. Procesamiento de la entrada acondicionada en \c GPIO_8 (relevador remoto RF de 433 MHz) con lógica
 *             dual: llamado de paciente (melodía Drama + LED 3 fijo) si la cola FIFO está vacía, o desencolado de
 *             la alerta más antigua si la cola contiene eventos; en paralelo con el botón web de desencolado.
 *          3. Atención de solicitudes web de reproducción de melodía de advertencia (\c songPacman_version_guille_cm)
 *             y locución hablada LPCM (<i>"Ayuda en camino"</i>).
 *          4. Filtrado digital pasabajos IIR de primer orden (media móvil exponencial con <b>α = 1/8</b>) sobre la
 *             tensión del potenciómetro para eliminar fluctuaciones por ruido térmico o mecánico del cursor.
 *          5. Mapeo lineal afín del valor filtrado hacia el rango de FC (<b>40 a 140 bpm</b>) o FR (<b>8 a 24 rpm</b>).
 *          6. Comparación de FC contra los umbrales normales (<b>50 a 130 bpm</b>) con banda de histéresis de <b>2 bpm</b>.
 *          7. Comparación de FR contra los umbrales normales (<b>12 a 20 rpm</b>) con banda de histéresis de <b>1 rpm</b>.
 *          8. Gobierna el \c LED_1 (\c GPIO_11): encendido continuo en alerta de FC o destello monostable de <b>60 ms</b> en cada pico R.
 *          9. Gobierna el \c LED_2 (\c GPIO_10): encendido continuo en alerta de FR o destello monostable de <b>120 ms</b> en cada inspiración.
 * @param[in] pvParameter Puntero a parámetros de creación de la tarea (no utilizado).
 */
static void TaskPotenciometro_LED(void *pvParameter) {
    uint8_t tiempo_destello_ecg_ms = 0;
    uint8_t tiempo_destello_resp_ms = 0;
    uint32_t pot_filtrado = 1650;
    bool en_alerta_fc = false;
    bool en_alerta_fr = false;

    bool tec1_anterior = false;
    bool tec2_anterior = false;

    while (1) {
        // 1. Lectura de SWITCH_1 (GPIO_4) y SWITCH_2 (GPIO_15) con detección de flanco y exclusión mutua
        uint8_t teclas = SwitchesRead();
        bool tec1_actual = (teclas == SWITCH_1 || teclas == (SWITCH_1 | SWITCH_2));
        bool tec2_actual = (teclas == SWITCH_2 || teclas == (SWITCH_1 | SWITCH_2));

        // Flanco ascendente en Tecla 1: sólo conmuta entre MODO_SIM_NINGUNO y MODO_SIM_FC (ignorado si está en MODO_SIM_FR)
        if (tec1_actual && !tec1_anterior) {
            if (modo_simulacion == MODO_SIM_NINGUNO) {
                modo_simulacion = MODO_SIM_FC;
            } else if (modo_simulacion == MODO_SIM_FC) {
                modo_simulacion = MODO_SIM_NINGUNO;
            }
        }
        tec1_anterior = tec1_actual;

        // Flanco ascendente en Tecla 2: sólo conmuta entre MODO_SIM_NINGUNO y MODO_SIM_FR (ignorado si está en MODO_SIM_FC)
        if (tec2_actual && !tec2_anterior) {
            if (modo_simulacion == MODO_SIM_NINGUNO) {
                modo_simulacion = MODO_SIM_FR;
            } else if (modo_simulacion == MODO_SIM_FR) {
                modo_simulacion = MODO_SIM_NINGUNO;
            }
        }
        tec2_anterior = tec2_actual;

        // 2. Gestión de GPIO_8 (relevador remoto 433 MHz) y botón Web para llamado de atención / desencolado FIFO
        bool pulsador_gpio8 = RelevadorRemotoEventoActivacion();
        bool req_clear_web = WebUnidadPacienteConsumirSolicitudClearAlert();

        if (pulsador_gpio8) {
            uint8_t total_fifo = 0;
            FifoConsultarFrente(&total_fifo);
            if (total_fifo == 0) {
                // Cola vacía: el paciente solicita asistencia -> Encola alerta, fija LED 3 y lanza melodía DRAMA
                s_llamado_paciente_activo = true;
                FifoEncolarAlerta(ALERTA_ID_LLAMADO_PACIENTE);
                AvisoSonoroReproducirRtttl(songDrama_version_guille_cm);
            } else {
                // Cola con alertas activas: desencola la más antigua, detiene la melodía en curso y actualiza estado
                FifoDesencolarMasAntigua();
                AvisoSonoroDetenerRtttl();
                if (!FifoContieneAlerta(ALERTA_ID_LLAMADO_PACIENTE)) {
                    s_llamado_paciente_activo = false;
                }
            }
        }

        // Procesamiento del botón remoto "Desactivar Alerta más Antigua (FIFO)" presionado en el navegador web
        if (req_clear_web) {
            FifoDesencolarMasAntigua();
            AvisoSonoroDetenerRtttl();
            if (!FifoContieneAlerta(ALERTA_ID_LLAMADO_PACIENTE)) {
                s_llamado_paciente_activo = false;
            }
        }

        // 3. Solicitudes Web de aviso sonoro bajo demanda (Melodía RTTTL de advertencia o Voz LPCM "Ayuda en camino")
        if (WebUnidadPacienteConsumirSolicitudAdvertencia3s()) {
            AvisoSonoroReproducirRtttl(songPacman_version_guille_cm);
        }
        if (WebUnidadPacienteConsumirSolicitudVozAyuda()) {
            AvisoSonoroReproducirVozAyuda();
        }

        /*
         * 4. Filtro digital pasabajos IIR exponencial sobre la lectura compartida del potenciómetro:
         *    Ecuación en diferencias: y[k] = (7/8)*y[k-1] + (1/8)*x[k].
         *    Suaviza las transiciones mecánicas y elimina el jitter del bit menos significativo del ADC.
         */
        pot_filtrado = (pot_filtrado * 7 + pot_raw_compartido) / 8;

        // 5. Actualización selectiva de FC (40 a 140 bpm) o FR (8 a 24 rpm) mediante interpolación lineal entera
        if (modo_simulacion == MODO_SIM_FC) {
            uint16_t fc_calculada = FC_MIN + (pot_filtrado * (FC_MAX - FC_MIN)) / ADC_MAX_VAL;
            if (fc_calculada > FC_MAX) fc_calculada = FC_MAX;
            if (fc_calculada < FC_MIN) fc_calculada = FC_MIN;
            fc_actual = fc_calculada;
        } else if (modo_simulacion == MODO_SIM_FR) {
            uint16_t fr_calculada = FR_MIN + (pot_filtrado * (FR_MAX - FR_MIN)) / ADC_MAX_VAL;
            if (fr_calculada > FR_MAX) fr_calculada = FR_MAX;
            if (fr_calculada < FR_MIN) fr_calculada = FR_MIN;
            fr_actual = fr_calculada;
        }

        // 6. Evaluación de rango fisiológico normal de FC (50 a 130 bpm) con banda de histéresis interna de 2 bpm
        if (!en_alerta_fc) {
            if (fc_actual < FC_NORMAL_MIN || fc_actual > FC_NORMAL_MAX) {
                en_alerta_fc = true;
                FifoEncolarAlerta(ALERTA_ID_ECG);
            }
        } else {
            // Para salir del estado de alerta exige reingresar al intervalo [52, 128] bpm, evitando oscilaciones en el borde
            if (fc_actual >= (FC_NORMAL_MIN + 2) && fc_actual <= (FC_NORMAL_MAX - 2)) {
                en_alerta_fc = false;
            }
        }
        alerta_fc_activa = en_alerta_fc;

        // 7. Evaluación de rango fisiológico normal de FR (12 a 20 rpm) con banda de histéresis interna de 1 rpm
        if (!en_alerta_fr) {
            if (fr_actual < FR_NORMAL_MIN || fr_actual > FR_NORMAL_MAX) {
                en_alerta_fr = true;
                FifoEncolarAlerta(ALERTA_ID_RESPIRAT);
            }
        } else {
            // Para salir del estado de alerta exige reingresar al intervalo [13, 19] rpm
            if (fr_actual >= (FR_NORMAL_MIN + 1) && fr_actual <= (FR_NORMAL_MAX - 1)) {
                en_alerta_fr = false;
            }
        }
        alerta_fr_activa = en_alerta_fr;

        // 8. Control de LED 1 (GPIO_11 - Frecuencia Cardíaca): Fijo en alerta, o destello de 60 ms (3 ciclos de 20 ms) por latido
        if (en_alerta_fc) {
            LedOn(LED_1);
            pulso_detectado = false;
            tiempo_destello_ecg_ms = 0;
        } else {
            if (pulso_detectado) {
                pulso_detectado = false;
                LedOn(LED_1);
                tiempo_destello_ecg_ms = 60;
            } else if (tiempo_destello_ecg_ms > 0) {
                tiempo_destello_ecg_ms = (tiempo_destello_ecg_ms >= 20) ? (tiempo_destello_ecg_ms - 20) : 0;
                if (tiempo_destello_ecg_ms == 0) LedOff(LED_1);
            } else {
                LedOff(LED_1);
            }
        }

        // 9. Control de LED 2 (GPIO_10 - Frecuencia Respiratoria): Fijo en alerta, o destello de 120 ms (6 ciclos de 20 ms) por inspiración
        if (en_alerta_fr) {
            LedOn(LED_2);
            pulso_resp_detectado = false;
            tiempo_destello_resp_ms = 0;
        } else {
            if (pulso_resp_detectado) {
                pulso_resp_detectado = false;
                LedOn(LED_2);
                tiempo_destello_resp_ms = 120;
            } else if (tiempo_destello_resp_ms > 0) {
                tiempo_destello_resp_ms = (tiempo_destello_resp_ms >= 20) ? (tiempo_destello_resp_ms - 20) : 0;
                if (tiempo_destello_resp_ms == 0) LedOff(LED_2);
            } else {
                LedOff(LED_2);
            }
        }

        // Transiciona la tarea al estado Blocked durante 20 ms cediendo la CPU al resto del sistema
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/**
 * @brief Tarea de adquisición ambiental y registro histórico en MicroSD (Prioridad 2, Stack de 8192 bytes, ciclo base de 100 ms).
 * @details Coordina tres escalas de tiempo mediante divisores de frecuencia sobre su ciclo base de <b>100 ms</b>:
 *          - <b>Cada 100 ms (10 Hz):</b> Evalúa el flanco ascendente del sensor piroeléctrico PIR HC-SR501 (\c GPIO_12)
 *            y el estado del detector de alcohol/gases MQ-3 (\c GPIO_19). Si cualquiera de ellos se activa,
 *            encola la alerta correspondiente en la FIFO y fija en \c true el <i>latch</i> de ventana de 10 segundos
 *            (\c s_latch_movimiento_10s o \c s_latch_gases_10s) para asegurar que el evento quede registrado en
 *            la tarjeta MicroSD aunque concluya antes del segundo 10.
 *          - <b>Cada 15 ciclos (1500 ms = 1.5 s):</b> Ejecuta la adquisición Single-Wire por captura de flancos en ISR
 *            del sensor higrotérmico DHT22 (\c GPIO_9), actualizando \c temp_decimas_actual y \c hum_decimas_actual,
 *            verificando los umbrales de <b>[15.0, 35.0] °C</b> y <b>[30.0, 80.0] %</b> y re-encolando cualquier
 *            condición de alarma persistente.
 *          - <b>Cada 100 ciclos (10.000 ms = 10 s):</b> Construye la estructura \c registro_clinico_sd_t e invoca
 *            \c MicroSdEscribirRegistro() para ejecutar el ciclo atómico de apertura, escritura CSV y cierre con
 *            volcado físico (<i>flush</i>) a la tabla FAT16 sobre el bus SPI2, limpiando los <i>latches</i> al concluir.
 * @param[in] pvParameter Puntero a parámetros de creación de la tarea (no utilizado).
 */
static void TaskSensoresAmbientales(void *pvParameter) {
    dht22_data_t datos_dht;
    uint8_t contador_100ms_dht = 0;
    uint8_t contador_100ms_sd = 0;

    while (1) {
        // 1. Detección instantánea por flanco ascendente del sensor PIR HC-SR501 en GPIO_12
        if (HcSr501PirEventoMovimiento()) {
            FifoEncolarAlerta(ALERTA_ID_MOVIMIENTO);
            s_latch_movimiento_10s = true;
        }
        pir_estado_instantaneo = HcSr501PirEstadoActual();
        if (pir_estado_instantaneo) {
            s_latch_movimiento_10s = true;
        }
        pir_total_eventos = HcSr501PirObtenerContadorEventos();

        // 2. Supervisión continua del sensor de gases/alcohol MQ-3 en GPIO_19 (inhibido los primeros 60 s de calentamiento)
        mq3_listo_calentado = Mq3AlcPrecalentamientoListo();
        if (Mq3AlcActualizarEstado()) {
            mq3_alcohol_activo = true;
            s_latch_gases_10s = true;
            FifoEncolarAlerta(ALERTA_ID_ALCOHOL);
        } else {
            mq3_alcohol_activo = false;
        }
        mq3_seg_en_bajo = Mq3AlcObtenerDuracionPulsoSeg();

        // 3. Cada 1500 ms (15 ciclos * 100 ms) lee el sensor DHT22 en GPIO_9 mediante captura de flancos por ISR
        contador_100ms_dht++;
        if (contador_100ms_dht >= 15) {
            contador_100ms_dht = 0;

            if (Dht22Read(&datos_dht)) {
                temp_decimas_actual = datos_dht.temp_decimas_c;
                hum_decimas_actual = datos_dht.hum_decimas_pct;

                // Evaluación de límites térmicos (15.0 °C a 35.0 °C)
                if (temp_decimas_actual < TEMP_MIN_DECIMAS_C) {
                    FifoEncolarAlerta(ALERTA_ID_TEMP_BAJA);
                } else if (temp_decimas_actual > TEMP_MAX_DECIMAS_C) {
                    FifoEncolarAlerta(ALERTA_ID_TEMP_ALTA);
                }

                // Evaluación de límites higrométricos (30.0 % a 80.0 %)
                if (hum_decimas_actual < HUM_MIN_DECIMAS_PCT) {
                    FifoEncolarAlerta(ALERTA_ID_HUMEDAD_BAJA);
                } else if (hum_decimas_actual > HUM_MAX_DECIMAS_PCT) {
                    FifoEncolarAlerta(ALERTA_ID_HUMEDAD_ALTA);
                }
            }

            // Re-encolado de seguridad si alguna variable fisiológica permanece en estado de alerta
            if (alerta_fc_activa) {
                FifoEncolarAlerta(ALERTA_ID_ECG);
            }
            if (alerta_fr_activa) {
                FifoEncolarAlerta(ALERTA_ID_RESPIRAT);
            }
        }

        // 4. Cada 10 segundos (100 ciclos * 100 ms) graba y cierra (flush FAT16) el registro clínico en la tarjeta MicroSD
        contador_100ms_sd++;
        if (contador_100ms_sd >= 100) {
            contador_100ms_sd = 0;

            registro_clinico_sd_t reg_sd = {
                .fc_bpm = fc_actual,
                .fr_rpm = fr_actual,
                .temp_decimas_c = temp_decimas_actual,
                .hum_decimas_pct = hum_decimas_actual,
                .alerta_gases = s_latch_gases_10s || mq3_alcohol_activo,
                .movimiento_sala = s_latch_movimiento_10s || pir_estado_instantaneo
            };

            if (MicroSdEscribirRegistro(&reg_sd)) {
                // Limpia los latches de ventana una vez confirmado el volcado físico a la memoria Flash de la MicroSD
                s_latch_gases_10s = false;
                s_latch_movimiento_10s = false;
            }
        }

        // Pasa la tarea a estado Blocked durante 100 ms
        vTaskDelay(pdMS_TO_TICKS(PERIODO_TAREA_AMB_MS));
    }
}

/**
 * @brief Tarea de gestión de interfaz hombre-máquina local (LCD 16x2 I2C, baliza LED 3 y audio de alarma)
 *        y publicación de telemetría hacia el servidor Web (Prioridad 2, Stack de 4096 bytes, ciclo de 200 ms).
 * @details En cada ciclo de <b>200 ms</b> (<b>5 Hz</b>) recopila el estado de todos los subsistemas,
 *          consulta el reloj RTC y el estado de la MicroSD y actualiza de forma atómica el servidor HTTP
 *          mediante \c WebUnidadPacienteActualizarDatos(). @n
 *          Cada 5 ciclos (<b>1000 ms = 1 s</b>), o de forma inmediata si el operador presiona
 *          \c SWITCH_1 o \c SWITCH_2 para abrir una ventana de simulación, actualiza las dos filas del
 *          display LCD 16x2 a través del bus I2C y gobierna la baliza luminosa (\c LED_3 en \c GPIO_5) y
 *          las melodías de alerta (\c songDrama_version_guille_cm para llamado de paciente o
 *          \c songMarioHurryUp_version_guille_cm para alertas clínicas/ambientales).
 * @param[in] pvParameter Puntero a parámetros de creación de la tarea (no utilizado).
 */
static void TaskDisplayLCD(void *pvParameter) {
    char linea_superior_web[17];
    char linea_inferior[24];
    bool estado_baliza_1s = false;
    uint8_t ciclos_200ms = 0;
    modo_simulacion_t modo_previo = MODO_SIM_NINGUNO;

    // Suprime advertencia del compilador por variable estática reservada
    (void)s_ticks_advertencia_3s;

    while (1) {
        ciclos_200ms++;
        modo_simulacion_t modo_actual = modo_simulacion;

        // Consulta atómica de la cantidad de alertas encoladas y del texto correspondiente al frente de la FIFO
        uint8_t cant_fifo = 0;
        tipo_alerta_id_t frente_fifo = FifoConsultarFrente(&cant_fifo);
        const char *texto_frente_fifo = FifoObtenerTextoAlerta(frente_fifo);

        // Verifica si el llamado del paciente continúa presente en algún lugar de la cola FIFO
        if (!FifoContieneAlerta(ALERTA_ID_LLAMADO_PACIENTE)) {
            s_llamado_paciente_activo = false;
        }

        /*
         * Empaquetado integral del estado del sistema para el endpoint JSON /api/status:
         * Las banderas booleanas de alerta combinan mediante OR lógico el estado instantáneo del sensor
         * con la presencia de la alerta en la cola FIFO (FifoContieneAlerta), garantizando que el indicador
         * visual en la página web continúe parpadeando en rojo hasta que el personal reconozca la alarma.
         */
        estado_unidad_paciente_t datos_web = {
            .fc_bpm = fc_actual,
            .fr_rpm = fr_actual,
            .alerta_fc = alerta_fc_activa || FifoContieneAlerta(ALERTA_ID_ECG),
            .alerta_fr = alerta_fr_activa || FifoContieneAlerta(ALERTA_ID_RESPIRAT),
            .modo_sim_local = (uint8_t)modo_actual,
            .temp_decimas_c = temp_decimas_actual,
            .hum_decimas_pct = hum_decimas_actual,
            .mq3_precalentado = mq3_listo_calentado,
            .mq3_alcohol_detectado = mq3_alcohol_activo || FifoContieneAlerta(ALERTA_ID_ALCOHOL),
            .mq3_segundos_bajo = mq3_seg_en_bajo,
            .pir_movimiento = pir_estado_instantaneo || FifoContieneAlerta(ALERTA_ID_MOVIMIENTO),
            .pir_contador_eventos = pir_total_eventos,
            .alertas_fifo_cant = cant_fifo,
            .paciente_requiere_atencion = s_llamado_paciente_activo
        };

        // Copia segura del texto del frente de la FIFO, estado de la MicroSD y fecha/hora actual del RTC
        strncpy(datos_web.alerta_fifo_actual, texto_frente_fifo, sizeof(datos_web.alerta_fifo_actual) - 1);
        datos_web.alerta_fifo_actual[sizeof(datos_web.alerta_fifo_actual) - 1] = '\0';
        datos_web.sd_montada = MicroSdEstadoMontada();
        datos_web.sd_registros_totales = MicroSdObtenerTotalEscritos();
        MicroSdObtenerFechaHoraActual(datos_web.fecha_actual, sizeof(datos_web.fecha_actual),
                                      datos_web.hora_actual, sizeof(datos_web.hora_actual));
        WebUnidadPacienteActualizarDatos(&datos_web);

        // Refresca el display físico cada 1 segundo (5 * 200 ms) o instantáneamente al cambiar el modo de simulación por teclas
        if (ciclos_200ms >= 5 || modo_actual != modo_previo) {
            if (ciclos_200ms >= 5) {
                ciclos_200ms = 0;

                if (cant_fifo > 0) {
                    // 1. Control del LED 3 (GPIO_5): encendido continuo ante llamado del paciente, o baliza 1s ON / 1s OFF en alertas
                    if (s_llamado_paciente_activo) {
                        LedOn(LED_3);
                    } else {
                        estado_baliza_1s = !estado_baliza_1s;
                        if (estado_baliza_1s) {
                            LedOn(LED_3);
                        } else {
                            LedOff(LED_3);
                        }
                    }

                    // 2. Control del motor de audio en GPIO_23:
                    //    Si concluyó la reproducción previa y no se está emitiendo voz LPCM, inicia o repite
                    //    la partitura RTTTL correspondiente a la alerta más antigua alojada en el frente de la FIFO.
                    if (!AvisoSonoroReproduciendoRtttl() && !AvisoSonoroReproduciendoVoz()) {
                        if (frente_fifo == ALERTA_ID_LLAMADO_PACIENTE) {
                            AvisoSonoroReproducirRtttl(songDrama_version_guille_cm);
                        } else {
                            AvisoSonoroReproducirRtttl(songMarioHurryUp_version_guille_cm);
                        }
                    }
                } else {
                    // Cola FIFO vacía (Sistema Normal): apaga la baliza LED 3 y silencia tonos continuos
                    estado_baliza_1s = false;
                    LedOff(LED_3);
                    AvisoSonoroApagar();
                }
            }
            modo_previo = modo_actual;

            /*
             * Arbitraje de jerarquía de visualización en la Fila 0 del Display LCD 16x2 (68 tramas I2C):
             * Prioridad 1 (Máxima): Ventana de ajuste local activa por Tecla 1 ("SIMULE Frec CARD") o Tecla 2 ("SIMULE Frec RESP").
             * Prioridad 2 (Alta)  : Alerta más antigua presente en el frente de la cola FIFO.
             * Prioridad 3 (Normal): Texto suministrado por el módulo Web ("BIENVENIDO!" -> Dirección IPv4 -> Paciente / Mensaje).
             */
            if (modo_actual == MODO_SIM_FC) {
                Display16x2I2cWriteLine(LCD_ROW_0, "SIMULE Frec CARD");
            } else if (modo_actual == MODO_SIM_FR) {
                Display16x2I2cWriteLine(LCD_ROW_0, "SIMULE Frec RESP");
            } else if (cant_fifo > 0) {
                Display16x2I2cWriteLine(LCD_ROW_0, texto_frente_fifo);
            } else {
                WebUnidadPacienteObtenerTextoFila0(linea_superior_web);
                Display16x2I2cWriteLine(LCD_ROW_0, linea_superior_web);
            }

            // Actualización de la Fila 1 del Display LCD 16x2 con las frecuencias vigentes: "FC: 60 - FR: 15 " (68 tramas I2C)
            snprintf(linea_inferior, sizeof(linea_inferior), "FC:%3u - FR:%3u ", fc_actual, fr_actual);
            Display16x2I2cWriteLine(LCD_ROW_1, linea_inferior);
        }

        // Bloquea la tarea durante 200 ms cediendo el control al planificador de FreeRTOS
        vTaskDelay(pdMS_TO_TICKS(PERIODO_CICLO_LCD_MS));
    }
}

/*==================[main application]=======================================*/
/**
 * @fn void app_main(void)
 * @brief Punto de entrada de la aplicación en ESP-IDF. Ejecuta la secuencia ordenada de inicialización
 *        de periféricos, buses de comunicación, temporizadores de hardware y creación de tareas de FreeRTOS.
 */
void app_main(void) {
    // Suprime los mensajes de diagnóstico del sistema operativo sobre UART0 para mantener limpia la trama de Teleplot
    esp_log_level_set("*", ESP_LOG_NONE);

    // 1. Inicialización de actuadores LED (GPIO_11, GPIO_10, GPIO_5), teclas (GPIO_4, GPIO_15) y Relevador Remoto (GPIO_8)
    LedsInit();
    SwitchesInit();
    RelevadorRemotoInit(RELEVADOR_DEFAULT_GPIO);

    // 2. Inicialización de sensores ambientales (DHT22 en GPIO_9, MQ-3 en GPIO_19, PIR en GPIO_12), Audio (GPIO_23) y Mutex/RTC MicroSD
    Dht22Init(DHT22_DEFAULT_GPIO);
    Mq3AlcInit(MQ3_ALC_DEFAULT_GPIO);
    HcSr501PirInit(HCSR501_PIR_DEFAULT_GPIO);
    AvisoSonoroInit(AVISO_SONORO_DEFAULT_GPIO);
    MicroSdInit();

    // 3. Inicialización del bus I2C a 100 kHz y escritura del estado inicial en el Display LCD 16x2 (GPIO_6 y GPIO_7)
    Display16x2I2cInit();
    Display16x2I2cWriteLine(LCD_ROW_0, "BIENVENIDO!");
    Display16x2I2cWriteLine(LCD_ROW_1, "FC: 60 - FR: 15 ");

    // 4. Inicialización de la pila TCP/IP, conexión Wi-Fi en Modo Estación (STA) y arranque del servidor HTTP en puerto 80
    WebUnidadPacienteInit();

    // 5. Inicialización de los moduladores analógicos Sigma-Delta de alta frecuencia: GPIO_0 (ECG) y GPIO_18 (Respirograma)
    AnalogOutputInit();
    AnalogOutput2Init();

    // 6. Configuración e inicialización de los 3 canales del conversor SAR ADC1: CH1 (GPIO_1), CH2 (GPIO_2) y CH3 (GPIO_3)
    analog_input_config_t config_adc_ecg = {
        .input = CH1,
        .mode = ADC_SINGLE,
        .func_p = NULL,
        .param_p = NULL,
        .sample_frec = 0
    };
    AnalogInputInit(&config_adc_ecg);

    analog_input_config_t config_adc_pot = {
        .input = CH2,
        .mode = ADC_SINGLE,
        .func_p = NULL,
        .param_p = NULL,
        .sample_frec = 0
    };
    AnalogInputInit(&config_adc_pot);

    analog_input_config_t config_adc_resp = {
        .input = CH3,
        .mode = ADC_SINGLE,
        .func_p = NULL,
        .param_p = NULL,
        .sample_frec = 0
    };
    AnalogInputInit(&config_adc_resp);

    // 7. Configuración e inicialización del puerto serie UART_PC a 115200 baudios para envío de curvas a Teleplot
    serial_config_t config_uart = {
        .port = UART_PC,
        .baud_rate = 115200,
        .func_p = NULL,
        .param_p = NULL
    };
    UartInit(&config_uart);

    // 8. Configuración de los Temporizadores de Hardware A y B con período de 2000 us (500 Hz exactos cada uno)
    timer_config_t timer_a = {
        .timer = TIMER_A,
        .period = CONFIG_PERIOD_TIMER_A,
        .func_p = FuncTimerA,
        .param_p = NULL
    };
    TimerInit(&timer_a);

    timer_config_t timer_b = {
        .timer = TIMER_B,
        .period = CONFIG_PERIOD_TIMER_B,
        .func_p = FuncTimerB,
        .param_p = NULL
    };
    TimerInit(&timer_b);

    /*
     * 9. Creación de las 5 tareas de aplicación en el planificador de FreeRTOS:
     *    - TaskGenerarECG (Prioridad 5, Stack 2048 B): Guarda su descriptor en task_generar_ecg_handle para recibir notificaciones de FuncTimerA.
     *    - TaskLeerECG_UART (Prioridad 4, Stack 2048 B): Guarda su descriptor en task_leer_ecg_handle para recibir notificaciones de FuncTimerB.
     *    - TaskPotenciometro_LED (Prioridad 2, Stack 2048 B): Supervisión de teclas, GPIO_8, filtro IIR y destellos de LED 1 y LED 2 cada 20 ms.
     *    - TaskDisplayLCD (Prioridad 2, Stack 4096 B): Actualización de servidor web cada 200 ms y refresco de LCD I2C, LED 3 y melodías cada 1 s.
     *    - TaskSensoresAmbientales (Prioridad 2, Stack 8192 B): Muestreo PIR/MQ-3 cada 100 ms, lectura DHT22 cada 1.5 s y escritura FAT16 en MicroSD cada 10 s.
     */
    xTaskCreate(TaskGenerarECG, "GenerarECG", 2048, NULL, 5, &task_generar_ecg_handle);
    xTaskCreate(TaskLeerECG_UART, "LeerECG_UART", 2048, NULL, 4, &task_leer_ecg_handle);
    xTaskCreate(TaskPotenciometro_LED, "Pot_LED", 2048, NULL, 2, NULL);
    xTaskCreate(TaskDisplayLCD, "DisplayLCD", 4096, NULL, 2, NULL);
    xTaskCreate(TaskSensoresAmbientales, "SensoresAmb", 8192, NULL, 2, NULL);

    // 10. Puesta en marcha de los contadores de hardware TIMER_A y TIMER_B una vez creadas y listas todas las tareas
    TimerStart(timer_a.timer);
    TimerStart(timer_b.timer);
}

/*==================[end of file]============================================*/