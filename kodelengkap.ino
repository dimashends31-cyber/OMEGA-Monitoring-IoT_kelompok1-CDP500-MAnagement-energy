#include <ModbusMaster.h>
// #include <Wire.h> // DIHAPUS: Tidak dipakai DHT11
// #include <Adafruit_BME280.h> // DIHAPUS: Diganti DHT
#include <DHT.h> // BARU: Library untuk DHT
#include <Adafruit_Sensor.h> // Tetap dipakai oleh DHT
#include <WiFi.h>
#include <HTTPClient.h>

// === PIN DEFINITIONS ===

// BARU: Pin & Tipe Sensor DHT
#define DHTPIN 15    // Sesuai permintaan Anda
#define DHTTYPE DHT11  // Tipe sensor

// RS485 (Tidak berubah)
#define MAX485_DE 4
#define MAX485_RE 4
#define RXD2 16
#define TXD2 17

// IR Sensor (Tidak berubah)
#define IR1 32
#define IR2 33
#define IR3 25
#define IR4 26

// === GLOBAL OBJECTS ===
// Adafruit_BME280 bme; // DIHAPUS
DHT dht(DHTPIN, DHTTYPE); // BARU: Buat objek DHT
ModbusMaster node;

// === KONFIGURASI WIFI ===
const char* ssid = "OMEGA";         // ganti dengan SSID WiFi kamu
const char* password = "11234567";  // ganti dengan password WiFi kamu
const char* serverName = "http://192.168.89.15:8080/api/data"; // IP lokal PC kamu

// === MODBUS FUNCTIONS (Tidak berubah) ===
void preTransmission() {
  digitalWrite(MAX485_RE, HIGH);
  digitalWrite(MAX485_DE, HIGH);
}

void postTransmission() {
  digitalWrite(MAX485_RE, LOW);
  digitalWrite(MAX485_DE, LOW);
}

// === SETUP ===
void setup() {
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8E1, 16, 17); // RS485 Modbus

  pinMode(MAX485_RE, OUTPUT);
  pinMode(MAX485_DE, OUTPUT);
  digitalWrite(MAX485_RE, LOW);
  digitalWrite(MAX485_DE, LOW);

  // IR Sensor
  pinMode(IR1, INPUT_PULLUP);
  pinMode(IR2, INPUT_PULLUP);
  pinMode(IR3, INPUT_PULLUP);
  pinMode(IR4, INPUT_PULLUP);

  // BME280 (DIHAPUS)
  // Wire.begin(21, 22);
  // if (!bme.begin(0x76)) {
  //   Serial.println("Gagal deteksi BME280!");
  // }

  // BARU: Mulai sensor DHT11
  dht.begin();
  Serial.println("Sensor DHT11 siap.");


  node.begin(1, Serial2);   // ID PM800 (cek di menu COMM → ADDRESS)
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  // === KONEKSI WIFI ===
  WiFi.begin(ssid, password);
  Serial.print("🔗 Menghubungkan ke WiFi ");
  Serial.println(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi Terhubung!");
  Serial.print("📡 IP ESP32: ");
  Serial.println(WiFi.localIP());
}

// === LOOP (VERSI PERBAIKAN) ===
void loop() {
  uint8_t result;
  uint16_t rawLow, rawHigh;
  uint32_t combined;
  
  // --- Buat variabel untuk menampung SEMUA data ---
  float temp = 0.0, humid = 0.0;
  float voltage = 0.0, current = 0.0, power = 0.0;
  int s1, s2, s3, s4;

  // --- 1. Sensor IR ---
  s1 = digitalRead(IR1);
  s2 = digitalRead(IR2);
  s3 = digitalRead(IR3);
  s4 = digitalRead(IR4);

  // --- 2. Sensor DHT11 ---
  temp = dht.readTemperature();
  humid = dht.readHumidity();

  if (isnan(temp) || isnan(humid)) {
    Serial.println("Gagal membaca data DHT11!");
    temp = 0.0;
    humid = 0.0;
  }
  
  // --- 3. Sensor Modbus (Power Meter) ---
  
  // Baca Voltage (hanya 1x)
  delay(200); // Jeda antar pembacaan Modbus
  result = node.readInputRegisters(1123, 2);
  if (result == node.ku8MBSuccess) {
    rawLow  = node.getResponseBuffer(0);
    rawHigh = node.getResponseBuffer(1);
    combined = ((uint32_t)rawHigh << 16) | rawLow;
    memcpy(&voltage, &combined, 4);
  } else {
    Serial.print("Error read Voltage, code: ");
    Serial.println(result);
  }

  // Baca Current
  delay(200);
  result = node.readInputRegisters(1099, 2);
  if (result == node.ku8MBSuccess) {
    rawLow  = node.getResponseBuffer(0);
    rawHigh = node.getResponseBuffer(1);
    combined = ((uint32_t)rawHigh << 16) | rawLow;
    memcpy(&current, &combined, 4);
  } else {
    Serial.print("Error read Current, code: ");
    Serial.println(result);
  }

  // Baca Power
  delay(200);
  result = node.readInputRegisters(1139, 2);
  if (result == node.ku8MBSuccess) {
    rawLow  = node.getResponseBuffer(0);
    rawHigh = node.getResponseBuffer(1);
    combined = ((uint32_t)rawHigh << 16) | rawLow;
    memcpy(&power, &combined, 4);
  } else {
    Serial.print("Error read Power, code: ");
    Serial.println(result);
  }

  // --- 4. Tampilkan semua di Serial Monitor ---
  Serial.println("======================================");
  Serial.printf("IR: %d %d %d %d\n", s1, s2, s3, s4);
  Serial.printf("Suhu: %.2f °C | Kelembapan: %.2f %%\n", temp, humid);
  Serial.printf("Tegangan: %.2f V | Arus: %.2f A | Daya: %.2f W\n", voltage, current, power);
  Serial.println("======================================\n");


  // === 5. KIRIM SEMUA DATA KE SERVER ===
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");

    // DIUBAH: Mengirim semua data
    String payload = "{\"suhu\":" + String(temp, 2) +
                     ",\"humid\":" + String(humid, 2) +
                     ",\"tegangan\":" + String(voltage, 2) + // BARU
                     ",\"arus\":" + String(current, 2) +     // BARU
                     ",\"daya\":" + String(power, 3) +       // DIUBAH (key dari "kwh" ke "daya")
                     ",\"object\":\"" + String((s1==LOW||s2==LOW||s3==LOW||s4==LOW) ? "TERDETEKSI" : "TIDAK") + "\"}";

    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0) {
      Serial.printf("📡 Data terkirim ke server! Kode: %d\n", httpResponseCode);
    } else {
      Serial.printf("❌ Gagal kirim data! Error: %d\n", httpResponseCode);
    }

    http.end();
  } else {
    Serial.println("⚠ WiFi terputus, mencoba ulang...");
    WiFi.reconnect();
  }

  delay(2000); // Jeda aman (DHT11 tidak boleh dibaca terlalu cepat)
}