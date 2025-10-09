//#include <Adafruit_MPU6050.h>
//#include <Adafruit_Sensor.h>
#include <EEPROM.h>
#include <Adafruit_MCP23017.h>
#include <Adafruit_ADS1015.h>
#include "SimpleEspNowConnection.h"
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>

// 4 Channel analog to digital i2c converter
Adafruit_ADS1015 ads;     /* Use this for the 12-bit version */
// 16 Channel digial IO ic2
Adafruit_MCP23017 mcp;
// IMU, not used yet
//Adafruit_MPU6050 mpu;

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

// PPM output for TXN module will go to this pin
#define OUTPUT_PIN 27

//RTC_NOINIT_ATTR int bootCount=0;

// PPM output channel values
int16_t channels[10];

// PPM control variables
uint16_t elapsedUs=0;
uint8_t numChannels;
uint8_t currentChannel;
boolean state;
uint16_t frameCount=0;
uint16_t msgCount=0;

// PPM Constansts
const uint16_t MIN = 1000;
const uint16_t MAX = 2000;
const uint16_t PPM_PULSE_LENGTH_uS = 300;
const uint16_t PPM_FRAME_LENGTH_uS = 22500;

// Timer and mutex for PPM
hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// Main loop counters
uint16_t loopCount=0;

// Built in buttons
const byte BUTTON_1        = 0;
const byte BUTTON_2        = 35;

//Input buttons, not used yet

/*
const int NUM_INPUTS = sizeof(inputs) / sizeof(inputs[0]);
*/

// Input Buttons
const byte UP     = 0;
const byte DOWN   = 1;
const byte LEFT   = 2;
const byte RIGHT  = 3;
const byte CENTER = 4;
const byte SET    = 5;
const byte RESET  = 6;

const byte NUM_BUTTONS  = 7;

// Input channel range and settings struct
typedef struct {
  int ioChannel;
  char name[25];
} buttonInput;

// Input channel range and settings, stored in EEPROM
buttonInput buttonInputs[NUM_BUTTONS] = {
  {0,"Up"},
  {0,"Down"},
  {0,"Left"},
  {0,"Right"},
  {0,"Center"},
  {0,"Set"},
  {0,"Reset"}
};

// RC Channel alias AETR 
const byte AILERON  = 0;
const byte ELEVATOR = 1;
const byte THROTTLE = 2;
const byte RUDDER   = 3;

const byte NUM_INPUT_CHANNELS = 4;

// Input channel range and settings struct
typedef struct {
  int adcChannel;
  int min;
  int max;
  int trim;
  int raw;
  int pos;
  float rate;
  float expo;
  boolean reverse;
  char name[25];
} inputChannel;

// Input channel range and settings, stored in EEPROM
inputChannel inputChannels[NUM_INPUT_CHANNELS] = {
  {0,0,0,0,0,0,1.0,1.0,false,"Aileron"},
  {0,0,0,0,0,0,1.0,1.0,false,"Elevator"},
  {0,0,0,0,0,0,1.0,1.0,false,"Throttle"},
  {0,0,0,0,0,0,1.0,1.0,false,"Rudder"}
};


// Output channel struct
typedef struct {
  int pos;
  boolean reverse;
  char name[25];
} outputChannel;

// PPM Output channels, post mixer
outputChannel outputChannels[4] = {
  {0,false,"Channel1"},
  {0,false,"Channel2"},
  {0,false,"Channel3"},
  {0,false,"Channel4"}
};

// Different mixer models
const byte STANDARD  = 0;
const byte ELEVON = 1;

int mixType=ELEVON;

// PPM channel order
int ppmOrder[4] = {
  AILERON,
  ELEVATOR,
  THROTTLE,
  RUDDER
};

// ESP Now States
const int NOT_INITIALIZED = 0;
const int PAIRING         = 1;
const int PAIRED          = 2;
const int CONNECTED       = 3;
const int RECEIVED_MSG    = 4;

// ESP Now Status Strings
char* statusStrings[] = {
  "Not Initialized",
  "Pairing",
  "Paired",
  "Connected",
  "Received Msg"
};

// ESP Now Status
int status=NOT_INITIALIZED;

// ESP Server or Client
boolean isServer=true;

// Address of counter part
String address;
SimpleEspNowConnection simpleEspConnection(SimpleEspNowRole::SERVER); // SimpleEspNowRole::CLIENT

// Messsage we use to exchange channel data
typedef struct struct_message {
  char type;
  int16_t channels[4];
} struct_message;

// Data we send
struct_message myData;

// Data we received
struct_message remoteData;


// PPM Timer method, With critical section
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

// Method to send message with channel data
bool sendStructMessage()
{
  myData.type = '#'; // just to mark first byte. It's on you how to distinguish between struct and text message
  if(isServer) {
    return(simpleEspConnection.sendMessage((uint8_t *)&myData, sizeof(myData), address));
  }
  return(simpleEspConnection.sendMessage((uint8_t *)&myData, sizeof(myData)));
}

void OnSendError(uint8_t* ad)
{
  Serial.println("SENDING TO '"+simpleEspConnection.macToStr(ad)+"' WAS NOT POSSIBLE!");
}

void OnMessage(uint8_t* ad, const uint8_t* message, size_t len)
{
  msgCount++;
  
  if((char)message[0] == '#') // however you distinguish....
  {
    //struct_message myData;
    memcpy(&remoteData, message, len);
    //status=RECEIVED_MSG;
    if(!isServer && status!=PAIRED)status=PAIRED;
  }
  else
    Serial.printf("MESSAGE:[%d]%s from %s\n", len, (char *)message, simpleEspConnection.macToStr(ad).c_str());
}

void OnPaired(uint8_t *ga, String ad)
{
  Serial.println("EspNowConnection : Client '"+ad+"' paired! ");
  simpleEspConnection.endPairing();
  status=PAIRED;
  address = ad;
}

void OnConnected(uint8_t *ga, String ad)
{
  Serial.println("EspNowConnection : Client '"+ad+"' connected! ");
  status=CONNECTED;
  simpleEspConnection.sendMessage((uint8_t *)"Message at OnConnected from Server", 34, ad);
}

void OnNewGatewayAddress(uint8_t *ga, String ad)
{  
  Serial.println("New GatewayAddress '"+ad+"'");
  address = ad;

  simpleEspConnection.setServerMac(ga);
}

void setup() 
{
  Serial.begin(115200);
  Serial.println();
  EEPROM.begin(sizeof(inputChannels)+sizeof(buttonInputs));
  
  Serial.println(sizeof(inputChannel));
  Serial.println(sizeof(inputChannels));

  //Serial.println(bootCount++);
  
  pinMode(BUTTON_1, INPUT);
  pinMode(BUTTON_2, INPUT);

  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, HIGH);
  
  mcp.begin(7); 
  ads.begin();

  for(int i=0;i<16;i++) {
    mcp.pinMode(i, INPUT);
    mcp.pullUp(i, HIGH);   
  }
   
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);

  char* menu1[] = {"Load Cfg","Calibrate"};
  int choice=dispMenu(menu1);
  if(choice==1) {
    calibrate();
    calibrateButtons();
    writeCfg();
  } else {
    readCfg();
  }
  
  char* menu2[] = {"Skip","Calibrate Trim"};
  choice=dispMenu(menu2);
  if(choice==1) {
    calibrateTrim();
    writeCfg();
  }
  
  printCfg();

  char* menu3[] = {"Server EspNow Mode","Client EspNow Mode"};
  choice=dispMenu(menu3);
  if(choice==0) {
    isServer=true;
  } else {
    isServer=false;
  }  
  tft.fillScreen(TFT_BLACK);     
  tft.drawString(isServer?"Server":"Client",0,0,4);
  delay(1000);

  simpleEspConnection._role = isServer?SimpleEspNowRole::SERVER:SimpleEspNowRole::CLIENT;
  simpleEspConnection.begin();
  simpleEspConnection.onSendError(&OnSendError);  
  simpleEspConnection.onMessage(&OnMessage);  
  if(isServer) {
    simpleEspConnection.onPaired(&OnPaired);  
    simpleEspConnection.onConnected(&OnConnected);  
  } else {
    simpleEspConnection.setServerMac(address);
    simpleEspConnection.onNewGatewayAddress(&OnNewGatewayAddress);    
  }
  
  Serial.println(WiFi.macAddress());   
  
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

  //mpu.begin();
  //mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  //mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  //mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);  
}

void readCfg() {
  for(int address=0;address<sizeof(inputChannels);address++) {
    ((byte*)inputChannels)[address]=EEPROM.readByte(address);
  }  
  int offset=sizeof(inputChannels);
  for(int address=0;address<sizeof(buttonInputs);address++) {
    ((byte*)buttonInputs)[address]=EEPROM.readByte(address+offset);
  } 
}

void writeCfg() {
  for(int address=0;address<sizeof(inputChannels);address++) {
    EEPROM.writeByte(address, ((byte*)inputChannels)[address]);
  }
  int offset=sizeof(inputChannels);
  for(int address=0;address<sizeof(buttonInputs);address++) {
    EEPROM.writeByte(address+offset, ((byte*)buttonInputs)[address]);
  }
  EEPROM.commit();  
}

void printCfg() {
  for(int i=0;i<NUM_INPUT_CHANNELS;i++) {
    Serial.printf("Cfg %s ADC Channel %d Min %d Max %d Reverse %d\n",inputChannels[i].name,inputChannels[i].adcChannel,inputChannels[i].min,inputChannels[i].max,inputChannels[i].reverse);
  }
  for(int i=0;i<NUM_BUTTONS;i++) {
    Serial.printf("Cfg %s IO Channel %d\n",buttonInputs[i].name,buttonInputs[i].ioChannel);
  }  
}

void loop() 
{
  // manage the communication in the background
  simpleEspConnection.loop();

  if(!digitalRead(BUTTON_1) && status==NOT_INITIALIZED) {
    simpleEspConnection.startPairing(30);
    status=PAIRING;
  }

  if(/*isServer && */status==PAIRED) {
    if(!sendStructMessage())
    {
      Serial.println("SENDING TO '"+address+"' WAS NOT POSSIBLE!");
    }
  }

  readAndScaleInputs();
  mixer();
  writePPM();
  if(loopCount%4==0) {
    drawScreen();
    readTrim();
  }
  delay(10);  

  loopCount++;

  /*
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  Serial.print("Rotation X: ");
  Serial.print(g.gyro.x);
  Serial.print(", Y: ");
  Serial.print(g.gyro.y);
  Serial.print(", Z: ");
  Serial.print(g.gyro.z);
  Serial.println(" rad/s");
  */
  //Vector normGyro = mpu.readNormalizeGyro();
  //https://github.com/jarzebski/Arduino-MPU6050/blob/master/MPU6050_gyro_simple/MPU6050_gyro_simple.ino
  //https://randomnerdtutorials.com/esp32-mpu-6050-web-server/
}

const byte DISPLAY_MAIN=0;
const byte DISPLAY_TRIM=1;

byte displayMode=DISPLAY_MAIN;

void drawScreen()
{
  if(!mcp.digitalRead(buttonInputs[SET].ioChannel)) {
    displayMode=displayMode==DISPLAY_MAIN?DISPLAY_TRIM:DISPLAY_MAIN;
    tft.fillScreen(TFT_BLACK);
    delay(200);
  }
  
  char buffer[50];
  if(displayMode==DISPLAY_MAIN) {
    sprintf(buffer,"%d %-40s",msgCount,statusStrings[status]);
    tft.drawString(buffer,0,0,4); 

    for(int i=0;i<16;i++) {
      tft.fillRect(1+i*15, 40, 12, 12, mcp.digitalRead(i)?TFT_GREEN:TFT_RED);    
    }

    drawInputs();
    drawOutputs();
    drawMsgChannels();
    drawTrim();
  } else {
    sprintf(buffer,"A %d E %d T %d R %d       ",inputChannels[0].trim,inputChannels[1].trim,inputChannels[2].trim,inputChannels[3].trim);
    tft.drawString(buffer,0,0,4);
    drawTrim();    
    drawTrim();
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

void readTrim() {
  boolean rightStick=mcp.digitalRead(buttonInputs[CENTER].ioChannel);
  byte upDown=rightStick?ELEVATOR:THROTTLE;
  byte leftRight=rightStick?AILERON:RUDDER;

  if(!mcp.digitalRead(buttonInputs[RIGHT].ioChannel)) {
    if(inputChannels[leftRight].trim<50)inputChannels[leftRight].trim++;
  }
  if(!mcp.digitalRead(buttonInputs[LEFT].ioChannel)) {
    if(inputChannels[leftRight].trim>-50)inputChannels[leftRight].trim--;
  }
  if(!mcp.digitalRead(buttonInputs[UP].ioChannel)) {
    if(inputChannels[upDown].trim<50)inputChannels[upDown].trim++;
  }
  if(!mcp.digitalRead(buttonInputs[DOWN].ioChannel)) {
    if(inputChannels[upDown].trim>-50)inputChannels[upDown].trim--;
  } 

//  if(!mcp.digitalRead(buttonInputs[SET].ioChannel) &&
//     !mcp.digitalRead(buttonInputs[RESET].ioChannel)) {
//    writeCfg();
//    delay(500);         
//  }
}


void readAndScaleInputs() {
  for(int i=0;i<4;i++) {
    int raw = ads.readADC_SingleEnded(inputChannels[i].adcChannel);
    //if(i==2)Serial.printf("Raw %d %d\n",i,raw);
    int pos=map(raw,inputChannels[i].min,inputChannels[i].max,0,100);
    if(inputChannels[i].reverse)pos=100-pos;
    //if(i==2)Serial.printf("Pos %d %d\n",i,pos);
    
    //if(i==2)Serial.printf("Trimmed %d %d\n",i,pos);
    //pos=(pos-50)*inputChannels[i].rate+50;
    //pos=constrain(pos, 0, 100);
    //if(i==2)Serial.printf("Scaled %d %d\n",i,pos);
    // TODO: https://www.rcgroups.com/forums/showthread.php?375044-what-is-the-formula-for-the-expo-function
    // y=x*exp(abs(x*expo))*1.0/exp(expo)
    // x=0..1  -> y=0..1    expo 0 ->linear    (0.7, 0.85, 1, 1.5
    
    //if(i==2)Serial.printf("Reversed %d %d\n",i,pos);
    inputChannels[i].raw=raw;
   
    if(status==PAIRED) {
      // Write stick position to my data packet
      myData.channels[i]=pos;
      // If we are paired use the stick position from the remote data packet
      pos=remoteData.channels[i];
    } else {
      // If we are not paired use the local stick position
    }

    // Adjust for trim now, trim is specific to the airplane not the input (in other words,
    // when Mark controls Andreas' plane, Mark's stick + Andreas' trim set the servos
    pos+=inputChannels[i].trim;
    pos=constrain(pos, 0, 100);
    inputChannels[i].pos=pos;
  }  
}

void mixer() {
  if(mixType==STANDARD)  {
    outputChannels[AILERON].pos=inputChannels[AILERON].pos;
    outputChannels[ELEVATOR].pos=inputChannels[ELEVATOR].pos;
    outputChannels[THROTTLE].pos=inputChannels[THROTTLE].pos;
    outputChannels[RUDDER].pos=inputChannels[RUDDER].pos;    
  } else if(mixType==ELEVON) {

/*
    ele ail out
    50  50  50
    0   50  0
    0   0   0
    100 50  100

    limit(ele + ail -50)


    inputChannels[ELEVATOR].pos+inputChannels[AILERON].pos-100)/2+50
*/    
    //outputChannels[AILERON].pos=100-((inputChannels[ELEVATOR].pos+(100-inputChannels[AILERON].pos)-100)/2+50);
    //outputChannels[ELEVATOR].pos=    (inputChannels[ELEVATOR].pos+inputChannels[AILERON].pos-100)/2+50;

    outputChannels[AILERON].pos=100-constrain(inputChannels[ELEVATOR].pos-inputChannels[AILERON].pos+50, 0, 100);
    outputChannels[ELEVATOR].pos=constrain(inputChannels[ELEVATOR].pos+inputChannels[AILERON].pos-50, 0, 100);
    
    outputChannels[THROTTLE].pos=inputChannels[THROTTLE].pos;
    outputChannels[RUDDER].pos=0;
  } 
}

void writePPM() {
  for(int i=0;i<4;i++) {
    int idx=ppmOrder[i];
    //int pos=map(inputChannels[idx].pos,inputChannels[idx].min,inputChannels[idx].max,0,100);
    //if(status!=PAIRED) {
      setChannelPercent(i,outputChannels[idx].pos);
    //} else {
    //  setChannelPercent(i,remoteData.channels[idx]);
    //}
    
    //myData.channels[i]=outputChannels[idx].pos;
  }  

  // Lights on for CLIENT remote
  if(status==PAIRED) {
    setChannelPercent(4,isServer?100:0);
  } else {
    setChannelPercent(4,isServer?0:100);
  }
}

void drawInputs() {
  for(int i=0;i<4;i++) {
    int pos=inputChannels[i].pos;
    int maxVal=map(100,0,100,0,110);
    tft.fillRect(1, 60+i*6, pos, 5, TFT_GREEN);
    tft.fillRect(pos, 60+i*6, maxVal, 5, TFT_BLACK); 
    //Serial.printf("ADC %d %d\n",i,inputChannels[i].pos); 
  }
}

void drawOutputs() {
  for(int i=0;i<4;i++) {
    int pos=outputChannels[i].pos;
    int maxVal=map(100,0,100,0,110);
    tft.fillRect(1, 90+i*6, pos, 5, TFT_RED);
    tft.fillRect(pos, 90+i*6, maxVal, 5, TFT_BLACK); 
    //Serial.printf("Output %d %d\n",i,pos); 
  }
}

void drawMsgChannels() {
  for(int i=0;i<4;i++) {
    int pos=remoteData.channels[i];
    int maxVal=map(100,0,100,110,220);
    tft.fillRect(1+110, 60+i*6, pos, 5, TFT_YELLOW);
    tft.fillRect(pos+110, 60+i*6, maxVal, 5, TFT_BLACK); 
    //Serial.printf("ADC %d %d\n",i,inputChannels[i].pos); 
  }
}

void drawTrim() {
  for(int i=0;i<4;i++) {
    int pos=inputChannels[i].trim+50;
    int maxVal=map(100,0,100,110,220);
    tft.fillRect(1+110, 90+i*6, pos, 5, TFT_BLUE);
    tft.fillRect(pos+110, 90+i*6, maxVal, 5, TFT_BLACK); 
    //Serial.printf("ADC %d %d\n",i,inputChannels[i].pos); 
  }
}

void calibrate() {
  char buffer[100];  
  for(int i=0;i<NUM_INPUT_CHANNELS;i++) {
    tft.fillScreen(TFT_BLACK);    
    sprintf(buffer,"1) Move %s",inputChannels[i].name);
    tft.drawString(buffer,0,0,4); 
    tft.drawString("2) Hold right or up",0,40,4); 
    tft.drawString("3) Then press btn 1",0,80,4); 
    delay(500);

    int min[NUM_INPUT_CHANNELS]={-1,-1,-1,-1};
    int max[NUM_INPUT_CHANNELS]={-1,-1,-1,-1};
    int last[NUM_INPUT_CHANNELS]={-1,-1,-1,-1};
    while(digitalRead(BUTTON_1)) {
      for(int j=0;j<NUM_INPUT_CHANNELS;j++) {
        int16_t adc;
        adc = ads.readADC_SingleEnded(j);
        if(min[j]==-1 || adc<min[j])min[j]=adc;
        if(max[j]==-1 || adc>max[j])max[j]=adc;
        last[j]=adc;
      }
    }

    int maxRange=-1;
    int adcChannel=-1; 
    for(int j=0;j<NUM_INPUT_CHANNELS;j++) {
      int range=abs(max[j]-min[j]);
      if(range>maxRange || j==0) {
        maxRange=range;
        adcChannel=j;
      }  
    } 

    int deltaToMin=abs(last[adcChannel]-min[adcChannel]);
    int deltaToMax=abs(last[adcChannel]-max[adcChannel]);

    inputChannels[i].adcChannel=adcChannel;
    inputChannels[i].min=min[adcChannel];
    inputChannels[i].max=max[adcChannel]; 
    inputChannels[i].reverse=deltaToMin<deltaToMax;

    Serial.printf("Calibrated %s ADC Channel %d Min %d Max %d Reverse %d\n",inputChannels[i].name,inputChannels[i].adcChannel,inputChannels[i].min,inputChannels[i].max,inputChannels[i].reverse);
    
    while(!digitalRead(BUTTON_1)) {}
  }
}

void calibrateButtons() {
  char buffer[100];  
  for(int i=0;i<NUM_BUTTONS;i++) {
    tft.fillScreen(TFT_BLACK);    
    sprintf(buffer,"Press %s button",buttonInputs[i].name);
    tft.drawString(buffer,0,0,4); 
    delay(500);

    boolean found=false;
    while(!found)  {
      for(int j=0;j<16;j++) { 
        if(!mcp.digitalRead(j)) {
          buttonInputs[i].ioChannel=j; 
          found=true;
          break;
        }
      }
    }
    Serial.printf("Calibrated %s Button IO Channel %d\n",buttonInputs[i].name,buttonInputs[i].ioChannel);
    // Wait until button is no longer pressed
    while(!mcp.digitalRead(buttonInputs[i].ioChannel)) {tft.drawString("                                       ",0,0,4); }
  }
}

void calibrateTrim() {
  Serial.println("Calibrate Trim");
  char buffer[100];
  delay(1000);  
  tft.fillScreen(TFT_BLACK);  
  while(digitalRead(BUTTON_1)) {
    readTrim();
    sprintf(buffer,"A %d E %d T %d R %d       ",inputChannels[0].trim,inputChannels[1].trim,inputChannels[2].trim,inputChannels[3].trim);
    tft.drawString(buffer,0,0,4);
    drawTrim();
    delay(50);
  }
}

int dispMenu(char** menu) {
  int item=0;

  while(true) {
    tft.fillScreen(TFT_BLACK);     
    tft.drawString(menu[item],0,0,4); 
    delay(200);
    while(digitalRead(BUTTON_1) && digitalRead(BUTTON_2)) { }
    if(!digitalRead(BUTTON_2)) {
      item++;
      if(item>=2)item=0;
    } else {
      return item;
    }
    delay(500);
  }            
}
