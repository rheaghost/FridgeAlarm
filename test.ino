// 하기싫다 다 전부다 떄려치고싶다 
#include "DHT.h"
#include "RTClib.h"
//web server related
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Hash.h>

#include <FS.h> 

#define DHTPIN 14 //(D5  as we usen't SPI in this example
#define DHTTYPE DHT22   // Define the sensor type
#define BUILTIN_LED D4  //builtin LED
#define BUZZER D6
/*  from google ai
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

const char* strPCIP="your_pc_local_ip";//your pc's ip check as it always changes 
//cf google: arduino constant stringlocal 

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

float h=0;
float t=0;
float f=0;//fahrenheit

//int doorState=1;

int dht_count=0;

unsigned long lastLog=0;;  //flash log limiter to prevent flash burning
//door too long open things 2605061720
#define DOOR_OPENED 0 // 0 for magnet south pole simulation, 1 for actual fridge
#define DOOR_CLOSED 1 // 1 for magnet south pole simulation, 0 for actual fridge
#define DOOR_TIMEOUT 15000  // 60 sec for door timeout

#define DOOR_TIMEOUT_TEST 60000 

unsigned long doorOpenStart=0;
bool doorWasOpen=false;

unsigned long doorLastOpen=0;//last door open time 2605101455


//new door related globals

char strDoorStatus[16]="";
char doorMsgs[3][16]={"DOOR CLOSED!!\n","DOOR OPEN!!\n","DOOR TIMEOUT!!\n"};
unsigned long lastDoorOpenedTime=0; //last time when door changed from closed to open

int doorState=DOOR_CLOSED; //value of digitalRead(D7),either reed or 3144 digital hall sensor
bool doorWasOpened=false;  //init:false,if opened before: true, true->false: when door closed,false->true: door opened
bool doortoolongopen=false; //init:false, false->true: set to true if now-lastDoorOpenedTime>DOOR_TIMEOUT,true->false: door closed

//delete the above as the new algorithm didn't work due to overall changes and differences 2605130837



unsigned long lastLEDTime=0; 

//added globals for drop in scheduler
unsigned long now;
unsigned long lastDHT=0;
unsigned long lastDisplay=0;
//unsigned long lastLog=0;
unsigned long lastDoorCheck=0; //reed or DIGITAL hall sensor 2605102022
unsigned long lastSoil=0; //last soil sensor read time 2605102033
unsigned long lastrtc=0; //last rtc check time 2605122001

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
  const char* ssid = "your_id"; // Replace with your WiFi SSID
  const char* password = "your_pwd"; // Replace with your WiFi password
  WiFi.begin(ssid, password);


  int retries=0;
  while(WiFi.status()!=WL_CONNECTED && retries<20){
    delay(500);
    retries++;
  }//https://chatgpt.com/c/69be4627-c6f0-8321-830c-36928452b4ec //while weifi





  Serial.println("");
  Serial.println("WiFi connected");
  
  // Starting the web server
  server.begin();
  Serial.println("Web server running. Waiting for the ESP IP...");
  delay(10000);
  
  // Printing the ESP IP address
  Serial.print("Local IP of ESP8266 is: ");
  Serial.println(WiFi.localIP());
  Serial.println("**************************");
  booze();
  //delay(6000);
  delay(3000);
  //end wifi connect
   
  //lastLog=millis();
  lastDoorCheck=now;
  lastLog=now;

}//setup


//loop start

void loop() {
  int door_onoff=1;//assume closed here

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
  /*
  unsigned long doorOpenStart=0;
  bool doorWasOpen=false;
  bool doortoolongopen=false; //flag to be reset by closing door again 2605070848
  unsigned long doorLastOpen=0;//last door open time 2605101455

  hall sensor digitalinput d7:
   0 if magnet south pole passed
   1 otherwise

  */

//end 2.doorcheck

  if (now - lastDoorCheck > 500) {   // every 50ms   //500->1000ms
    lastDoorCheck = now;
    Serial.println("**** Door check!!! ****");
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
     //Serial.print(digitalRead(D7));
     Serial.print(door_onoff);
 
  
   if(door_onoff==DOOR_OPENED)//door open , here: door open means magnet south pole passes the reed ,this is because of test conditions,TEST ON REAL FRIDGE!!
   {
        if(!doorWasOpen){
           doorOpenStart=millis();
           doorWasOpen=true;

        }
    //debug
        if(doorWasOpen) Serial.println("doorWasOpen=true");//OK this works 2605070825

        Serial.println("Door open!!!");
        
        strcpy(strDoorStatus,doorMsgs[1]); //door open
       

    /**/
    WiFiClient client;
    if (client.connect(strPCIP, 5000)) { //aduito adjust to ACTUAL address 2605061936
       client.print("GET /alert?msg=door_open HTTP/1.1\r\n");
       client.print("Host: YOUR_PC_IP\r\n");
       client.print("Connection: close\r\n\r\n");
        }//if client

    
    booze();
    

    //Now we change the algorithm so that notification is only given if 

     // trigger after 60 seconds
    if (millis() - doorOpenStart > DOOR_TIMEOUT_TEST && doorWasOpen==true) {  //door was iopen fop  problem: this condition remains after door reset, next normal door open is 'too long' ///10000->15000
    //doorWasOpen remains true if it remained open after the last opening; becomes FALSE whenever closed again
    //OK problem solved 2605071040
        doortoolongopen=true;
        Serial.println("Door open too long!");
        booze1();  //different sound
        //sendAlert("door_open_too_long");
        //reedStr="Door open too long";
        strcpy(strDoorStatus,doorMsgs[2]); //door timeout
        
         WiFiClient client;
         //send door open too long msg to pc
         if (client.connect(strPCIP, 5000)) {  //check PC address on your router admin menu!!
             client.print("GET /alert?msg=door_open_too_long HTTP/1.1\r\n");
             client.print("Host: 192.168.0.9\r\n");
             //client.print(rtcStr+"\r\n");
             client.print("Connection: close\r\n\r\n");
            }//if client
         
        
       }//if millis
       else {
        //doorWasOpen = false;  This was incorrect logic as this would reset the doorwasopen condition each time
          }//else


  }//door open if door onoff==0
  
  //replace end  2605071241



  else  //door close: door_onoff==1
  {
    doorWasOpen=false;
    doortoolongopen=false;//reset door open & door too long open flag if door is closed again
    digitalWrite(D4,HIGH);
    Serial.println("Door closed!!!");
    //display.setCursor(10,0);
    display.setCursor(0,10);//2605052224
    display.println(" DOOR CLOSED !!");
    //reedStr="DOOR CLOSED!!";
    strcpy(strDoorStatus,doorMsgs[0]);//door closed

    //log doosed to pc
    
    WiFiClient client;
    if (client.connect(strPCIP, 5000)) {  //check PC address on your router admin menu!!
       client.print("GET /alert?msg=door_close HTTP/1.1\r\n");
       client.print("Host: 192.168.0.9\r\n");
       client.print("Connection: close\r\n\r\n");
      }//if client
    

    }//else (door closed)


     
     
  }//every 500  for door open check

 
  Serial.println(strDoorStatus);



  // =========================
  // 3. DHT (slow sensor)
  // =========================
  if (now - lastDHT > 2000) {   // every 2 sec
    lastDHT = now;

    float nh = dht.readHumidity();
    float nt = dht.readTemperature();

    if (!isnan(nh) && !isnan(nt)) {
      h = nh;
      t = nt;
      snprintf(humidityTemp,sizeof(humidityTemp),"%.2f\n",h);
      snprintf(celsiusTemp,sizeof(celsiusTemp),"%.2f\n",t);



    }//if isnan
  }//if now lastdht


  // =========================
  // 4. SOIL (analog)
  // =========================
  
  if (now - lastSoil > 1000) {   // reuse timing
    lastSoil=now;
    soil = analogRead(A0);

  //int raw = analogRead(A0);

   Serial.print("RAW=");
   Serial.println(soil);
   snprintf(waterStr,sizeof(waterStr),"%3d",soil);
  }//if now lastSoil
  

  // =========================
  // 5. DISPLAY (heavy)
  // =========================
  if (now - lastDisplay > 500) {
    lastDisplay = now;

    display.clearDisplay();
    rtc_update();
    //display.setCursor(0,0);
    display.setCursor(0,20);
    display.print("T:");
    display.print(t);

    //display.setCursor(0,10);
    display.setCursor(50,20);
    display.print("C, H:");
    display.print(h);
    display.print(" %");

    //Change this: into a char[], T: (temp)+C,H:  % better visible 2605122051

    //display.setCursor(0,20);
    display.setCursor(0,30);//from 40
    display.print("Soil:");
    display.print(soil);

    //display.setCursor(0,30);
    display.setCursor(0,40); //frm 50
    display.print("Hall:");
    display.print(digitalRead(D7));

    //display.setCursor(0,40);
    display.setCursor(0,50);//from 60
    display.print(strDoorStatus);
    


    display.display();
  }//if now lastdisplay

 
  // =========================
  // 6. LOGGING (slow)
  // =========================
  
  if (now - lastLog > 60000) {   // every 60 sec
    //booze1();  removed as write log is ok 2605132117
    lastLog = now;
    
   logDataOld(String(now) + "," + String(soil) + "," + String(t));

  }//if now lastlog
  



  
  //rtc_update();
  yield();
  Serial.println(rtcStr);
  //handleClient();
  yield();

}//loop
//end main loop





void booze()
{
  // Play a 1000Hz tone for 500ms
  tone(BUZZER, 1000); 
  //delay(500);
  digitalWrite(D4,HIGH);
  delay(100); //2605052235 for now
  digitalWrite(D4,LOW);
  // Stop the tone for 500ms
  noTone(BUZZER);
  //delay(500);
  delay(50);
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

void test1306()//code from google ai
{
 

// I2C address is  0x3C (common 0.96/0.91 inch address)
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
  
  display.setTextSize(1);
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
  
    rtc.adjust(DateTime(2026, 5, 4, 11, 0, 0));

    char strLostPower[80];//lost power string
    memcpy(strLostPower,0,sizeof(strLostPower)); //clear
    strcat(strLostPower,"BOOT LOSTPOWER");
    strcat(strLostPower,rtcStr);


    logDataOld("BOOT LOSTPOWER,"+rtcStr);  //power loss log 2605071519
    logDataOld(strLostPower);
                                                                                                                                                                                                 
  }

  // When time needs to be re-set on a previously configured device, the
  // following line sets the RTC to the date & time this sketch was compiled
  //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  // This line sets the RTC with an explicit date & time, for example to set
  // January 21, 2014 at 3am you would call:
  //rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
 

}//rtc_setup

void rtc_update()//https://randomnerdtutorials.com/esp8266-nodemcu-ds3231-real-time-clock-arduino/ ok 2605041038
{
  if(now-lastrtc>1000)
 {
  // Get the current time from the RTC
 

    DateTime now = rtc.now();

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
        rtc.getTemperature()
    );

    //Serial.println(rtcStr);

    display.setCursor(0,0);
    display.println(rtcStr);



  //display.setCursor(0,50);
  display.setCursor(0,0);
  display.println(rtcStr);
  //display.println(formattedTime);
  //display.display();

 }//if now lastrtc

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
    client.println(strDoorStatus);

    client.println("<br>");
    client.println(waterStr);



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

    //client.println(reedStr);
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
  if(client.connect("192.168.0.6'",5000)){  //ur pc ip
    client.print("GET /alert?msg=" + msg + " HTTP/1.1\r\n");
    client.print("Host: 192.168.0.6\r\n");
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
    //String req=""; hgeap corruption
    //char req[512];

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









