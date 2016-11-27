/*
pH Reading Code
RPNE Technologies
11-27-16

This is code that takes inputs on A0, A1, and A2 (MSB to LSB) and performs actions based upon these inputs.

Key:
A0 A1 A2
1  0  0   = pH 7 Calibration
1  0  1   = pH 4 Calibration
1  1  0   = pH 10 Calibration
1  1  1   = pH Read and Datalog

The Arduino will continuously monitor for inputs. 

*/
#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>                           //we have to include the SoftwareSerial library, or else we can't use it
#define rx 2                                          //define what pin rx is going to be
#define tx 3                                          //define what pin tx is going to be

#include <Wire.h>                                     //RTC (DS3231) libraries
#include <DS3231.h>

SoftwareSerial myserial(rx, tx);                      //define how the soft serial port is going to work

DS3231 clock;                                         //rtc parameters
RTCDateTime dt;   

const int chipSelect=4;                               //chipSelect for the SD card Shield

String inputstring = "";                              //a string to hold incoming data from the PC
String sensorstring = "";                             //a string to hold the data from the Atlas Scientific product
boolean input_string_complete = false;                //have we received all the data from the PC
boolean sensor_string_complete = false;               //have we received all the data from the Atlas Scientific product
float pH;                                             //used to hold a floating point number that is the pH
String fileName="";                                   //used for the naming of the date file

boolean readPH=false;                                 //condition for the start and pause


int ms1 = 0;   // State of MASTER 1 PIN
int ms2 = 0;   // State of MASTER 2 PIN
int ms3 = 0;   // State of MASTER 3 PIN
int tempcount = 0;  // Keeps track of time MASTER PINS are at some constant reading
int tempintms = 0;  // Stores the last recorded MASTER PINS signal
int binout = 0;   // Signal to master from slave, 1 = slave is done with task
int binin = 0;    // Confirmed signal from master, in base 10
int intms = 0;    // Stores (temporary) base 10 representation of signal from master
int calT=1000;     // Calibration time constant - delay in 
int timeoutss = 0; // Timeout in pHRead
long sigthreshold = 200;    // Threshold for consecutive readings to be considered a solid command from master
                            // Threshold of 200 is ~5ms of signal based on empirical timing tests

File dataFile;      // File object for SD card

////////////////
void setup() {
  Serial.begin(9600);                                 //set baud rate for the hardware serial port_0 to 9600, debugging
  myserial.begin(9600);                               //set baud rate for the software serial port to 9600
  
  clock.begin();                                      // Start clock
  //clock.setDateTime(__DATE__, __TIME__);              // Function to update time, do not upload permanently - only
                                                        // upload when clock needs readjustment (then comment out again)

  inputstring.reserve(10);                            //set aside some bytes for receiving data from the PC
  sensorstring.reserve(30);                           //set aside some bytes for receiving data from Atlas Scientific product

  pinMode(A0,INPUT);          // To receive input from the master
  pinMode(A1,INPUT);
  pinMode(A2,INPUT);
  initializeSD();             // Initializes SD card

  createFileName();           // Determines file name and creates file if not existing

  delay(1000);
  myserial.print("RESPONSE,0\r");   // Responses (like *OK) are turned OFF from Atlas Scientific
  delay(1000);
  myserial.print("C,0\r");          // Continuous mode for Atlas Scientific OFF
  delay(2000);
  pHRead();                         // One READ command to clear *ER output
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void loop(){

  binout = 0;             // Sets signal output to master to 0
  ms1 = digitalRead(A0);   // Reads master signal incoming on some pins
  ms2 = digitalRead(A1);
  ms3 = digitalRead(A2);

  intms = ms1*4+ms2*2+ms3;  // Base 10 representation of binary signal from master. ms1 is MSB, ms3 is LSB.

  // The below IF statement stack makes sure that there is a solid, meaningful signal from the master to
  // perform a task. If there is some non-zero signal, the slave checks that the SAME signal is 
  // sent for consecutive cycles in time before deciding that the signal is indeed a real signal, and only
  // then does it assign this signal to the variable binin.
  if (intms != 0)               // If there is some non-zero signal from the master, enter stack
  {
    if (intms == tempintms)     // If the current signal was the same as the last one from last cycle
    {
      tempcount = tempcount + 1;    // Increment counter by 1
      if (tempcount > sigthreshold)      // If the counter is greater than some threshold
      {
        binin = intms;          // Set binin to the current measured signal from the master
      }
    }
    tempintms = intms;          // Set current signal as temporary signal, for comparisons
  }
  else    // If no signal detected, set tempcount to 0
  {
    tempcount = 0;
  }
  
  // If binin is 100=4, 101=5, 110=6, or 111=7, then perform calibrations or readings
  switch (binin) {
    case 4:
      pHCal7();     // Calibration or reading function
      binout = 1;   // Tells master that slave is done, and to bring master signal to 000
      delay(calT);   // Delay to allow time for master to bring signal to 000
      binin = 0;    // Set binin back to 0 to ensure that an action only happens once per master command
      tempcount = 0;
      Serial.println("Cal 7");
      break;
    case 5:
      pHCal4();
      binout = 1;
      delay(calT);
      binin = 0;
      tempcount = 0;
      Serial.println("Cal 4");
      break;
    case 6:
      pHCal10();
      binout = 1;
      delay(calT);
      binin = 0;
      tempcount = 0;
      Serial.println("Cal 10");
      break;
    case 7:
      pHRead();
      binout = 1;
      delay(calT);
      binin = 0;
      tempcount = 0;
      break;
  }

}

void pHCal7() // All calibration functions will be very similar
{
  myserial.print("Cal,mid,7.00\r");
  pHRead();
}

void pHCal4() // All calibration functions will be very similar
{
  myserial.print("Cal,low,4.00\r");
  pHRead();
}

void pHCal10() // All calibration functions will be very similar
{
  myserial.print("Cal,high,10.00\r");
  pHRead();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void serialEvent() {                                  //if the hardware serial port_0 receives a char
  inputstring = Serial.readStringUntil(13);                                   //read the string until we see a <CR>
  input_string_complete = true;                       //set the flag used to tell if we have received a completed string from the PC
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void pHRead() {                                     //here we go...
  myserial.print("R\r");    // Atlas amplifier only sends when requested with R
  timeoutss = 0;
  while ((sensor_string_complete == false) && (timeoutss < 320000)) {
    if (myserial.available() > 0) {                     //if we see that the Atlas Scientific product has sent a character
      char inchar = (char)myserial.read();              //get the char we just received
      
      if (inchar == '\r') {                             //if the incoming character is a <CR>
        sensor_string_complete = true;                  //set the flag
      }
      else{
        sensorstring += inchar;                               //add the char to the var called sensorstring
      }
    } 
    timeoutss = timeoutss + 1;
  }

  if (timeoutss >= 320000) {
    Serial.println("TIMEOUT");
  }
    
  if (input_string_complete) {                        //if a string from the PC has been received in its entirety
    myserial.print(inputstring);                      //send that string to the Atlas Scientific product
    myserial.print('\r');                             //add a <CR> to the end of the string
    inputstring = "";                                 //clear the string
    input_string_complete = false;                    //reset the flag used to tell if we have received a completed string from the PC
  }

  if (sensor_string_complete == true) {                     //if a string from the Atlas Scientific product has been received in its entirety
    dataFile=SD.open(fileName,FILE_WRITE);

    if(dataFile==false){
      Serial.println("error opening .txt file");
      initializeSD();
    }
    else{
      Serial.print(sensorstring);                           //send that string to the PC's serial monitor
      Serial.print(",");
      dataFile.print(sensorstring);
      dataFile.print(",");
      getTime();                                            //print out time to SD
      dataFile.close();
    }
      sensorstring = "";                                    //clear the string
      sensor_string_complete = false;                       //reset the flag used to tell if we have received a completed string from the Atlas Scientific product

  }
}
////////////////////////////////////////////
void listenAS() {
  
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void getTime(){                                            //function to calculate time elapsed

   dt=clock.getDateTime();
      
   dataFile.print(dt.hour);    dataFile.print(":");
   dataFile.print(dt.minute);  dataFile.print(":");
   dataFile.print(dt.second);
   dataFile.print(",");
   dataFile.print(dt.year);    dataFile.print("-");
   dataFile.print(dt.month);   dataFile.print("-");
   dataFile.println(dt.day);   

   Serial.print(dt.hour);    Serial.print(":");
   Serial.print(dt.minute);  Serial.print(":");
   Serial.print(dt.second);
   Serial.print(",");
   Serial.print(dt.year);    Serial.print("-");
   Serial.print(dt.month);   Serial.print("-");
   Serial.println(dt.day); 
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void initializeSD(){                                        //initilize SD card, use so that if the SD catd is removed, 
  Serial.print("Initializing SD card...");                  //this function can be called to try and reinitialize the card
  
  if (!SD.begin(chipSelect)) {                              // see if the card is present and can be initialized:
    Serial.println("Card failed, or not present");
    return;
  }
  Serial.println("Card initialized.");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void createFileName(){                                      //create a fileName for the txt file
  dt=clock.getDateTime();
  
  fileName += dt.month;
  fileName += dt.day;
  fileName += dt.hour;
  if(dt.minute<10){
    fileName +="0";
  }
  fileName += dt.minute;
  fileName += ".txt";
}
