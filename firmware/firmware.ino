// LoopSensors LS400 v1.0.0 firmware

// Developed by AKstudios
// Updated: 3/7/2018

#include <RFM69.h>  //  https://github.com/LowPowerLab/RFM69
#include <SPI.h>
#include <Arduino.h>
#include <Wire.h> 
#include <Adafruit_SHT31.h> //https://github.com/adafruit/Adafruit_SHT31
#include <Adafruit_Sensor.h>  // https://github.com/adafruit/Adafruit_Sensor
#include <Adafruit_TSL2591.h> // https://github.com/adafruit/Adafruit_TSL2591_Library
#include <avr/sleep.h>
#include <avr/wdt.h>.
#include "config.h"

// define node parameters
//#define NODEID 2
#define GATEWAYID     1
#define NETWORKID     101
#define FREQUENCY     RF69_915MHZ //Match this with the version of your Moteino! (others: RF69_433MHZ, RF69_868MHZ)
//#define ENCRYPTKEY    "Tt-Mh=SQ#dn#JY3_" //has to be same 16 characters/bytes on all nodes, not more not less!
#define IS_RFM69HW    //uncomment only for RFM69HW! Leave out if you have RFM69W!
#define LED           9 // led pin
#define POWER         4

int t=0;
boolean t_flag = false;

// define objects
RFM69 radio;
Adafruit_SHT31 sht31 = Adafruit_SHT31();
Adafruit_TSL2591 tsl = Adafruit_TSL2591(2591); // pass in a number for the sensor identifier (for your use later)

// define other global variables
char dataPacket[150];


// ISR 
ISR(WDT_vect)  // Interrupt Service Routine for WatchDog Timer
{
  wdt_disable();  // disable watchdog
}

void setup()
{
  //pinMode(10, OUTPUT);
  Serial.begin(115200);
  //Serial.println("Setup");
  
  pinMode(POWER, OUTPUT);
  pinMode(LED, OUTPUT);  // pin 9 controls LED
  
  radio.initialize(FREQUENCY,NODEID,NETWORKID);
#ifdef IS_RFM69HW
  radio.setHighPower(); //uncomment only for RFM69HW!
#endif
  radio.encrypt(ENCRYPTKEY);
  
  digitalWrite(LED, HIGH);
  delay(5);
  digitalWrite(LED, LOW);
  delay(10);
  digitalWrite(LED, HIGH);
  delay(5);
  digitalWrite(LED, LOW);
}


void sleep()
{
  //Serial.flush(); // empty the send buffer, before continue with; going to sleep
  radio.sleep();
  digitalWrite(POWER, LOW);
  delay(1);
  
  cli();          // stop interrupts
  MCUSR = 0;
  WDTCSR  = (1<<WDCE | 1<<WDE);     // watchdog change enable
  WDTCSR  = 1<<WDIE | (1<<WDP3) | (0<<WDP2) | (0<<WDP1) | (1<<WDP0); // set  prescaler to 8 second
  sei();  // enable global interrupts

  byte _ADCSRA = ADCSRA;  // save ADC state
  ADCSRA &= ~(1 << ADEN);

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();       

  sleep_enable();  
  sleep_bod_disable();
  sei();       
  sleep_cpu();   
    
  sleep_disable();   
  sei();  

  ADCSRA = _ADCSRA; // restore ADC state (enable ADC)
  delay(5);
}


void loop() 
{
  sleep();
  checktime();
  
  if(t_flag == true)
  {
    readSensors();
    
    //Serial.println(dataPacket);
    //delay(10);
   
    // send datapacket
    radio.sendWithRetry(GATEWAYID, dataPacket, strlen(dataPacket), 5, 100);  // send data, retry 5 times with delay of 100ms between each retry
    dataPacket[0] = (char)0; // clearing first byte of char array clears the array
  
    digitalWrite(LED, HIGH);
    delay(10);
    digitalWrite(LED, LOW);
    //fadeLED();
    t_flag = false;
  }
}

void checktime()
{
  if(t_flag == false)
  {
    t++;    // increment t every 8 seconds (WDT)
    if(t>=37) // check if time is 5 mins
    {
      t=0;  // reset the time to 0
      t_flag = true;
    }
  }
}


void readSensors()
{
  digitalWrite(POWER, HIGH);
  delay(10);

  // T/RH - SHT31
  sht31.begin(0x44);
  float temp = sht31.readTemperature();
  float rh = sht31.readHumidity();

  // Light Intensity - TSL2591
  long lux;
  tsl.begin();
  tsl.setGain(TSL2591_GAIN_MED);      // 25x gain
  tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);  // shortest integration time (bright light)
  sensors_event_t event;
  tsl.getEvent(&event);
  if ((event.light == 0) | (event.light > 4294966000.0) | (event.light <-4294966000.0))
  {
    lux = 0;  // invalid value; replace with 'NAN' if needed
  }
  else
  {
    lux = event.light;
  }

  // US sensors thermistor readings
  // get sensor values
  float adc = averageADC(A0);
  float R = resistance(adc, 10000); // Replace 10,000 ohm with the actual resistance of the resistor measured using a multimeter (e.g. 9880 ohm)
  float air_temp = steinhart(R);  // get temperature from thermistor using the custom Steinhart-hart equation by US sensors
  
  
  // read battery level
  int a = analogRead(A7);   // discard first reading
  float avg=0.0;
  for(int i=0; i<5; i++)
  {
    avg = avg + analogRead(A7);
  }
  float adc_a7 = avg / 5.0;
  float batt = (adc_a7/1023 * 3.3) * 2.0;
  

  // define character arrays for all variables
  char _i[3];
  char _t[7];
  char _h[7];
  char _l[7];
  char _e[7];
  char _b[5];
  
  // convert all flaoting point and integer variables into character arrays
  dtostrf(NODEID, 1, 0, _i);
  dtostrf(temp, 3, 2, _t);  // this function converts float into char array. 3 is minimum width, 2 is decimal precision
  dtostrf(rh, 3, 2, _h);
  dtostrf(lux, 1, 0, _l);
  dtostrf(air_temp, 3, 2, _e);
  dtostrf(batt, 4, 2, _b);
  delay(10);
  
  dataPacket[0] = 0;  // first value of dataPacket should be a 0
  
  // create datapacket by combining all character arrays into a large character array
  strcat(dataPacket, "i:");
  strcat(dataPacket, _i);
  strcat(dataPacket, ",t:");
  strcat(dataPacket, _t);
  strcat(dataPacket, ",h:");
  strcat(dataPacket, _h);
  strcat(dataPacket, ",l:");
  strcat(dataPacket, _l);
  strcat(dataPacket, ",e:");
  strcat(dataPacket, _e);
  strcat(dataPacket, ",b:");
  strcat(dataPacket, _b);
  delay(5);
}




// Averaging ADC values to counter noise in readings  *********************************************
float averageADC(int pin)
{
  float sum=0.0;
  for(int i=0;i<5;i++)
  {
     sum = sum + analogRead(pin);
  }
  float average = sum/5.0;
  return average;
}

// Get resistance ****************************************************************
float resistance(float adc, int true_R)
{
  float R = true_R/(1023.0/adc-1.0);
  return R;
}

// Get temperature from Steinhart equation (US sensors thermistor, 10K, B = 3892) *****************************************
float steinhart(float R)
{
  float A = 0.00113929600457259;
  float B = 0.000231949467390149;
  float C = 0.000000105992476218967;
  float D = -0.0000000000667898975192618;
  float E = log(R);
  
  float T = 1/(A + (B*E) + (C*(E*E*E)) + (D*(E*E*E*E*E)));
  delay(50);
  return T-273.15;
}


void fadeLED()
{
  int brightness = 0;
  int fadeAmount = 5;
  for(int i=0; i<510; i=i+5)  // 255 is max analog value, 255 * 2 = 510
  {
    analogWrite(LED, brightness);  // pin 9 is LED
  
    // change the brightness for next time through the loop:
    brightness = brightness + fadeAmount;  // increment brightness level by 5 each time (0 is lowest, 255 is highest)
  
    // reverse the direction of the fading at the ends of the fade:
    if (brightness <= 0 || brightness >= 255)
    {
      fadeAmount = -fadeAmount;
    }
    // wait for 20-30 milliseconds to see the dimming effect
    delay(10);
  }
  digitalWrite(LED, LOW); // switch LED off at the end of fade
}

// bruh
