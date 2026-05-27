/
#include "DHT.h"
#include "RTClib.h"
//web server related
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Hash.h>

#include <Adafruit_Sensor.h>
#include <FS.h> 
//end web server related
//functional ok but sensor work gets delayed web works ok 2605171445

//ntp related   google: esp8266 wifi ds3231 time sync 2605182234
#include <Wire.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
const long utcOffsetInSeconds = 0; // Adjust for your timezone

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

//ntp time display related
char timestr[32];
byte last_second, second_, minute_, hour_, day_, month_;
int year_;
//end ntp time display related


//end npt related 


#define DHTPIN 14 //(D5  as we usen't SPI in this example
#define DHTTYPE DHT22   // Define the sensor type
#define BUILTIN_LED D4  //builtin LED
#define BUZZER D6
/*
from g: esp82656 pins
D0: 16 GPIO16 (Deep Sleep 깨우기용)
D1: 5  GPIO5 (I2C SCL)
D2: 4  GPIO4 (I2C SDA)  //D1 D2 FOR 3231 AND SSD1306
D3: 0 X GPIO0 (Flash 버튼, Pull-up) UNUSEABLE
D4: 2 GPIO2 (Built-in LED)   
D5: 14  GPIO14 (SPI SCK) -> DHT22
D6: 12  GPIO12 (SPI MISO) -> DOOR OPEN BUZZER
D7: 13  GPIO13 (SPI MOSI) -> DOOR OPEN REED SWITCH INPUT 
D8: 15  GPIO15 (Pull-down)
NOTE:
Builtin LED is HIGH=OFF, LOW=ON and NOT the other way around!!  2605040820
*/

//ssd1306 related google: 
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64 // 0.91인치인 경우 32로 설정
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

DHT dht(DHTPIN, DHTTYPE);

RTC_DS3231 rtc;


WiFiServer server(80);

const char* strPCIP="mypcip";
char strLocalIP[16]="";
//cf google: arduino constant string

char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
char strSoilDoor[80]=""; //string for soil sensor and door open reed
char strDHT[80]=""; //DHT sensor value string

char c; //read from client

char rtcStr[64];

static char reedStr[16]="";
static char waterStr[16]="";

//temp vars
static char celsiusTemp[7];
static char fahrenheitTemp[7];
static char humidityTemp[7];

int potPin=D1;


int soil=0;//the only analog pin

//cached sensor values
float h=0;
float t=0;
float f=0;//fahrenheit

//int doorState=1;

int dht_count=0;

unsigned long lastLog=0;;  //flash log limiter to prevent flash burning 05061705  to 0 2605071447
//door too long open things 2605061720
#define DOOR_OPENED 0 // 0 for magnet south pole simulation, 1 for actual fridge
#define DOOR_CLOSED 1 // 1 for magnet south pole simulation, 0 for actual fridge
#define DOOR_TIMEOUT 15000  // 60 sec for door timeout

#define DOOR_TIMEOUT_TEST 30000 //

unsigned long doorOpenStart=0;
bool doorWasOpen=false;

unsigned long doorLastOpen=0;//last door open time 2605101455

int door_onoff=DOOR_CLOSED; //moved to global due to service function separation 2605201324


//new door related globals

char strDoorStatus[16]="";

char doorMsgs[3][16]={"DOOR CLOSED!!","DOOR OPEN!!","DOOR TIMEOUT!!"};
unsigned long lastDoorOpenedTime=0; //last time when door changed from closed to open

int doorState=DOOR_CLOSED; //value of digitalRead(D7),either reed or 3144 digital hall sensor
int previousDoorState=DOOR_CLOSED; //check logic  used to decide whether to send door state to pc 2605202103
bool doorWasOpened=false;  //init:false,if opened before: true, true->false: when door closed,false->true: door opened
bool doortoolongopen=false; //init:false, false->true: set to true if now-lastDoorOpenedTime>DOOR_TIMEOUT,true->false: door closed





unsigned long lastLEDTime=0; 

//added globals for drop in scheduler
unsigned long now;
unsigned long lastDHT=0;
unsigned long lastDisplay=0;
//unsigned long lastLog=0;
unsigned long lastDoorCheck=0; //reed or DIGITAL hall sensor 2605102022
unsigned long lastSoil=0; //last soil sensor read time 2605102033
unsigned long lastrtc=0; //last rtc check time 2605122001
unsigned long lastSerial=0; //last serial output time 2605200655
unsigned long lastbooz=0; //experimental seems to work 2605231`044
unsigned long lastbooz_50=0;
unsigned long lastbooz1=0; //experimental
//
unsigned long lastbooz1_500=0;
bool lastbooz1_500_endeded=true;
unsigned long lastbooz1_100_1=0;
bool lastbooz1_100_1_ended=true;
unsigned long lastbooz1_100_2=0;
bool lastbooz1_100_2_ended=true;
unsigned long lastbooz1_100_3=0;
bool lastbooz1_100_3_ended=true;


//intervals(in ms)
#define DOOR_CHECK_INTERVAL 500
#define WATER_INTERVAL 1000  //set water sensor interval HERE
//#define RTC_READ_INTERVAL 0  //ds3231 read interval
#define DHT_INTERVAL 2000 //dht22 interval is 2000 FIXED 2605201247
#define OLED_REFRESH_INTERVAL 500 //sd1306 refresh interval
#define LOG_SAVE_INTERVAL 60000  //60 sec flash log interval
#define SERIAL_PRINT_INTERVAL 5000  //serial print interval



char req[512];

void setup() {
  Serial.begin(115200);

  pinMode(D7, INPUT_PULLUP);  //d7 is now the reed sensor
  // pinMode(D1, INPUT_PULLUP); use analog soil sensor instead
  pinMode(D4,OUTPUT); //builtin LED
  digitalWrite(D4,HIGH);
  pinMode(D6,OUTPUT); //buzzer


   test1306();
   delay(3000);

  Serial.println("PIN TEST START");
  delay(2000);
  dht.begin();
  rtc_setup();


  //file system setup
  Serial.println();
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }
  
  //check file system before starting anything
  FSInfo fsInfo;
  SPIFFS.info(fsInfo);
  Serial.print("totalBytes: "); Serial.println(fsInfo.totalBytes);
  Serial.print("usedBytes: "); Serial.println(fsInfo.usedBytes);
  //listDir("/");
  delay(600);//file system ok 2605130820
  //wifi setup

  //connect to wifi
  const char* ssid = "myid"; // Replace with your WiFi SSID
  const char* password = "mypwd"; // Replace with your WiFi password
  WiFi.begin(ssid, password);


  int retries=0;
  while(WiFi.status()!=WL_CONNECTED && retries<20){
    delay(500);
    retries++;
  }

   
  
  Serial.println("");
  Serial.println("WiFi connected");
  
  // Starting the web server
  server.begin();
  Serial.println("Web server running. Waiting for the ESP IP...");
  //delay(10000);
  delay(3000);
  // Printing the ESP IP address
  Serial.print("Local IP of ESP8266 is: ");
  IPAddress local_ip=WiFi.localIP();
  Serial.println(WiFi.localIP());
  sprintf(strLocalIP, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
  Serial.println("**************************");
  booze();
  
  delay(3000);
  //end wifi connect

  //ntp related
   //rtc.begin already at rtc_setup  2605182241
   timeClient.begin();
   timeClient.update();
   // Set RTC to NTP time
   rtc.adjust(DateTime(timeClient.getEpochTime()));
   Serial.println("RTC time set to NPT time!");

    DateTime now1 = rtc.now();
    
    // show ntp time
      
  // Print time to Serial
   delay(1000);
   //end ntp related 2605182243
   
  
  lastDoorCheck=now;
  lastLog=now;

}//setup


//loop start

void loop() {
  //int door_onoff=1;//assume closed here
  door_onoff=DOOR_CLOSED;

  now = millis();

  // ⭐ ALWAYS FIRST: keep WiFi alive
  yield();

  // =========================
  // 1. HANDLE WEB (fast!)
  // =========================
  handleClient();   // your WiFiServer logic ONLY
  yield();


//2.doorcheck

  // =========================
  // 2. DOOR CHECK (fast)
  // =========================
  

//end 2.doorcheck

  if (now - lastDoorCheck > DOOR_CHECK_INTERVAL) {   // every 50ms   //500->1000ms
     lastDoorCheck=now;

    //start door status check ftn

    checkDoor();
    //end door open close logic
     
  }//every 500  for door open check

  
  // =========================
  // 3. DHT (slow sensor)
  // =========================
  if (now - lastDHT > DHT_INTERVAL) {   // every 2 sec
     lastDHT=now; //check this on all service functions 2605200000

    //start readDHT()
    readDHT();
    
    //end readDHT


  }//if now lastdht


  // =========================
  // 4. SOIL (analog)
  // =========================
  
  if (now - lastSoil > WATER_INTERVAL) {   // reuse timing
    lastSoil=now;

    //start readSoil
    readSoil();
   //end readSoil

  }//if now lastSoil
  

  if (now - lastrtc > 1000)
  {
    lastrtc = now;

    rtc_update();
  }
  // =========================
  // 5. DISPLAY (heavy)
  // =========================
  if (now - lastDisplay > OLED_REFRESH_INTERVAL) {
    lastDisplay = now;
   

   //start refresh_display()
    refresh_display();

    
    //end refresh_display()

  }//if now lastdisplay

  //readFile("/log.txt");//test only 2605130819

  // =========================
  // 6. LOGGING (slow)
  // =========================
  
  if (now - lastLog > LOG_SAVE_INTERVAL) {   // every 60 sec
    //booze1();  removed as write log is ok 2605132117
    lastLog = now;


    //Serial.println("Writing log data....");
    //delay(200);
   //logData(String(now) + "," + String(soil) + "," + String(t)+"\r\n");
   logDataOld(String(now) + "," + String(soil) + "," + String(t));

  }//if now lastlog
  //delay(100);//for test only 2605130825

   if(now-lastSerial>SERIAL_PRINT_INTERVAL)//output status msgs to serial port very 5 saecs

   {
    lastSerial=now;
    printSerial();
   }


  
  //rtc_update();
  yield();
 // Serial.println(rtcStr);
  //handleClient();
  yield();

}//loop
//end main loop


////////
// service functions
void checkDoor()//check fridge door status
{

//lastDoorCheck = now;
    //Serial.println("**** Door check!!! ****");
    int d = digitalRead(D7);
    /*
    if (d != doorState) {
      doorState = d;
     
    }
    */

    doorState=digitalRead(D7); //read either reed or 3144 digital hall sensor
    door_onoff=doorState;
    //start door open logic

     //door_onoff=digitalRead(D7);
    
     Serial.print(door_onoff);
 
  
   if(door_onoff==DOOR_OPENED)//door open , here: door open means magnet south pole passes the reed ,this is because of test conditions,TEST ON REAL FRIDGE!!
   {
        if(!doorWasOpen)
        {
            doorOpenStart=millis();
            doorWasOpen=true;
        }//if !doorWasOpen ->the door status changed from closed to open 2605192114

           //debug
        //if(doorWasOpen) Serial.println("doorWasOpen=true");//OK this works 2605070825

        //Serial.println("Door open!!!");
       
        strcpy(strDoorStatus,doorMsgs[1]); //door open
       
            //log door open to pc
            //send wifi only if state has changed 2605202022
            // THIS CAused MALFUNCTIONS!! 2605202136
            if (door_onoff != previousDoorState)
             {
               previousDoorState = door_onoff;
          
              WiFiClient client;
              if (client.connect(strPCIP, 5000)) //send message to pc server if connected
              { //aduito adjust to ACTUAL address 2605061936
                client.print("GET /alert?msg=door_open HTTP/1.1\r\n");
                client.print("Host: YOUR_PC_IP\r\n");
                client.print("Connection: close\r\n\r\n");
              }//if client
            }
    
        booze();  //door open sound & led
    

         //Now we change the algorithm so that notification is only given if 
         // trigger after 60 seconds
             if (millis() - doorOpenStart > DOOR_TIMEOUT_TEST && doorWasOpen==true) 
           {   //door was iopen fop  problem: this condition remains after door reset, next normal door open is 'too long' ///10000->15000
               //doorWasOpen remains true if it remained open after the last opening; becomes FALSE whenever closed again
               //OK problem solved 2605071040
               doortoolongopen=true;
               
               booze1();  //different sound
               //sendAlert("door_open_too_long");
              
               strcpy(strDoorStatus,doorMsgs[2]); //door timeout
        
               //actually does not matter much as only activated while rare door timeout condition
               // in which alarm would be required  2605291938
               //Door open timeout is a very abnormal and dangerous condition thus wifi message will continue until situation cleared 2605231931
                if (door_onoff != previousDoorState) //
                { //
                  // previousDoorState = door_onoff;

                  WiFiClient client;
                  //send door open too long msg to pc
                  if (client.connect(strPCIP, 5000)) 
                  {  //check PC address on your router admin menu!!
                      client.print("GET /alert?msg=door_open_too_long HTTP/1.1\r\n");
                      client.print("Host: 192.168.0.9\r\n");
                      //client.print(rtcStr+"\r\n");
                      client.print("Connection: close\r\n\r\n");
                  }//if client
                }//
              

                 
            }//if millis
            else 
            {
            //doorWasOpen = false;  This was incorrect logic as this would reset the doorwasopen condition each time
            }//else


    }//if door onoff door open condition
  
  //replace end  2605071241

   else  //door closed: door_onoff==1
   {
       doorWasOpen=false;
       doortoolongopen=false;//reset door open & door too long open flag if door is closed again
       digitalWrite(D4,HIGH);
       
       strcpy(strDoorStatus,doorMsgs[0]);//door closed

       //log doosed to pc
    
       //door closed message sent to network only if door open siatus changed  2605201916
          if(door_onoff!=previousDoorState)
          {
            previousDoorState=door_onoff;
           WiFiClient client;
           if (client.connect(strPCIP, 5000)) 
           {  //check PC address on your router admin menu!!
              client.print("GET /alert?msg=door_close HTTP/1.1\r\n");
              client.print("Host: pcip\r\n");
              client.print("Connection: close\r\n\r\n");
           }//if client
          }

    
    }//else (door closed)


    //end door open logic
   
    //Serial.print("reed=");
    //Serial.println(digitalRead(D7));
    //if(d==0) booze();
    

}//end checkDoor

void readDHT()   //read dht value each time
{
    //lastDHT = now;

    float nh = dht.readHumidity();
    float nt = dht.readTemperature();

    if (!isnan(nh) && !isnan(nt)) {
      h = nh;
      t = nt;
      //snprintf(humidityTemp,sizeof(humidityTemp),"%.2f\n",h);
      //snprintf(celsiusTemp,sizeof(celsiusTemp),"%.2f\n",t);
      snprintf(humidityTemp,sizeof(humidityTemp),"%.2f",h);
      snprintf(celsiusTemp,sizeof(celsiusTemp),"%.2f",t);



    }//if isnan

}//readDHT

void readSoil() //read water sensor from analoginput a0
{
 soil = analogRead(A0);


    //simple moving average
    soil = (soil * 3 + analogRead(A0)) / 4; //moving average 2605191021

  //int raw = analogRead(A0);

   Serial.print("RAW=");
   Serial.println(soil);
   snprintf(waterStr,sizeof(waterStr),"%3d",soil);
}//readSoil

//2605200010
void refresh_display()//refresh ssd1306 display with new values 2605200015
{//display formatttng ok for now 2605232025 don't change anything from now!!
    display.clearDisplay();
    //rtc_update(); cxhanged: called from main loop 2605201532  as a result rtc string became blank 2605202156
  
    display.setCursor(0,0);
    display.println(rtcStr);
 
    display.setCursor(0,20);
    display.print("T:");
    display.print(t);

    //display.setCursor(0,10);
    display.setCursor(50,20);
    display.print("C, H:");
    display.print(h);
    display.print(" %");

    //Change this: into a char[], T: (temp)+C,H:  % better visible 2605122051

    
    display.setCursor(0,30);//from 40
    display.print("Soil:");
    display.print(soil);
    display.setCursor(50,30);
    display.print(",L:");
    display.setCursor(58,30);
    display.print(strLocalIP);

    
    display.setCursor(0,40); //frm 50
    display.print("Hall:");
    //display.print("DR");
    display.print(digitalRead(D7));
    display.setCursor(32,40);
    display.print(",PC");
    display.setCursor(56,40);
    display.print(strPCIP);
    



    //display.setCursor(0,40);
    display.setCursor(0,50);//from 60
    display.print(strDoorStatus);
    

    

    display.display();

}//refresh_display  2605200018

void printSerial() //serial output every 5 seconds
{
  Serial.print("rtcStr=");
  Serial.print(rtcStr);
  Serial.print(",  DOOR= ");
  Serial.print(strDoorStatus);
  Serial.print(", Temp: ");
  Serial.print(celsiusTemp);
  Serial.print(",  Humid: ");
  Serial.print(humidityTemp);
  Serial.print(",  Water: ");
  Serial.println(waterStr);


}//end printserial 2605200921



void booze()
{
  // Play a 1000Hz tone for 500ms
  tone(BUZZER, 1000); 
  //delay(500);
  digitalWrite(D4,HIGH);
  delay(100); //2605052235 for now
  if(millis()-lastbooz>=100)
  {
    lastbooz=millis();
    digitalWrite(D4,LOW);
  // Stop the tone for 500ms
    noTone(BUZZER);
  //delay(500);
  }
  lastbooz_50=millis();
  if(millis()-lastbooz_50>=50)
  {
    lastbooz_50=millis();
    //delay(1);
  }
}//ok 2605059951

void booze1()//door open too long sound
{

  // Play a 1000Hz tone for 500ms
  tone(BUZZER, 2000); 
  //delay(500);
  digitalWrite(D4,HIGH);
  delay(100); //2605052235 for now
  tone(BUZZER, 1000); 
  digitalWrite(D4,LOW);
  delay(100);
  digitalWrite(D4,HIGH);
  tone(BUZZER, 500); 
  delay(100);
  digitalWrite(D4,LOW);
  // Stop the tone for 500ms
  noTone(BUZZER);
  //delay(500);

}//booze1



void test1306()
{
  /
 
// I2C 주소 0x3C (일반적인 0.96/0.91인치 주소)
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println("SSD1306 error!!");
    delay(1000);
    booze();
    delay(500);
    booze();  //pc bios style display error
  }//if display
  else{
  display.display();
  delay(1000);
  display.clearDisplay();
  
  display.setTextSize(1); //1->2 2605121834
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Hello, NodeMCU!");
  display.display();
  }//else

}//test1306

void rtc_setup()
{

 

  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    delay(1000);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, let's set the time!");
    
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    

    char strLostPower[80];//lost power string
    memset(strLostPower,0,sizeof(strLostPower)); //clear
    strcat(strLostPower,"BOOT LOSTPOWER");
    strcat(strLostPower,rtcStr);


    //logDataOld("BOOT LOSTPOWER,"+rtcStr);  //power loss log 2605071519
    logDataOld(strLostPower);
                                                                                                                                                                                                 
  }

  
 

}//rtc_setup

void rtc_update()
{
  
     DateTime now = rtc.now();  //Scope of this now is only in this rtc_update()


    snprintf(
        rtcStr,
        sizeof(rtcStr),
        "%s, %04d-%02d-%02d %02d:%02d:%02d %.2fC",
        daysOfTheWeek[now.dayOfTheWeek()],
        now.year(),
        now.month(),
        now.day(),
        now.hour(),
        now.minute(),
        now.second(),
        rtc.getTemperature()  //rtc temoerature has nothing to do with time
    );
   





 //}//if now lastrtc

}

void logDataOld(String line)
{
  File f=SPIFFS.open("/logold.txt","a");
  if(f)
  {
     //important: limit log size
    if (f.size() > 50000) {
    SPIFFS.remove("/logold.txt");
      }
    f.println(line);//what does it print on the file?
    Serial.print("Log data: ");
    Serial.println(line);
    delay(100);
    f.close();
  }//if f
  else
  {
    Serial.println("SPIFFS error!!");
  }
}//logdata

//additional file utility functions replace with your own made ones!!

//
  void handleStatus(WiFiClient &client)
{
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();

    client.println("<html><body>");
    client.println("<h1>ESP8266 STATUS</h1>");

    client.println("<br>");
    client.println(rtcStr);

    client.println("<br>");
    //client.println(reedStr);
    //client.println(strDoorStatus);

    //client.println("<br>");
    //client.println(waterStr);  //duplicate removed 


    client.println("<h1>ESP8266 - DHT22 Temperature and Humidity</h1><h3>Temperature in Celsius: ");
    client.println(celsiusTemp);
    client.println("*C</h3><h3>Temperature in Fahrenheit: ");
    client.println(fahrenheitTemp);
    client.println("*F</h3><h3>Humidity: ");
    client.println(humidityTemp);
    client.println("%</h3><h3>");
    client.println("<br>");
    client.println(rtcStr);
    client.println("<br>");

    
    client.println(strDoorStatus);
    client.println("<br>");
    client.println(waterStr);


    client.println("</body></html>");
}//handleStatus

void handleLogs(WiFiClient &client)
{
    uint8_t buf[80];
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();

    File f = SPIFFS.open("/log.txt","r");

    if(f)
    {
        while(f.available())
        {  
            int len=f.read(buf,sizeof(buf));
            //client.write(f.read());
            client.write(buf,len);
            yield(); //required 2605071853
        }

        f.close();
    }
    else
    {
        client.println("No log file");
    }
}//handleLogs

void handleRootOld(WiFiClient &client)
{
   client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();

    client.println("<html><body>");
    client.println("<h1>ESP8266 STATUS</h1>");

    client.println("<br>");
    client.println(rtcStr);

    client.println("<br>");
    //client.println(reedStr);
    client.println(strDoorStatus);

    client.println("<br>");
    client.println(waterStr);

    client.println("</body></html>");

}

void handleRoot(WiFiClient &client)
{
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();

  client.println("<html><body>");
  client.println("<h1>ESP8266</h1>");
  client.println("</body></html>");
}


void sendAlert(String msg)
{
  WiFiClient client;
  if(client.connect(strPCIP,5000)){  //ur pc ip
    client.print("GET /alert?msg=" + msg + " HTTP/1.1\r\n");
    client.print("Host: pc ip\r\n");
    client.print("Connection: close\r\n\r\n");

  }
}

void handleData(WiFiClient &client)
{
  File f=SPIFFS.open("/logold.txt","r");
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/plain");
  client.println("Connection: close");
  client.println();

  

  //smoother file handling
  

    if(f)
    {
        uint8_t buf[128];

        while(f.available())
        {
            int len = f.read(buf, sizeof(buf));
            client.write(buf, len);
            yield();
        }//while f available

        f.close();
    }//if f

  
}//handleData

//handleGraph gets NO data as it is ; thus only shows blank screen 2605130836
// Uncaught SyntaxError: Unexpected token '-' at f12-console 26051609598
void handleGraphOld(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();

  client.println(R"rawliteral(
  <html>
  <body>
  <canvas id="c"></canvas>
  <script>
  fetch('/data').then(r=>r.text()).then(t=>{
    console.log("ESP8266 LOG DATA: ");
    console.log(t);
    let lines = t.trim().split('\n').slice(-200);
    console.log(lines);
    let soil=[], temp=[];
    lines.forEach(l=>{
      colsole.log("LINE=",l);
      l=l.trim();
      if(l.length==0) return;
      let p=l.split(',');
      console.log(p);
      if(p.length>=3){
        soil.push(parseInt(p[1]));
        temp.push(parseFloat(p[2]));
           if(!isNaN(s) && !isNaN(tt)){
              soil.push(s);
              temp.push(tt);
              }

      }
    });

    let c=document.getElementById('c');
    c.width=320; c.height=160;
    let ctx=c.getContext('2d');

    function draw(arr, color, scale){
      ctx.beginPath();
      ctx.strokeStyle=color;
      arr.forEach((v,i)=>{
        //let x=i;
        let x-i*(320/arr.length);
        let y=160 - v*scale;
        if(i==0) ctx.moveTo(x,y);
        else ctx.lineTo(x,y);
      });
      ctx.stroke();
    }

    ctx.clearRect(0,0,320,160);
    draw(soil,'green',1);   // soil 0–100
    draw(temp,'red',2);     // temp scaled

  });
  </script>
  </body>
  </html>
  )rawliteral");
}//handleGraph

/*
void handleGraph(WiFiClient &client)
{
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();

  client.println("<html>");
  client.println("<body>");
  client.println("<h1>GRAPH TEST</h1>");

  client.println("<script>");

  client.println("fetch('/data')");
  client.println(".then(r => r.text())");
  client.println(".then(t => {");
  client.println("  console.log(t);");
  client.println("});");

  client.println("</script>");
  client.println("</body>");
  client.println("</html>");

}
//ok graph tesht, data:2730978,666,27.50 2791655,668,27.50 2852329,668,27.50 2913006,662,27.50 2973678,667,27.60 3034355,666,27.60 3095029,663,27.70 60848,754,27.90 123836,756,27.90 183898,756,27.90 247041,754,27.90 307774,756,27.90 368496,754,28.00 429219,754,28.00 62518,754,28.00 123249,754,28.00 183941,756,27.90 244606,758,28.00
*/

void handleGraphOld1(WiFiClient &client)
{
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();

  client.println("<html>");
  client.println("<body>");
  client.println("<canvas id='c'></canvas>");

  client.println("<script>");

  client.println("fetch('/data')");
  client.println(".then(r => r.text())");
  client.println(".then(t => {");

  client.println("console.log(t);");

  client.println("let lines = t.trim().split('\\n');");

  client.println("let soil = [];");

  client.println("lines.forEach(l => {");
  client.println("  let p = l.split(',');");
  client.println("  if(p.length >= 3){");
  client.println("    soil.push(parseInt(p[1]));");
  client.println("  }");
  client.println("});");

  client.println("console.log(soil);");

  client.println("let c = document.getElementById('c');");
  client.println("c.width = 320;");
  client.println("c.height = 200;");

  client.println("let ctx = c.getContext('2d');");

  client.println("ctx.beginPath();");

  client.println("soil.forEach((v,i)=>{");

  client.println("let x = i * 2;");
  //client.println("let y = 200 - (v / 1024.0) * 200;");
  client.println("let min = Math.min(...soil);");
  client.println("let max = Math.max(...soil);");

  client.println("if(max == min) max = min + 1;");

  client.println("ctx.beginPath();");

  client.println("soil.forEach((v,i)=>{");

  client.println(" let x = i * 2; ");

  client.println("let y = 200 - ((v - min) / (max - min)) * 180;");

  client.println("if(i==0) ctx.moveTo(x,y);");
   
  client.println("else ctx.lineTo(x,y);");

client.println("});");

client.println("ctx.stroke();") ;


  client.println("if(i==0) ctx.moveTo(x,y);");
  client.println("else ctx.lineTo(x,y);");

  client.println("});");

  client.println("ctx.stroke();");

  client.println("});");

  client.println("</script>");
  client.println("</body>");
  client.println("</html>");
}

// handlegraph ok 2605161120
void handleGraphText(WiFiClient &client)
{

client.println("let min = Math.min(...soil);");
client.println("let max = Math.max(...soil);");

client.println("if(max == min) max = min + 1;");

client.println("ctx.beginPath();");

client.println("soil.forEach((v,i)=>{");

client.println("let x = i * 2;");

client.println("let y = 200 - ((v - min) / (max - min)) * 180;");

client.println("if(i==0) ctx.moveTo(x,y);");
client.println("else ctx.lineTo(x,y);");

client.println("});");

client.println("ctx.stroke();");

}


  void handleGraph(WiFiClient &client)
{
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();

  client.println("<html>");
  client.println("<body>");
  client.println("<canvas id='c'></canvas>");

  client.println("<script>");

  client.println("fetch('/data')");
  client.println(".then(r => r.text())");
  client.println(".then(t => {");

  client.println("console.log(t);");

  client.println("let lines = t.trim().split('\\n');");

  client.println("let soil = [];");

  client.println("lines.forEach(l => {");
  client.println("  let p = l.split(',');");
  client.println("  if(p.length >= 3){");
  client.println("    soil.push(parseInt(p[1]));");
  client.println("  }");
  client.println("});");

  client.println("console.log(soil);");

  client.println("let c = document.getElementById('c');");
  client.println("c.width = 320;");
  client.println("c.height = 200;");

  client.println("let ctx = c.getContext('2d');");

  // auto scaling
  client.println("let min = Math.min(...soil);");
  client.println("let max = Math.max(...soil);");

  client.println("if(max == min) max = min + 1;");

  // start graph
  client.println("ctx.beginPath();");

  client.println("soil.forEach((v,i)=>{");

  client.println("  let x = i * 2;");

  client.println("  let y = 200 - ((v - min) / (max - min)) * 180;");

  client.println("  if(i==0) ctx.moveTo(x,y);");
  client.println("  else ctx.lineTo(x,y);");

  client.println("});");

  client.println("ctx.stroke();");

  client.println("});");

  client.println("</script>");
  client.println("</body>");
  client.println("</html>");
}


void handleClient()
{//this part ok 2605130637
// Listenning for new clients
  //client handling  start

  WiFiClient client = server.available();
 
  if (client) {
    
    memset(req,0,sizeof(req));
    int idx=0;
    Serial.println("New client");
    // bolean to locate when the http request ends
    boolean blank_line = true;

   

    unsigned long timeout=millis();
    while (client.connected() && millis()-timeout<2000   ) 
    {  //2605061427
       yield(); // REQUIRED to prevent crash reboot 2605071852


      if (client.available())
      {
        
        c=client.read();
        

        
        if(idx<sizeof(req)-1)  //prevent crash 2605071810
        {
          req[idx++]=c;
          req[idx]='\0';
        }//if idx sizeof
        //End of HTTp headers
        

        if(strstr(req,"\r\n\r\n")){
          break;
        }//if strstr req null
        

        //sensor read move out
       


      }//if client available


    }  //while client connected

        //handle requests after loop
        Serial.println("REQ:");
        Serial.println(req);

        if (strstr(req,"GET /status")) 
        {
          handleStatus(client);
        }
        else if (strstr(req,"GET /graph")) 
        {
           handleGraph(client);
        }
        else if (strstr(req,"GET /data")) 
        {
           handleData(client);
        }
        else if (strstr(req,"GET /logs")) 
        {
           handleLogs(client);
        }
        else 
        {
           handleRoot(client);
        }



    // closing the client connection
    delay(1);
    client.stop();
    Serial.println("Client disconnected.");
  }//if client
   
}//void handleClient










