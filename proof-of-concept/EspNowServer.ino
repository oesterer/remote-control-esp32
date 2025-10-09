#include <Adafruit_MCP23017.h>
#include <Adafruit_ADS1015.h>

#include "SimpleEspNowConnection.h"
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>

// Adafruit_ADS1115 ads;  /* Use this for the 16-bit version */
Adafruit_ADS1015 ads;     /* Use thi for the 12-bit version */

Adafruit_MCP23017 mcp;

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

#define OUTPUT_PIN 27

int16_t channels[10];
uint16_t elapsedUs=0;

uint8_t numChannels;
uint8_t currentChannel;
boolean state;

const uint16_t MIN = 1000;
const uint16_t MAX = 2000;
const uint16_t PPM_PULSE_LENGTH_uS = 300;
const uint16_t PPM_FRAME_LENGTH_uS = 22500;

uint16_t frameCount=0;
uint16_t loopCount=0;

hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;


const byte BUTTON_1        = 0;
const byte BUTTON_2        = 35;

const byte UP              = 27;
const byte DOWN            = 26;
const byte LEFT            = 25;
const byte RIGHT           = 33;
const byte MIDDLE          = 32;
const byte SET             = 12;
const byte RESET           = 13;

byte inputs[] = {
  UP,
  DOWN,
  LEFT,
  RIGHT,
  MIDDLE,
  SET,
  RESET  
};

const int NUM_INPUTS = sizeof(inputs) / sizeof(inputs[0]);


const int NOT_INITIALIZED = 0;
const int PAIRING         = 1;
const int PAIRED          = 2;
const int CONNECTED       = 3;
const int RECEIVED_MSG    = 4;

char* statusStrings[] = {
  "Not Initialized",
  "Pairing",
  "Paired",
  "Connected",
  "Received Msg"
};

int status=NOT_INITIALIZED;


SimpleEspNowConnection simpleEspConnection(SimpleEspNowRole::SERVER);

typedef struct struct_message {
  char type;
  boolean inputs[NUM_INPUTS];
} struct_message;

struct_message myData;

String inputString;
String clientAddress;

// With critical section
void IRAM_ATTR onTimer() {
  uint16_t nextTimerUs=0;
  portENTER_CRITICAL_ISR(&timerMux);

  if (state) {
    digitalWrite(OUTPUT_PIN, LOW);
    nextTimerUs = PPM_PULSE_LENGTH_uS;
    elapsedUs += PPM_PULSE_LENGTH_uS;
  } else {
    digitalWrite(OUTPUT_PIN, HIGH);
    if (currentChannel >= numChannels) {
      currentChannel = 0;
      nextTimerUs = PPM_FRAME_LENGTH_uS - elapsedUs;
      elapsedUs = 0;
      frameCount++;
    } else {
      nextTimerUs = channels[currentChannel] - PPM_PULSE_LENGTH_uS;
      elapsedUs = elapsedUs + nextTimerUs;
      currentChannel++;
    }
  }
  timerAlarmWrite(timer, nextTimerUs, true);
  state = !state; 
  
  portEXIT_CRITICAL_ISR(&timerMux);    
}


bool sendStructMessage()
{
  myData.type = '#'; // just to mark first byte. It's on you how to distinguish between struct and text message
  for(int i=0;i<NUM_INPUTS;i++) {
    myData.inputs[i]=digitalRead(inputs[i]);
  }
  return(simpleEspConnection.sendMessage((uint8_t *)&myData, sizeof(myData), clientAddress));
}

void OnSendError(uint8_t* ad)
{
  Serial.println("SENDING TO '"+simpleEspConnection.macToStr(ad)+"' WAS NOT POSSIBLE!");
}

void OnMessage(uint8_t* ad, const uint8_t* message, size_t len)
{
  if((char)message[0] == '#') // however you distinguish....
  {
    struct_message myData;
    memcpy(&myData, message, len);
    status=RECEIVED_MSG;
  }
  else
    Serial.printf("MESSAGE:[%d]%s from %s\n", len, (char *)message, simpleEspConnection.macToStr(ad).c_str());
}

void OnPaired(uint8_t *ga, String ad)
{
  Serial.println("EspNowConnection : Client '"+ad+"' paired! ");
  simpleEspConnection.endPairing();
  status=PAIRED;
  clientAddress = ad;
}

void OnConnected(uint8_t *ga, String ad)
{
  Serial.println("EspNowConnection : Client '"+ad+"' connected! ");
  status=CONNECTED;
  simpleEspConnection.sendMessage((uint8_t *)"Message at OnConnected from Server", 34, ad);
}

void setup() 
{

  
  mcp.begin(7); 

  ads.begin();
   
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  
  Serial.begin(115200);
  Serial.println();

  simpleEspConnection.begin();
  simpleEspConnection.onMessage(&OnMessage);  
  simpleEspConnection.onPaired(&OnPaired);  
  simpleEspConnection.onSendError(&OnSendError);
  simpleEspConnection.onConnected(&OnConnected);  

  Serial.println(WiFi.macAddress());   

  for(int i=0;i<16;i++) {
    //pinMode(inputs[i], INPUT_PULLUP);
    mcp.pinMode(i, INPUT);
    mcp.pullUp(i, HIGH);   
  }
  
  pinMode(BUTTON_1, INPUT);
  pinMode(BUTTON_2, INPUT);

  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, HIGH);

  state = true;
  elapsedUs = 0;
  currentChannel = 0;
  numChannels = 6;

  for (uint8_t ch = 0; ch < numChannels; ch++) {
    setChannelPercent(ch, 50);
  }

  timer = timerBegin(0, 80, true);                
  timerAttachInterrupt(timer, &onTimer, true);    
  timerAlarmWrite(timer, 500000, true);      
  timerAlarmEnable(timer);
  
}

void loop() 
{
  // manage the communication in the background
  simpleEspConnection.loop();

  if(!digitalRead(BUTTON_1) && status==NOT_INITIALIZED) {
    simpleEspConnection.startPairing(30);
    status=PAIRING;
  }

  if(status!=NOT_INITIALIZED /*!digitalRead(BUTTON_2)*/) {
    if(!sendStructMessage())
    {
      Serial.println("SENDING TO '"+clientAddress+"' WAS NOT POSSIBLE!");
    }
  }

  drawScreen();
  delay(50);  

  //Serial.printf("Frame Count %d\n",frameCount); 
  
//  int16_t adc0;

  // Comparator will only de-assert after a read
//  adc0 = ads.getLastConversionResults();
//  Serial.print("AIN0: "); Serial.println(adc0);
}


void drawScreen()
{
  char buffer[50];
  sprintf(buffer,"%-40s",statusStrings[status]);
  tft.drawString(buffer,0,0,4); 

  for(int i=0;i<16;i++) {
    //tft.fillRect(1+i*15, 90, 12, 12, digitalRead(inputs[i])?TFT_GREEN:TFT_RED);
    tft.fillRect(1+i*15, 40, 12, 12, mcp.digitalRead(i)?TFT_GREEN:TFT_RED);    
  }

  for(int i=0;i<4;i++) {
    int16_t adc;
    adc = ads.readADC_SingleEnded(i);
    tft.fillRect(1, 60+i*12, adc/5, 8, TFT_GREEN);
    tft.fillRect(adc/5, 60+i*12, 1100/5, 8, TFT_BLACK); 

    setChannelPercent(i,adc/10);
  }
}

void setChannel(uint8_t channel, uint16_t value) {
  channels[channel] = constrain(value, MIN, MAX);
}

void setChannelPercent(uint8_t channel, uint8_t percent) {
  percent = constrain(percent, 0, 100);
  setChannel(channel, map(percent, 0, 100, MIN, MAX));
}

void setChannelScaled(uint8_t channel, uint8_t val, uint16_t maxVal) {
  val = constrain(val, 0, maxVal);
  setChannel(channel, map(val, 0, maxVal, MIN, MAX));
}
