#include <Arduino.h>
#include <WiFi.h>

#include <ESPAsyncWebServer.h> //Library for the creation of the Async server, which has superior performance to the "standard" ESP32 server lib.
#include <WebSocketsServer.h> // to send and recieve data to/from the webpage without the need of refreshing it.

#include <Ticker.h> //It eases the timing of functions without having to use delays. It's almost multi task. 

#include <SPIFFS.h> //Library responsible of managing the ESP32 SPIFFS memory and saving the webpage there.

#include "soc/timer_group_struct.h" //WatchDog Reset Related Stuff
#include "soc/timer_group_reg.h"

#include "arduinoFFT.h" //Self-explanatory
#include <FIRFilter.h>
#include <IIRFilter.h>


TaskHandle_t web_Task; // Handle for the extra task created. This is so we can use both processors instead of just 1 


#define SAMPLES 512        //Must be a power of 2
#define SAMPLING_FREQUENCY 600 //Hz, must be less than 10000 due to ADC


//Variables related to the FFT and FIR-IIR filters.
arduinoFFT FFT = arduinoFFT();
unsigned int sampling_period_us;
unsigned long microseconds;
double vReal[SAMPLES];
double vImag[SAMPLES];
double FilteredData=0;
const uint8_t ECG_pin = 34; //ADC PIN
const int16_t DC_offset = 0;


// 50 Hz notch
const double b_notch[] = { 1.39972748302835,  -1.79945496605670, 1.39972748302835 };
// 0.3 Hz high-pass
const double b_hp[] = { 1, -1 };
const double a_hp[] = { 1, -0.995 };

// 35 Hz Butterworth low-pass
const double b_lp[] = { 0.00113722762905776, 0.00568613814528881, 0.0113722762905776,  0.0113722762905776,  0.00568613814528881, 0.00113722762905776 };
const double a_lp[] = { 1, -3.03124451613593, 3.92924380774061,  -2.65660499035499, 0.928185738776705, -0.133188755896548 };



FIRFilter notch(b_notch);
IIRFilter lp(b_lp, a_lp);
IIRFilter hp(b_hp, a_hp);
 


/* -------------------Plot Refresh Rate--------------- */
float refresh_rate = 20; //ms
/* --------------------------------------------------- */


/* Creat the tasks for the several functions */


/* -------------------WI-FI PASSWORD--------------- */
const char * ssid = "AndroidAP";
const char * password = "123456789";
//const char * ssid = "NOS-DB99";
//const char * password = "JK7JE5VG";



//char* will store the char as a "static" array, 
//making the compiler save it in static storage and prevents modification to it
/* ------------------------------------------------ */
AsyncWebServer server(80); // Create AsyncWebServer object on port 80


WebSocketsServer webSocket = WebSocketsServer(81);


//int i = 0;
bool type_filtering = true;


double bpm=0;

int16_t measureECGAndSend() {
  int16_t value = analogRead(ECG_pin);
  double filtered = notch.filter(
                       lp.filter(
                      hp.filter(value - DC_offset)));
  value = round(filtered) + DC_offset;
  // Serial.println(value);
  return value;
}

void getData() {

    String json= "{\"value\":";
    //json += analogRead(ECG_pin); //add data here
    //FilteredData=measureECGAndSend();
   // json +=",";
   if (type_filtering)
   {
     json += analogRead(ECG_pin);
   }else
   {
     json += measureECGAndSend();
   }

    json +=", \"bpm\":";
    json += bpm;
    json +="}";
    webSocket.broadcastTXT(json.c_str(), json.length());  
   // delay(refresh_rate);    
}

Ticker timer1(getData, refresh_rate);

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
//Serial.println((const char *) &payload[0]);
 if(type == WStype_TEXT){
    String payload_str = String((char*) payload);
    if (payload_str == "unfiltered"){
      type_filtering = !type_filtering;
      return;
    }
    float dataRate = (float) atof((const char *) &payload[0]);
    timer1.interval(dataRate);
  }


}




void web_Task_Function(void * parameter) {

  for (;;) {
    
    webSocket.loop();
    timer1.update();
    //getData();
    TIMERG0.wdt_wprotect = TIMG_WDT_WKEY_VALUE;
    TIMERG0.wdt_feed = 1; //RESETS THE WATCHDOG AND PREVENTS A CRASH.
    TIMERG0.wdt_wprotect = 0; //necessary because FreeRTOS Task does NOT FEED the hardware WatchDog 😠
  }

}



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
  
  sampling_period_us = round(1000000*(1.0/SAMPLING_FREQUENCY));

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
webSocket.onEvent(webSocketEvent);

   /*SAMPLING*/
    for(int i=0; i<SAMPLES; i++)
    {
        microseconds = micros();    //Overflows after around 70 minutes!
  
  if (type_filtering)
   {
     vReal[i] = analogRead(ECG_pin);
   }else
   {
     vReal[i] = measureECGAndSend();
   }
    
        vImag[i] = 0;
        while(micros() < (microseconds + sampling_period_us)){
        }
    }
  /*FFT*/
    FFT.Windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.Compute(vReal, vImag, SAMPLES, FFT_FORWARD);
    FFT.ComplexToMagnitude(vReal, vImag, SAMPLES);
    bpm = FFT.MajorPeak(vReal, SAMPLES, SAMPLING_FREQUENCY);


}
