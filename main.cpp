#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <SPIFFS.h>
#include <esp_adc_cal.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <EmonLib.h>


//////////////////////////////////////////////////////////////////////////

#define VERSION 0.5

#define DEBUG_SERIAL true      // Enable debbuging over serial interface      

#define HEATPUMP_L1 33          // GPIO for L1 measurement
#define HEATPUMP_L2 35          // GPIO for L2 measurement
#define HEATPUMP_L3 39          // GPIO for L3 measurement
#define HEATPUMP_RELAY 16       // GPIO for heat pump relay 

#define HEATROD 34              // GPIO for heatrod measurement
#define HEATROD_RELAY 17        // GPIO for heat pump relay 

#define ON false
#define OFF true 

// ======================================================================
// Setting parameters with default values
// ======================================================================

const char* WIFI_SSID = "---";                      // WLAN-SSID
const char* WIFI_PW = "---";        // WLAN-Password
String HOSTNAME = "ESP-32";                          // Enter Hostname here
String MQTT_BROKER = "192.168.178.120";              // MQTT-Broker
String EXTERNAL_URL = "www.telekom.de";              // URL of external Website

int VOLTAGE = 240;
int POWER_HEATPUMP = 0;
int POWER_HEATPUMP_L1 = 0;
int POWER_HEATPUMP_L2 = 0;
int POWER_HEATPUMP_L3 = 0;
String heatpump_checked = "";

int POWER_HEATROD = 0;

float SCT_013_010_SLOPE = 3.53;
float SCT_013_010_OFFSET = 0;

boolean HEATPUMP_SWITCH = ON;                       // Enable Heatpump
boolean HEATROD_SWITCH = ON;                        // Enable Heatrod

long lastMillis = 0;                                // save last milliseconds
int MQTT_PUBLISH_TIME = 10;                         // time in seconds for publishing MQTT message                  
// ======================================================================
// Initialize Objects
// ======================================================================

// Initialize WebServer
AsyncWebServer server(80);

// Initialize MQTT Publisher
char json_msg[128];
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Initialize EnergyMonitor
EnergyMonitor emon;

// ======================================================================
// Functions
// ======================================================================

void loadConfig(){
    if (!SPIFFS.begin(true)) { 
        if(DEBUG_SERIAL){Serial.print("! An error occurred during SPIFFS mounting !");}
        return; 
    }

    File config = SPIFFS.open("/config.json","r");

    if(config) { 
        // Deserialize the JSON document
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, config);

        if(DEBUG_SERIAL){Serial.println(config.readString());}

        if (error) { 
            if(DEBUG_SERIAL){
                Serial.print("! DeserializeJson failed! -> ");
                Serial.println(error.f_str());
            }
            return; 
        }
        JsonObject obj = doc.as<JsonObject>();
                
        String hostname = obj["HOSTNAME"];
        HOSTNAME = hostname.c_str();
        String mqtt_broker = doc["MQTT_BROKER"];     
        MQTT_BROKER = mqtt_broker.c_str();
        MQTT_PUBLISH_TIME = doc["MQTT_PUBLISH_TIME"];
        String ext_url = doc["EXTERNAL_URL"];     
        EXTERNAL_URL = ext_url.c_str();
        VOLTAGE = doc["VOLTAGE"];
        SCT_013_010_SLOPE = doc["SCT_013_010_SLOPE"];
        SCT_013_010_OFFSET = doc["SCT_013_010_OFFSET"];
    }
    config.close();
}

void saveConfig(AsyncWebServerRequest *request) {
    if (!SPIFFS.begin(true)) { 
        if(DEBUG_SERIAL){Serial.print("!An error occurred during SPIFFS mounting!");}
        return; 
    }

    File config = SPIFFS.open("/config.json","w");

    if(config) { 
        // Serialize the JSON document
        DynamicJsonDocument doc(1024);

        int paramsNr = request->params();
        for(int i=0;i<paramsNr;i++){
            AsyncWebParameter* p = request->getParam(i);
            
            if(p->name() == "HOSTNAME") {
                HOSTNAME = p->value();
                doc["HOSTNAME"] = HOSTNAME;
            }

            if(p->name() == "MQTT_BROKER") {
                MQTT_BROKER = p->value();
                doc["MQTT_BROKER"] = MQTT_BROKER;
            }

            if(p->name() == "MQTT_PUBLISH_TIME") {
                MQTT_PUBLISH_TIME = p->value().toInt();
                doc["MQTT_PUBLISH_TIME"] = MQTT_PUBLISH_TIME;
            }

            if(p->name() == "EXTERNAL_URL") {
                EXTERNAL_URL = p->value();
                doc["EXTERNAL_URL"] = EXTERNAL_URL;
            }

            if(p->name() == "VOLTAGE") {
                VOLTAGE = p->value().toInt();
                doc["VOLTAGE"] = VOLTAGE;
            }

            if(p->name() == "SCT_013_010_SLOPE") {
                SCT_013_010_SLOPE = p->value().toInt();
                doc["SCT_013_010_SLOPE"] = SCT_013_010_SLOPE;
            }

            if(p->name() == "SCT_013_010_OFFSET") {
                SCT_013_010_OFFSET = p->value().toInt();
                doc["SCT_013_010_OFFSET"] = SCT_013_010_OFFSET;
            }
        }
        serializeJsonPretty(doc, config);
    }
    config.close();
}

void saveConfig() {
 if (!SPIFFS.begin(true)) { 
        if(DEBUG_SERIAL){Serial.print("!An error occurred during SPIFFS mounting!");}
        return; 
    }

    File config = SPIFFS.open("/config.json","w");

    if(config) { 
        // Serialize the JSON document
        DynamicJsonDocument doc(1024);
        doc["HOSTNAME"] = HOSTNAME;
        doc["MQTT_BROKER"] = MQTT_BROKER;
        doc["MQTT_PUBLISH_TIME"] = MQTT_PUBLISH_TIME;
        doc["EXTERNAL_URL"] = EXTERNAL_URL;
        doc["VOLTAGE"] = VOLTAGE;   
        doc["SCT_013_010_SLOPE "] = SCT_013_010_SLOPE;    
        doc["SCT_013_010_OFFSET"] = SCT_013_010_OFFSET;             
        serializeJsonPretty(doc, config);
    }   
    config.close();
}

void connectWifi() {
    //connect to your local wi-fi network
    //WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);

    WiFi.setHostname(HOSTNAME.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PW);
    
    pinMode(BUILTIN_LED,OUTPUT);
    if(DEBUG_SERIAL){
        Serial.print("Connecting to WiFi: ");
        Serial.print(WIFI_SSID);
    }

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(100);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(100);
        digitalWrite(LED_BUILTIN, LOW);
        if(DEBUG_SERIAL){ Serial.print(".");}
    }
    
    if(DEBUG_SERIAL){
        Serial.println("OK!");
        Serial.print("Hostname: ");
        Serial.println(WiFi.getHostname());
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());  
    }
}

String processor(const String& var){

    if(var == "HOSTNAME"){ return HOSTNAME; }
    if(var == "EXTERNAL_URL"){ return EXTERNAL_URL; }
    if(var == "VOLTAGE"){  return String(VOLTAGE);  }
    if(var == "SCT_013_010_SLOPE"){  return String(SCT_013_010_SLOPE);  }
    if(var == "SCT_013_010_OFFSET"){  return String(SCT_013_010_OFFSET);  }
        
    if(var == "WIFI_SSID"){  return String(WIFI_SSID);  }
    if(var == "WIFI_PW"){  return String(WIFI_PW);  }
    if(var == "MQTT_BROKER"){  return String(MQTT_BROKER);  }
    if(var == "MQTT_PUBLISH_TIME"){  return String(MQTT_PUBLISH_TIME);  }

    if(var == "POWER_HEATPUMP"){  return String(POWER_HEATPUMP);  } 
    if(var == "POWER_HEATPUMP_L1"){  return String(POWER_HEATPUMP_L1);  } 
    if(var == "POWER_HEATPUMP_L2"){  return String(POWER_HEATPUMP_L2);  } 
    if(var == "POWER_HEATPUMP_L3"){  return String(POWER_HEATPUMP_L3);  } 

    if(var == "HEATPUMP_SWITCH") {
        String heatpump_switch_state = "";
        if (HEATPUMP_SWITCH == true) {
            heatpump_switch_state = "/switch_heatpump_on";
        } else {
            heatpump_switch_state = "/switch_heatpump_off";
        }
        return heatpump_switch_state;  
    }

    if(var == "HEATPUMP_STATE") {
        String heatpump_switch_state = "";
        if (HEATPUMP_SWITCH == false) {
            heatpump_switch_state = " checked";
        }
        return heatpump_switch_state;
    }

    if(var == "POWER_HEATROD"){  return String(POWER_HEATROD);  } 
    if(var == "HEATROD_SWITCH") {
        String heatrod_switch_state = "";
        if (HEATROD_SWITCH == true) {
            heatrod_switch_state = "/switch_heatrod_on";
        } else {
            heatrod_switch_state = "/switch_heatrod_off";
        }
        return heatrod_switch_state;  
    }
    if(var == "HEATROD_STATE") {
        String heatrod_switch_state = "";
        if (HEATROD_SWITCH == false) {
            heatrod_switch_state = " checked";
        } 
        return heatrod_switch_state;
    }

    if(var == "VERSION"){  return String(VERSION);  } 

    return String();   
}

void startWebServer(){
    // start OTA WebServer
    AsyncElegantOTA.begin(&server);
    server.begin();
    if(DEBUG_SERIAL){Serial.println("OTA WebServer started!");}

    // Make index.html available
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        if(DEBUG_SERIAL){Serial.println("Requested index.html page");}
        request->send(SPIFFS, "/index.html", String(), false, processor);
    });

    // Make config.html available
    server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
        if(DEBUG_SERIAL){Serial.println("Requested config.html page");}
        request->send(SPIFFS, "/config.html", String(), false, processor);
    });

    // Save config-parameters
    server.on("/save-config", HTTP_GET, [](AsyncWebServerRequest *request){
        if(DEBUG_SERIAL){Serial.println("Requested config.html page");}
        saveConfig(request);
        request->redirect("/config");
    });

    // Make check.html available
    server.on("/check", HTTP_GET, [](AsyncWebServerRequest *request){
        if(DEBUG_SERIAL){Serial.println("Requested check.html page");}
        request->send(SPIFFS, "/check.html", String(), false, processor);
    });

    // Make style.css available
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/style.css","text/css");
    });

    // Enable Heatpump
    server.on("/switch_heatpump_on", HTTP_GET, [](AsyncWebServerRequest *request){       
        if(DEBUG_SERIAL){Serial.println("Switch Heatpump <ON>!");}
        HEATPUMP_SWITCH = ON;
        request->redirect("/");
    });

    // Disable Heatpump
    server.on("/switch_heatpump_off", HTTP_GET, [](AsyncWebServerRequest *request){       
        if(DEBUG_SERIAL){Serial.println("Switch Heatpump <OFF>!");}
        HEATPUMP_SWITCH = OFF;
        request->redirect("/");
    });

    // Enable Heatrod
    server.on("/switch_heatrod_on", HTTP_GET, [](AsyncWebServerRequest *request){       
        if(DEBUG_SERIAL){Serial.println("Switch Heatrod <ON>!");}
        HEATROD_SWITCH = ON;
        request->redirect("/");
    });

    // Disable Heatrod
    server.on("/switch_heatrod_off", HTTP_GET, [](AsyncWebServerRequest *request){       
        if(DEBUG_SERIAL){Serial.println("Switch Heatrod <OFF>!");}
        HEATROD_SWITCH = OFF;
        request->redirect("/");
    });

        // Make SMA Power Monitor available
    server.on("/power_meter", HTTP_GET, [](AsyncWebServerRequest *request){       
        DynamicJsonDocument doc(200);
               
        doc["power_heatpump"] = POWER_HEATPUMP;
        doc["power_heatpump_L1"] = POWER_HEATPUMP_L1;
        doc["power_heatpump_L2"] = POWER_HEATPUMP_L2;
        doc["power_heatpump_L3"] = POWER_HEATPUMP_L3;
        doc["power_heatrod"] = POWER_HEATROD;
      
        String buf;
        serializeJson(doc, buf);
        request->send(200, "application/json", buf);
    });
}

void publishMessage() {

    mqttClient.setServer(MQTT_BROKER.c_str(), 1883);

    if(!mqttClient.connected()) {
        int b = 0;
        if(DEBUG_SERIAL){Serial.print("Connecting to MQTT Broker");}
        
        while (!mqttClient.connected()) {
            mqttClient.connect(HOSTNAME.c_str());
            if(DEBUG_SERIAL){Serial.print(".");}      
            delay(100);     
        }
        if(DEBUG_SERIAL){Serial.println("OK!");}
    }
    mqttClient.loop();

    if (mqttClient.connect(HOSTNAME.c_str())) {
        StaticJsonDocument<256> doc;                            // build JSON object
        doc["sender"] = HOSTNAME;
        doc["heatpump"] = POWER_HEATPUMP;
        doc["heatpump_L1"] = POWER_HEATPUMP_L1;
        doc["heatpump_L2"] = POWER_HEATPUMP_L2;
        doc["heatpump_L3"] = POWER_HEATPUMP_L3;
        doc["heatrod"] = POWER_HEATROD;
      
        int msg_size = serializeJson(doc, json_msg);

        mqttClient.publish("heatpump", json_msg, msg_size);
    } 
}

void initRelayBoard() {
    pinMode(HEATPUMP_RELAY, OUTPUT_OPEN_DRAIN);
    digitalWrite(HEATPUMP_RELAY, HIGH);
    pinMode(HEATROD_RELAY, OUTPUT_OPEN_DRAIN);
    digitalWrite(HEATROD_RELAY, HIGH);
    
    if(DEBUG_SERIAL) { Serial.println("Relais initialized!");     }
}

float getDCVoltage(byte ADC_Pin){
    float slope = 0.97;
    float off_set = 0.15;
    
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
    
    int adc_val = analogRead(ADC_Pin);

    return((adc_val/4096.0 * 3.3) * slope + off_set);
}

float getVPP(byte ADC_Pin, int milliseconds)
{
    float slope = 0.97;
    float off_set = 0.15;
    int read_val;                // value read from the sensor
    int maxValue = 0;             // store max value here
    int minValue = 4096;          // store min value here ESP32 ADC resolution

    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  
    uint32_t start_time = millis();
    while((millis()-start_time) < milliseconds) //sample for 1 Sec
    {
        read_val = analogRead(ADC_Pin);
        // see if you have a new maxValue       
        if (read_val > maxValue) 
        {
           /*record the maximum sensor value*/
           maxValue = read_val;
        }
        if (read_val < minValue) 
        {
           /*record the minimum sensor value*/
           minValue = read_val;
        }    
    }

    float ret_val = ((maxValue - minValue)/4096.0 * 3.3);
    if ((floorf(ret_val*10)/10)==0) {
        ret_val = 0;
    } else {
        ret_val = ret_val * slope + off_set;
    }
    return(ret_val);
    
    //return((maxValue - minValue)/4096.0 * 3.3); //ESP32 ADC resolution 4096
 }

float getCurrency(byte ADC_Pin, int milliseconds, float off_set, float slope){
    int read_val;                // value read from the sensor
    int maxValue = 0;             // store max value here
    int minValue = 4096;          // store min value here ESP32 ADC resolution

    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  
    uint32_t start_time = millis();
    while((millis()-start_time) < milliseconds) //sample for 1 Sec
    {
        read_val = analogRead(ADC_Pin);
        // see if you have a new maxValue       
        if (read_val > maxValue) 
        {
           /*record the maximum sensor value*/
           maxValue = read_val;
        }
        if (read_val < minValue) 
        {
           /*record the minimum sensor value*/
           minValue = read_val;
        }    
    }

    float ret_val = ((maxValue - minValue)/4096.0 * 3.3);

    if ((floorf(ret_val*10)/10)==0) {
        ret_val = 0;
    } else {
        ret_val = ret_val * slope + off_set;
    }

    return ret_val;
}

float getADCValue(byte ADC_Pin) {
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

  return (analogRead(ADC_Pin));
}

double getIrms(byte ADC_Pin, unsigned int Number_of_Samples) {

    int sample = 0;
    int off_set = 1925;
    int calibration = 1;
    int filtered = 0;
    int sq = 0;
    int sum = 0;
    float Irms = 0;

    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

  for (unsigned int n = 0; n < Number_of_Samples; n++)
  {
    sample = analogRead(ADC_Pin);

    // Digital low pass filter extracts the 2.5 V or 1.65 V dc offset,
    //  then subtract this - signal is now centered on 0 counts.
    //offsetI = (offsetI + (sampleI-offsetI)/1024);
    filtered = sample - off_set;

    // Root-mean-square method current
    // 1) square current values
    sq = filtered * filtered;
    // 2) sum
    sum += sq;
  }

  //double I_RATIO = ICAL *((SupplyVoltage/1000.0) / (ADC_COUNTS));
  Irms = sqrt(sum / Number_of_Samples);

  //Reset accumulators
  sum = 0;
  //--------------------------------------------------------------------------------------

  return Irms;
}

// ======================================================================
// Setup
// ======================================================================
void setup() {
   if(DEBUG_SERIAL){
        Serial.begin(115200);
        Serial.println("Welcome to ESP-32!");
    }

    initRelayBoard();           // Starting WebServer
    loadConfig();               // Loading Config from config.json
    connectWifi();              // Initialize WiFi Connection
    startWebServer();           // Starting WebServer

    emon.current(HEATPUMP_L1, 10.0);
}

void loop() {
    POWER_HEATPUMP_L1 = getCurrency(HEATPUMP_L1, 500, SCT_013_010_OFFSET, SCT_013_010_SLOPE) * VOLTAGE;
    POWER_HEATPUMP_L2 = getCurrency(HEATPUMP_L2, 500, SCT_013_010_OFFSET, SCT_013_010_SLOPE) * VOLTAGE;
    POWER_HEATPUMP_L3 = getCurrency(HEATPUMP_L3, 500, SCT_013_010_OFFSET, SCT_013_010_SLOPE) * VOLTAGE;
    POWER_HEATPUMP = POWER_HEATPUMP_L1 + POWER_HEATPUMP_L2 +POWER_HEATPUMP_L3;
    
    POWER_HEATROD = getCurrency(HEATROD, 500, SCT_013_010_OFFSET, SCT_013_010_SLOPE) * VOLTAGE;

    if (HEATPUMP_SWITCH == ON) {
        digitalWrite(HEATPUMP_RELAY, LOW);
    } else {
        digitalWrite(HEATPUMP_RELAY, HIGH);
    }


    if (HEATROD_SWITCH == ON) {
        digitalWrite(HEATROD_RELAY, LOW);
    } else {
        digitalWrite(HEATROD_RELAY, HIGH);
    }

    if ((millis() - lastMillis) > (1000*MQTT_PUBLISH_TIME)) {
        lastMillis = millis();
        if(WiFi.status() != WL_CONNECTED) { connectWifi(); }
        publishMessage();
    }


    if(DEBUG_SERIAL) {
        Serial.print("Heatpump -> ADC Value=");
        Serial.print(getADCValue(HEATPUMP_L1),0);
        Serial.print(" | L1=");
        Serial.print(POWER_HEATPUMP_L1);
        Serial.print("W | L2=");
        Serial.print(POWER_HEATPUMP_L2);
        Serial.print("W | L3=");
        Serial.print(POWER_HEATPUMP_L3);
        Serial.print("W | Switch=");
        Serial.println(HEATPUMP_SWITCH);
    }
  delay(1000);
}
