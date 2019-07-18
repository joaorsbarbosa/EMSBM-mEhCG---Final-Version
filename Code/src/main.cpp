#include <Arduino.h>
#include <WiFi.h>

#include <ESPAsyncWebServer.h> //Library for the creation of the Async server, which has superior performance to the "standard" ESP32 server lib and it's able to
//handle several users at the same time without a drop in performance. Something that the standard server lib can not. 
#include <WebSocketsServer.h> // to send and recieve data to/from the webpage without the need of refreshing it.

#include <Ticker.h> //It eases the timing of functions without having to use delays. It's almost as having multi task, almost... 

#include <SPIFFS.h> //Library responsible of managing the ESP32 SPIFFS (external) memory and saving the webpage there. It has 4 MEGABYTES! 

#include "soc/timer_group_struct.h" //WatchDog Reset Related Stuff
#include "soc/timer_group_reg.h"

#include "arduinoFFT.h" //Self-explanatory
#include <FIRFilter.h>
#include <IIRFilter.h>

#include <Wire.h> // Libraries for I2C and for the ADS1115 16-bit 860SPS ADC
#include <Adafruit_ADS1015.h>
#define ADS1015_REG_CONFIG_DR_3300SPS   (0x00C0) //This library, made by Adafruit, is more suited for the ADS10115 but it still works with the ADS1115
//I checked the datasheet and confirmed that this will alter the ADS1115 registry for 860SPS (instead of the 3300SPS of the ADS1015)

Adafruit_ADS1115 ads; 

TaskHandle_t web_Task; // Handle for the extra task created. This is so we can use both processors instead of just one, improving performance by sharing the load



/* -------------------FFT--------------- */
//Variables related to the FFT and FIR-IIR filters.

#define SAMPLES 512        //Must be a power of 2 - Samples for the FFT 
#define SAMPLING_FREQUENCY 860 //Sampling rate of the ADC in Hz
arduinoFFT FFT = arduinoFFT();
unsigned int sampling_period_us;
unsigned long microseconds;
double vReal[SAMPLES];
double vImag[SAMPLES];
double FilteredData=0;

const int16_t DC_offset = 0;
const uint8_t ECG_pin = 34; //ADC pin on the ESP32

/* -------------------------------------- */


/* -------------------FIR & IIR stuff--------------- */
//These next constants are for the FIR and IIR filters.
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
/* --------------------------------------------------- */


/* -------------------Plot Refresh Rate--------------- */
float refresh_rate = 20; //ms
//this is how fast the ESP will send information to the webpage
//it can be controlled by the user directly on the webpage
/* --------------------------------------------------- */


/* Creat the tasks for the several functions */


/* -------------------WI-FI PASSWORD--------------- */

//INSERT YOUR WIFI DATA HERE// 

//const char * ssid = "MEO-EC52D0";
//const char * password = "";
//const char * ssid = "NOS-DB99";
//const char * password = "JK7JE5VG";
const char * ssid = "AndroidAP";
const char * password = "123456789";

//char* will store the char as a "static" array, 
//making the compiler save it in static storage and prevents modification to it

/* ------------------------------------------------ */


/* -------------------Server Stuff--------------- */
AsyncWebServer server(80); // Creates an AsyncWebServer object on port 80
WebSocketsServer webSocket = WebSocketsServer(81); //And this will creat a websocket on port 81
/* ------------------------------------------------ */


/* -------------------Misc variables--------------- */

bool type_filtering = true; //I was not able to send exactly the type of filtering the user wants through the webpage. I can only get a "signal" everytime
//the user changes the filter. To work around that, i'll simply toggle a bool everytime i get the ping and switch filters this way. One downside is that if the webpage is reloaded
//it will default to unfiltered but it will not tell the ESP32, resulting in the filters being switched if the last one used were the FIR filters
double bpm=0; //result of the FFT peak frequency 
double fft_result=0; //same thing but I needed to variables to handle that and sending it to the website
double ads_adc=0; //var to store the output of the ADS1115
/* ------------------------------------------------ */




int16_t measureECGAndSend() { //Function responsible for the FIR and IIR filters. It will store the ouput from the ADC and apply the filters and DC offset if needed
  int16_t value = analogRead(ECG_pin);
  double filtered = notch.filter(
                      lp.filter(
                      hp.filter(value - DC_offset)));
  value = round(filtered) + DC_offset;
  // Serial.println(value);
  return value;
}

//This function purpose is the get the information from the ADC and FFT peak, parse it into JSON form, and then broadcast it to all clients through the websocket
void getData() {
    String json= "{\"value\":";
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
   //the result is a string that looks something like this '{ "value":154, "pbm":30}'. This will alow the JavaScript on the webpage to get back the values 
   //since it can do something like this"bpm = received_string.bpm"
}

Ticker timer1(getData, refresh_rate); //the ticker is responsible for running the getData function at the intervals defined by the refresh rate
//this will not use any delays, saving this way some cpu time and increasing performance


//This is the function responsible of handling any INcoming data from the user. It will have to check what information it is, if it's type of filter or a new refresh rate.
//if it's refresh rate, it will update the value in the ticker function. If the user wants to turn off or on the filter, it will toggle the bool var "type_filtering"
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
 if(type == WStype_TEXT){
    String payload_str = String((char*) payload);
    if (payload_str == "unfiltered"){
      type_filtering = !type_filtering;
      return;
    }
    float dataRate = (float) atof((const char *) &payload[0]); //converts string to float
    timer1.interval(dataRate); //updates the ticker
  }
}



//This is the "loop" that will run inside the core 0.
//Any code inside the void loop() will run in core 1. But because it's important that the plotting in the webpage does not lag
//the function responsible for sending data to it will run alone on core 0. this way, we can run whatever code we want in core 1 and know that the web functionality
//will not be affected 
void web_Task_Function(void * parameter) {
  for (;;) {
    webSocket.loop(); //handles the websocket stuff 
    timer1.update(); //calls back the getData function when the time is right

    TIMERG0.wdt_wprotect = TIMG_WDT_WKEY_VALUE; //Everytime we create a new task (basically a new loop()...loop), the ESP32 will check if said loop is feeding
    // a timer it has (watchdog) to see if the function running ok or not. If it does not feed it, it will reboot the ESP32.
    TIMERG0.wdt_feed = 1; //RESETS THE WATCHDOG AND PREVENTS A CRASH.
    TIMERG0.wdt_wprotect = 0; //necessary because FreeRTOS Task do NOT FEED the hardware WatchDog 😠
  }

}



void setup() {
//Stuff to create the task so we can run code on core 0.
  xTaskCreatePinnedToCore(
    web_Task_Function, /* Task function. */
    "web_Task", /* name of task. */
    10000, /* Stack size of task */
    NULL, /* parameter of the task */
    0, /* priority of the task */ 
    &web_Task, /* Task handle to keep track of created task */
    0); /* pin task to core 0 */
  delay(500);// is needed to make sure that the task creation goes ok 


  Serial.begin(115200);

  sampling_period_us = round(1000000*(1.0/SAMPLING_FREQUENCY)); //sampling rate for the FFT


  if (!SPIFFS.begin(true)) { //Mounts the SPIFFS 
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  } else {
    Serial.println("SPIFFS mounted successfully");
  }

  // Connects to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println("WiFI Connection Established. IP:");
  Serial.println(WiFi.localIP());

  //this is the async server function that sends the html page to every user that connects to the esp32 
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/index.html");
  });


  //starts all the server stuff etc
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  timer1.start();

  ads.setGain(GAIN_ONE); // The ADS1115 has a PGA, and this sets the gain to 1. It's possible to go up to x16


}



void loop() {

  webSocket.onEvent(webSocketEvent);
    //Sampling code for the FFT
      for(int i=0; i<SAMPLES; i++)
      {
          microseconds = micros();    //Overflows after around 70 minutes!
    
    if (type_filtering) //if the user turns on the filter, the data fed into the FFT will go through the FIR and IIR digital filters
    {
      vReal[i] = analogRead(ECG_pin);
    }else
    {
      vReal[i] = measureECGAndSend();
    }
      
          vImag[i] = 0; // the Imaginary values of the FFT are not useful for this use case so I'll just declare them 0 
          while(micros() < (microseconds + sampling_period_us)){ // "delays" so that we get the correct sampling rate 
          }
      }
    /*FFT*/
      FFT.Windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
      FFT.Compute(vReal, vImag, SAMPLES, FFT_FORWARD);
      FFT.ComplexToMagnitude(vReal, vImag, SAMPLES);
      fft_result = FFT.MajorPeak(vReal, SAMPLES, SAMPLING_FREQUENCY); // calculates the highest frequency on the FFT


      // sometimes when the peak gets too close to 0 the FFT.MajorPeak function will return a nan, which in turn 
      //will crash the JSON parsing function on the website side and stop the plot until a digit is received. To avoid that issue, the value will only be uptaded if it's 
      //indeed a digit and not a...not a number.

      if (isDigit(fft_result))
      {
        bpm = fft_result;

      }
}
