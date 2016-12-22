



/* USER INTERFACE **************************************************************************/
//Total number of vials being tested this time.  Make sure you updated the master code too!
int numVials = 4;

//The total number of times you want to run trials in a day
const int num_trials = 4;

//You can enter here up to four different times to check in a day.
//These times should track with the middle time of measurement, 
//aka: if you put in midnight, then the program will make sure that the machine finishes measuring half the vials by the time inputted.

//hr is the hours of the time in military time
//mi is the minutes and track with the array positions on hr.

//For example: if you want to run a trial at midnight, then you could set hr[0] = 0, and match mi[0] = 0
//However, if you were intending to check at midnight, and set hr[0] = 0 but mi[1] = 0 instead of mi[0],
//then the program will not reliably measure at midnight

//In this example, the times for measurement would be read as : 0:00, 6:05, 12:10,  20:30


/*
If you want to calculate the times based on when you would like SOME VIAL X in the series to be measured by SOME TIME, then use this equation:

int machine_homing_s = 40; //in s
int calibration_s = 136;
int reading_s = 70;
int ph_convergence_s = 60; //Just assume it's a minute

START_TIME = SOME_TIME - (homing + calibration + num_vials/2 * (reading_times + convergence_time))
           = SOME_TIME - (40sec + 136s + num_vials/2 * (70s + 60s))
           


*/

const int hr[num_trials] = {20, 20, 20, 20};                     
const int mi[num_trials] = {32, 35, 38, 53}; 

//MAKE SURE this matches whatever is in the master code.
int nX = 2;
int nY = 7;
/********************************************************************************************/

/*
pH Automation Code
RPNE Technologies
12-21-16

This is code that takes inputs on A0, A1, and A2 (MSB to LSB) and performs actions based upon these inputs.

Key:
A0 A1 A2
1  0  0   = pH 7 Calibration
1  0  1   = pH 4 Calibration
1  1  0   = pH 10 Calibration
1  1  1   = pH Read and Datalog

The Arduino will continuously monitor for inputs.

Technical Notes:
1) From the time of sending a read command to the Atlas Scientific (AS), it takes approximately 800 ms for the AS to send 
back a full response. Thus, for a safety factor, the Arduino will wait 1500 ms (timeoutthresh) for a response from the AS.

2) Only uncomment the clock.setDateTime(__DATE__, __TIME__) command when connected to a PC, and when the RTC must be updated
by the PC. Be sure to comment it after setting the time.

*/
#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>                           // Allows for Arduino to initiate serial communication with Atlas Scientific
#define rx 2                                          // RX digital pin for AS communication
#define tx 3                                          // TX digital pin for AS communication

#include <Wire.h>                                     // Real-time clock (RTC) (DS3231) libraries
#include <DS3231.h>

SoftwareSerial myserial(rx, tx);                      // Define how the soft serial port is going to work

DS3231 clock;                                         // RTC variables, setup
RTCDateTime dt;   

const int chipSelect = 4;                             // ChipSelect for the SD card Shield

String inputstring = "";                              // String to hold incoming data from the PC
String sensorstring = "";                             // String to hold the data from the Atlas Scientific product
String fileName = "";
boolean input_string_complete = false;                // Have we received all the data from a connected PC?
boolean sensor_string_complete = false;               // Have we received all the data from the Atlas Scientific?

int ms1 = 0;                                          // State of MASTER 1 PIN
int ms2 = 0;                                          // State of MASTER 2 PIN
int ms3 = 0;                                          // State of MASTER 3 PIN

int tempcount = 0;                                    // Keeps track of time MASTER PINS are at some constant reading
int tempintms = 0;                                    // Stores the last recorded MASTER PINS signal
int binout = 0;                                       // Signal to master from slave, 1 = slave is done with task
int binin = 0;                                        // Confirmed signal from master, in base 10
int intms = 0;                                        // Stores (temporary) base 10 representation of signal from master
int calT = 1000;                                      // Calibration time constant - delay in
int xPos = 1;
int yPos = 1;

long sigthreshold = 200;                              // Threshold for consecutive readings to be considered a solid command from master
                                                      // Threshold of 200 is ~5ms of signal based on empirical timing tests
                            
unsigned long receiveAS;                              // Stores time where Arduino starts to listen to AS
unsigned long timeoutthresh = 1500;                   // Milliseconds to wait for input from AS

const int strLoop = 8;                                // Flag to signal master to start loop                                 

File dataFile;                                        // File object for SD card

//track the number of vials
int counter_vials = 0;
boolean finished_trial = false;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// setup Function
// Arduino runs through this function once upon startup

void setup(){
  Serial.begin(9600);                                 // set baud rate for the hardware serial port_0 to 9600, debugging
  myserial.begin(9600);                               // set baud rate for the software serial port to 9600
  
  clock.begin();                                      // Start clock
  //clock.setDateTime(__DATE__, __TIME__);            // Function to update time, do not upload permanently - only
                                                      // upload when clock needs readjustment (then comment out again)

  inputstring.reserve(10);                            // Set aside bytes for receiving data from the PC
  sensorstring.reserve(30);                           // Set aside bytes for receiving data from Atlas Scientific product

  pinMode(A0,INPUT);                                  // To receive input from the master
  pinMode(A1,INPUT);
  pinMode(A2,INPUT);

  pinMode(strLoop,OUTPUT);                            // To send signal to master
  digitalWrite(strLoop,LOW);
  fileName = createFileName();
  initializeSD();                                     // Initializes SD card
  
                                    // Determines file name and creates file if not existing

  pHRead(false);                                      // One command to clear *ER output
  myserial.print("RESPONSE,0\r");                     // Responses (like *OK) are turned OFF from Atlas Scientific
  delay(1500);                                        // Delays necessary between direct myserial commands
  myserial.print("C,0\r");                            // Continuous mode for Atlas Scientific OFF
  delay(1500);                      
  
  pHRead(false);                  
  // To make sure the Atlas Scientific buffer is clear, we run through a 'listen' function a few times
  listenAS(false);
  listenAS(false);
  listenAS(false);
 
 }
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// loop Function
// This is the main function that the Arduino will loop through forever after setup.
void loop(){
  
  binout = 0;                                         // Sets signal output to master to 0
  ms1 = digitalRead(A0);                              // Reads master signal incoming on some pins
  ms2 = digitalRead(A1);
  ms3 = digitalRead(A2);
  
  intms = ms1*4+ms2*2+ms3;                            // Base 10 representation of binary signal from master. ms1 is MSB, ms3 is LSB.

  // The below IF statement stack makes sure that there is a solid, meaningful signal from the master to
  // perform a task. If there is some non-zero signal, the slave checks that the SAME signal is 
  // sent for consecutive cycles in time before deciding that the signal is indeed a real signal, and only
  // then does it assign this signal to the variable binin.
  if (intms != 0)                                     // If there is some non-zero signal from the master, enter stack
  {
    if (intms == tempintms){                          // If the current signal was the same as the last one from last cycle
      tempcount = tempcount + 1;                      // Increment counter by 1
      if (tempcount > sigthreshold){                  // If the counter is greater than some threshold
        binin = intms;                                // Set binin to the current measured signal from the master
      }
    }
    tempintms = intms;                                // Set current signal as temporary signal, for comparisons
  }
  else{                                               // If no signal detected, set tempcount to 0
    tempcount = 0;
  }

  //TEST!
  //if (binin != 0) {Serial.print("this is binin : "); Serial.println(binin);}
  //if (intms != 0) {Serial.print("intms is not 0: "); Serial.println(intms);}
  // If binin is 001 =1, 100=4, 101=5, 110=6, or 111=7, then perform calibrations or readings
  
  
  switch (binin){
    case 1:                                           //solo for checking time
      if(checkTime() == true){
        digitalWrite(strLoop,HIGH);
        delay(2500);
        digitalWrite(strLoop,LOW);
      } else { digitalWrite(strLoop,LOW); };
      xPos = 1;                                       // reset X & Y positions
      yPos = 1;
      tempcount = 0;
      binin = 0;
      counter_vials = 0;
      finished_trial = false;
      break;
    case 4:
      Serial.println("In case 4");
      pHCal7();                                       // Calibration or reading function
      binout = 1;                                     // Tells master that slave is done, and to bring master signal to 000
      delay(calT);                                    // Delay to allow time for master to bring signal to 000
      binin = 0;                                      // Set binin back to 0 to ensure that an action only happens once per master command
      tempcount = 0;
      Serial.println("Cal 7");
      break;
    case 5:
      Serial.println("In case 5");
      pHCal4();
      binout = 1;
      delay(calT);
      binin = 0;
      tempcount = 0;
      Serial.println("Cal 4");
      break;
    case 7:
      Serial.println("This is case 7");
      pHRead(true);
      binout = 1;
      delay(calT);
      binin = 0;
      tempcount = 0;
      changePos();
      break;
  }
 
} 

void pHCal7(){                                        // Calibrates to 7 pH
  myserial.print("Cal,mid,7.00\r");
  delay(1000);
}

void pHCal4(){                                        // Calibrates to 4 pH
  myserial.print("Cal,low,4.00\r");
  delay(1000);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void serialEvent(){                                   // if the hardware serial port_0 receives a char
  inputstring = Serial.readStringUntil(13);           // read the string until we see a <CR>
  input_string_complete = true;                       // set the flag used to tell if we have received a completed string from the PC
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// pHRead function
// Initiates pH reading with AS. Only stores data if A is TRUE.
void pHRead(boolean A){                                     
  myserial.print("R\r");                              // AS only reads when requested with R
  listenAS(A);
  
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void listenAS(boolean A){
  sensorstring="";                                    // Clears sensorstring
  receiveAS = millis();                               // Records start time of initiating a read
  // Loop until either the string is complete, or a timeout occurs (subject to timeoutthresh).
  // The last condition is to prevent overflows from keeping the Arduino in this loop.
  while ((sensor_string_complete == false) && (millis() - receiveAS < timeoutthresh) && (millis() - receiveAS >= 0)) {
    if (myserial.available() > 0){                    // if we see that the Atlas Scientific product has sent a character
      char inchar = (char)myserial.read();            // get the char we just received
      
      if (inchar == '\r') {                           // If the incoming character is a <CR>
        sensor_string_complete = true;                // Set flag to signal that the entire string from the AS is received
      }
      else{
        sensorstring += inchar;                       // Add the received character to the sensorstring variable
      }
    } 
  }

  if (sensor_string_complete == false){               // For debugging only - prints if TIMEOUT was reached
    Serial.println("TIMEOUT");
    Serial.println(sensorstring);
  }
    
  if (input_string_complete){                         // if a string from the PC has been received in its entirety
    myserial.print(inputstring);                      // send that string to the Atlas Scientific product
    myserial.print('\r');                             // add a <CR> to the end of the string
    inputstring = "";                                 // clear the string
    input_string_complete = false;                    // reset the flag used to tell if we have received a completed string from the PC
  }
  
  if(sensor_string_complete == true){
    Serial.print(sensorstring);                       // send that string to the PC's serial monitor
    Serial.print(",");
    if(A==true){
      recordData(sensorstring);
    }
    Serial.print('\n');
    sensorstring = "";                                // clear the string
    sensor_string_complete = false;                   // reset flag
  }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
boolean checkTime(){
  dt=clock.getDateTime();
  //checks the times always
  for(int i=0; i < num_trials; i++) {
     if( (dt.hour == hr[i])
        && (  (dt.minute >= mi[i]) 
            && (dt.minute < (mi[i] + 1))
            )
         ){
          Serial.println("it's time");
          return true;
    }
  }
  delay(10000); //don't need to check all the time, can wait 10 seconds
  return false;
  }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// recordData function
// Records the input string into the SD card. If the file cannot be opened because 1) the SD card is absent, or 2) the SD
// card was not initialized, this function will attempt to initialize the SD card.
void recordData(String A){
  //turn to charArray for happiness
    char temp[13];
    
    fileName.toCharArray(temp, 13);
    dataFile=SD.open(temp, FILE_WRITE);

    if(dataFile==false){
      Serial.println("error opening .txt file");
      initializeSD();
    }
    else{
      dataFile.print(A);                              // Print the string A
      dataFile.print(",");
      recTime();                                      // Print present time string to file
      recPos();
      dataFile.close();                               // Closes the file
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void recTime(){                                       // function to calculate time elapsed

   dt=clock.getDateTime();
   dataFile.print("\t");
   if (dt.hour < 10) {dataFile.print("0");}      
   dataFile.print(dt.hour);    dataFile.print(":");
   if (dt.minute < 10) {dataFile.print("0");}
   dataFile.print(dt.minute);  dataFile.print(":");
   if (dt.second < 10) {dataFile.print("0");}
   dataFile.print(dt.second);
   dataFile.print(",");
   dataFile.print(dt.year);    dataFile.print("-");
   if (dt.month < 10) {dataFile.print("0");}
   dataFile.print(dt.month);   dataFile.print("-");
   if (dt.day < 10)   {dataFile.print("0");}
   dataFile.print(dt.day);   
   dataFile.print(",");

   Serial.print(dt.hour);    Serial.print(":");
   Serial.print(dt.minute);  Serial.print(":");
   Serial.print(dt.second);
   Serial.print(",");
   Serial.print(dt.year);    Serial.print("-");
   Serial.print(dt.month);   Serial.print("-");
   Serial.println(dt.day); 
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void initializeSD(){                                  // Initialize SD card 
  if (!SD.begin(chipSelect)) {                        // See if the card is present and can be initialized
    Serial.println("Card failed");
    return;
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
String createFileName(){ 
 // Create a fileName for the .txt file
  String this_string = "";
  dt=clock.getDateTime();
  
  if(dt.month<10){
    this_string +="0";
  }
  this_string += dt.month;
  if(dt.day<10){
    this_string +="0";
  }
  this_string += dt.day;
  if(dt.hour<10){
    this_string +="0";
  }
  this_string += dt.hour;
  if(dt.minute<10){
    this_string +="0";
  }
  
  this_string += dt.minute;
  this_string += ".txt";

  return this_string;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void changePos(){
  counter_vials += 1;
  
  if (counter_vials >= numVials){
    xPos = 3;
    yPos = 0;
    finished_trial = true;
    counter_vials = 0;
  } else if (xPos >= nX) {
    if(finished_trial != true) {xPos = 1; yPos += 1;} //we assume that X pos is maximised first
    else{xPos += 1;} //This is that final calibration retest, for pH 4
    
    } else { xPos += 1;};
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void recPos(){
  dataFile.print("\t");
  dataFile.print(xPos);
  dataFile.print(", ");
  dataFile.println(yPos);
  
}

