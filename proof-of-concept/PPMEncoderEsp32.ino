#include <Arduino.h>

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

// With critical section
void IRAM_ATTR onTimer() {
  uint16_t nextTimerUs=0;
  portENTER_CRITICAL_ISR(&timerMux);

  if (state) {
    digitalWrite(OUTPUT_PIN, HIGH);
    nextTimerUs = PPM_PULSE_LENGTH_uS;
    elapsedUs += PPM_PULSE_LENGTH_uS;
  } else {
    digitalWrite(OUTPUT_PIN, LOW);
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


void setup() {
  Serial.begin(115200);
    
  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, LOW);

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

void loop() {
// You can comment Enter/ Exit critical section
//    portENTER_CRITICAL(&timerMux);
//    portEXIT_CRITICAL(&timerMux);
    
   delay(2000);   
   Serial.printf("Frame Count %d\n",frameCount); 

   loopCount++;
   if(loopCount%10==0) {
     for (uint8_t ch = 0; ch < numChannels; ch++) {
       setChannelPercent(ch, (loopCount%20==0)?10:90 );
     }    
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
