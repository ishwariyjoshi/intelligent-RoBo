#define DEBUG

#ifdef DEBUG
  #define Debug(x)    Serial.print(x)
  #define Debugln(x)  Serial.println(x)
  #define Debugf(...) Serial.printf(__VA_ARGS__)
  #define Debugflush  Serial.flush
#else
  #define Debug(x)    {}
  #define Debugln(x)  {}
  #define Debugf(...) {}
  #define Debugflush  {}
#endif


#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
//#include <EEPROM.h>
#include <Ticker.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "machinevision.h"

extern "C" {
  #include "user_interface.h" //Needed for the reset command
}
String hostName ="Inmake"; //The MQTT ID -> MAC adress will be added to make it kind of unique
int iotMode=0; //IOT mode: 0 = Web control, 1 = MQTT (No const since it can change during runtime)



//GPIO's
#define OUTPIN_04 14 //output pin //relay1
#define OUTPIN_12 12 //output pin //relay2
#define OUTPIN_13 4//output pin   //relay3
#define OUTPIN_14 13 //output pin //relay4
#define INPIN 0  // input pin (push button)

#define SWITCH_INPIN1 2    
#define SWITCH_INPIN2 5  
#define SWITCH_INPIN3 15 
#define SWITCH_INPIN4 16 
#define RESTARTDELAY 3 
#define HUMANPRESSDELAY 50 // the delay in ms untill the press should be handled as a normal push by human. Button debounce. !!! Needs to be less than RESTARTDELAY & RESETDELAY!!!
#define RESETDELAY 20 //
//##### Object instances ##### 
MDNSResponder mdns;
ESP8266WebServer server(80);
WiFiClient wifiClient;
PubSubClient mqttClient;
Ticker btn_timer;
Ticker otaTickLoop;

//##### Flags ##### They are needed because the loop needs to continue and cant wait for long tasks!
int rstNeed=0;   // Restart needed to apply new settings
int toPub=0; // determine if state should be published.
int configToClear=0; // determine if config should be cleared.
int otaFlag=0;
boolean inApMode=0;
//##### Global vars ##### 
int webtypeGlob;
int otaCount=300; //imeout in sec for OTA mode
int current; //Current state of the button
unsigned long count = 0; //Button press time counter
String st; //WiFi Stations HTML list
String state; //State of device
char buf[40]; //For MQTT data recieve
char* host; //The DNS hostname
//To be read from Config file
String esid="";
String epass = "";
String pubTopic;
String subTopic;
String mqttServer = "";
const char* otaServerIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

String javaScript,XML;
int switch_status1, switch_status2, switch_status3, switch_status4; //Physical state of the switch
int state_04, state_12, state_13, state_14;
int send_status_04, send_status_12, send_status_13, send_status_14;

//-------------- void's -------------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(10);
  // prepare GPIO2
  pinMode(OUTPIN_04, OUTPUT);
  pinMode(OUTPIN_12, OUTPUT);
  pinMode(OUTPIN_13, OUTPUT);
  pinMode(OUTPIN_14, OUTPUT);
 // pinMode(OUTLED, OUTPUT);
//  pinMode(5, INPUT_PULLUP);
//    pinMode(15, INPUT_PULLUP);

  //digitalWrite(OUTLED, HIGH);

  //define manual switch
   pinMode(SWITCH_INPIN1, INPUT);
   pinMode(SWITCH_INPIN2, INPUT);
   pinMode(SWITCH_INPIN3, INPUT);
   pinMode(SWITCH_INPIN4, INPUT);

  
  btn_timer.attach(0.05, btn_handle);
  Debugln("DEBUG: Entering loadConfig()");
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
  }
  
  uint8_t mac[6];
  WiFi.macAddress(mac);
  hostName += "-";
  hostName += macToStr(mac);
  String hostTemp=hostName;
  hostTemp.replace(":","-");
  host = (char*) hostTemp.c_str();
  loadConfig();
  //loadConfigOld();
  Debugln("DEBUG: loadConfig() passed");
  
  // Connect to WiFi network
  Debugln("DEBUG: Entering initWiFi()");
  initWiFi();
  Debugln("DEBUG: initWiFi() passed");
  Debug("iotMode:");
  Debugln(iotMode);
  Debug("webtypeGlob:");
  Debugln(webtypeGlob);
  Debug("otaFlag:");
  Debugln(otaFlag);
  Debugln("DEBUG: Starting the main loop");
}


void btn_handle()
{
  if(!digitalRead(INPIN)){
    ++count; // one count is 50ms
  } else {
    if (count > 1 && count < HUMANPRESSDELAY/5) { //push between 50 ms and 1 sec      
      Serial.print("button pressed "); 
      Serial.print(count*0.05); 
      Serial.println(" Sec."); 
    
      Serial.print("device is ");
      Serial.println(digitalRead(OUTPIN_04));
      
      Serial.print("Switching device to "); 
      Serial.println(!digitalRead(OUTPIN_04));
      digitalWrite(OUTPIN_04, !digitalRead(OUTPIN_04)); 
      state = digitalRead(OUTPIN_04);
      if(iotMode==1 && mqttClient.connected()){
        toPub=1;        
        Debugln("DEBUG: toPub set to 1");
      }
    } else if (count > (RESTARTDELAY/0.05) && count <= (RESETDELAY/0.05)){ //pressed 3 secs (60*0.05s)
      Serial.print("button pressed "); 
      Serial.print(count*0.05); 
      Serial.println(" Sec. Restarting!"); 
      setOtaFlag(!otaFlag);      
      ESP.reset();
    } else if (count > (RESETDELAY/0.05)){ //pressed 20 secs
      Serial.print("button pressed "); 
      Serial.print(count*0.05); 
      Serial.println(" Sec."); 
      Serial.println(" Clear settings and resetting!");       
      configToClear=1;
      }
    count=0; //reset since we are at high
  }
}



//-------------------------------- Main loop ---------------------------
void loop() {
 
//----------------------------------------------------------------------------gpio_04

if(switch_status1==(digitalRead(SWITCH_INPIN1)))// to read the status of physical switch
   {
        // send_status=0;
       // switch_status1=(digitalRead(SWITCH_INPIN1));
   }
   else
  {
    switch_status1=(digitalRead(SWITCH_INPIN1));
     send_status_04=1;
   }
if(send_status_04==1)
  {
     send_status_04=0;
     toPub = 1;   
  }
  else
  {   
     toPub = 0;
  }

if(((state_04)&&(switch_status1))||((!state_04)&&(!switch_status1)))  //exor logic
      {
      //digitalWrite(OUTLED, HIGH);
      digitalWrite(OUTPIN_04, HIGH);
     // toPub = 1;
       state="device is ON";
      //Serial.print("device switched via web request to  ");      
      //Serial.println(digitalWrite(OUTPIN, HIGH));      
      }
      else
      {
      digitalWrite(OUTPIN_04, LOW);
      //toPub = 1;
       state="device is OFF";
      //Serial.print("device switched via web request to  ");      
      //Serial.println(digitalWrite(OUTPIN, LOW)); 
      }

///*---------------------------------------------------------------------*/
if(switch_status2==(digitalRead(SWITCH_INPIN2)))// to read the status of physical switch  gpio5--gnd
   {
        // send_status=0;
   }
   else
  {
    switch_status2=(digitalRead(SWITCH_INPIN2));
     send_status_12=1;
   }
if(send_status_12==1)
  {
     send_status_12=0;
     toPub = 1;   
  }
  else
  {   
     toPub = 0;
  }


if(((state_12)&&(!switch_status2))||((!state_12)&&(switch_status2)))  //exor logic
      {
      //digitalWrite(OUTLED, HIGH);
      digitalWrite(OUTPIN_12, HIGH);
     // toPub = 1;
       state="device is ON";
      //Serial.print("device switched via web request to  ");      
      //Serial.println(digitalWrite(OUTPIN, HIGH));      
      }
      else
      {
      digitalWrite(OUTPIN_12, LOW);
      //toPub = 1;
       state="device is OFF";
      //Serial.print("device switched via web request to  ");      
      //Serial.println(digitalWrite(OUTPIN, LOW)); 
      }

///*---------------------------------------------------------------------*gpio13/
if(switch_status3==(digitalRead(SWITCH_INPIN3)))// to read the status of physical switch
   {
        // send_status=0;
   }
   else
  {
    switch_status3=(digitalRead(SWITCH_INPIN3));
     send_status_13=1;
   }
if(send_status_13==1)
  {
     send_status_13=0;
     toPub = 1;   
  }
  else
  {   
     toPub = 0;
  }


if(((state_13)&&(!switch_status3))||((!state_13)&&(switch_status3)))  //exor logic
      {
      //digitalWrite(OUTLED, HIGH);
      digitalWrite(OUTPIN_13, HIGH);
     // toPub = 1;
       state="device is ON";
      //Serial.print("device switched via web request to  ");      
      //Serial.println(digitalWrite(OUTPIN, HIGH));      
      }
      else
      {
      digitalWrite(OUTPIN_13, LOW);
      //toPub = 1;
       state="device is OFF";
      //Serial.print("device switched via web request to  ");      
      //Serial.println(digitalWrite(OUTPIN, LOW)); 
      }

///*---------------------------------------------------------------------*gpio14/
//
if(switch_status4==(digitalRead(SWITCH_INPIN4)))// to read the status of physical switch
   {
        // send_status=0;
   }
   else
  {
    switch_status4=(digitalRead(SWITCH_INPIN4));
     send_status_14=1;
   }
if(send_status_14==1)
  {
     send_status_14=0;
     toPub = 1;   
  }
  else
  {   
     toPub = 0;
  }


if(((state_14)&&(switch_status4))||((!state_14)&&(!switch_status4)))  //exor logic
      {
      //digitalWrite(OUTLED, HIGH);
      digitalWrite(OUTPIN_14, HIGH);
     // toPub = 1;
       state="device is ON";
      //Serial.print("device switched via web request to  ");      
      //Serial.println(digitalWrite(OUTPIN, HIGH));      
      }
      else
      {
      digitalWrite(OUTPIN_14, LOW);
      //toPub = 1;
       state="device is OFF";
      //Serial.print("device switched via web request to  ");      
      //Serial.println(digitalWrite(OUTPIN, LOW)); 
      }
/*---------------------------------------------------------------------*/


  
  //Debugln("DEBUG: loop() begin");
  if(configToClear==1){
    //Debugln("DEBUG: loop() clear config flag set!");
    clearConfig()? Serial.println("Config cleared!") : Serial.println("Config could not be cleared");
    delay(1000);
    ESP.reset();
  }
  //Debugln("DEBUG: config reset check passed");  
  if (WiFi.status() == WL_CONNECTED && otaFlag){
    if(otaCount<=1) {
      Serial.println("OTA mode time out. Reset!"); 
      setOtaFlag(0);
      ESP.reset();
      delay(100);
    }
    server.handleClient();
    delay(1);
  } else if (WiFi.status() == WL_CONNECTED || webtypeGlob == 1){
    //Debugln("DEBUG: loop() wifi connected & webServer ");
    if (iotMode==0 || webtypeGlob == 1){
      //Debugln("DEBUG: loop() Web mode requesthandling ");
      server.handleClient();
      delay(1);
      if(esid != "" && WiFi.status() != WL_CONNECTED) //wifi reconnect part
      {
        Scan_Wifi_Networks();
      }
    } else if (iotMode==1 && webtypeGlob != 1 && otaFlag !=1){
          //Debugln("DEBUG: loop() MQTT mode requesthandling ");
          if (!connectMQTT()){
              delay(200);          
          }                    
          if (mqttClient.connected()){            
              //Debugln("mqtt handler");
              mqtt_handler();
          } else {
              Debugln("mqtt Not connected!");
          }
    }
  } else{
    Debugln("DEBUG: loop - WiFi not connected");  
    delay(1000);
    initWiFi(); //Try to connect again
  }
    //Debugln("DEBUG: loop() end");
}
