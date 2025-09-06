/*
  Proyecto: Sistema de Monitoreo Tanque v1.0.4
  Hardware: LilyGO T-SIM7080G-S3 (ESP32-S3 + SIM7080G + OLED I2C)
  Versión: 1.0.4 - 05/09/2025
  Autor: LEDesign (Luis) - luis@dycelectronica.com.ar

  Seguimiento / Chat:
  CHAT_NAME: Hilo-Programar LilyGo SIM7080
  CHAT_URL : https://chatgpt.com/c/68af81ea-353c-832d-9ff2-5dd3f01e688e
  Usuario  : luis@dycelectronica.com.ar

  Build: __DATE__ " " __TIME__

  Notas:
    - OLED probado y funcionando en IO8/IO9 (0x3C)
    - WiFi AP + Web OK
    - En esta versión se prueban comandos AT al SIM7080 con timeout
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <HardwareSerial.h>

// ===== Configuración =====
#define VERSION_SW   "Sistema Tanque v1.0.4 - 05/09/2025"
#define WIFI_SSID    "Tanque-TEST"
#define WIFI_PASS    "admin123"
#define SERIAL_BAUD  115200

// ===== Pines I2C OLED / AXP2101 =====
const uint8_t I2C_SDA = 8;  // IO8
const uint8_t I2C_SCL = 9;  // IO9

// ===== Pines SIM7080G =====
#define MODEM_TX 43   // ESP TX1 -> SIM RXD
#define MODEM_RX 44   // ESP RX1 <- SIM TXD
#define MODEM_PWR 12  // PowerKey
#define MODEM_DTR 10
#define MODEM_RI  11

HardwareSerial SerialAT(1);

// ===== OLED =====
bool oledPresent = false;
int oledAddr = 0;
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

// ===== Webserver =====
WebServer server(80);

// ===== Utilidades =====
void drawOLED(const String& l1="", const String& l2="", const String& l3="") {
  if (!oledPresent) return;
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);
  if (l1.length()) oled.drawStr(0, 12, l1.c_str());
  if (l2.length()) oled.drawStr(0, 28, l2.c_str());
  if (l3.length()) oled.drawStr(0, 44, l3.c_str());
  oled.sendBuffer();
}

void i2cScanAndInit() {
  Serial.println("Escaneando bus I2C...");
  oledPresent = false;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" -> I2C device at 0x%02X\n", address);
      if (address == 0x3C || address == 0x3D) {
        oledPresent = true;
        oledAddr = address;
      }
    }
  }
  if (oledPresent) {
    oled.begin();
    drawOLED("OLED OK", "Dir: 0x" + String(oledAddr, HEX), VERSION_SW);
  } else {
    Serial.println("No se detectó OLED, deshabilitado.");
  }
}

// ===== Web page =====
String htmlPage() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>Dashboard v1.0.4</title></head><body>";
  html += "<h1>Dashboard de Prueba</h1>";
  html += "<p><b>Versión:</b> " + String(VERSION_SW) + "</p>";
  html += "<p><b>Build:</b> " + String(__DATE__) + " " + String(__TIME__) + "</p>";
  html += "<p><b>OLED:</b> " + (oledPresent ? "OK en 0x" + String(oledAddr, HEX) : "No presente") + "</p>";
  html += "<p><b>SIM7080:</b> chequeando en Serial...</p>";
  html += "</body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", htmlPage());
}

// ===== SIM7080 =====
void modemPowerOn() {
  pinMode(MODEM_PWR, OUTPUT);
  digitalWrite(MODEM_PWR, LOW);
  delay(10);
  digitalWrite(MODEM_PWR, HIGH);
  delay(150);
  digitalWrite(MODEM_PWR, LOW);
  delay(3000);
  pinMode(MODEM_DTR, OUTPUT);
  digitalWrite(MODEM_DTR, LOW);
}

// Envía comando AT y espera respuesta
bool sendAT(const char* cmd, const char* expect, uint32_t timeout=2000) {
  SerialAT.println(cmd);
  Serial.print(">> "); Serial.println(cmd);
  uint32_t start = millis();
  String resp = "";
  while (millis() - start < timeout) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      resp += c;
    }
    if (resp.indexOf(expect) >= 0) {
      Serial.print("<< "); Serial.println(resp);
      if (oledPresent) drawOLED(cmd, "OK", "");
      return true;
    }
  }
  Serial.print("!! Timeout esperando "); Serial.println(expect);
  if (oledPresent) drawOLED(cmd, "Timeout", "");
  return false;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(100);

  Serial.println("======================================");
  Serial.println(VERSION_SW);
  Serial.print("Build: "); Serial.print(__DATE__); Serial.print(" "); Serial.println(__TIME__);
  Serial.println("Placa: LilyGO T-SIM7080G-S3 | Serial: 115200");
  Serial.println("TX1->GPIO43  RX1->GPIO44  PWRKEY->GPIO12  DTR->GPIO10");
  Serial.println("OLED SDA->IO8  SCL->IO9");
  Serial.println("======================================");

  // I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  i2cScanAndInit();

  // WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  Serial.print("AP listo. IP: "); Serial.println(WiFi.softAPIP());
  if (oledPresent) drawOLED("AP listo", WiFi.softAPIP().toString(), VERSION_SW);

  // Webserver
  server.on("/", handleRoot);
  server.begin();

  // Modem
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  modemPowerOn();
  delay(2000);

  // Secuencia de pruebas AT
  sendAT("AT", "OK", 2000);
  sendAT("ATI", "SIMCOM", 3000);      // Info del módulo
  sendAT("AT+CPIN?", "READY", 3000);  // Estado de SIM
  sendAT("AT+CSQ", "+CSQ", 3000);     // Señal
  sendAT("AT+CFUN?", "+CFUN", 3000);  // Estado funcional
}

void loop() {
  server.handleClient();

  // Volcar cualquier respuesta extra
  while (SerialAT.available()) {
    String r = SerialAT.readStringUntil('\n');
    r.trim();
    if (r.length()) {
      Serial.println("SIM7080> " + r);
      if (oledPresent) drawOLED("SIM7080:", r.substring(0,22), "");
    }
  }
}
