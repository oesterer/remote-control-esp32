#include "SimpleEspNowConnection.h"
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include <WiFi.h>

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

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


SimpleEspNowConnection simpleEspConnection(SimpleEspNowRole::CLIENT);

String serverAddress;

typedef struct struct_message {
  char type;
  boolean inputs[NUM_INPUTS];
} struct_message;

struct_message myData;

String inputString;


bool sendStructMessage()
{
  myData.type = '#'; // just to mark first byte. It's on you how to distinguish between struct and text message
  for(int i=0;i<NUM_INPUTS;i++) {
    myData.inputs[i]=digitalRead(inputs[i]);
  }
  return(simpleEspConnection.sendMessage((uint8_t *)&myData, sizeof(myData)));
}

void OnSendError(uint8_t* ad)
{
  Serial.println("SENDING TO '"+simpleEspConnection.macToStr(ad)+"' WAS NOT POSSIBLE!");
}

void OnMessage(uint8_t* ad, const uint8_t* message, size_t len)
{
  if((char)message[0] == '#') // however you distinguish....
  {
    memcpy(&myData, message, len);
    status=RECEIVED_MSG;
  }
  else
    Serial.printf("MESSAGE:[%d]%s from %s\n", len, (char *)message, simpleEspConnection.macToStr(ad).c_str());
}


void OnNewGatewayAddress(uint8_t *ga, String ad)
{  
  Serial.println("New GatewayAddress '"+ad+"'");
  serverAddress = ad;

  simpleEspConnection.setServerMac(ga);
}

void setup() 
{
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  
  Serial.begin(115200);
  Serial.println();
 
  simpleEspConnection.begin();
  simpleEspConnection.setServerMac(serverAddress);
  simpleEspConnection.onNewGatewayAddress(&OnNewGatewayAddress);    
  simpleEspConnection.onSendError(&OnSendError);  
  simpleEspConnection.onMessage(&OnMessage);  
  
  Serial.println(WiFi.macAddress());   

  for(int i=0;i<NUM_INPUTS;i++) {
    pinMode(inputs[i], INPUT_PULLUP);
  }
  
  pinMode(BUTTON_1, INPUT);
  pinMode(BUTTON_2, INPUT);
}

void loop() 
{
  // manage the communication in the background
  simpleEspConnection.loop();

  if(!digitalRead(BUTTON_1) && status==NOT_INITIALIZED) {
    simpleEspConnection.startPairing(30);
    status=PAIRING;
  }

  if(!digitalRead(BUTTON_2)) {
    if(!sendStructMessage())
    {
      Serial.println("SENDING TO '"+serverAddress+"' WAS NOT POSSIBLE!");
    }
  }

  drawScreen();
  delay(100);  
}


void drawScreen()
{
  char buffer[50];
  sprintf(buffer,"%-40s",statusStrings[status]);
  tft.drawString(buffer,0,0,4); 

  for(int i=0;i<NUM_INPUTS;i++) {
    tft.fillRect(1+i*15, 90, 12, 12, myData.inputs[i]?TFT_GREEN:TFT_RED);
  }
}
