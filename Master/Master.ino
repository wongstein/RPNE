/*
pH Automation Code
RPNE Technologies
12-10-16

//Stuff changed/fixwd from version Auto_Loop_12_3_DG
added simultaneous x and y homing (z homing is left to be done first for safety concerns, i.e. a probe inside a vial)
added a timeout for the z homing in case it gets stuck
added parameter of number of vials so that the machine doesn't try to finish off a row with no vials
increased x and y speed from 300 to 250


This is a test for measuring vials across an array for the pH project sketch.
This program assumes that the vials (including the tub and sponge) are in an array.
The probe is moved along the x axis and y axis independently between positions.
The order of operation is 1) the tub, 2) the sponge, and 3) the nth vial, where n increments and
represents the index of the presently 'selected' vial to read.

PHYSICAL ARRAY LAYOUT ASSUMPTION

  
  nY |     (1,nY)        (2,nY)     ...     (nX,nY)               Tub position = (1,0)
     |                                                            Sponge position = (nX,0)
  ...|      ...            ...      ...       ...
     |
  3  |  (1+xShift,1)  (2+xShift,1)  ...  (nX+xShift,1)
     |
  2  |      (1,2)         (2,2)     ...     (nX,2)
     |
  1  |  (1+xShift,1)  (2+xShift,1)  ...  (nX+xShift,1)
     |
  0  |      (1,0)         (2,0)     ...     (nX,0)
      --------------------------------------------------
              1     `       2       ...       nX

Key to pH Slave:

A0 A1 A2
1  0  0   = pH 7 Calibration
1  0  1   = pH 4 Calibration
1  1  0   = pH 10 Calibration
1  1  1   = pH Read and Datalog

*/

//CNC and Sample Layout Constants
const float xDis = 38.27;//Units(mm)       //Spacing of vials in X direction
const float yDis = 38.27;//Units(mm)       //Spacing of vials in Y direction
const int xyRes = 40;//(steps/mm)       //Resolution constant value of the X and Y axis
//Note: For X & Y, 1400 steps = 35 mm => 40 steps/mm
int X = xyRes*xDis;                   //Number of steps between vials in X direction
int Y = xyRes*yDis;                   //Number of steps between vials in Y direction

const int nX = 7;                     //Number of vials in the X direction
const int nY = 3;                     //Number of vials in the Y direction
const int numVials = 3;               // Number of vials in array (before storing)
int count = 0;                        //Counter to keep track of the number of vials

const int db = 10;                    //Limit switch debounce constant  
const int rt = 10;                    //Reading time for probe
const int spd = 250;                  //Winding energization pause time (x and y), related to motor speed
                                      //Value in microseconds, smaller values = faster motor speed
const int zspd = 400;                 // Speed of z axis

const int Z = 100;//13000;                      // Distance that probe moves up/down upon a reading

boolean shifted = false;                //Parameter to check if x axis has been shifted

// Units of vial index, (1,0) is 'first' vial
int xPos = 1; int yPos = 0;             //Position of the probe and initial position
int xDes = 1; int yDes = 1;             //Position of the sample
const int xTub = 1; const int yTub = 0;                     //Position of tub
const int xSpg = 2; const int ySpg = 0;                     //Position of sponge
const int xCal7 = 3; const int yCal7 = 0;                   //Position of calibration solution vials
const int xCal4 = 4; const int yCal4 = 0;
const int xCal10 = 5; const int yCal10 = 0;
const int xStrg = 7; const int yStrg = 0;                   //Position of storage solutions
int Pos[] = {xPos, yPos};
int startTime = 0;      // Keeps track of start and end times of the z-axis 'swish' loop
int currentTime = 0;
long swishtime = 1;//60000;   // Swishes probe tip around for this many ms
  
// CONSTANTS AS PER WIRING ON BOARDS, DO NOT CHANGE
 const int xp = 2;                      //X position pin #
 const int yp = 3;                      //Y position pin #
 const int zp = 4;                      //Z position pin #
 const int xpd = 5;                     //X direction pin #
 const int ypd = 6;                     //Y direction pin # 
 const int zpd = 7;                     //Z direction pin #
 const int xl = 9;                      //X limit switch pin #
 const int yl = 10;                     //Y limit switch pin #
 const int zl = 12;                     //Z limit switch pin #
                            

////////////////////////////////////////////////////////////////////////////////////////////////////
void setup() {
  // Serial.begin(9600);
  pinMode(8,OUTPUT);                    //'Enable' for motors at 'low' on pin 8
  digitalWrite(8,LOW);
  
  pinMode(xp,OUTPUT);                   //Pin for X steps
  pinMode(xpd,OUTPUT);                  //Pin for X direction: LOW=-X HIGH=+X
  pinMode(yp,OUTPUT);                   //Pin for Y steps
  pinMode(ypd,OUTPUT);                  //Pin for Y direction: LOW=-X HIGH=+X
  pinMode(zp,OUTPUT);                   //Pin for Z steps
  pinMode(zpd,OUTPUT);                  //Pin for Z direction: LOW=-X HIGH=+X
  pinMode(xl,INPUT_PULLUP);             //Pin for X limit switch: LOW = depressed
  pinMode(yl,INPUT_PULLUP);             //Pin for Y limit switch: LOW = depressed
  pinMode(zl,INPUT_PULLUP);             //Pin for Z limit switch: LOW = depressed
  
  pinMode(A0,OUTPUT);
  pinMode(A1,OUTPUT);
  pinMode(A2,OUTPUT);
  pinMode(A3,INPUT_PULLUP);

  digitalWrite(xp, LOW);                //initialize all pins to logic 0
  digitalWrite(xpd, LOW);
  digitalWrite(yp, LOW);
  digitalWrite(ypd, LOW);
  digitalWrite(zp, LOW);                   
  digitalWrite(zpd, LOW);
  findLimits();                         //Return probe to the origin

  seven_four(true);                           //Calibrates probe at 4 and 7
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void loop() {
  while(digitalRead(xl) == HIGH || digitalRead(yl) == HIGH || digitalRead(zl) == HIGH){
    for(int c=0; c < numVials; c++){
      washProbe();                                                                                                                           
      moveTo(xDes,yDes);              //Move to current sample
      dunkProbe(1,1,1);               //Dunk probe and take reading              //or do something like in lines 166-168
      nextSample();                   //Get Position of the next sample
    }
    seven_four(false);
    storeProbe(10);     //Stores probe between cycles (arg of 1 = 1 sec of storage)
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Function dunks the probe and writes signals to the pH circuit, then waits for a response before 
// reseting outputs and rising again. Note that if ABC = 000, there no swish occurs
void dunkProbe(bool A,bool B,bool C){               
  moveZ(Z,zspd);
  if (A != 0 || B !=0 || C != 0) {      // Delay of 30 seconds while swishing up/down probe tip in solution to 'equilibrate' it faster
    startTime = millis();
    currentTime = startTime;
    while (((currentTime - startTime) <= swishtime) && ((currentTime - startTime) >= 0)) // do this loop for 30s
    {
      moveZ(-200,zspd);     // Moves the probe up just a little
      delay(500);
      moveZ(200,zspd);      // Moves the probe back down just a little
      delay(500);
      currentTime = millis();   // Updates the current 'time'
    }
  }
  else{
    
  }
  //delay(1000*30);                                      // Delay to allow probe to equilibrate with surrounding media
  masterSlaveWrite(A,B,C);                           
  waitForSlave();                                   
  masterSlaveWrite(0,0,0);                          // Writes 0s back to line
  delay(2000);                                    //Probe read time
  moveZ(-1*Z,zspd);                                  
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void storeProbe(int st){                                  //Function stores the probe for a specified 
  moveTo(xStrg,yStrg);                                    //length of time in seconds
  moveZ(Z,zspd);
  delay(st*1000);
  moveZ(-1*Z,zspd);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void moveTo(int A, int B){
  int xc = 0; 
  int yc = 0;
  int xSpd;
  int ySpd;
  int S;
  if(Pos[0] < A){
    digitalWrite(xpd, HIGH);
  }
  else{
    digitalWrite(xpd, LOW);
  }
  if(Pos[1] < B){
    digitalWrite(ypd, HIGH);
  }
  else{
    digitalWrite(ypd, LOW);
  }

  while(Pos[0]!=A || Pos[1]!=B){
    if(Pos[0]!=A && Pos[1]!=B){
      S=0.5*spd;
    }
    else{
      S=spd;
    }
    if(Pos[0]!=A){
      digitalWrite(xp, HIGH);
      delayMicroseconds(S);
      digitalWrite(xp, LOW); 
      delayMicroseconds(S);
      xc++;
    }
    if(Pos[1]!=B){
      digitalWrite(yp, HIGH);
      delayMicroseconds(S);
      digitalWrite(yp, LOW); 
      delayMicroseconds(S);
      yc++;
    }

    if(xc==X){
      if(Pos[0] < A){
        Pos[0]++;
      }
      if(Pos[0] > A){
        Pos[0]--;
      }      
      xc=0;
    }
    if(yc==Y){
      if(Pos[1] < B){
        Pos[1]++;
      }
      if(Pos[1] > B){
        Pos[1]--;
      }   
      yc = 0;
    }
  }
}

void findLimits(){
  int S;
  int zTimeout = 30000;
  
  startTime = millis();
  
  for (int c = 0; c < db; c++){
    if(digitalRead(zl) == HIGH){
      c = 0;
    }
    moveZ(-5,500);
    currentTime = millis();        //Updates "currentTime"
    if((currentTime - startTime) >= zTimeout) {     //If elapsed time exceeds 30 sec, move down and start again
      moveZ(600, 500);
    }
  }
  moveZ(1000,1000);
  for (int c = 0; c < db; c++){   // Slow movement, to allow for fine tuning of home position
    if(digitalRead(zl) == HIGH){
      c = 0;
    }
    moveZ(-1,1500);
  }
  moveZ(8000,500);

  digitalWrite(xpd,LOW);
  digitalWrite(ypd,LOW);

  while(digitalRead(xl) == HIGH || digitalRead(yl) == HIGH){
    if(digitalRead(xl) == HIGH){
      digitalWrite(xp, HIGH);
      delayMicroseconds(S);
      digitalWrite(xp, LOW); 
      delayMicroseconds(spd);
    }
    if(digitalRead(yl) == HIGH){
      digitalWrite(yp, HIGH);
      delayMicroseconds(S);
      digitalWrite(yp, LOW); 
      delayMicroseconds(spd);
    }
    if(digitalRead(xl) == LOW || digitalRead(yl) == LOW){
      S = 2*300;
    }
    else{
      S = 300;
    } 
  }

  digitalWrite(xpd, HIGH);
  digitalWrite(ypd, HIGH);  

  for(int i = 0; i < 250;i++){
    digitalWrite(xp, HIGH);
    delayMicroseconds(S);
    digitalWrite(xp, LOW); 
    delayMicroseconds(spd);
    digitalWrite(yp, HIGH);
    delayMicroseconds(S);
    digitalWrite(yp, LOW); 
    delayMicroseconds(spd);
  }

  digitalWrite(xpd, LOW);
  digitalWrite(ypd, LOW);  

  while(digitalRead(xl) == HIGH || digitalRead(yl) == HIGH){
    if(digitalRead(xl) == HIGH){
      digitalWrite(xp, HIGH);
      delayMicroseconds(S);
      digitalWrite(xp, LOW); 
      delayMicroseconds(S);
    }
    if(digitalRead(yl) == HIGH){
      digitalWrite(yp, HIGH);
      delayMicroseconds(S);
      digitalWrite(yp, LOW); 
      delayMicroseconds(S);
    }  
  }

  moveX(1050, spd);
  moveY(1050, spd);
  
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sends set of signals to slave (111, 110, 101, or 100)
void masterSlaveWrite(bool x, bool y, bool z){
  digitalWrite(A0,x);
  digitalWrite(A1,y);
  digitalWrite(A2,z);  
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void moveX(int i, int p){               //Sends command to gshield to move X axis
  if(i < 0){
    digitalWrite(xpd,LOW);
  }
  else{
    digitalWrite(xpd,HIGH);
  }
  for (int c = 0; c < abs(i); c++) {
    digitalWrite(xp, HIGH);
    delayMicroseconds(p);
    digitalWrite(xp, LOW); 
    delayMicroseconds(p); 
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void moveY(int i, int p){                //Sends command to gshield to move Y axis
  if(i < 0){
    digitalWrite(ypd,LOW);
  }
  else{
    digitalWrite(ypd,HIGH);
  }

  for (int c = 0; c < abs(i); c++) {
    digitalWrite(yp, HIGH);
    delayMicroseconds(p);
    digitalWrite(yp, LOW); 
    delayMicroseconds(p); 
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void moveZ(int i, int p){                //Sends command to gshield to move Z axis
  if(i<0){
    digitalWrite(zpd,LOW);
  }
  else{
    digitalWrite(zpd,HIGH);
  }

  for (int c = 0; c < abs(i); c++) {
    digitalWrite(zp, HIGH);
    delayMicroseconds(p);
    digitalWrite(zp, LOW); 
    delayMicroseconds(p); 
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void nextSample(){                //This function/method calculates the positon of the next sample
  xDes++;
  count++;
  if(xDes > nX){
    xDes = 1;
    yDes++;
  }

  if(yDes == nY){
   yDes = 1;
   xDes = 1;
 }

 if(count == numVials){
  xDes = 1;
  yDes = 1;
  count = 0;
 }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Is designed to wait for a signal from slave (signifying completion of reading) to continue moving
// For now, it is only a short delay as a placeholder, and actually determines the master-to-slave signal
// length of time
void waitForSlave(){                                
delay(rt);

/*  
  delay(500);
  while(digitalRead(A3) = 0){
    delay(500);
  }
*/
}


////////////////////////////////////////////////////////////////////////////////////////////////////
void washProbe(){                                   //Function washes and dries probe
  moveTo(xTub,yTub);
  dunkProbe(0,0,0);
  moveTo(xSpg,ySpg);
  dunkProbe(0,0,0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//Calibration of probe to a pH of 7, then 4, then 10. The probe is washed and dried after every calibration.
//calibration: true if this is a calibration, false if this is a final reading
void seven_four(boolean calibration){                                    
  washProbe();
  moveTo(xCal7,yCal7);
  if(calibration == true){dunkProbe(1,0,0); }
  else{dunkProbe(1,1,1); };
          // Cal 7 = 100 to slave
 
  washProbe();
  moveTo(xCal4,yCal4);
  if(calibration == true){dunkProbe(1,0,1); }
  else{dunkProbe(1,1,1); };
           // Cal 4 = 101
}
