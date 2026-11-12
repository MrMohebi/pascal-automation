#include "board_pins.h"
#include "control.h"
#include "local_ui.h"
#include "outputs.h"
#include "pascal_network.h"
#include "storage.h"
#include <Arduino.h>
#include <DHT.h>
#include <esp_task_wdt.h>
#include <sys/time.h>

pascal::Controller controller;
Storage storage;
PascalNetwork network;
LocalUI ui;
DHT sensor(boardpins::dht, DHT22);
uint32_t revision = 0;
String bootId, lastCommandId, resultStatus, resultMessage;
bool storageReady = false;
uint8_t persistedFaults = 0;

int secondsToday() {
  time_t t = time(nullptr);
  struct tm local{};
  localtime_r(&t, &local);
  return local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
}
bool clockValid() { return time(nullptr) >= 1704067200; }
bool saveConfig(const pascal::Config &c, uint32_t base) {
  if (base != revision || !pascal::valid(c) || !storageReady || revision == UINT32_MAX)
    return false;
  auto candidate = controller;
  candidate.apply(c, millis());
  candidate.updateSchedule(clockValid(), secondsToday() / 60);
  if (!storage.save(candidate, revision + 1)) {
    storageReady = false;
    return false;
  }
  controller = candidate;
  revision++;
  return true;
}
bool lightOverride(int light, int mode) {
  if (!storageReady || revision == UINT32_MAX)
    return false;
  auto candidate = controller;
  if (!candidate.overrideLight(light, mode, millis()))
    return false;
  if (!storage.save(candidate, revision + 1)) {
    storageReady = false;
    return false;
  }
  controller = candidate;
  revision++;
  return true;
}
bool acknowledgeFaults() {
  if (!storageReady || !controller.configured || !controller.sensorReady(millis()) ||
      controller.temperature >= controller.config.max_temperature)
    return false;
  if (storage.prefs.putUChar("faults", 0) != 1)
    return false;
  persistedFaults = 0;
  return controller.acknowledge(millis());
}
bool setClock(const String &s) {
  if (s.length() != 12)
    return false;
  for (char c : s)
    if (c < '0' || c > '9')
      return false;
  int y = s.substring(0, 4).toInt(), mo = s.substring(4, 6).toInt(),
      day = s.substring(6, 8).toInt(), h = s.substring(8, 10).toInt(),
      m = s.substring(10, 12).toInt();
  if (y < 2024 || y > 2099 || mo < 1 || mo > 12 || day < 1 || day > 31 || h > 23 || m > 59)
    return false;
  struct tm t{};
  t.tm_year = y - 1900;
  t.tm_mon = mo - 1;
  t.tm_mday = day;
  t.tm_hour = h;
  t.tm_min = m;
  t.tm_isdst = 0;
  time_t epoch = mktime(&t);
  if (t.tm_year != y - 1900 || t.tm_mon != mo - 1 || t.tm_mday != day || t.tm_hour != h ||
      t.tm_min != m)
    return false;
  timeval tv{epoch, 0};
  return settimeofday(&tv, nullptr) == 0;
}
void snapshot(JsonDocument &d) {
  uint32_t now = millis();
  d["protocol"] = 1;
  d["boot_id"] = bootId;
  d["uptime_ms"] = now;
  d["revision"] = revision;
  if (controller.configured)
    writeConfig(d["config"].to<JsonObject>(), controller.config);
  else
    d["config"] = nullptr;
  if (std::isfinite(controller.temperature) && controller.temperature >= -40 &&
      controller.temperature <= 80)
    d["temperature"] = controller.temperature;
  else
    d["temperature"] = nullptr;
  if (std::isfinite(controller.humidity) && controller.humidity >= 0 && controller.humidity <= 100)
    d["humidity"] = controller.humidity;
  else
    d["humidity"] = nullptr;
  if (controller.sampled)
    d["sample_age_ms"] = uint32_t(now - controller.sampleAt);
  else
    d["sample_age_ms"] = nullptr;
  d["sensor_ok"] = controller.sensorReady(now);
  d["clock_ok"] = clockValid();
  d["simulated"] = outputs::simulated;
  d["profile"] = controller.scheduleState.known ? (controller.isDay ? "day" : "night") : "unknown";
  d["schedule_cached"] = controller.scheduleState.known;
  d["light_modes_persistent"] = true;
  d["reason"] = controller.reason;
  d["wifi_rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  d["display_ok"] = ui.present;
  d["storage_ok"] = storageReady;
  auto out = d["outputs"].to<JsonArray>();
  for (bool state : controller.outputs)
    out.add(state);
  auto faults = d["faults"].to<JsonArray>();
  if (controller.faults & 1)
    faults.add("overtemperature");
  if (controller.faults & 2)
    faults.add("heater_runtime");
  if (controller.faults & 4)
    faults.add("humidifier_runtime");
  if (!storageReady)
    faults.add("storage_failure");
  auto ovs = d["overrides"].to<JsonArray>();
  for (auto &o : controller.overrides) {
    auto obj = ovs.add<JsonObject>();
    obj["mode"] = o.mode;
    obj["until"] = 0; // No expiry: saved until explicitly changed.
  }
  if (lastCommandId.length()) {
    d["result"]["id"] = lastCommandId;
    d["result"]["status"] = resultStatus;
    d["result"]["message"] = resultMessage;
  }
}
void command(JsonDocument &d) {
  String id = d["id"].as<String>();
  if (id.isEmpty() || id == lastCommandId)
    return;
  lastCommandId = id;
  resultStatus = "rejected";
  resultMessage = "Expired, wrong boot, or stale revision";
  if (!clockValid() || d["expires_at"].as<int64_t>() <= time(nullptr) ||
      d["boot_id"].as<String>() != bootId || !d["base_revision"].is<uint32_t>() ||
      d["base_revision"].as<uint32_t>() != revision)
    return;
  String type = d["kind"].as<String>();
  bool success = false;
  if (type == "config") {
    pascal::Config c;
    success = readConfig(d["payload"], c) && saveConfig(c, revision);
  } else if (type == "override" && d["payload"]["light"].is<int>() &&
             d["payload"]["mode"].is<int>())
    success = lightOverride(d["payload"]["light"], d["payload"]["mode"]);
  else if (type == "acknowledge")
    success = acknowledgeFaults();
  resultStatus = success ? "applied" : "rejected";
  resultMessage =
      success ? "Applied on device" : "Invalid settings, storage error, or active safety interlock";
}
void serialInput() {
  static String line;
  static bool discard = false;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r')
      continue;
    if (ch != '\n') {
      if (line.length() < 8192 && !discard)
        line += ch;
      else {
        line = "";
        discard = true;
      }
      continue;
    }
    if (discard) {
      discard = false;
      Serial.println("{\"error\":\"Request too large\"}");
      continue;
    }
    JsonDocument d;
    auto error = deserializeJson(d, line);
    line = "";
    if (error) {
      Serial.println("{\"error\":\"Invalid JSON\"}");
      continue;
    }
    String op = d["op"].as<String>();
    if (op == "status") {
      JsonDocument status;
      snapshot(status);
      serializeJson(status, Serial);
      Serial.println();
    } else if (op == "clock") {
      Serial.println(setClock(d["value"].as<String>()) ? "{\"ok\":true}"
                                                       : "{\"error\":\"Invalid clock\"}");
    } else if (op == "provision") {
      String ssid = d["ssid"].as<String>(), password = d["password"].as<String>(),
             url = d["url"].as<String>(), ca = d["ca"].as<String>();
      while (url.endsWith("/"))
        url.remove(url.length() - 1);
      if (!storageReady || ssid.isEmpty() || ssid.length() > 32 || password.isEmpty() ||
          password.length() > 63 || !url.startsWith("https://") || url.length() > 256 ||
          ca.indexOf("BEGIN CERTIFICATE") < 0 || ca.length() > 4096) {
        Serial.println("{\"error\":\"Invalid provisioning fields\"}");
        continue;
      }
      JsonDocument creds;
      creds["ssid"] = ssid;
      creds["password"] = password;
      creds["url"] = url;
      creds["ca"] = ca;
      String stored;
      serializeJson(creds, stored);
      if (storage.prefs.putString("network", stored) != stored.length()) {
        Serial.println("{\"error\":\"Storage write failed\"}");
        continue;
      }
      Serial.println("{\"ok\":true,\"restarting\":true}");
      Serial.flush();
      delay(100);
      ESP.restart();
    } else
      Serial.println("{\"error\":\"Unknown operation\"}");
  }
}
void setup() {
  outputs::begin();
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  setenv("TZ", "IRST-3:30", 1);
  tzset();
  bootId = String(esp_random(), HEX) + String(esp_random(), HEX);
  controller.begin(millis());
  storageReady = storage.begin();
  if (storageReady) {
    if (storage.hasState() && !storage.load(controller, revision))
      storageReady = false;
    controller.faults = storage.prefs.getUChar("faults", 0);
    persistedFaults = controller.faults;
    JsonDocument creds;
    if (!deserializeJson(creds, storage.prefs.getString("network", ""))) {
      network.ssid = creds["ssid"].as<String>();
      network.password = creds["password"].as<String>();
      network.url = creds["url"].as<String>();
      network.ca = creds["ca"].as<String>();
    }
  }
  sensor.begin();
  ui.begin();
  network.begin();
  esp_task_wdt_config_t watchdog{.timeout_ms = 8000, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_init(&watchdog) == ESP_ERR_INVALID_STATE)
    esp_task_wdt_reconfigure(&watchdog);
  esp_task_wdt_add(nullptr);
  Serial.printf("{\"ready\":true,\"simulated\":%s,\"display_ok\":%s}\n",
                outputs::simulated ? "true" : "false", ui.present ? "true" : "false");
}
void loop() {
  static uint32_t sampleAt = millis(), renderAt = 0, publishAt = 0;
  uint32_t now = millis();
  if (uint32_t(now - sampleAt) >= 2500) {
    sampleAt = now;
    double rh = sensor.readHumidity(), t = sensor.readTemperature();
    controller.sample(t, rh, now);
  }
  JsonDocument incoming;
  if (network.receive(incoming))
    command(incoming);
  serialInput();
  const auto previousSchedule = controller.scheduleState;
  controller.tick(millis(), clockValid(), secondsToday() / 60, time(nullptr));
  // Save only transitions, not each sensor sample. Persist before applying outputs.
  if (storageReady && controller.scheduleState != previousSchedule &&
      !storage.save(controller, revision))
    storageReady = false;
  if (controller.faults != persistedFaults) {
    if (storageReady && storage.prefs.putUChar("faults", controller.faults) == 1)
      persistedFaults = controller.faults;
    else
      storageReady = false;
  }
  if (!storageReady)
    for (bool &state : controller.outputs)
      state = false;
  outputs::write(controller.outputs);
  if (uint32_t(now - renderAt) >= 250) {
    renderAt = now;
    ui.render(controller, now, WiFi.status() == WL_CONNECTED,
              network.serverConnected && uint32_t(now - network.lastSuccess) < 30000, clockValid(),
              outputs::simulated, revision, storageReady);
  }
  if (uint32_t(now - publishAt) >= 1000) {
    publishAt = now;
    JsonDocument d;
    snapshot(d);
    network.publish(d);
  }
  esp_task_wdt_reset();
  delay(5);
}
