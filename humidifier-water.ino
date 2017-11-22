#include <LiquidCrystal.h>
#include <Ethernet2.h>       // Arduino Ethernet 2 Shield w/ W5500 chip
#include <SPI.h>             // Ethernet Shield communicates w/ Arduino board via SPI bus


// I/O Pin Assignments
const byte pinFlowMeter          = 2,
           pinLCDLight           = 3,
           pinResetCounterButton = A2,         // Using analog pins #2 and 
           pinDisplayButton      = A3;         //  #3 as digital input

volatile unsigned int pulseCounter;
unsigned long lastTimeRead;
unsigned long lastTimeSentData;
const unsigned long sendDataInterval = 30000;  // Time interval (in msec) to send data to DB
float totalGallons;
float lastTotalGallons;
float flowRate;    // Gallons per minute (GPM)
float lastFlowRate;
const float pulseRate = 1386.6666666666666667; // Gems FT-210 flow sensor: 83200 pulses per gallon
                                               // 1 GPM = 1386.666666667 pulses per second

const byte rs=8, en=9, d4=4, d5=5, d6=6, d7=7; // Pin assignments
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
const unsigned int lcdLightOnTime = 10000;     // LCD backlight stays on for 10 seconds with button press
int displayButtonState = LOW;
int resetCounterButtonReading;
int resetCounterButtonLastState = LOW;
int resetCounterButtonCurrentState = LOW;
unsigned long lastDebounceTime = 0;            // Time when button was last pressed
unsigned long debounceDelay = 15;              // In milliseconds

boolean lcdCheckLightOn;
unsigned int  lcdLightTimer;
unsigned long lcdLightOnStartTime;


byte mac[] = { 0x90, 0xA2, 0xDA, 0x11, 0x15, 0xEF };
EthernetClient netClient;
char serverIP[] = "192.168.101.10";            // Database hosted on remote Prague server


void setup() {
  Ethernet.begin(mac);
  Serial.begin(9600);
  lcd.begin(16, 2);          // LCD display with 16 columns, 2 rows
  lcd.noCursor();            // Hide cursor on LCD display
  lcd.noDisplay();           // Start with LCD display off
  
  pinMode(pinDisplayButton, INPUT);
  pinMode(pinLCDLight, OUTPUT);
  digitalWrite(pinLCDLight, LOW);    // Start with LCD backlight off
  lcdCheckLightOn = false;
  
  pinMode(pinFlowMeter, INPUT);
  attachInterrupt(digitalPinToInterrupt(pinFlowMeter), detectFlow, FALLING);

  pulseCounter           = 0;
  lastTimeRead           = 0;
  lastTimeSentData       = 0;
  lcdLightTimer          = 0;
  lcdLightOnStartTime    = 0;
  totalGallons           = 0.0;
  lastTotalGallons       = 0.0;
  flowRate               = 0.0;
  lastFlowRate           = 0.0;
}


void loop() {
  if(millis() - lastTimeRead >= 1000) {
    // Variable(s) used by interrupt are non-atomic, so disable interrupt during calculations
    detachInterrupt(digitalPinToInterrupt(pinFlowMeter));

    // Calculate actual milliseconds since last loop, then scale to a 1 second interval.
    // Divide by sensor's known pulse rate (pulses per second) to get flow rate.
    flowRate = (((1000.0 / (millis() - lastTimeRead)) * pulseCounter) / pulseRate);

    lastTimeRead = millis();            // Record the time when we went through the loop

    totalGallons += (flowRate / 60);    // Add the volume from this loop to the total volume

    if((millis() - lastTimeSentData >= sendDataInterval) && \
    ((lastFlowRate != flowRate) || (lastTotalGallons != totalGallons))) {   // Connect and send data to database if
       sendToDB();                                                          //   it's been longer time than interval
    }

    pulseCounter = 0;                   // Reset interrupt counter each loop
    attachInterrupt(digitalPinToInterrupt(pinFlowMeter), detectFlow, FALLING);  // Re-enable interrupt
     
    lcd.clear();
    lcd.setCursor(3, 0);                // Cursor to 4th column, 1st row
    lcd.print(flowRate,4);              // Print flow rate to 4 decimal places
    lcd.print(" GPM");
    lcd.setCursor(2, 1);                // Cursor to 3rd column, 2nd row
    lcd.print(totalGallons,2);          // Print total cumulative volume (gallons)
    lcd.print(" Gallons");
  }


  ////////////////////////////////////////////////////////////
  // Reset "Gallons" to 0 when reset counter button pressed //
  ////////////////////////////////////////////////////////////
  
  // Debounce button circuit to avoid duplicate sends to DB due to noise.
  
  resetCounterButtonReading = digitalRead(pinResetCounterButton);

  if(resetCounterButtonReading != resetCounterButtonLastState) {
    lastDebounceTime = millis();
  }

  if((millis() - lastDebounceTime) >= debounceDelay) {
    if(resetCounterButtonReading != resetCounterButtonCurrentState) {
      resetCounterButtonCurrentState = resetCounterButtonReading;
      
      if(resetCounterButtonCurrentState == HIGH) {
        sendToDB();
        totalGallons = 0.0;
        turnOnLCD();
      }
    }
  }

  resetCounterButtonLastState = resetCounterButtonReading;


  /////////////////////////////////////////////////////
  // Turn on LCD display when display button pressed //
  /////////////////////////////////////////////////////

  displayButtonState = digitalRead(pinDisplayButton);

  if(displayButtonState == HIGH) {              // Button pressed
    turnOnLCD();
  }
  else {                                        // Button not pressed
    if(lcdCheckLightOn) {
      lcdLightTimer = millis() - lcdLightOnStartTime;
      if(lcdLightTimer >= lcdLightOnTime) {
        lcdCheckLightOn = false;
        digitalWrite(pinLCDLight, LOW);         // Turn off LCD backlight
        lcd.noDisplay();                        // Turn off LCD display text
      }
    }
  }
  
}

void turnOnLCD() {
    lcdLightOnStartTime = millis();             // Record the time when button was pressed
    lcdLightTimer = 0;                     
    lcdCheckLightOn = true;
    digitalWrite(pinLCDLight, HIGH);            // Turn on LCD backlight
    lcd.display();                              // Turn on LCD display's text
}

void sendToDB() {
  if(netClient.connect(serverIP, 80)) {
    netClient.print("GET /writedata.php?");
    netClient.print("totalGallons=");
    netClient.print(totalGallons,2);   
    netClient.print("&&");
    netClient.print("flowRate=");
    netClient.print(flowRate,4);
    netClient.println(" HTTP/1.1");
    netClient.println("Host: 192.168.101.10");
    netClient.println("Connection: close");  // End of THIS transmission, not closing net connection
    netClient.println();                     // Blank line
    netClient.println();                     // Blank line
    netClient.stop();                        // Close connection to server
        
    lastTimeSentData = millis();             // Record the time when we sent the data

    lastFlowRate = flowRate;                 // Record flow rate this time through the loop
    lastTotalGallons = totalGallons;         // Record volume this time through the loop
  }
}

// Interrupt Service Routine (ISR)
void detectFlow() {
   pulseCounter++;
}

