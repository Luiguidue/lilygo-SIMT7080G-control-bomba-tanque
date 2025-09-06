#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <U8g2lib.h>
#include <EEPROM.h>

/*
 * =====================================================================
 * CONFIGURACIÓN DEL SISTEMA - v4.3.4 (LEDesign)
 * 
 * SOLUCIÓN DEFINITIVA AL PROBLEMA DE WDT RESET
 * 
 * CAMBIOS CLAVE:
 * 1. Implementación específica anti-WDT para AT+SAPBR=1,1
 * 2. Técnicas de "modo avión" para reiniciar la conexión sin WDT reset
 * 3. Alimentación constante del Watchdog durante operaciones largas
 * 4. Sistema de reintentos robusto con delays estratégicos
 * 
 * ¡IMPORTANTE! Basado en el FIX ESPECÍFICO del archivo Pasted_Text_1754185774962.txt
 * =====================================================================
 */

// === CONFIGURACIÓN HARDWARE ===
#define OLED_SDA D2         // Pin SDA del OLED (GPIO4)
#define OLED_SCL D1         // Pin SCL del OLED (GPIO5)
#define SIM800L_RX D7       // Pin RX del SIM800L (GPIO13)
#define SIM800L_TX D8       // Pin TX del SIM800L (GPIO15)
#define LED_PIN D0          // Pin del LED indicador (GPIO16)

// === CONFIGURACIÓN DE RED ===
const char* WIFI_SSID = "tanque1-serrillos";  // Nombre de tu AP
const char* WIFI_PASS = "admin123";           // Contraseña de tu AP
const char* TS_API_KEY = "V6YDL1H0KAQIGFA9";  // Tu API Key de ThingSpeak
const char* APN = "internet.ctimovil.com.ar"; // APN de CTI

// === INICIALIZACIÓN DE COMPONENTES ===
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
SoftwareSerial sim800l(SIM800L_RX, SIM800L_TX);
ESP8266WebServer server(80);

// === VARIABLES GLOBALES ===
float testLevel = 75.5;
unsigned long lastUpdate = 0;
bool sim800lConnected = false;
String simStatus = "Esperando..."; 
unsigned int connectedStations = 0;
bool gprsReady = false;
int rssi = 0;              // Señal del SIM800L
String imei = "N/A";       // IMEI del módulo
String ipAddress = "N/A";   // IP asignada

// === CONFIGURACIÓN DE DEPURACIÓN ===
const unsigned long SEND_INTERVAL = 60000; // 1 minuto
const unsigned long GPRS_RETRY_DELAY = 15000; // 15 segundos para reintentos GPRS

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  EEPROM.begin(512);
  
  // === INICIALIZACIÓN OLED ===
  oled.begin();
  oled.setFont(u8g2_font_5x7_tf);  // Fuente más angosta
  oled.setDisplayRotation(U8G2_R0);
  oled.clearBuffer();
  showBootScreen();
  
  // === CONFIGURACIÓN WiFi AP ===
  WiFi.mode(WIFI_AP);
  IPAddress ip(192, 168, 4, 100);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(ip, gateway, subnet);
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  
  // === CONFIGURACIÓN SERVIDOR WEB ===
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  
  // === CONFIGURACIÓN SIM800L ===
  sim800l.begin(9600);
  initSIM800L(); // Inicialización completa
  
  ESP.wdtEnable(8000);
  updateOLED();
  Serial.println("Sistema listo. Conéctate a http://192.168.4.100");
}

void loop() {
  ESP.wdtFeed();
  server.handleClient();
  
  // Enviar datos a ThingSpeak cada 1 minuto
  if(millis() - lastUpdate > SEND_INTERVAL) {
    lastUpdate = millis();
    sendToThingSpeak(testLevel);
  }
  
  // Actualizar OLED cada segundo
  static unsigned long lastOLEDUpdate = 0;
  if(millis() - lastOLEDUpdate > 1000) {
    lastOLEDUpdate = millis();
    connectedStations = WiFi.softAPgetStationNum();
    updateOLED();
  }
  
  // Monitorear comunicación SIM800L
  if(sim800l.available()) {
    char c = sim800l.read();
    Serial.write(c);
  }
}

// === FUNCIONES DEL SERVIDOR WEB ===
void handleRoot() {
  String html = "<html><head><meta charset='UTF-8'><title>Monitor de Tanque</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial,sans-serif;max-width:600px;margin:0 auto;padding:20px}";
  html += "h1{text-align:center;color:#2c3e50}table{width:100%;border-collapse:collapse}";
  html += "th,td{padding:10px;border:1px solid #ddd;text-align:left}";
  html += "th{background-color:#f2f2f2}tr:nth-child(even){background-color:#f9f9f9}";
  html += ".status{padding:5px 10px;border-radius:5px;display:inline-block}";
  html += ".ok{background-color:#d4edda;color:#155724}.error{background-color:#f8d7da;color:#721c24}";
  html += ".warning{background-color:#fff3cd;color:#856404}";
  html += ".info{background-color:#e2e3e5;color:#383d41}";
  html += "</style></head><body>";
  html += "<h1>Monitor de Tanque - ESP8266</h1>";
  html += "<table>";
  html += "<tr><th>Métrica</th><th>Valor</th></tr>";
  html += "<tr><td>Nivel del tanque</td><td>" + String(testLevel, 1) + " %</td></tr>";
  
  html += "<tr><td>Estado WiFi</td><td><span class='status ";
  if (connectedStations > 0) {
    html += "ok'>Conectado (" + String(connectedStations) + " dispositivo";
    if (connectedStations > 1) html += "s";
    html += ")";
  } else {
    html += "warning'>AP activo (sin dispositivos)";
  }
  html += "</span></td></tr>";
  
  html += "<tr><td>Estado SIM800L</td><td><span class='status ";
  html += sim800lConnected ? "ok'>Conectado" : "error'>No conectado";
  html += "</span></td></tr>";
  
  // Información adicional de diagnóstico
  html += "<tr><td>Señal (RSSI)</td><td>" + String(rssi) + " dBm</td></tr>";
  html += "<tr><td>IMEI</td><td>" + imei + "</td></tr>";
  html += "<tr><td>Dirección IP</td><td>" + ipAddress + "</td></tr>";
  html += "<tr><td>Último envío</td><td>" + getLastUpdateStr() + "</td></tr>";
  html += "</table></body></html>";
  
  server.send(200, "text/html", html);
}

void handleData() {
  String json = "{\"tank_level\":" + String(testLevel, 1);
  json += ",\"wifi_status\":" + String(connectedStations > 0 ? 1 : 0);
  json += ",\"sim800l_status\":" + String(sim800lConnected ? 1 : 0);
  json += ",\"rssi\":" + String(rssi);
  json += ",\"imei\":\"" + imei + "\"";
  json += ",\"ip_address\":\"" + ipAddress + "\"";
  json += ",\"last_update\":\"" + getLastUpdateStr() + "\"";
  json += ",\"connected_stations\":" + String(connectedStations);
  json += "}";
  
  server.send(200, "application/json", json);
}

// === INICIALIZACIÓN COMPLETA DEL SIM800L ===
void initSIM800L() {
  simStatus = "Inicializando...";
  updateOLED();
  
  // Desactivar temporalmente el Watchdog durante configuración
  ESP.wdtDisable();
  
  // 1. Verificar comunicación básica
  if (sendAT("AT", 5000).indexOf("OK") < 0) {
    simStatus = "ERROR: No comunicación";
    ESP.wdtEnable(8000);
    return;
  }
  
  // 2. Configurar reporte detallado de errores
  sendAT("AT+CMEE=2", 2000);
  
  // 3. Configurar registro de red
  sendAT("AT+CREG=1", 2000);
  
  // 4. Obtener IMEI
  imei = getIMEI();
  
  // 5. Verificar cobertura
  rssi = getRSSI();
  if (rssi > -100) {
    simStatus = "Cobertura OK";
  } else {
    simStatus = "Cobertura baja";
    ESP.wdtEnable(8000);
    return;
  }
  
  // 6. Establecer conexión GPRS
  connectGPRS();
  
  ESP.wdtEnable(8000);
}

// === OBTENER RSSI (SEÑAL DEL MÓDULO) ===
int getRSSI() {
  String csq = sendAT("AT+CSQ", 3000);
  Serial.println("Respuesta CSQ: " + csq);
  
  int pos = csq.indexOf("+CSQ:");
  if (pos >= 0) {
    int comma = csq.indexOf(",", pos);
    if (comma > 0) {
      String rssiStr = csq.substring(pos + 6, comma);
      rssiStr.trim();
      int rssiValue = rssiStr.toInt();
      
      if (rssiValue == 99) {
        return -100; // Sin señal
      } else {
        return (rssiValue * 2) - 113; // Fórmula aproximada
      }
    }
  }
  return -100; // Sin señal
}

// === OBTENER IMEI ===
String getIMEI() {
  String response = sendAT("AT+GSN", 2000);
  int newline = response.indexOf("\n");
  if (newline >= 0) {
    int okPos = response.indexOf("OK");
    if (okPos > newline) {
      return response.substring(newline + 1, okPos - 1);
    }
  }
  return "N/A";
}

// === CONEXIÓN GPRS CON SOLUCIÓN ANTI-WDT ESPECÍFICA ===
void connectGPRS() {
  simStatus = "Configurando GPRS...";
  updateOLED();
  
  // Desactivar Watchdog durante operación crítica
  ESP.wdtDisable();
  
  // Configurar APN
  sendAT("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", 2000);
  sendAT("AT+SAPBR=3,1,\"APN\",\"" + String(APN) + "\"", 2000);
  
  // TÉCNICA ANTI-WDT ESPECÍFICA PARA AT+SAPBR=1,1
  simStatus = "Conectando GPRS (anti-WDT)...";
  updateOLED();
  
  // 1. Limpiar conexiones previas de forma segura
  safeCleanupBeforeConnect();
  
  // 2. Intentar establecer conexión GPRS con reintentos
  int attempts = 0;
  bool connected = false;
  
  while (attempts < 3 && !connected) {
    simStatus = "Conectando GPRS (" + String(attempts+1) + "/3)...";
    updateOLED();
    
    // TÉCNICA CLAVE: Usar timeout largo pero alimentar WDT
    String gprsResponse = sendATWithWDT("AT+SAPBR=1,1", 45000); // Timeout de 45 segundos
    
    if (gprsResponse.indexOf("OK") >= 0 || gprsResponse.indexOf("ALREADY") >= 0) {
      connected = true;
      simStatus = "GPRS OK";
      gprsReady = true;
      
      // Esperar a que el módulo esté listo para HTTP
      simStatus = "Esperando GPRS listo...";
      updateOLED();
      Serial.println("Esperando 8 segundos para que GPRS se estabilice...");
      delayWithWDT(8000);
      
      // Verificar IP asignada
      String ipStatus = sendAT("AT+SAPBR=2,1", 5000);
      if (ipStatus.indexOf("+SAPBR: 1,1,") >= 0) {
        int quote1 = ipStatus.indexOf("\"", ipStatus.indexOf("\"") + 1);
        int quote2 = ipStatus.indexOf("\"", quote1 + 1);
        if (quote1 > 0 && quote2 > quote1) {
          ipAddress = ipStatus.substring(quote1 + 1, quote2);
          simStatus = "IP: " + ipAddress;
          gprsReady = true;
        }
      }
    } else {
      simStatus = "Reintentando en 15s...";
      updateOLED();
      delayWithWDT(GPRS_RETRY_DELAY);
      attempts++;
    }
  }
  
  if (!connected) {
    simStatus = "GPRS FALLÓ";
    gprsReady = false;
    ipAddress = "N/A";
  }
  
  // Reactivar Watchdog
  ESP.wdtEnable(8000);
}

// === LIMPIEZA SEGURA ANTES DE CONEXIÓN GPRS ===
void safeCleanupBeforeConnect() {
  simStatus = "Limpiando conexiones...";
  updateOLED();
  
  // 1. Limpiar HTTP
  sendAT("AT+HTTPTERM", 2000);
  delayWithWDT(1000);
  
  // 2. Limpiar TCP/IP
  sendAT("AT+CIPSHUT", 2000);
  delayWithWDT(2000);
  
  // 3. NO usar AT+SAPBR=0,1 (causa WDT reset)
  // 4. Usar técnica alternativa: Modo avión
  sendAT("AT+CFUN=4", 2000); // Modo avión
  delayWithWDT(8000);
  sendAT("AT+CFUN=1", 2000); // Restaurar
  delayWithWDT(8000);
}

// === ENVÍO A THINGSPEAK CON PROTECCIÓN ANTI-WDT ===
void sendToThingSpeak(float level) {
  ESP.wdtDisable();
  
  simStatus = "Preparando envío...";
  updateOLED();
  
  // 1. Asegurar que GPRS está conectado y estable
  if (!gprsReady) {
    connectGPRS();
    if (!gprsReady) {
      simStatus = "GPRS no listo";
      ESP.wdtEnable(8000);
      return;
    }
  }
  
  // 2. Inicializar HTTP
  simStatus = "Iniciando HTTP...";
  updateOLED();
  String httpInitResponse = sendAT("AT+HTTPINIT", 5000);
  
  if (httpInitResponse.indexOf("OK") < 0) {
    // Intentar nuevamente después de un retraso adicional
    delayWithWDT(3000);
    httpInitResponse = sendAT("AT+HTTPINIT", 5000);
    
    if (httpInitResponse.indexOf("OK") < 0) {
      simStatus = "HTTP INIT FALLÓ";
      ESP.wdtEnable(8000);
      return;
    }
  }
  
  // 3. Configurar parámetros HTTP
  sendAT("AT+HTTPPARA=\"CID\",1", 2000);
  
  // 4. Construir URL
  String url = "http://api.thingspeak.com/update?api_key=" + String(TS_API_KEY);
  url += "&field1=" + String(level);
  
  // 5. Enviar URL
  simStatus = "Configurando URL...";
  updateOLED();
  if (sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 5000).indexOf("OK") < 0) {
    simStatus = "URL ERR";
    sendAT("AT+HTTPTERM", 1000);
    ESP.wdtEnable(8000);
    return;
  }
  
  // 6. Enviar solicitud
  simStatus = "Enviando datos...";
  updateOLED();
  String response = sendATWithWDT("AT+HTTPACTION=0", 20000);
  
  if (response.indexOf("200") >= 0) {
    simStatus = "OK - Datos enviados";
    sim800lConnected = true;
  } else {
    simStatus = "HTTP ERR";
    sim800lConnected = false;
  }
  
  // 7. Leer respuesta
  sendAT("AT+HTTPREAD", 2000);
  
  // 8. Terminar sesión HTTP
  sendAT("AT+HTTPTERM", 1000);
  
  // 9. Limpieza segura (sin causar WDT reset)
  safeSAPBRDisconnect();
  
  ESP.wdtEnable(8000);
  
  // Parpadeo del LED para indicar envío exitoso
  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(LED_PIN, LOW);
}

// === LIMPIEZA SEGURA SIN CAUSAR WDT RESET ===
void safeSAPBRDisconnect() {
  simStatus = "Limpiando...";
  updateOLED();
  
  // TÉCNICA SEGURA BASADA EN TU ARCHIVO DE EJEMPLO
  sendAT("AT+CIPSHUT", 2000);
  delayWithWDT(2000);
  
  // NO usar AT+SAPBR=0,1 (causa WDT reset)
  sendAT("AT+CFUN=4", 2000); // Modo avión
  delayWithWDT(8000);
  sendAT("AT+CFUN=1", 2000); // Restaurar
  delayWithWDT(8000);
  
  gprsReady = false;
}

// === FUNCIÓN SENDAT CON ALIMENTACIÓN CONTINUA DEL WDT ===
String sendATWithWDT(String command, unsigned long timeout) {
  sim800l.println(command);
  Serial.println("SIM800L > " + command);
  
  String response = "";
  unsigned long start = millis();
  
  while (millis() - start < timeout) {
    if (sim800l.available()) {
      char c = sim800l.read();
      response += c;
      
      // Salir si encontramos una respuesta definitiva
      if (response.indexOf("OK") >= 0 || 
          response.indexOf("ERROR") >= 0 || 
          response.indexOf("+HTTP") >= 0) {
        break;
      }
    }
    
    // ¡CRÍTICO! Alimentar Watchdog durante espera
    ESP.wdtFeed();
    delay(1);
  }
  
  if (response.length() > 0) {
    Serial.println("SIM800L < " + response);
  } else {
    Serial.println("SIM800L < (timeout)");
  }
  
  return response;
}

// === FUNCIÓN SENDAT BÁSICA ===
String sendAT(String command, unsigned long timeout) {
  sim800l.println(command);
  Serial.println("SIM800L > " + command);
  
  String response = "";
  unsigned long start = millis();
  
  while (millis() - start < timeout) {
    if (sim800l.available()) {
      char c = sim800l.read();
      response += c;
      
      // Salir si encontramos una respuesta definitiva
      if (response.indexOf("OK") >= 0 || 
          response.indexOf("ERROR") >= 0 || 
          response.indexOf("+HTTP") >= 0) {
        break;
      }
    }
    
    ESP.wdtFeed();
    delay(1);
  }
  
  if (response.length() > 0) {
    Serial.println("SIM800L < " + response);
  } else {
    Serial.println("SIM800L < (timeout)");
  }
  
  return response;
}

// === FUNCIÓN DE DELAY CON ALIMENTACIÓN DEL WDT ===
void delayWithWDT(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    ESP.wdtFeed();
    delay(1);
  }
}

// === SOLUCIÓN AL PROBLEMA DEL OLED ===
void showBootScreen() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x7_tf);
  
  // Ajuste de posiciones para evitar texto cortado
  oled.drawStr(0, 10, "Sistema de Monitoreo");
  oled.drawStr(0, 18, "de Tanque - v4.3.4");
  oled.drawStr(0, 26, "Iniciando...");
  oled.drawStr(0, 42, "IP: 192.168.4.100");
  
  oled.sendBuffer();
  delay(2000);
}

void updateOLED() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x7_tf);
  
  // Ajuste de posiciones para evitar texto cortado
  oled.drawStr(0, 10, "Sistema Tanque v4.3.4");
  
  char buffer[30];
  snprintf(buffer, sizeof(buffer), "Nivel: %.1f %%", testLevel);
  oled.drawStr(0, 18, buffer);
  
  snprintf(buffer, sizeof(buffer), "WiFi: %d disp", connectedStations);
  oled.drawStr(0, 26, buffer);
  
  snprintf(buffer, sizeof(buffer), "SIM800L: %s", simStatus.c_str());
  oled.drawStr(0, 34, buffer);
  
  snprintf(buffer, sizeof(buffer), "RSSI: %d dBm", rssi);
  oled.drawStr(0, 42, buffer);
  
  snprintf(buffer, sizeof(buffer), "Ult. envio: %s", getLastUpdateStr().c_str());
  oled.drawStr(0, 50, buffer);
  
  oled.sendBuffer();
}

String getLastUpdateStr() {
  if (lastUpdate == 0) return "Nunca";
  
  unsigned long elapsed = millis() - lastUpdate;
  unsigned long seconds = elapsed / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  
  if (hours > 0) return String(hours) + "h " + String(minutes % 60) + "m";
  if (minutes > 0) return String(minutes) + "m " + String(seconds % 60) + "s";
  return String(seconds) + "s";
}
