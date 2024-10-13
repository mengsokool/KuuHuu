#include <Arduino.h>
#include <PZEM004Tv30.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char *ssid = "YOUR_SSID";
const char *password = "YOUR_PASSWORD";

// GearMind API settings
const char *gearmindHost = "gearmind.geworn.cloud";
const int gearmindPort = 443;
const char *gearmindPath = "/api/v1/chat";
const char *gearmindApiKey = "YOUR_GEARMIND_API_KEY";

// Line Notify settings
const char *lineNotifyHost = "notify-api.line.me";
const char *lineNotifyPath = "/api/notify";
const char *lineNotifyToken = "YOUR_LINE_NOTIFY_TOKEN";

// Configuration
#define EMERGENCY_BUTTON 35
unsigned long PZEMReadInterval = 250;
unsigned long DiagnosisInterval = 60000; // Check every minute

// Initialize
float voltage = 0, current = 0, power = 0, frequency = 0, powerFactor = 0;
unsigned long previousPZEMReadInterval = 0;
unsigned long previousDiagnosisTime = 0;
bool emergencyButtonPressed = false;
unsigned long emergencyButtonPressTime = 0;
const unsigned long emergencyButtonCooldown = 5000; // 5 วินาทีสำหรับ cooldown

// Create Instance
PZEM004Tv30 pzem(Serial2, 16, 17, 0x1);

// Function Declarations
void readPZEMValues();
String performDiagnosis(const String &errorCode);
String getGearMindResponse(String message);
String urlEncode(String str);
void sendLineNotification(String message);
void checkEmergencyButton(unsigned long currentTime);

void setup()
{
  pinMode(EMERGENCY_BUTTON, INPUT_PULLUP);
  Serial.begin(115200);
  delay(2000);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
}

void loop()
{
  unsigned long currentTime = millis();

  if (currentTime - previousPZEMReadInterval > PZEMReadInterval)
  {
    previousPZEMReadInterval = currentTime;
    readPZEMValues();
  }
  checkEmergencyButton(currentTime);
}

void readPZEMValues()
{
  voltage = pzem.voltage();
  current = pzem.current();
  power = pzem.power();
  frequency = pzem.frequency();
  powerFactor = pzem.pf();

  if (isnan(voltage) || isnan(current) || isnan(power) || isnan(frequency) || isnan(powerFactor))
  {
    Serial.println("Error reading values from PZEM");
  }
  else
  {
    Serial.printf("V:%.2f A:%.2f W:%.2f Hz:%.2f PF:%.2f\n", voltage, current, power, frequency, powerFactor);
  }
}

String performDiagnosis(const String &errorCode)
{
  String diagnosisMessage = F("โปรดวิเคราะห์ข้อผิดพลาดต่อไปนี้ของเครื่องชาร์จรถยนต์ไฟฟ้า\n");
  diagnosisMessage += F("Error Code: ");
  diagnosisMessage += errorCode;
  diagnosisMessage += F("\nVoltage: ");
  diagnosisMessage += String(voltage);
  diagnosisMessage += F("\nCurrent: ");
  diagnosisMessage += String(current);
  diagnosisMessage += F("\nPower: ");
  diagnosisMessage += String(power);
  diagnosisMessage += F("\nFrequency: ");
  diagnosisMessage += String(frequency);
  diagnosisMessage += F("\nPower Factor: ");
  diagnosisMessage += String(powerFactor);

  String gearMindResponse = getGearMindResponse(diagnosisMessage);

  return gearMindResponse;
}

String getGearMindResponse(String message)
{
  Serial.println("Starting GearMind API request...");

  HTTPClient http;
  http.setTimeout(60000);
  String url = "https://" + String(gearmindHost) + gearmindPath;
  Serial.println("URL: " + url);

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(gearmindApiKey));

  DynamicJsonDocument doc(1024);
  doc["messages"][0]["role"] = "user";
  doc["messages"][0]["content"] = message;

  String requestBody;
  serializeJson(doc, requestBody);
  Serial.println("Request Body: " + requestBody);

  int httpResponseCode = http.POST(requestBody);
  String response = "Error in GearMind API";

  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode > 0)
  {
    response = http.getString();
    Serial.println("Raw response: " + response);

    DynamicJsonDocument responseDoc(1024);
    DeserializationError error = deserializeJson(responseDoc, response);

    if (error)
    {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      response = "Error parsing JSON response";
    }
    else
    {
      if (responseDoc.containsKey("content"))
      {
        response = responseDoc["content"].as<String>();
        Serial.println("Parsed response content: " + response);
      }
      else
      {
        Serial.println("'content' key not found in response");
        response = "Error: 'content' not found in response";
      }
    }
  }
  else
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    Serial.println(http.errorToString(httpResponseCode));
  }

  http.end();
  Serial.println("API request completed.");
  return response;
}

void sendLineNotification(String message)
{
  HTTPClient http;
  http.begin("https://" + String(lineNotifyHost) + lineNotifyPath);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Authorization", "Bearer " + String(lineNotifyToken));

  String postData = "message=" + urlEncode(message);
  int httpResponseCode = http.POST(postData);

  if (httpResponseCode > 0)
  {
    Serial.printf("Line Notify sent. Response: %d\n", httpResponseCode);
  }
  else
  {
    Serial.printf("Error sending Line Notify. Error: %d\n", httpResponseCode);
  }

  http.end();
}

String urlEncode(String str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++)
  {
    c = str.charAt(i);
    if (c == ' ')
    {
      encodedString += '+';
    }
    else if (isalnum(c))
    {
      encodedString += c;
    }
    else
    {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9)
      {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9)
      {
        code0 = c - 10 + 'A';
      }
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
  }
  return encodedString;
}

void checkEmergencyButton(unsigned long currentTime)
{
  if (digitalRead(EMERGENCY_BUTTON) == LOW)
  {
    if (!emergencyButtonPressed)
    {
      emergencyButtonPressed = true;
      emergencyButtonPressTime = currentTime;
      Serial.println("EMERGENCY BUTTON PRESSED!");
      sendLineNotification(performDiagnosis("EMERGENCY: EMERGENCY BUTTON PRESSED! System shutdown initiated!"));
    }
  }
  else
  {
    if (emergencyButtonPressed && (currentTime - emergencyButtonPressTime > emergencyButtonCooldown))
    {
      emergencyButtonPressed = false;
      Serial.println("EMERGENCY BUTTON RELEASED. System ready for next emergency.");
    }
  }
}