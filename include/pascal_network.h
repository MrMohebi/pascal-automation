#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <atomic>

struct Frame {
  char json[4096];
};
class PascalNetwork {
public:
  QueueHandle_t outbound = nullptr, inbound = nullptr;
  String ssid, password, url, token, ca;
  std::atomic<bool> serverConnected{false};
  std::atomic<uint32_t> lastSuccess{0};
  void begin() {
    outbound = xQueueCreate(1, sizeof(Frame));
    inbound = xQueueCreate(1, sizeof(Frame));
    if (!outbound || !inbound)
      return;
    // Credentials are immutable for this task's lifetime; provisioning reboots.
    if (ssid.isEmpty() || password.isEmpty() || !url.startsWith("https://") ||
        token.length() < 32 || ca.isEmpty())
      return;
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), password.c_str());
    configTzTime("IRST-3:30", "pool.ntp.org", "time.google.com");
    xTaskCreate(task, "pascal-network", 24576, this, 1, nullptr);
  }
  void publish(const JsonDocument &d) {
    if (!outbound)
      return;
    Frame f{};
    if (measureJson(d) >= sizeof(f.json))
      return;
    serializeJson(d, f.json, sizeof(f.json));
    xQueueOverwrite(outbound, &f);
  }
  bool receive(JsonDocument &d) {
    Frame f{};
    return inbound && xQueueReceive(inbound, &f, 0) == pdTRUE && !deserializeJson(d, f.json);
  }

private:
  static void task(void *arg) { static_cast<PascalNetwork *>(arg)->run(); }
  void run() {
    uint32_t retryAt = 0;
    for (;;) {
      if (WiFi.status() != WL_CONNECTED) {
        serverConnected = false;
        if (uint32_t(millis() - retryAt) > 30000) {
          WiFi.reconnect();
          retryAt = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
      // TLS certificate verification requires a valid clock. Never setInsecure().
      if (time(nullptr) < 1704067200) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
      Frame request{};
      if (xQueuePeek(outbound, &request, 0) == pdTRUE) {
        WiFiClientSecure client;
        client.setCACert(ca.c_str());
        client.setHandshakeTimeout(8);
        HTTPClient http;
        http.setConnectTimeout(5000);
        http.setTimeout(5000);
        if (http.begin(client, url + "/api/device/sync")) {
          http.addHeader("Content-Type", "application/json");
          http.addHeader("Authorization", "Bearer " + token);
          int code = http.POST(reinterpret_cast<uint8_t *>(request.json), strlen(request.json));
          serverConnected = false;
          if (code == 200 && http.getSize() <= 6000) {
            String body = http.getString();
            JsonDocument d;
            if (body.length() < 6000 && !deserializeJson(d, body) && d["protocol"].as<int>() == 1) {
              serverConnected = true;
              lastSuccess = millis();
              if (d["command"].is<JsonObject>() &&
                  measureJson(d["command"]) < sizeof(Frame::json)) {
                Frame reply{};
                serializeJson(d["command"], reply.json, sizeof(reply.json));
                xQueueOverwrite(inbound, &reply);
              }
            }
          }
          http.end();
        }
      }
      vTaskDelay(pdMS_TO_TICKS(10000));
    }
  }
};
