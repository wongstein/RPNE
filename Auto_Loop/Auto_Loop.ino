/*
pH Automation Code
RPNE Technologies
11-20-16

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

//CNC and Sample Layout Consants
const float xDis = 38.27;//Units(mm)       //Spacing of vials in X direction
const float yDis = 38.27;//Units(mm)       //Spacing of vials in Y direction
const int nX = 7;                       //Number of vials in the X direction
const int nY = 3;                       //Number of vials in the Y direction
const float xShift = 17.5;//Units(mm)   //Shifted measurement of odd numbered Y rows

const int xyRes = 40;//(steps/mm)       //Resolution constant value of the X and Y axis

const int db = 10;                      //Limit switch debounce constant  
const int rt = 10;                    //Reading time for probe
const int spd = 300;                    //Winding energization pause time (x and y), related to motor speed
                                        //Value in microseconds, smaller values = faster motor speed
const int zspd = 400;                 // Speed of z axis
                                                                        
//Note: For X & Y, 1400 steps = 35 mm => 40 steps/mm
int X = xyRes*xDis;                   //Number of steps between vials in X direction
int Y = xyRes*yDis;                   //Number of steps between vials in Y direction

const int Z = 13000;                      // Distance that probe moves up/down upon a reading

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

int starttime = 0;      // Keeps track of start and end times of the z-axis 'swish' loop
int endtime = 0;
long swishtime = 60000;   // Swishes probe tip around for this many ms

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
/*  moveXY(2,2);
  moveXY(6,5);
  moveXY(6,1);
  moveXY(1,0);
  delay(10000);*/
  calProbe();                           //Calibrates probe
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void loop() {
  while(digitalRead(xl) == HIGH || digitalRead(yl) == HIGH || digitalRead(zl) == HIGH){
    for(int c=0; c < nX*nY; c++){
      washProbe();                                                                                                                           
      moveTo(xDes,yDes);              //Move to current sample
      dunkProbe(1,1,1);               //Dunk probe and take reading                                                                                                                                                                                   //or do something like in lines 166-168
      nextSample();                   //Get Position of the next sample
    }
    storeProbe(60);                   //Stores probe between cycles (arg of 1 = 1 sec of storage)
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/*void moveTo(int A, int B){        //This function/method moves the x & y axis
  while(xPos != A){               //based on the position of the nextSample
    if(xPos <= A){                //and the X & Y interval
      xPos++;
      moveX((int)X,spd);                // Rounds X # steps?                                                            //type cast; float to int
    }
    if(xPos > A){
      xPos--;
      moveX((int)-1*X,spd);                                                                         //same here
    }
  }
  while(yPos != B){
    if(yPos <= B){
      yPos++;
      moveY((int)Y,spd);                                                                            //here
    }
    if(yPos > B){
      yPos--;
      moveY((int)-1*Y,spd);                                                                        //and here
    }
  }

}
*/
////////////////////////////////////////////////////////////////////////////////////////////////////
void moveX(int i, int p){               //Sends command to gshield to move X axis
  if(i<0){
    digitalWrite(xpd,LOW);
  }
  else{
    digitalWrite(xpd,HIGH);
  }
  for (int c=0; c < abs(i); c++) {
    digitalWrite(xp, HIGH);
    delayMicroseconds(p);
    digitalWrite(xp, LOW); 
    delayMicroseconds(p); 
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void moveY(int i, int p){                //Sends command to gshield to move Y axis
  if(i<0){
    digitalWrite(ypd,LOW);
  }
  else{
    digitalWrite(ypd,HIGH);
  }

  for (int c=0; c < abs(i); c++) {
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

  for (int c=0; c < abs(i); c++) {
    digitalWrite(zp, HIGH);
    delayMicroseconds(p);
    digitalWrite(zp, LOW); 
    delayMicroseconds(p); 
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void nextSample(){                //This function/method calculates the positon of the next sample
  xDes++;
  if(xDes > nX){
    xDes = 1;
    yDes++;
  }
  if(yDes > nY){
    yDes = 1;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void findLimits(){                    // This function finds the 'home' position for the probe
  // Z Homing
  for (int c = 0; c < db; c++){   // Fast movement
    if(digitalRead(zl) == HIGH){
      c = 0;
    }
    moveZ(-5,500);
   }
  moveZ(1000,1000);
  for (int c = 0; c < db; c++){   // Slow movement, to allow for fine tuning of home position
    if(digitalRead(zl) == HIGH){
      c = 0;
    }
    moveZ(-1,1500);
   }
  moveZ(8000,500);
   
  // X Homing
  for (int c = 0; c < db; c++){
    if(digitalRead(xl) == HIGH){
      c = 0;
    }
    moveX(-5,500);
  }
  moveX(500,2000);
  
  for (int c = 0; c < db; c++){
    if(digitalRead(xl) == HIGH){
      c = 0;
    }
    moveX(-1,3000);
   }
  moveX(1050,1500);

 // Y homing
  for (int c = 0; c < db; c++){
    if(digitalRead(yl) == HIGH){
      c = 0;
    }
    moveY(-5,500);
   }
  moveY(500,2000);
  
  for (int c = 0; c < db; c++){
    if(digitalRead(yl) == HIGH){
      c = 0;
    }
    moveY(-1,3000);
   }
  moveY(1050,1500); 
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
void calProbe(){                                    
  washProbe();
  moveTo(xCal7,yCal7);
  dunkProbe(1,0,0);         // Cal 7 = 100 to slave
  
  washProbe();
  moveTo(xCal4,yCal4);
  dunkProbe(1,0,1);         // Cal 4 = 101
  
  washProbe();
  moveTo(xCal10,yCal10);
  dunkProbe(1,1,0);         // Cal 10 = 110
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sends set of signals to slave (111, 110, 101, or 100)
void masterSlaveWrite(bool x,bool y,bool z){
  digitalWrite(A0,x);
  digitalWrite(A1,y);
  digitalWrite(A2,z);  
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
// Function dunks the probe and writes signals to the pH circuit, then waits for a response before 
// reseting outputs and rising again. Note that if ABC = 000, there no swish occurs
void dunkProbe(bool A,bool B,bool C){               
  moveZ(Z,zspd);
  if (A != 0 || B !=0 || C != 0) {      // Delay of 30 seconds while swishing up/down probe tip in solution to 'equilibrate' it faster
    starttime = millis();
    endtime = starttime;
    while (((endtime - starttime) <= swishtime) && ((endtime - starttime) >= 0)) // do this loop for 30s
    {
      moveZ(-200,zspd);     // Moves the probe up just a little
      delay(500);
      moveZ(200,zspd);      // Moves the probe back down just a little
      delay(500);
      endtime = millis();   // Updates the current 'time'
    }
  }
  else{
    
  }
  //delay(1000*30);                                      // Delay to allow probe to equilibrate with surrounding media
  masterSlaveWrite(A,B,C);                           
  waitForSlave();                                   
  masterSlaveWrite(0,0,0);                          //Calling the function with arguments (0,0,0) will dunk without a delay
  delay(1000);                                    //Probe read time
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
  int xc=0; 
  int yc=0;
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
  
/*  if(A-Pos[0]>B-Pos[1]){
    xSpd = spd;
    ySpd = spd*(A-Pos[0])/(B-Pos[1]);
  }
  else{
    ySpd = spd;
    xSpd = spd*(B-Pos[1])/(A-Pos[0]);
  }
  Serial.println(xSpd);*/


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


