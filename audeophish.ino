#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// ===== НАСТРОЙКИ =====
#define WIFI_SSID "WIFI_YOU"
#define WIFI_PASS "PASSWORD_YOU"
#define BOT_TOKEN "BOT_TOKEN_YOU"
#define CHAT_ID "TGID_YOU"

#define MIC_PIN 34
#define LED_PIN 2

#define SAMPLE_RATE 8000
#define RECORD_SECONDS 3
#define BUFFER_SIZE (SAMPLE_RATE * RECORD_SECONDS)

int16_t audioBuffer[BUFFER_SIZE];
int bufferIndex = 0;

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

unsigned long lastSampleTime = 0;
const unsigned long sampleInterval = 1000000 / SAMPLE_RATE;

enum State { LISTENING, RECORDING, WAITING_RESPONSE };
State currentState = LISTENING;
unsigned long recordingStart = 0;
unsigned long stateStart = 0;

int noiseLevel = 0;
int threshold = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  Serial.println("=== VOICE ASSISTANT (TELEGRAM VOICE FORMAT) ===");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi OK");
  client.setInsecure();

  // Калибровка шума
  long sum = 0;
  for (int i = 0; i < 200; i++) {
    sum += analogRead(MIC_PIN);
    delay(5);
  }
  noiseLevel = sum / 200;
  threshold = noiseLevel + 250;

  Serial.print("Noise: ");
  Serial.println(noiseLevel);
  Serial.print("Threshold: ");
  Serial.println(threshold);

  bot.sendMessage(CHAT_ID, "🎤 Голосовой ассистент готов (voice format)!", "");
}

void sendVoice() {
  Serial.println("Sending voice message...");

  uint32_t dataSize = bufferIndex * 2;
  uint32_t fileSize = dataSize + 36;

  // WAV заголовок
  uint8_t wavHeader[44];

  // RIFF header
  wavHeader[0] = 'R';
  wavHeader[1] = 'I';
  wavHeader[2] = 'F';
  wavHeader[3] = 'F';
  
  wavHeader[4] = fileSize & 0xFF;
  wavHeader[5] = (fileSize >> 8) & 0xFF;
  wavHeader[6] = (fileSize >> 16) & 0xFF;
  wavHeader[7] = (fileSize >> 24) & 0xFF;
  
  wavHeader[8] = 'W';
  wavHeader[9] = 'A';
  wavHeader[10] = 'V';
  wavHeader[11] = 'E';
  
  wavHeader[12] = 'f';
  wavHeader[13] = 'm';
  wavHeader[14] = 't';
  wavHeader[15] = ' ';
  
  // Format chunk size (16 for PCM)
  wavHeader[16] = 16;
  wavHeader[17] = 0;
  wavHeader[18] = 0;
  wavHeader[19] = 0;
  
  // Audio format (1 = PCM)
  wavHeader[20] = 1;
  wavHeader[21] = 0;
  
  // Number of channels (1 = mono)
  wavHeader[22] = 1;
  wavHeader[23] = 0;
  
  // Sample rate
  wavHeader[24] = (SAMPLE_RATE & 0xFF);
  wavHeader[25] = (SAMPLE_RATE >> 8) & 0xFF;
  wavHeader[26] = (SAMPLE_RATE >> 16) & 0xFF;
  wavHeader[27] = (SAMPLE_RATE >> 24) & 0xFF;
  
  // Byte rate
  uint32_t byteRate = SAMPLE_RATE * 2;
  wavHeader[28] = (byteRate & 0xFF);
  wavHeader[29] = (byteRate >> 8) & 0xFF;
  wavHeader[30] = (byteRate >> 16) & 0xFF;
  wavHeader[31] = (byteRate >> 24) & 0xFF;
  
  // Block align
  wavHeader[32] = 2;
  wavHeader[33] = 0;
  
  // Bits per sample
  wavHeader[34] = 16;
  wavHeader[35] = 0;
  
  // Data chunk
  wavHeader[36] = 'd';
  wavHeader[37] = 'a';
  wavHeader[38] = 't';
  wavHeader[39] = 'a';
  
  wavHeader[40] = (dataSize & 0xFF);
  wavHeader[41] = (dataSize >> 8) & 0xFF;
  wavHeader[42] = (dataSize >> 16) & 0xFF;
  wavHeader[43] = (dataSize >> 24) & 0xFF;

  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("Connection failed");
    return;
  }

  String boundary = "----ESP32Boundary";

  // ⭐ ИСПРАВЛЕНО: используем sendVoice вместо sendDocument
  String bodyStart = "--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
  bodyStart += String(CHAT_ID) + "\r\n";

  bodyStart += "--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"voice\"; filename=\"audio.wav\"\r\n";
  bodyStart += "Content-Type: audio/wav\r\n\r\n";

  String bodyEnd = "\r\n--" + boundary + "--\r\n";

  int contentLength = bodyStart.length() + 44 + dataSize + bodyEnd.length();

  client.println("POST /bot" + String(BOT_TOKEN) + "/sendVoice HTTP/1.1");
  client.println("Host: api.telegram.org");
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.println("Content-Length: " + String(contentLength));
  client.println();

  client.print(bodyStart);
  client.write(wavHeader, 44);

  // 🔥 Быстрая передача блоками
  client.write((uint8_t*)audioBuffer, dataSize);

  client.print(bodyEnd);

  Serial.println("Voice sent!");
  client.stop();
}

void loop() {
  if (currentState == LISTENING) {
    if (analogRead(MIC_PIN) > threshold) {
      Serial.println("Recording...");
      bufferIndex = 0;
      recordingStart = millis();
      lastSampleTime = micros();
      currentState = RECORDING;
    }
    delay(10);
  }

  else if (currentState == RECORDING) {
    if (micros() - lastSampleTime >= sampleInterval) {
      if (bufferIndex < BUFFER_SIZE) {
        audioBuffer[bufferIndex++] = analogRead(MIC_PIN);
      }
      lastSampleTime += sampleInterval;
    }

    digitalWrite(LED_PIN, millis() % 200 < 100);

    if (millis() - recordingStart >= RECORD_SECONDS * 1000) {
      digitalWrite(LED_PIN, LOW);

      Serial.print("Samples: ");
      Serial.println(bufferIndex);

      if (bufferIndex > 1000) {
        sendVoice();  // ← отправляем как voice
        currentState = WAITING_RESPONSE;
        stateStart = millis();
      } else {
        currentState = LISTENING;
      }
    }
  }

  else if (currentState == WAITING_RESPONSE) {
    int messages = bot.getUpdates(bot.last_message_received + 1);

    while (messages) {
      for (int i = 0; i < messages; i++) {
        if (bot.messages[i].chat_id == CHAT_ID && bot.messages[i].text != "") {
          Serial.println("AI: " + bot.messages[i].text);
          currentState = LISTENING;
        }
      }
      messages = bot.getUpdates(bot.last_message_received + 1);
    }

    if (millis() - stateStart > 15000) {
      currentState = LISTENING;
    }

    delay(200);
  }
}

