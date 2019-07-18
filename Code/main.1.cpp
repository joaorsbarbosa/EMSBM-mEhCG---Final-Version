  #include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <Ticker.h>
#include <SPIFFS.h>
 //#include <esp_task_wdt.h>
#include "soc/timer_group_struct.h" //WatchDog Reset Related Stuff
#include "soc/timer_group_reg.h"


const int size_array_ADC=100;
const int trigger_value=256;
const int trigger_data_points= 160; //should be the same as the data plot 
bool trigger_done=false;

unsigned int array_trigger[trigger_data_points];
volatile unsigned int array_ADC[size_array_ADC];

TaskHandle_t web_Task;
 
//TaskHandle_t ADC;
 
/* -------------------Plot Refresh Rate--------------- */
double refresh_rate = 20; //ms
/* --------------------------------------------------- */


/* Creat the tasks for the several functions */


/* -------------------WI-FI PASSWORD--------------- */
//const char * ssid = "AndroidAP";
//const char * password = "123456789";
//const char * ssid = "NOS-DB99";
//const char * password = "JK7JE5VG";
const char* ssid = "ZON-FDC0";
const char* password = "caralhoalex";


//char* will store the char as a "static" array, 
//making the compiler save it in static storage and prevents modification to it
/* ------------------------------------------------ */
AsyncWebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);


// Create AsyncWebServer object on port 80
double variavel = 0;
int i = 0;

double ekg[] = {0.19136,0.19136,0.27337,0.41006,0.54675,0.74282,2.0672,4.8761,9.1102,13.701,18.291,22.881,27.472,30.226,29.92,26.554,21.963,17.373,12.782,8.1921,4.2015,1.7103,0.7185,0.62638,0.53427,0.44215,0.35004,0.25792,0.16581,-0.9667,-6.2608,-14.335,-14.306,-1.4965,22.712,48.475,71.736,76.651,60.719,26.441,-10.339,-44.249,-57.116,-46.071,-20.956,-4.5927,0.149,0.24213,0.33526,0.42839,0.52151,0.61464,0.70777,0.80089,0.89402,0.98715,1.0803,1.1734,1.2665,1.3597,1.4528,1.5459,1.9248,3.0181,4.8258,7.0621,9.2985,11.535,13.771,15.65,16.097,15.053,12.877,10.64,8.404,6.1676,4.0046,2.3792,1.3647,0.88781,0.48964,0.1937,0.032284,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};



void getData() {
  /* variavel = ekg[i];
  i++;
  if (i>100)
  {
    i=0;
  }
  */

  /* 
  unsigned int adc_teste = analogRead(32);
  String json = "{\"value\":";
  json += adc_teste; //add data here
  json += "}";
  webSocket.broadcastTXT(json.c_str(), json.length());
*/
if (analogRead(32)>= trigger_value && !trigger_done)
{
  for (size_t i = 0; i < trigger_data_points; i++)
  {
  
  String json = "{\"value\":";
  json += analogRead(32); //add data here
  json +="}";
  webSocket.broadcastTXT(json.c_str(), json.length());  
  delay(refresh_rate);
  }
  trigger_done =true;
  
} 
else
{
  trigger_done=false;
}


   
    
}


void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  // Do something with the data from the client
  if (type == WStype_TEXT) {
    float dataRate = (float) atof((const char * ) & payload[0]);
   
    //Ticker timer1(getData, dataRate);
  }
}


Ticker timer1(getData, refresh_rate);




void web_Task_Function(void * parameter) {

  // Serial.print("web_Task running on core ");
  // Serial.println(xPortGetCoreID());  
  for (;;) {
    webSocket.loop();
    //timer1.update();
    getData();

    TIMERG0.wdt_wprotect = TIMG_WDT_WKEY_VALUE;
    TIMERG0.wdt_feed = 1; //RESETS THE WATCHDOG AND PREVENTS A CRASH.
    TIMERG0.wdt_wprotect = 0; //necessary because FreeRTOS Task does NOT FEED the hardware WatchDog 😠
  }

}



/* 
void ADC_Function(void * parameter) {
  for(;;){
  if (ADC_WEB){
    
    Serial.println("running ADC");
      for (int i = 0; i < size_array_ADC; i++) {
        array_ADC[i] = analogRead(32);
      }
      Serial.println("finished ADC");
      if (i > size_array_ADC) {
       i = 0;
      }
  
  }
  ADC_WEB= !ADC_WEB;
  
  TIMERG0.wdt_wprotect = TIMG_WDT_WKEY_VALUE;
  TIMERG0.wdt_feed = 1; //RESETS THE WATCHDOG AND PREVENTS A CRASH.
  TIMERG0.wdt_wprotect = 0; //necessary because FreeRTOS Task does NOT FEED the hardware WatchDog 😠
  break;
  }
}
*/



void setup() {

  //esp_task_wdt_init(20, false);

  xTaskCreatePinnedToCore(
    web_Task_Function, /* Task function. */
    "web_Task", /* name of task. */
    10000, /* Stack size of task */
    NULL, /* parameter of the task */
    0, /* priority of the task */ 
    &web_Task, /* Task handle to keep track of created task */
    0); /* pin task to core 0 */
  delay(500);




  Serial.begin(115200);

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  } else {
    Serial.println("SPIFFS mounted successfully");
  }

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println("WiFI Connection Established. IP:");
  Serial.println(WiFi.localIP());


  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/index.html");
  });



  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  timer1.start();



}



void loop() {
  

/* 
      for (int i = 0; i < size_array_ADC; i++) {
        array_ADC[i] = analogRead(32);
      }
     
      if (i > size_array_ADC) {
       i = 0;
      }
 */
  

}