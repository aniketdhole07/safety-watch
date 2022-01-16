//JSON
#include <ArduinoJson.h>
#include <PubSubClient.h>

//Wifi
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

//Time
#include <NTPClient.h>
#include <WiFiUdp.h>

// Header files for GPS Sensor reading
#include <TinyGPSPlus.h>
#include <SoftwareSerial.h>

//Display
#include <Wire.h> 
#include "SSD1306Wire.h" 

//HTTP Web Portal for Phone Numbers
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

AsyncWebServer server(80);

const char* INPUT_1 = "ip1";
const char* INPUT_2 = "ip2";
const char* INPUT_3 = "ip3";

// HTML web page
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
    <title>Safety Watch Contacts Form</title>
  </head>
  <body>
  <h1>Enter Phone Numbers on which SOS gets Sent<h1>
  <form action="/get">
    Phone Number 1: <input type="text" name="ip1">
    <input type="submit" value="Submit">
  </form><br>
  <form action="/get">
    Phone Number 2: <input type="text" name="ip2">
    <input type="submit" value="Submit">
  </form><br>
  <form action="/get">
    Phone Number 3: <input type="text" name="ip3">
    <input type="submit" value="Submit">
  </form>
</body></html>)rawliteral";

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

//Twilio
//Removed safetywatch api details
#include "base64.h"
const char* account_sid = "";
const char* auth_token = ";
String from_number      = "";
String to_number        = "";
String message_body     = "";
const char fingerprint[] = "";

// GPS UART Pins
#define GPS_TX_PIN D8  //D4
#define GPS_RX_PIN D7  //D3

#define LAT_STRING    "latitude"
#define LNG_STRING    "longitude"
#define SPD_STRING    "speed"
#define BUTTON_STRING "panic_button"

// WiFi Configuration
#define WIFI_AP       ""
#define WIFI_PASSWORD ""

ESP8266WiFiMulti WiFiMulti;

// ThingsBoard Configuration
char thingsboardServer[] =  "demo.thingsboard.io"; 
#define TOKEN               ""     //Removed Device Key
#define TB_TELEMETRY_TOPIC  "v1/devices/me/telemetry"
#define TB_DEV_ATTR_TOPIC   "v1/devices/me/attributes"

#define SUCCESS                0
#define ERR_INVALID_LOCATION  -1
#define ERR_CONN_LOST         -2

// Thingsboard function
short publishGPSData(char * lats, char *longs, char *speed_kmph);

//TinyGPS++ Object
TinyGPSPlus gps;

//Time Added IST Time
const long timezoneOffset = 5.5 * 60 * 60; // ? hours * 60 * 60

const char          *ntpServer  = "pool.ntp.org"; // change it to local NTP server if needed
const unsigned long updateDelay = 900000;         // update time every 15 min
const unsigned long retryDelay  = 5000;           // retry 5 sec later if time query failed
const String        weekDays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

unsigned long lastUpdatedTime = updateDelay * -1;
unsigned int  second_prev = 0;
bool          colon_switch = false;
long timer = 0;


//WiFiClient Object
WiFiClient wifiClient;
int status = WL_IDLE_STATUS;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer);

// Initialize the OLED display using Wire library
SSD1306Wire  display(0x3c, D4,D3);

// PubSubClient 
PubSubClient client(wifiClient);

// Serial Connection to the GPS device
SoftwareSerial ss(GPS_RX_PIN, GPS_TX_PIN);

// Periodical Lat-Long Publishing related variables
unsigned long previousMillis = 0;
const int reportingPeriod =  10000; // 1000 ms = 1 Second

// GPS Global variables
char latitude[16];
char longitude[16];
char speed_kmph[10];

// Button interrupt variables
volatile int buttonState = 0;
bool sos=false;

//store Phone Numbers
String phone_nos[3];


// Setup Function
void setup()
{
  // Start the Serial communitation with terminal
  Serial.begin(115200);

  // Start the serial communication with the GPS Module
  ss.begin(9600);

  // let the serial connection stabilize
  delay(10);

  // Initialize the WiFi Connection
  InitWiFi();

  // MQTT Settings
  client.setServer( thingsboardServer, 1883);
  client.setCallback(on_message);

  // Interrupt settings for SOS button
  attachInterrupt(digitalPinToInterrupt(D6), IntCallback, RISING);

  // Enable Interrupts
  interrupts();
  delay(1000);
  Serial.println("Initializing OLED Display");
  display.init();
  WiFiMulti.addAP(WIFI_AP, WIFI_PASSWORD); // multiple ssid/pw can be added
  while (WiFiMulti.run() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  //display.flipScreenVertically();
  display.setFont(ArialMT_Plain_16);
 
  //  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  //    display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "Started");
  display.display();
  timeClient.setTimeOffset(timezoneOffset);
  timeClient.begin();

  // Send web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Send a GET request
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
    String inputParam;
    // GET input1 value on <ESP_IP>/get?input1=<inputMessage>
    if (request->hasParam(INPUT_1)) {
      inputMessage = request->getParam(INPUT_1)->value();
      inputParam = INPUT_1;
      phone_nos[0]=inputMessage;
    }
    // GET input2 value on <ESP_IP>/get?input2=<inputMessage>
    else if (request->hasParam(INPUT_2)) {
      inputMessage = request->getParam(INPUT_2)->value();
      inputParam = INPUT_2;
      phone_nos[1]=inputMessage;
    }
    // GET input3 value on <ESP_IP>/get?input3=<inputMessage>
    else if (request->hasParam(INPUT_3)) {
      inputMessage = request->getParam(INPUT_3)->value();
      inputParam = INPUT_3;
    }
    else {
      inputMessage = "No message sent";
      inputParam = "none";
    }
    Serial.println(inputMessage);
    request->send(200, "text/html", "HTTP GET input field (" 
                                     + inputParam + ") with value: " + inputMessage +
                                     "<br><a href=\"/\">Return to Home Page</a>");
  });
  server.onNotFound(notFound);
  server.begin();
  
  delay(100);
}

// Main Loop
void loop()
{
  if (WiFiMulti.run() == WL_CONNECTED && millis() - lastUpdatedTime >= updateDelay) {
    bool updated = timeClient.update();
    if (updated) {
      Serial.println("NTP time updated.");
      lastUpdatedTime = millis();
    } else {
      Serial.println("Failed to update time. Waiting for retry...");
      lastUpdatedTime = millis() - updateDelay + retryDelay;
    }
  } else {
    if (WiFiMulti.run() != WL_CONNECTED) Serial.println("WiFi disconnected!");
  }
  
  unsigned long t = millis();
  unsigned int year = getYear();
  unsigned int month = getMonth();
  unsigned int day = getDate();
  unsigned int hour = timeClient.getHours();
  unsigned int minute = timeClient.getMinutes();
  unsigned int second = timeClient.getSeconds();
  String weekDay = weekDays[timeClient.getDay()];

  if (second != second_prev) colon_switch = !colon_switch;

  String fYear = String(year);
  String fDate = (month < 10 ? "0" : "") + String(month) + "/" + (day < 10 ? "0" : "") + String(day);
  String fTime = (hour < 10 ? "0" : "") + String(hour) + (colon_switch ? ":" : " ") + (minute < 10 ? "0" : "") + String(minute);

  display.clear();
  display.display();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 10, "Started");
  display.display();
  unsigned long currentMillis = 0;
  short sRetVal;
  
  // Check if client is connected, otherwise reconnect
  if(!client.connected())
  {
    display.clear();
    display.display();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 10, "Re-Connecting to Wifi");
    display.display();
    reconnect();
  }

  // Take the elapsed milliseconds since start
  currentMillis = millis();

  // Read the switch state
  if(buttonState == true)
  {
    // Publish the Button state to device attibutes at TB
    sRetVal = publishButtonState(buttonState, TB_DEV_ATTR_TOPIC);
    if(sRetVal == 0)
    {
      //Reset the buttonState
      buttonState = false;   
    }
    
  }

  if(sos){
    display.clear();
    display.display();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 10, "Starting GPS");
    display.setFont(ArialMT_Plain_24);
    display.drawString(0, 26, "SOS");
    display.display();
    delay(100);
    // Read the GPS sensor
    while(ss.available() > 0 and sos==true)
    {
      if(gps.encode(ss.read()))
      {
        if((currentMillis - previousMillis) > reportingPeriod)
        {
          previousMillis = currentMillis;
          if(gps.location.isValid())
          {
            // Convert the readings into String format
            dtostrf(gps.location.lat(), 9, 6, latitude);
            dtostrf(gps.location.lng(), 9, 6, longitude);
  
            if(gps.speed.isValid())
            {
              dtostrf(gps.speed.mps(), 4, 2, speed_kmph);
            }
            else
            {
              speed_kmph[0] = '\0';
            }
            
            // MQTT Publish Logic
            Serial.println("");
            Serial.println("Publishing over MQTT...");
            display.clear();
            display.display();
            display.setFont(ArialMT_Plain_16);
            display.drawString(0, 10, "Publishing over MQTT to Thingsboard");
            display.display();
            sRetVal = publishGPSData(latitude, longitude, speed_kmph);
            if(sRetVal == SUCCESS)  
            {
              Serial.print("Published Latitude: ");
              Serial.print(latitude);
              
              Serial.print(", Longitude: ");
              Serial.println(longitude);
  
              Serial.print(", Speed: ");
              Serial.println(speed_kmph);
              display.clear();
              display.display();
              display.setFont(ArialMT_Plain_16);
              display.drawString(0, 0, latitude);
              display.drawString(0, 16, longitude);
              display.drawString(0, 32, longitude);
              display.display();
              delay(5000);
           if(phone_nos[0].length()>5){
             send_sms("[SOS] : I am in Danger Please Help. Location : https://maps.google.com/?q="+latitude+","+longitude,phone_nos[0]);
           }
           if(phone_nos[1].length()>5){
             send_sms("[SOS] : I am in Danger Please Help. Location : https://maps.google.com/?q="+latitude+","+longitude,phone_nos[1]);
           }
           if(phone_nos[2].length()>5){
             send_sms("[SOS] : I am in Danger Please Help. Location : https://maps.google.com/?q="+latitude+","+longitude,phone_nos[2]);
           }
            Serial.println(phone_nos[0]);
            Serial.println(phone_nos[1]);
            Serial.println(phone_nos[2]);
            Serial.println("Sent Location!"); 
            }
            else
            {
              Serial.print("publishLocation(): Failed with error ");
              display.clear();
              display.display();
              display.setFont(ArialMT_Plain_16);
              display.drawString(0, 10, "Failed Sending Location");
              display.display();
              Serial.println(sRetVal);
            }
          }
          else
          {
            
            display.clear();
            display.display();
            display.setFont(ArialMT_Plain_16);
            display.drawString(0, 10, "Sent SOS Location");
            display.drawString(0, 25, lati+","+longi);
            display.display();
            delay(5000);
           if(phone_nos[0].length()>5){
             send_sms("[SOS] : I am in Danger Please Help. Location : https://maps.google.com/?q="+latitude+","+longitude,phone_nos[0]);
           }
           if(phone_nos[1].length()>5){
             send_sms("[SOS] : I am in Danger Please Help. Location : https://maps.google.com/?q="+latitude+","+longitude,phone_nos[1]);
           }
           if(phone_nos[2].length()>5){
             send_sms("[SOS] : I am in Danger Please Help. Location : https://maps.google.com/?q="+latitude+","+longitude,phone_nos[2]);
           }
            Serial.println(phone_nos[0]);
            Serial.println(phone_nos[1]);
            Serial.println(phone_nos[2]);
            Serial.println("Sent Location!"); 
          }
        }                   
      }  
    }
  }
  
  else{
    unsigned long t = millis();

    unsigned int year = getYear();
    unsigned int month = getMonth();
    unsigned int day = getDate();
    unsigned int hour = timeClient.getHours();
    unsigned int minute = timeClient.getMinutes();
    unsigned int second = timeClient.getSeconds();
    String weekDay = weekDays[timeClient.getDay()];

    if (second != second_prev) colon_switch = !colon_switch;

    String fYear = String(year);
    String fDate = (month < 10 ? "0" : "") + String(month) + "/" + (day < 10 ? "0" : "") + String(day);
    String fTime = (hour < 10 ? "0" : "") + String(hour) + (colon_switch ? ":" : " ") + (minute < 10 ? "0" : "") + String(minute);

    display.clear();
    display.display();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    
    
    display.setFont(ArialMT_Plain_16);
    display.drawString(1, 4, strcpy(new char[fDate.length() + 1], fDate.c_str()));
    display.setFont(ArialMT_Plain_16);
    display.drawString(80, 4, strcpy(new char[fYear.length() + 1], fYear.c_str()));
    display.setFont(ArialMT_Plain_16);
    display.drawString(80, 18, strcpy(new char[weekDay.length() + 1], weekDay.c_str()));
    display.setFont(ArialMT_Plain_24);
    display.drawString(30, 38, strcpy(new char[fTime.length() + 1], fTime.c_str()));
    display.display();
    second_prev = second;
  
    int diff = millis() - t;
    delay(diff >= 0 ? (500 - (millis() - t)) : 0);
  }
  
  client.loop();
}

void InitWiFi() 
{
  Serial.println("Connecting to AP ...");

  // attempt to connect to WiFi network
  WiFi.begin(WIFI_AP, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to AP");
}


void reconnect() 
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    status = WiFi.status();
    if ( status != WL_CONNECTED) 
    {
      WiFi.begin(WIFI_AP, WIFI_PASSWORD);
      while (WiFi.status() != WL_CONNECTED) 
      {
        delay(500);
        Serial.print(".");
      }
      Serial.println("Connected to AP");
    }
    Serial.print("Connecting to Thingsboard node ...");
    
    // Attempt to connect (clientId, username, password)
    if ( client.connect("ESP8266 Device", TOKEN, NULL) ) 
    {
      Serial.println( "[DONE]" );
      // Subscribing to receive RPC requests
      client.subscribe("v1/devices/me/rpc/request/+");
      
    } 
    else 
    {
      Serial.print( "[FAILED] [ rc = " );
      Serial.print( client.state() );
      Serial.println( " : retrying in 5 seconds]" );
      // Wait 5 seconds before retrying
      delay( 5000 );
    }
  }
}

// The callback for when a PUBLISH message is received from the server.
void on_message(const char* topic, byte* payload, unsigned int length) 
{
  Serial.println("On message");

  char json[length + 1];
  strncpy (json, (char*)payload, length);
  json[length] = '\0';

  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  Serial.println(json);
}

short publishGPSData(char * lats, char *longs, char *speed_kmph)
{
  short sRetVal;
  
  // Prepare JSON payload string
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& data = jsonBuffer.createObject();
 
  data[String(LAT_STRING)] = lats;
  data[String(LNG_STRING)] = longs;
  data[String(SPD_STRING)] = (speed_kmph[0] != '\0')? speed_kmph : "-1";
  
  char payload[256];
  data.printTo(payload, sizeof(payload));
  String strPayload = String(payload);

  // Now Publish the prepared payload
  if(client.connected())
  {
    Serial.println("Publishing the latest location...");
    client.publish(TB_TELEMETRY_TOPIC, strPayload.c_str());
    sRetVal = SUCCESS;  
  }
  else
  {
    Serial.println("TB Connection Lost...");
    sRetVal = ERR_CONN_LOST;
  }  
  return sRetVal;
}

short publishButtonState(bool buttonState, char * attribTopic)
{
  short sRetVal;
  
  // Prepare JSON payload string
  StaticJsonBuffer<50> jsonBuffer;
  JsonObject& data = jsonBuffer.createObject();
 
  data[String(BUTTON_STRING)] = buttonState;
  
  char payload[50];
  data.printTo(payload, sizeof(payload));
  String strPayload = String(payload);

  // Now Publish the prepared payload
  if(client.connected())
  {
    Serial.println("Publishing the switch press status...");
    client.publish(TB_DEV_ATTR_TOPIC, strPayload.c_str());
    sRetVal = SUCCESS;  
  }
  else
  {
    Serial.println("TB Connection Lost...");
    sRetVal = ERR_CONN_LOST;
  }  
  return sRetVal;
}

unsigned int getYear() {
  time_t rawtime = timeClient.getEpochTime();
  struct tm * ti;
  ti = localtime (&rawtime);
  unsigned int year = ti->tm_year + 1900;
  return year;
}

unsigned int getMonth() {
  time_t rawtime = timeClient.getEpochTime();
  struct tm * ti;
  ti = localtime (&rawtime);
  unsigned int month = ti->tm_mon + 1;
  return month;
}

unsigned int getDate() {
  time_t rawtime = timeClient.getEpochTime();
  struct tm * ti;
  ti = localtime (&rawtime);
  unsigned int month = ti->tm_mday;
  return month;
}

String urlencode(String str) {
  String encodedString = "";
  char c;
  char code0;
  char code1;
  char code2;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encodedString += '+';
    } else if (isalnum(c)) {
      encodedString += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) {
        code0 = c - 10 + 'A';
      }
      code2 = '\0';
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
    yield();
  }
  return encodedString;
}

String get_auth_header(const String& user, const String& password) {
  size_t toencodeLen = user.length() + password.length() + 2;
  char toencode[toencodeLen];
  memset(toencode, 0, toencodeLen);
  snprintf(toencode, toencodeLen, "%s:%s", user.c_str(), password.c_str());
  String encoded = base64::encode((uint8_t*)toencode, toencodeLen - 1);
  String encoded_string = String(encoded);
  int i = 0;
  // Strip newlines (after every 72 characters in spec)
  while (i < encoded_string.length()) {
    i = encoded_string.indexOf('\n', i);
    if (i == -1) {
      break;
    }
    encoded_string.remove(i, 1);
  }
  return "Authorization: Basic " + encoded_string;
}

void send_sms(String text_send, String phone_number){
    //Twilio SMS
  WiFiClientSecure client;
  client.setFingerprint(fingerprint);
  Serial.printf("+ Using fingerprint '%s'\n", fingerprint);
  const char* host = "api.twilio.com";
  const int   httpsPort = 443;
  Serial.print("+ Connecting to ");
  Serial.println(host);
  if (!client.connect(host, httpsPort)) {
    Serial.println("- Connection failed.");
    return; // Skips to loop();
  }
  Serial.println("+ Connected.");
  Serial.println("+ Post an HTTP send SMS request.");
  String post_data = "To=" + urlencode(phone_number)
                     + "&From=" + urlencode(from_number)
                     + "&Body=" + urlencode(text_send);
  String auth_header = get_auth_header(account_sid, auth_token);
  String http_request = "POST /2010-04-01/Accounts/" + String(account_sid) + "/Messages HTTP/1.1\r\n"
                        + auth_header + "\r\n" 
                        + "Host: " + host + "\r\n"
                        + "Cache-control: no-cache\r\n"
                        + "User-Agent: ESP8266 Twilio Example\r\n"
                        + "Content-Type: application/x-www-form-urlencoded\r\n"
                        + "Content-Length: " + post_data.length() + "\r\n"
                        + "Connection: close\r\n"
                        + "\r\n"
                        + post_data
                        + "\r\n";
  client.println(http_request);
  // Read the response.
  String response = "";
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    response += (line);
    response += ("\r\n");
  }
  Serial.println("+ Connection is closed.");
  Serial.println("+ Response:");
  Serial.println(response);
}

ICACHE_RAM_ATTR void IntCallback(){
  Serial.println("Button Clicked ");
  if(sos==false)
  sos=true;
  else
  sos=false;
  //yield();
}
