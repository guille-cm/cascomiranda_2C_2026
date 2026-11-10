/**
 * @file web_unidad_paciente.c
 * @brief Implementación del cliente Wi-Fi STA y Servidor Web HTTP embebido con soporte para
 *        supervisión clínica en tiempo real, gráficas históricas apiladas desde MicroSD y
 *        sincronización automática/manual de fecha y hora RTC.
 * 
 * @details Administra la máquina de estados de conexión Wi-Fi en modo estación (autenticación WPA2-PSK
 *          y obtención de IP por DHCP) y expone una API REST de 10 endpoints sobre el puerto TCP 80:
 *          - \c GET  \c /                : Sirve el Dashboard HTML5/CSS/SVG/Canvas desde memoria Flash (\c .rodata).
 *          - \c GET  \c /api/status      : Entrega el estado integral del sistema en formato JSON cada 1 segundo.
 *          - \c POST \c /api/patient     : Actualiza el nombre del paciente (reemplazando la IP en la Fila 0 del LCD).
 *          - \c POST \c /api/msg         : Muestra un mensaje clínico en la Fila 0 del LCD (y dispara voz si es "AYUDA EN CAMINO").
 *          - \c POST \c /api/clear_alert : Solicita desencolar la alerta más antigua de la cola FIFO.
 *          - \c POST \c /api/sound_warn  : Solicita activar la melodía de advertencia en el parlante.
 *          - \c POST \c /api/play_voice  : Solicita reproducir la locución de voz LPCM ("Ayuda en camino").
 *          - \c POST \c /api/time        : Recibe el timestamp Unix local desde el navegador y sincroniza el RTC interno.
 *          - \c GET  \c /api/sd_history  : Recupera los últimos 40 registros de \c DATALOG.CSV en JSON para las 5 gráficas Canvas.
 *          - \c GET  \c /api/sd_csv      : Transmite en streaming por bloques de 512 bytes el archivo \c DATALOG.CSV completo. @n
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
#include "web_unidad_paciente.h"
#include "microsd_datalogger.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"

/*==================[internal data declaration]==============================*/
/**
 * @brief Copia maestra interna del estado de la unidad de paciente.
 * @details Es actualizada cada 200 ms por \c TaskDisplayLCD mediante \c WebUnidadPacienteActualizarDatos()
 *          y leída cada 1000 ms por \c get_status_handler() bajo protección del spinlock \c s_web_spinlock.
 */
static estado_unidad_paciente_t s_datos_actuales = {
    .fc_bpm = 60,
    .fr_rpm = 15,
    .alerta_fc = false,
    .alerta_fr = false,
    .modo_sim_local = 0,
    .temp_decimas_c = 220,
    .hum_decimas_pct = 500,
    .mq3_precalentado = false,
    .mq3_alcohol_detectado = false,
    .mq3_segundos_bajo = 0,
    .pir_movimiento = false,
    .pir_contador_eventos = 0,
    .alertas_fifo_cant = 0,
    .alerta_fifo_actual = "SIN ALERTAS",
    .sd_montada = false,
    .sd_registros_totales = 0,
    .fecha_actual = "2026-01-01",
    .hora_actual = "00:00:00",
    .paciente_requiere_atencion = false
};

/** @brief Cadena ASCII con la dirección IPv4 asignada por el servidor DHCP del router (ej. "192.168.1.7") */
static char s_ip_asignada[WEB_LCD_TEXT_MAX_LEN + 1] = "Conectando WiFi";

/** @brief Nombre del paciente ingresado desde la interfaz web (máximo 16 caracteres; vacío = mostrar IP) */
static char s_nombre_paciente[WEB_LCD_TEXT_MAX_LEN + 1] = "";

/** @brief Mensaje clínico predefinido seleccionado desde la interfaz web para mostrarse en la Fila 0 del LCD */
static char s_mensaje_activo[WEB_LCD_TEXT_MAX_LEN + 1] = "";

/** @brief Bandera volátil que indica si la interfaz Wi-Fi STA posee actualmente enlace y dirección IPv4 válida */
static volatile bool s_conectado_wifi = false;

/** @brief Bandera de pulso solicitada por POST /api/clear_alert para desencolar la alerta más antigua de la FIFO */
static volatile bool s_req_clear_alert = false;

/** @brief Bandera de pulso solicitada por POST /api/sound_warn para reproducir la melodía de advertencia */
static volatile bool s_req_warn_3s = false;

/** @brief Bandera de pulso solicitada por POST /api/play_voice (o mensaje "AYUDA EN CAMINO") para reproducir voz LPCM */
static volatile bool s_req_play_voice = false;

/** @brief Spinlock de exclusión mutua para proteger el acceso concurrente entre la tarea HTTP y las tareas de aplicación */
static portMUX_TYPE s_web_spinlock = portMUX_INITIALIZER_UNLOCKED;

/** @brief Buffer estático en sección .bss (640 bytes) para armar el JSON de /api/status sin consumir pila de httpd */
static char s_json_status_buf[640];

/** @brief Buffer estático en sección .bss (3072 bytes) para armar el JSON histórico de /api/sd_history sin consumir pila */
static char s_json_history_buf[3072];

/**
 * @brief Código fuente completo del Dashboard Web (HTML5 + CSS3 + SVG + Canvas 2D + JavaScript ES6)
 *        alojado en la sección de solo lectura (.rodata) de la memoria Flash del ESP32-C6.
 * @details Su arquitectura en el navegador cliente ejecuta:
 *          - Sincronización automática transparente del reloj RTC al detectar \c !d.rtc_ok en la primera lectura.
 *          - Polling periódico cada 1000 ms (\c setInterval(upd, 1000)) hacia \c /api/status para actualizar
 *            valores numéricos, velocidad de animación de los íconos de corazón y pulmones, emoji de estado
 *            del paciente y clases CSS de parpadeo de alarma (\c .badge.alerta y \c .tag-amb.alerta).
 *          - Renderizado vectorial en 5 elementos \c <canvas> apilados (\c drawPlot y \c drawEvents) al consultar
 *            el historial almacenado en la tarjeta MicroSD (\c GET /api/sd_history).
 */
static const char INDEX_HTML[] =
"<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
"<title>Monitor Unidad de Paciente - Unidad 1</title>"
"<style>"
"body{font-family:Arial,sans-serif;background:#0f172a;color:#f8fafc;margin:0;padding:16px;}"
".container{max-width:880px;margin:auto;}"
".header{background:#1e293b;padding:16px 20px;border-radius:10px;border-left:5px solid #38bdf8;margin-bottom:16px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;}"
"h1{margin:0;font-size:1.35rem;color:#38bdf8;}"
".badge{padding:6px 12px;border-radius:6px;font-weight:bold;font-size:0.85rem;display:inline-flex;align-items:center;gap:6px;}"
".ok{background:#065f46;color:#6ee7b7;}"
".alerta{background:#991b1b;color:#fca5a5;animation:bl 1s infinite;}"
"@keyframes bl{50%{opacity:0.4;}}"
"@keyframes beat{0%{transform:scale(1);}15%{transform:scale(1.35);}30%{transform:scale(1);}45%{transform:scale(1.2);}60%{transform:scale(1);}}"
"@keyframes breathe{0%{transform:scale(1);opacity:0.65;}50%{transform:scale(1.28);opacity:1;}100%{transform:scale(1);opacity:0.65;}}"
".ico-beat{display:inline-block;transform-origin:center;}"
".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:14px;margin-bottom:16px;}"
".card{background:#1e293b;padding:16px;border-radius:10px;border:1px solid #334155;margin-bottom:14px;}"
".card h3{margin:0 0 10px 0;font-size:0.92rem;color:#94a3b8;letter-spacing:0.5px;}"
".val-row{display:flex;justify-content:space-between;align-items:center;margin:4px 0;}"
".val{font-size:2.0rem;font-weight:bold;line-height:1;}"
".sub{font-size:0.8rem;color:#64748b;}"
".tag-amb{display:inline-block;padding:3px 9px;border-radius:5px;font-weight:bold;font-size:0.9rem;transition:all 0.3s;}"
".fifo-alert-box{text-align:center;margin:12px 0;}"
".patient-sub-block{border-top:1px solid #334155;padding-top:10px;margin-top:10px;}"
"input,select,button{padding:9px 12px;border-radius:6px;border:1px solid #475669;background:#0f172a;color:#fff;font-size:0.9rem;margin-top:6px;}"
"button{background:#0284c7;border:none;cursor:pointer;font-weight:bold;margin-right:6px;}"
"button:hover{background:#0369a1;}"
".btn-warn{background:#d97706;}.btn-warn:hover{background:#b45309;}"
".btn-danger{background:#dc2626;}.btn-danger:hover{background:#b91c1c;}"
".btn-voice{background:#7c3aed;}.btn-voice:hover{background:#6d28d9;}"
".btn-sd{background:#059669;}.btn-sd:hover{background:#047857;}"
".row-ctrl{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:6px;}"
".chart-box{background:#0f172a;border:1px solid #334155;border-radius:8px;padding:10px;margin-top:10px;}"
".chart-title{font-size:0.85rem;font-weight:bold;color:#cbd5e1;margin-bottom:4px;display:flex;justify-content:space-between;}"
"canvas{width:100%;height:150px;display:block;}"
"</style></head><body><div class='container'>"
"<div class='header'><div><h1>Monitor Unidad de Paciente</h1>"
"<div class='sub' id='sim_txt'>Conectando con ESP32-C6 de Guille CM...</div></div>"
"<div id='global_badge' class='badge ok'>SISTEMA NORMAL</div></div>"

"<div class='grid'>"

/* --- RECUADRO UNIFICADO: FC + FR + ESTADO DEL PACIENTE --- */
"<div class='card' style='margin-bottom:0'>"
"<div><h3>FRECUENCIA CARDÍACA</h3>"
"<div class='val-row'><div class='val' id='fc_val'>-- bpm</div>"
"<div id='fc_st' class='badge ok'><span id='ico_heart' class='ico-beat'>&#10084;</span><span id='fc_txt'>NORMAL</span></div></div>"
"<div class='sub'>Rango normal: 50 - 130 bpm</div></div>"

"<div class='patient-sub-block'><h3>FRECUENCIA RESPIRATORIA</h3>"
"<div class='val-row'><div class='val' id='fr_val'>-- rpm</div>"
"<div id='fr_st' class='badge ok'>"
"<svg id='ico_lung' class='ico-beat' viewBox='0 0 24 24' width='16' height='16' fill='none' stroke='currentColor' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'>"
"<path d='M12 3v8'/><path d='M8 6c-2.4 0-4.2 1.8-4.2 4.5 0 3.2 1.8 6.5 4.2 7.5 1 0 1.8-1 1.8-2V8'/><path d='M16 6c2.4 0 4.2 1.8 4.2 4.5 0 3.2-1.8 6.5-4.2 7.5-1 0-1.8-1-1.8-2V8'/>"
"</svg><span id='fr_txt'>NORMAL</span></div></div>"
"<div class='sub'>Rango normal: 12 - 20 rpm</div></div>"

"<div class='patient-sub-block'><h3>ESTADO DEL PACIENTE</h3>"
"<div class='val-row'><div style='font-size:1.15rem;font-weight:bold;' id='pat_st_title'>Condición General</div>"
"<div id='pat_st_badge' class='badge ok'><span id='pat_emoji' class='ico-beat' style='font-size:1.1rem;'>&#128522;</span><span id='pat_st_txt'>NORMAL</span></div></div>"
"<div class='sub' id='pat_sub_txt'>Paciente estable en reposo</div></div>"
"</div>"

/* --- TARJETA AMBIENTE APILADA --- */
"<div class='card' style='margin-bottom:0'><h3>AMBIENTE</h3>"
"<div style='margin:7px 0'>Temperatura: <span id='tag_t' class='tag-amb ok'><b id='t_val'>--</b> °C</span> <span class='sub'>(15-40°C)</span></div>"
"<div style='margin:7px 0'>Humedad: <span id='tag_h' class='tag-amb ok'><b id='h_val'>--</b> %</span> <span class='sub'>(30-80%)</span></div>"
"<div style='margin:7px 0'>Gases inflamables: <span id='tag_g' class='tag-amb ok'><b id='g_val'>--</b></span></div>"
"<div style='margin:7px 0'>Movimiento personas:<br><span id='tag_p' class='tag-amb ok' style='margin-top:4px'><b id='p_val'>SIN MOV.</b> (Eventos: <b id='p_cnt'>0</b>)</span></div></div>"
"</div>"

/* --- IDENTIFICACIÓN PACIENTE Y MENSAJES LCD --- */
"<div class='card'><h3>IDENTIFICACIÓN DE PACIENTE Y MENSAJES A DISPLAY LCD 16x2</h3>"
"<div class='sub'>Texto base Fila 0: <b id='lcd_f0' style='color:#38bdf8'>BIENVENIDO!</b></div>"
"<div class='row-ctrl'>"
"<input type='text' id='pat_in' maxlength='16' placeholder='Nombre Paciente (vacío = IP)'>"
"<button onclick='sendText(\"/api/patient\",\"pat_in\")'>Guardar Paciente</button>"
"</div>"
"<div class='row-ctrl' style='margin-top:10px'>"
"<select id='msg_sel'>"
"<option value=''>-- Mostrar Paciente / IP --</option>"
"<option value='AYUDA EN CAMINO'>AYUDA EN CAMINO (y Voz LPCM)</option>"
"<option value='CONTROL MEDICO'>CONTROL MEDICO</option>"
"<option value='SUMINISTRAR MED'>SUMINISTRAR MED</option>"
"<option value='SILENCIO SALA'>SILENCIO SALA</option>"
"</select>"
"<button onclick='sendText(\"/api/msg\",\"msg_sel\")'>Enviar a Fila 0 LCD</button>"
"</div></div>"

/* --- COLA FIFO CON ALERTA CENTRADA --- */
"<div class='card'><h3>COLA FIFO DE ALARMAS Y AVISO SONORO</h3>"
"<div style='margin-bottom:4px'>Alertas en Cola FIFO: <b id='fifo_cnt'>0</b></div>"
"<div style='margin-bottom:4px'>Primera alerta a atender:</div>"
"<div class='fifo-alert-box'><b id='fifo_top' style='color:#00e676;font-size:1.25rem;letter-spacing:1px;'>SIN ALERTAS</b></div>"
"<div class='row-ctrl'>"
"<button class='btn-danger' onclick='postAct(\"/api/clear_alert\")'>Desactivar Alerta más Antigua (FIFO)</button>"
"<button class='btn-warn' onclick='postAct(\"/api/sound_warn\")'>Tono Advertencia (3s)</button>"
"<button class='btn-voice' onclick='postAct(\"/api/play_voice\")'>Reproducir Voz LPCM ('Ayuda en camino')</button>"
"</div></div>"

/* --- REGISTRO EN MICROSD Y GRÁFICAS --- */
"<div class='card'><h3>REGISTRO EN MICROSD Y GRÁFICAS HISTÓRICAS</h3>"
"<div style='margin-bottom:6px'>Estado MicroSD: <span id='sd_st' class='tag-amb ok'>--</span></div>"
"<div style='margin-bottom:6px'>Registros grabados: <b id='sd_cnt'>0</b></div>"
"<div style='margin-bottom:10px'>Reloj ESP32: <b id='rtc_txt' style='color:#38bdf8'>--</b></div>"
"<div class='row-ctrl'>"
"<input type='datetime-local' id='dt_in' step='1'>"
"<button onclick='syncTimeManual()'>Sincronizar Fecha y Hora</button>"
"<button class='btn-sd' onclick='loadCharts()'>Consultar / Actualizar Gráficas desde MicroSD</button>"
"<button onclick='window.location.href=\"/api/sd_csv\"'>Descargar DATALOG.CSV</button>"
"</div>"
"<div id='charts_wrap' style='display:none;margin-top:12px'>"
"<div class='chart-box'><div class='chart-title'><span>Frecuencia Cardíaca (bpm) - (50-130)</span></div><canvas id='cv_fc' width='800' height='150'></canvas></div>"
"<div class='chart-box'><div class='chart-title'><span>Frecuencia Respiratoria (rpm) - (12-20)</span></div><canvas id='cv_fr' width='800' height='150'></canvas></div>"
"<div class='chart-box'><div class='chart-title'><span>Temperatura Ambiente (°C) - (15-40)</span></div><canvas id='cv_t' width='800' height='150'></canvas></div>"
"<div class='chart-box'><div class='chart-title'><span>Humedad Relativa (%) - (30-80)</span></div><canvas id='cv_h' width='800' height='150'></canvas></div>"
"<div class='chart-box'><div class='chart-title'>"
"<div style='display:flex; justify-content:space-between; margin-bottom:4px;'><span>Gases Inflamables <span style='color:#bfbfbf'>(Gris)</span></span></div>"
"<div style='display:flex; justify-content:space-between;'><span>Movimiento <span style='color:#ff0000'>(Rojo)</span></span></div>"
"</div><canvas id='cv_ev' width='800' height='140'></canvas></div>"

"</div></div></div>"

"<script>"
"let autoSynced=false;"


"async function syncEpoch(sec){await fetch('/api/time',{method:'POST',headers:{'Content-Type':'text/plain'},body:String(sec)});}"


"function syncTimeManual(){const v=document.getElementById('dt_in').value;if(!v)return;"
"const d=new Date(v);const localSec=Math.floor((d.getTime()-d.getTimezoneOffset()*60000)/1000);syncEpoch(localSec);upd();}"


"async function upd(){"
"try{const r=await fetch('/api/status');const d=await r.json();"
"if(!autoSynced&&!d.rtc_ok){autoSynced=true;const now=new Date();"
"const localSec=Math.floor((now.getTime()-now.getTimezoneOffset()*60000)/1000);syncEpoch(localSec);}"
"document.getElementById('fc_val').innerText=d.fc+' bpm';"
"document.getElementById('fr_val').innerText=d.fr+' rpm';"
"document.getElementById('t_val').innerText=d.temp;"
"document.getElementById('h_val').innerText=d.hum;"
"document.getElementById('p_cnt').innerText=d.pir_n;"
"document.getElementById('lcd_f0').innerText=d.fila0;"
"document.getElementById('fifo_cnt').innerText=d.fifo_n;"
"document.getElementById('sd_cnt').innerText=d.sd_n;"
"document.getElementById('rtc_txt').innerText=d.fecha+' '+d.hora;"
"const sdB=document.getElementById('sd_st');"
"sdB.className=d.sd_ok?'tag-amb ok':'tag-amb alerta';"
"sdB.innerText=d.sd_ok?'MONTADA (FAT16)':'NO DETECTADA';"
"const iH=document.getElementById('ico_heart');"
"if(d.fc>0){const s=(60/d.fc).toFixed(2);iH.style.animation='beat '+s+'s infinite ease-in-out';}else{iH.style.animation='none';}"
"const iL=document.getElementById('ico_lung');"
"if(d.fr>0){const s=(60/d.fr).toFixed(2);iL.style.animation='breathe '+s+'s infinite ease-in-out';}else{iL.style.animation='none';}"
"const fcB=document.getElementById('fc_st');"
"fcB.className=d.al_fc?'badge alerta':'badge ok';"
"document.getElementById('fc_txt').innerText=d.al_fc?'ALERTA! ECG':'NORMAL';"
"const frB=document.getElementById('fr_st');"
"frB.className=d.al_fr?'badge alerta':'badge ok';"
"document.getElementById('fr_txt').innerText=d.al_fr?'ALERTA! RESPIRAT':'NORMAL';"

/* Estado del Paciente con Emoji */
"const pBadge=document.getElementById('pat_st_badge');"
"const pEmoji=document.getElementById('pat_emoji');"
"const pTxt=document.getElementById('pat_st_txt');"
"const pSub=document.getElementById('pat_sub_txt');"
"if(d.pat_req){"
"pBadge.className='badge alerta';"
"pEmoji.innerHTML='&#128543;';" // Cara triste / preocupada
"pTxt.innerText='ATENCIÓN';"
"pSub.innerText='Paciente requiere atención!';"
"pSub.style.color='#fca5a5';"
"}else{"
"pBadge.className='badge ok';"
"pEmoji.innerHTML='&#128522;';" // Cara feliz / normal
"pTxt.innerText='NORMAL';"
"pSub.innerText='Paciente estable en reposo';"
"pSub.style.color='#64748b';"
"}"

"const tNum=parseFloat(d.temp);const tagT=document.getElementById('tag_t');"
"tagT.className=(!isNaN(tNum)&&(tNum<15||tNum>40))?'tag-amb alerta':'tag-amb ok';"
"const hNum=parseFloat(d.hum);const tagH=document.getElementById('tag_h');"
"tagH.className=(!isNaN(hNum)&&(hNum<30||hNum>80))?'tag-amb alerta':'tag-amb ok';"
"const tagG=document.getElementById('tag_g');"
"if(!d.mq3_rdy){tagG.className='tag-amb ok';document.getElementById('g_val').innerText='Calentando (1 min)';}"
"else if(d.mq3_al){tagG.className='tag-amb alerta';document.getElementById('g_val').innerText='ALERTA GASES ('+d.mq3_s+'s)';}"
"else{tagG.className='tag-amb ok';document.getElementById('g_val').innerText='Normal';}"
"const tagP=document.getElementById('tag_p');"
"if(d.pir){tagP.className='tag-amb alerta';document.getElementById('p_val').innerText='! MOVIMIENTO !';}"
"else{tagP.className='tag-amb ok';document.getElementById('p_val').innerText='SIN MOV.';}"

"const fTop=document.getElementById('fifo_top');"
"fTop.innerText=d.fifo_txt;fTop.style.color=(d.fifo_n>0)?'#fca5a5':'#00e676';"
"const gB=document.getElementById('global_badge');"
"if(d.fifo_n>0||d.al_fc||d.al_fr||d.pat_req){gB.className='badge alerta';gB.innerText='ALERTA ACTIVA ('+d.fifo_n+')';}"
"else{gB.className='badge ok';gB.innerText='SISTEMA NORMAL';}"
"const sm=['Ajuste local inactivo','Tecla 1 activa: Simulando FC','Tecla 2 activa: Simulando FR'];"
"document.getElementById('sim_txt').innerText='IP: '+d.ip+' | '+sm[d.sim||0];"
"}catch(e){}}"


"function drawPlot(cid,labels,vals,yMin,yMax,color,limLow,limHigh){"
"const cv=document.getElementById(cid);const ctx=cv.getContext('2d');"
"const W=cv.width,H=cv.height,padL=42,padR=16,padT=14,padB=26;"
"const pW=W-padL-padR,pH=H-padT-padB;"
"ctx.clearRect(0,0,W,H);ctx.strokeStyle='#1e293b';ctx.fillStyle='#94a3b8';ctx.font='11px Arial';"
"for(let i=0;i<=4;i++){const y=padT+(pH*i/4);const v=(yMax-(yMax-yMin)*(i/4)).toFixed(0);"
"ctx.beginPath();ctx.moveTo(padL,y);ctx.lineTo(W-padR,y);ctx.stroke();ctx.fillText(v,6,y+4);}"
"if(limLow!==null){const yL=padT+pH*(1-(limLow-yMin)/(yMax-yMin));ctx.strokeStyle='#ef444488';ctx.setLineDash([4,4]);ctx.beginPath();ctx.moveTo(padL,yL);ctx.lineTo(W-padR,yL);ctx.stroke();}"
"if(limHigh!==null){const yH=padT+pH*(1-(limHigh-yMin)/(yMax-yMin));ctx.strokeStyle='#ef444488';ctx.setLineDash([4,4]);ctx.beginPath();ctx.moveTo(padL,yH);ctx.lineTo(W-padR,yH);ctx.stroke();}"
"ctx.setLineDash([]);if(vals.length===0)return;"
"ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();"
"for(let i=0;i<vals.length;i++){const x=padL+(vals.length>1?(pW*i/(vals.length-1)):pW/2);"
"let cl=Math.max(yMin,Math.min(yMax,vals[i]));const y=padT+pH*(1-(cl-yMin)/(yMax-yMin));"
"if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);}ctx.stroke();"
"for(let i=0;i<vals.length;i++){const x=padL+(vals.length>1?(pW*i/(vals.length-1)):pW/2);"
"let cl=Math.max(yMin,Math.min(yMax,vals[i]));const y=padT+pH*(1-(cl-yMin)/(yMax-yMin));"
"ctx.fillStyle=(limLow!==null&&(vals[i]<limLow||vals[i]>limHigh))?'#ef4444':color;"
"ctx.beginPath();ctx.arc(x,y,3.8,0,Math.PI*2);ctx.fill();"
"if(i===0||i===vals.length-1||i%Math.max(1,Math.floor(vals.length/5))===0){ctx.fillStyle='#94a3b8';ctx.fillText(labels[i],x-20,H-6);}}}"


"function drawEvents(cid,labels,gasArr,movArr){"
"const cv=document.getElementById(cid);const ctx=cv.getContext('2d');"
"const W=cv.width,H=cv.height,padL=42,padR=16,padT=18,padB=26,pW=W-padL-padR,pH=H-padT-padB;"
"ctx.clearRect(0,0,W,H);ctx.font='11px Arial';ctx.fillStyle='#94a3b8';"
"ctx.fillText('1 (Sí)',4,padT+6);ctx.fillText('0 (No)',4,padT+pH+4);"
"ctx.strokeStyle='#1e293b';ctx.beginPath();ctx.moveTo(padL,padT);ctx.lineTo(W-padR,padT);ctx.moveTo(padL,padT+pH);ctx.lineTo(W-padR,padT+pH);ctx.stroke();"
"for(let i=0;i<labels.length;i++){const x=padL+(labels.length>1?(pW*i/(labels.length-1)):pW/2);"
"const yG=padT+pH*(1-gasArr[i]);const yM=padT+pH*(1-movArr[i]);"
"ctx.fillStyle='#bfbfbf';ctx.beginPath();ctx.arc(x-3,yG,4,0,Math.PI*2);ctx.fill();"
"ctx.fillStyle='#ff0000';ctx.beginPath();ctx.arc(x+3,yM,4,0,Math.PI*2);ctx.fill();"
"if(i===0||i===labels.length-1||i%Math.max(1,Math.floor(labels.length/5))===0){ctx.fillStyle='#94a3b8';ctx.fillText(labels[i],x-20,H-6);}}}"

"async function loadCharts(){"
"const r=await fetch('/api/sd_history');const d=await r.json();"
"document.getElementById('charts_wrap').style.display='block';"
"const rows=d.rows||[];const lbl=[],fc=[],fr=[],t=[],h=[],g=[],m=[];"
"for(let i=0;i<rows.length;i++){lbl.push(rows[i][1]);fc.push(rows[i][2]);fr.push(rows[i][3]);t.push(rows[i][4]);h.push(rows[i][5]);g.push(rows[i][6]);m.push(rows[i][7]);}"
"drawPlot('cv_fc',lbl,fc,40,140,'#38bdf8',50,130);"
"drawPlot('cv_fr',lbl,fr,8,24,'#34d399',12,20);"
"drawPlot('cv_t',lbl,t,10,45,'#fb7185',15,40);"
"drawPlot('cv_h',lbl,h,0,100,'#60a5fa',30,80);"
"drawEvents('cv_ev',lbl,g,m);}"


"async function sendText(u,id){const v=document.getElementById(id).value;"
"await fetch(u,{method:'POST',headers:{'Content-Type':'text/plain'},body:v});upd();}"


"async function postAct(u){await fetch(u,{method:'POST'});upd();}"
"setInterval(upd,1000);upd();"
"</script></body></html>";

/*==================[internal functions declaration]=========================*/
static esp_err_t get_root_handler(httpd_req_t *req);
static esp_err_t get_status_handler(httpd_req_t *req);
static esp_err_t post_patient_handler(httpd_req_t *req);
static esp_err_t post_msg_handler(httpd_req_t *req);
static esp_err_t post_clear_alert_handler(httpd_req_t *req);
static esp_err_t post_sound_warn_handler(httpd_req_t *req);
static esp_err_t post_play_voice_handler(httpd_req_t *req);
static esp_err_t post_time_handler(httpd_req_t *req);
static esp_err_t get_sd_history_handler(httpd_req_t *req);
static esp_err_t get_sd_csv_handler(httpd_req_t *req);
static void iniciar_servidor_http(void);
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

/*==================[internal functions definition]==========================*/
/**
 * @brief Manejador HTTP para la ruta raíz (\c GET /).
 * @details Configura la cabecera MIME como documento HTML en codificación UTF-8 y transmite
 *          la cadena constante \c INDEX_HTML directamente desde la memoria Flash (\c .rodata).
 * @param[in] req Descriptor de la petición HTTP entrante.
 * @return \c ESP_OK tras enviar la respuesta completa al navegador.
 */
static esp_err_t get_root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Manejador HTTP para el endpoint de telemetría en tiempo real (\c GET /api/status).
 * @details Ejecuta una copia local atómica de \c s_datos_actuales protegida por spinlock,
 *          separa las décimas de temperatura y humedad mediante división entera y módulo,
 *          consulta el estado de sincronización del RTC y empaqueta todas las variables
 *          en el buffer estático \c s_json_status_buf para su envío cada 1 segundo al navegador.
 * @param[in] req Descriptor de la petición HTTP entrante.
 * @return \c ESP_OK tras transmitir el objeto JSON.
 */
static esp_err_t get_status_handler(httpd_req_t *req) {
    char fila0[WEB_LCD_TEXT_MAX_LEN + 1];
    estado_unidad_paciente_t copia;

    // 1. Obtiene el texto vigente de la Fila 0 del display LCD (BIENVENIDO!, Mensaje, Paciente o IP)
    WebUnidadPacienteObtenerTextoFila0(fila0);

    /*
     * 2. Lectura atómica mediante sección crítica (portENTER_CRITICAL / portEXIT_CRITICAL):
     *    Deshabilita temporalmente las interrupciones y el cambio de contexto mientras se copia
     *    la estructura completa s_datos_actuales hacia la variable local 'copia', garantizando que
     *    TaskDisplayLCD no modifique ningún campo a mitad de la lectura.
     */
    portENTER_CRITICAL(&s_web_spinlock);
    copia = s_datos_actuales;
    portEXIT_CRITICAL(&s_web_spinlock);

    /*
     * 3. Desglose aritmético de punto fijo (décimas) a parte entera y dígito decimal:
     *    - t_ent = temp_decimas_c / 10 (ej. 254 -> 25), t_dec = abs(temp_decimas_c % 10) (ej. 254 -> 4).
     *    - h_ent = hum_decimas_pct / 10 (ej. 528 -> 52), h_dec = hum_decimas_pct % 10 (ej. 528 -> 8).
     */
    int16_t t_ent = copia.temp_decimas_c / 10;
    int16_t t_dec = abs(copia.temp_decimas_c % 10);
    uint16_t h_ent = copia.hum_decimas_pct / 10;
    uint16_t h_dec = copia.hum_decimas_pct % 10;

    // 4. Construcción del documento JSON sobre el buffer estático s_json_status_buf (en sección .bss)
    snprintf(s_json_status_buf, sizeof(s_json_status_buf),
             "{\"fc\":%u,\"fr\":%u,\"al_fc\":%s,\"al_fr\":%s,\"sim\":%u,"
             "\"pat_req\":%s,"
             "\"temp\":\"%d.%d\",\"hum\":\"%u.%u\","
             "\"mq3_rdy\":%s,\"mq3_al\":%s,\"mq3_s\":%lu,"
             "\"pir\":%s,\"pir_n\":%lu,"
             "\"fifo_n\":%u,\"fifo_txt\":\"%s\","
             "\"fila0\":\"%s\",\"ip\":\"%s\","
             "\"sd_ok\":%s,\"sd_n\":%lu,\"rtc_ok\":%s,"
             "\"fecha\":\"%s\",\"hora\":\"%s\"}",
             copia.fc_bpm,
             copia.fr_rpm,
             copia.alerta_fc ? "true" : "false",
             copia.alerta_fr ? "true" : "false",
             copia.modo_sim_local,
             copia.paciente_requiere_atencion ? "true" : "false",
             t_ent, t_dec,
             h_ent, h_dec,
             copia.mq3_precalentado ? "true" : "false",
             copia.mq3_alcohol_detectado ? "true" : "false",
             (unsigned long)copia.mq3_segundos_bajo,
             copia.pir_movimiento ? "true" : "false",
             (unsigned long)copia.pir_contador_eventos,
             copia.alertas_fifo_cant,
             copia.alerta_fifo_actual,
             fila0,
             s_ip_asignada,
             copia.sd_montada ? "true" : "false",
             (unsigned long)copia.sd_registros_totales,
             MicroSdRelojSincronizado() ? "true" : "false",
             copia.fecha_actual,
             copia.hora_actual);

    // 5. Envía la respuesta JSON al navegador cliente
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, s_json_status_buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Manejador HTTP para guardar el nombre del paciente (\c POST /api/patient).
 * @details Recibe el cuerpo de texto plano de la petición HTTP, lo trunca a 16 caracteres
 *          (\c WEB_LCD_TEXT_MAX_LEN) y lo almacena bajo sección crítica en \c s_nombre_paciente.
 *          Si se recibe una cadena vacía, la Fila 0 del LCD vuelve a mostrar la dirección IPv4.
 * @param[in] req Descriptor de la petición HTTP entrante.
 * @return \c ESP_OK tras confirmar la operación con "OK".
 */
static esp_err_t post_patient_handler(httpd_req_t *req) {
    char buf[24] = {0};
    // Lee el payload enviado por fetch() desde el socket TCP
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len >= 0) {
        buf[len] = '\0';
        portENTER_CRITICAL(&s_web_spinlock);
        strncpy(s_nombre_paciente, buf, WEB_LCD_TEXT_MAX_LEN);
        s_nombre_paciente[WEB_LCD_TEXT_MAX_LEN] = '\0';
        portEXIT_CRITICAL(&s_web_spinlock);
    }
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

/**
 * @brief Manejador HTTP para enviar un mensaje predeterminado a la Fila 0 del LCD (\c POST /api/msg).
 * @details Copia el mensaje recibido en \c s_mensaje_activo bajo spinlock. Si el mensaje seleccionado
 *          es exactamente \c "AYUDA EN CAMINO", activa simultáneamente la bandera \c s_req_play_voice
 *          para que \c TaskPotenciometro_LED dispare la locución hablada LPCM en el parlante.
 * @param[in] req Descriptor de la petición HTTP entrante.
 * @return \c ESP_OK tras confirmar la operación.
 */
static esp_err_t post_msg_handler(httpd_req_t *req) {
    char buf[24] = {0};
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len >= 0) {
        buf[len] = '\0';
        portENTER_CRITICAL(&s_web_spinlock);
        strncpy(s_mensaje_activo, buf, WEB_LCD_TEXT_MAX_LEN);
        s_mensaje_activo[WEB_LCD_TEXT_MAX_LEN] = '\0';
        if (strcmp(s_mensaje_activo, "AYUDA EN CAMINO") == 0) {
            s_req_play_voice = true;
        }
        portEXIT_CRITICAL(&s_web_spinlock);
    }
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

/**
 * @brief Manejador HTTP para desencolar la alerta más antigua de la cola FIFO (\c POST /api/clear_alert).
 * @param[in] req Descriptor de la petición HTTP entrante.
 * @return \c ESP_OK.
 */
static esp_err_t post_clear_alert_handler(httpd_req_t *req) {
    portENTER_CRITICAL(&s_web_spinlock);
    s_req_clear_alert = true;
    portEXIT_CRITICAL(&s_web_spinlock);
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

/**
 * @brief Manejador HTTP para disparar el aviso sonoro de advertencia (\c POST /api/sound_warn).
 * @param[in] req Descriptor de la petición HTTP entrante.
 * @return \c ESP_OK.
 */
static esp_err_t post_sound_warn_handler(httpd_req_t *req) {
    portENTER_CRITICAL(&s_web_spinlock);
    s_req_warn_3s = true;
    portEXIT_CRITICAL(&s_web_spinlock);
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

/**
 * @brief Manejador HTTP para reproducir bajo demanda la locución LPCM "Ayuda en camino" (\c POST /api/play_voice).
 * @param[in] req Descriptor de la petición HTTP entrante.
 * @return \c ESP_OK.
 */
static esp_err_t post_play_voice_handler(httpd_req_t *req) {
    portENTER_CRITICAL(&s_web_spinlock);
    s_req_play_voice = true;
    portEXIT_CRITICAL(&s_web_spinlock);
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

/**
 * @brief Manejador HTTP para sincronizar la fecha y hora del reloj RTC (\c POST /api/time).
 * @details Recibe como cadena ASCII los segundos Unix locales calculados por el navegador web,
 *          los convierte a entero de 64 bits con signo mediante \c atoll() y actualiza el
 *          contador RTC interno invocando \c MicroSdSincronizarRelojEpoch().
 * @param[in] req Descriptor de la petición HTTP entrante.
 * @return \c ESP_OK.
 */
static esp_err_t post_time_handler(httpd_req_t *req) {
    char buf[32] = {0};
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        int64_t epoch_sec = atoll(buf);
        MicroSdSincronizarRelojEpoch(epoch_sec);
    }
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

/**
 * @brief Manejador HTTP para consultar los últimos 40 registros de la MicroSD (\c GET /api/sd_history).
 * @details Invoca \c MicroSdLeerUltimosRegistrosJson() sobre el buffer estático \c s_json_history_buf
 *          (3072 bytes en \c .bss) y envía el arreglo JSON al navegador para graficar en los 5 Canvas.
 * @param[in] req Descriptor de la petición HTTP entrante.
 * @return \c ESP_OK.
 */
static esp_err_t get_sd_history_handler(httpd_req_t *req) {
    MicroSdLeerUltimosRegistrosJson(s_json_history_buf, sizeof(s_json_history_buf), 40);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, s_json_history_buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Manejador HTTP para descargar el archivo completo DATALOG.CSV (\c GET /api/sd_csv).
 * @param[in] req Descriptor de la petición HTTP entrante.
 * @return Resultado de la transferencia por bloques desde \c MicroSdEnviarCsvCompletoHttp().
 */
static esp_err_t get_sd_csv_handler(httpd_req_t *req) {
    return MicroSdEnviarCsvCompletoHttp(req);
}

/**
 * @brief Configura e inicia la tarea del servidor HTTP embebido registrando las 10 rutas URI.
 * @details Asigna 8192 bytes de pila (\c config.stack_size = 8192) a la tarea del servidor HTTP
 *          para soportar holgadamente las operaciones del sistema de archivos FAT16 sobre SPI
 *          durante las consultas históricas y descargas de CSV, y amplía \c max_uri_handlers a 12.
 */
static void iniciar_servidor_http(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 12;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        const httpd_uri_t u_root  = {.uri="/",                .method=HTTP_GET,  .handler=get_root_handler};
        const httpd_uri_t u_stat  = {.uri="/api/status",      .method=HTTP_GET,  .handler=get_status_handler};
        const httpd_uri_t u_pat   = {.uri="/api/patient",     .method=HTTP_POST, .handler=post_patient_handler};
        const httpd_uri_t u_msg   = {.uri="/api/msg",         .method=HTTP_POST, .handler=post_msg_handler};
        const httpd_uri_t u_clr   = {.uri="/api/clear_alert", .method=HTTP_POST, .handler=post_clear_alert_handler};
        const httpd_uri_t u_warn  = {.uri="/api/sound_warn",  .method=HTTP_POST, .handler=post_sound_warn_handler};
        const httpd_uri_t u_voice = {.uri="/api/play_voice",  .method=HTTP_POST, .handler=post_play_voice_handler};
        const httpd_uri_t u_time  = {.uri="/api/time",        .method=HTTP_POST, .handler=post_time_handler};
        const httpd_uri_t u_sdh   = {.uri="/api/sd_history",  .method=HTTP_GET,  .handler=get_sd_history_handler};
        const httpd_uri_t u_sdc   = {.uri="/api/sd_csv",      .method=HTTP_GET,  .handler=get_sd_csv_handler};

        httpd_register_uri_handler(server, &u_root);
        httpd_register_uri_handler(server, &u_stat);
        httpd_register_uri_handler(server, &u_pat);
        httpd_register_uri_handler(server, &u_msg);
        httpd_register_uri_handler(server, &u_clr);
        httpd_register_uri_handler(server, &u_warn);
        httpd_register_uri_handler(server, &u_voice);
        httpd_register_uri_handler(server, &u_time);
        httpd_register_uri_handler(server, &u_sdh);
        httpd_register_uri_handler(server, &u_sdc);
    }
}

/**
 * @brief Manejador asincrónico de eventos de la capa Wi-Fi (802.11) y de la capa IP (DHCP).
 * @details Gobierna las tres transiciones de estado del enlace inalámbrico:
 *          1. \c WIFI_EVENT_STA_START: El transceptor Wi-Fi finalizó su arranque e inicia el
 *             proceso de autenticación y asociación con el router mediante \c esp_wifi_connect().
 *          2. \c WIFI_EVENT_STA_DISCONNECTED: Si se pierde la señal del router, baja la bandera
 *             \c s_conectado_wifi a \c false y lanza un reintento automático de conexión.
 *          3. \c IP_EVENT_STA_GOT_IP: El servidor DHCP del router asignó una dirección IPv4 a la
 *             unidad; se extrae la estructura \c ip_event_got_ip_t, se formatea la dirección en
 *             notación decimal con puntos dentro de \c s_ip_asignada y se activa \c s_conectado_wifi.
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_conectado_wifi = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        snprintf(s_ip_asignada, sizeof(s_ip_asignada), IPSTR, IP2STR(&event->ip_info.ip));
        s_conectado_wifi = true;
    }
}

/*==================[external functions definition]==========================*/
/**
 * @brief Inicializa el subsistema de almacenamiento NVS, la pila de red TCP/IP, el cliente Wi-Fi STA y el servidor HTTP.
 */
void WebUnidadPacienteInit(void) {
    /*
     * 1. Inicialización de la partición NVS (Non-Volatile Storage) en memoria Flash:
     *    Requerida por el driver Wi-Fi de ESP-IDF para almacenar datos de calibración de RF.
     *    Si la partición está llena o cambió de versión, se formatea e inicializa nuevamente.
     */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /*
     * 2. Inicialización de la interfaz de red (esp_netif), del bucle de eventos por defecto
     *    del sistema y vinculación del adaptador Wi-Fi Station con la pila TCP/IP lwIP.
     */
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    // 3. Reserva de recursos de hardware y tarea interna del driver Wi-Fi con configuración por defecto
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // 4. Registro de callbacks para escuchar eventos de capa de enlace (WIFI_EVENT) y de capa de red (IP_EVENT)
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    /*
     * 5. Carga de credenciales de red (SSID y Password) con umbral de seguridad mínimo WPA2-PSK,
     *    selección del modo Estación (WIFI_MODE_STA) y puesta en marcha del transceptor (esp_wifi_start).
     */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID_DEFAULT,
            .password = WIFI_PASS_DEFAULT,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    // 6. Puesta en marcha del servidor HTTP en el puerto 80
    iniciar_servidor_http();
}

/**
 * @brief Copia de forma segura el nuevo estado del sistema hacia la estructura interna del servidor web.
 * @param[in] nuevo_estado Puntero a la estructura actualizada por \c TaskDisplayLCD.
 */
void WebUnidadPacienteActualizarDatos(const estado_unidad_paciente_t *nuevo_estado) {
    if (nuevo_estado == NULL) return;
    portENTER_CRITICAL(&s_web_spinlock);
    s_datos_actuales = *nuevo_estado;
    portEXIT_CRITICAL(&s_web_spinlock);
}

/**
 * @brief Determina el texto de hasta 16 caracteres que debe exhibirse en la Fila 0 del display LCD 16x2
 *        cuando el sistema se encuentra sin alertas activas ni simulación local abierta.
 * @param[out] buffer_17_bytes Buffer de destino de 17 bytes.
 */
void WebUnidadPacienteObtenerTextoFila0(char *buffer_17_bytes) {
    if (buffer_17_bytes == NULL) return;

    /*
     * Evaluación jerárquica bajo sección crítica:
     * 1. Si aún no obtuvo dirección IPv4 del router (!s_conectado_wifi) -> "BIENVENIDO!".
     * 2. Si el operador envió un mensaje clínico predefinido (s_mensaje_activo no vacío) -> muestra el mensaje.
     * 3. Si el operador ingresó el nombre del paciente (s_nombre_paciente no vacío) -> muestra el nombre.
     * 4. Por defecto (campos web vacíos y Wi-Fi conectado) -> muestra la dirección IPv4 asignada (s_ip_asignada).
     */
    portENTER_CRITICAL(&s_web_spinlock);
    if (!s_conectado_wifi) {
        strncpy(buffer_17_bytes, "BIENVENIDO!", WEB_LCD_TEXT_MAX_LEN);
    } else if (s_mensaje_activo[0] != '\0') {
        strncpy(buffer_17_bytes, s_mensaje_activo, WEB_LCD_TEXT_MAX_LEN);
    } else if (s_nombre_paciente[0] != '\0') {
        strncpy(buffer_17_bytes, s_nombre_paciente, WEB_LCD_TEXT_MAX_LEN);
    } else {
        strncpy(buffer_17_bytes, s_ip_asignada, WEB_LCD_TEXT_MAX_LEN);
    }
    buffer_17_bytes[WEB_LCD_TEXT_MAX_LEN] = '\0';
    portEXIT_CRITICAL(&s_web_spinlock);
}

/**
 * @brief Consume de manera atómica la solicitud remota de desencolado de alerta FIFO.
 * @return true si había una solicitud pendiente (y la limpia a false); false en caso contrario.
 */
bool WebUnidadPacienteConsumirSolicitudClearAlert(void) {
    bool req = false;
    portENTER_CRITICAL(&s_web_spinlock);
    if (s_req_clear_alert) {
        req = true;
        s_req_clear_alert = false;
    }
    portEXIT_CRITICAL(&s_web_spinlock);
    return req;
}

/**
 * @brief Consume de manera atómica la solicitud remota de aviso sonoro de advertencia.
 * @return true si había una solicitud pendiente (y la limpia a false); false en caso contrario.
 */
bool WebUnidadPacienteConsumirSolicitudAdvertencia3s(void) {
    bool req = false;
    portENTER_CRITICAL(&s_web_spinlock);
    if (s_req_warn_3s) {
        req = true;
        s_req_warn_3s = false;
    }
    portEXIT_CRITICAL(&s_web_spinlock);
    return req;
}

/**
 * @brief Consume de manera atómica la solicitud remota de reproducción de voz LPCM ("Ayuda en camino").
 * @return true si había una solicitud pendiente (y la limpia a false); false en caso contrario.
 */
bool WebUnidadPacienteConsumirSolicitudVozAyuda(void) {
    bool req = false;
    portENTER_CRITICAL(&s_web_spinlock);
    if (s_req_play_voice) {
        req = true;
        s_req_play_voice = false;
    }
    portEXIT_CRITICAL(&s_web_spinlock);
    return req;
}

/*==================[end of file]============================================*/