
//http://esp8266.ru/arduino-ide-esp8266

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
//#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include "FS.h"
extern "C" { //to set dhcp hostname 
#include "user_interface.h"
}

#define STARTPWR 1000 //start power 
#define DEBUG(msg) Serial.print(msg) 
//#define OTA

#define PWM_OUT 2 //gpio2, to mosfet
#define RESET_IN 0 //gpio0, reset
#define SENSOR_IN 12 //gpio12, input from motion detector
#define LED 13 //internal led

char ssid[20]; //wifi network
char passwd[20];//wifi password
char my_name[20];//dns name/ap name
const char *ap="esp8266"; //if no name given to device

int pwr=0;//pwm power
const int led = 13; //led turns on if connected to router
String page1=""; //dashboard page
File fsUploadFile; //need global for http POST processing

//default dasboard
const char* defpage PROGMEM = "<div>File /page.htm does not exist, upload it via <a href='/config'>config</a> please</div>";

//page for /config
const char* confpage PROGMEM = "<html><body>"
  "<div><form action='/config' method='POST'>"
  "SSID:<input type='text' name='ssid'><br>"
  "Password:<input type='text' name='pwd'><br>"
  "Name:<input type='text' name='my_name'><br>"
  "<input type='submit' name='SUBMIT' value='Submit'></form></div>"
  "<div><form method='POST' action='/upload' enctype='multipart/form-data'>"
  "<input type='file' name='page.html'><input type='submit' value='Upload'>"
  "</form></div>"
  "<div><a href='/'>back</a></div>"
  "</body></html>";
ESP8266WebServer server(80);
#ifdef OTA
ESP8266HTTPUpdateServer updater;
#endif

//read page.html from FS, use default page if error
void readPage() {
  File pageFile = SPIFFS.open("/page.html", "r");
  DEBUG ("Reading /page.html");  
  if (pageFile) {
    page1=pageFile.readString();
    DEBUG (" OK\n");  
  } else {
    page1=FPSTR(defpage);
    DEBUG (" not found\n");  
  }
}

//read 20 bytes from file to buf
void readbuf(char *buf, char *fname) {
  DEBUG ("readbuf "+String(fname));  
  memset(buf,0,20);
  File f = SPIFFS.open(fname, "r");
  if (f) {
    f.readBytes(buf,20);
    f.close();  
    DEBUG (" OK\n");  
  } else {
    DEBUG (" not found\n");  
  }
}


void writebuf(const char *buf, char *fname) {
  File f = SPIFFS.open(fname, "w");
  DEBUG ("writebuf "+String(fname));  
  if (f) {
    f.print(buf);
    f.flush();
    f.close();  
    delay(500); //need some time for writing
    DEBUG (" OK\n");  
  } else {
    DEBUG (" Open failed!\n");  
  }
}
void writeString (String s, char* fname) {
  writebuf(s.c_str(),fname);
}

//read config from FS
void readConfig() {
  readbuf(ssid,"/ssid");
  readbuf(passwd,"/passwd");
  readbuf(my_name,"/my_name");
}

//html 200 response
void resp(const char* s) {
    server.sendHeader("Connection", "close");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/html", s); 
}


//handle for dashboard page uploading
//copypasted from examples
void handlePageUpload(){
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    DEBUG("\nhandleFileUpload start: "+filename); 
    fsUploadFile = SPIFFS.open("/page.html", "w");
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(fsUploadFile) {
      fsUploadFile.write(upload.buf, upload.currentSize);
      fsUploadFile.flush();
      DEBUG("\nhandleFileUpload wite "+String(upload.currentSize)); 
    }
  } else if(upload.status == UPLOAD_FILE_END){
      DEBUG("\nhandleFileUpload end "); 
    if(fsUploadFile) {
      DEBUG("\nclose file "); 
      fsUploadFile.flush();
      fsUploadFile.close();
      delay(1000);
      readPage();//reload page
    }
    DEBUG("\nhandleFileUpload Size: "); 
    DEBUG(upload.totalSize);
  }
}

//wifi connect
int wifiMode=0;//WIFI_AP, WIFI_STA, WIFI_AP_STA or WIFI_OFF

void CheckConnect() {
  if (wifiMode==WIFI_STA && WiFi.status()==WL_CONNECTED) {
    digitalWrite(led,1);
    return;
  }
  digitalWrite(led,0);
  if (wifiMode==WIFI_AP) return;
  if (ssid[0]==0) {
      DEBUG ("No SSID, starting in AP mode\nname=");
      DEBUG (ap);
      DEBUG (",pw=");
      wifiMode=WIFI_AP;
      WiFi.mode(WIFI_AP);
      if (passwd[0]==0) {
        WiFi.softAP(ap);
        DEBUG("none");
      }
      if (passwd[0]!=0) {
        WiFi.softAP(ap,passwd);
        DEBUG(passwd);
      }
      DEBUG ("\n");
  } else {
    DEBUG("Connecting to router...\n");
    wifiMode=WIFI_STA;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, passwd);
    wifi_station_set_hostname(my_name);
    for (int i=0;i<10;i++) {
      digitalWrite(led,1);
      if (WiFi.status() == WL_CONNECTED) break;
      DEBUG(".");
      delay(1000);
      digitalWrite(led,0);
    }
  }
  //printDiag can show previous wifi password!
  //WiFi.printDiag(Serial);

  if (my_name[0]==0) {
    MDNS.begin(ap);
  } else {
    MDNS.begin(my_name);
  }
  MDNS.addService("http", "tcp", 80);
  #ifdef OTA 
    updater.setup(&server);
  #endif
  server.begin();
  DEBUG("server started\n");

}

//function for handlers
void setpwr(int newpwr) {
  pwr=newpwr;
  if (pwr>1023) pwr=1023;
  if (pwr<0) pwr=0;
  analogWrite(PWM_OUT, pwr);
}

void setup(void){
  pinMode(LED, OUTPUT);
  digitalWrite(LED, 0);
  pinMode(PWM_OUT, OUTPUT);
  pinMode(RESET_IN, INPUT_PULLUP);
  Serial.begin(115200);

  hook_init();
  setpwr(STARTPWR);
  
  if (!SPIFFS.begin()) {
    DEBUG ("\nBegin error, format FS");
    SPIFFS.format();
  } else {
    DEBUG ("FS OK");
  }

  readPage();
  readConfig();

  //show dashboard by default
  server.on("/", [](){
    resp(page1.c_str());
  });

  //*********** SENSORS *********
  //sensor for name
  server.on("/name", [](){
    resp(my_name);
  });
  
  //sensor for power status
  server.on("/pwr", [](){
    resp(String(pwr).c_str());
  });

  //********** CONTROLS **********
  //handler for turn on 
  server.on("/on", [](){
    DEBUG("ON\n");
    setpwr(1023);//full pwm
    resp("OK");
  });

  server.on("/off", [](){
    DEBUG("OFF\n");
    setpwr(0);
    resp("OK");
  });

  server.on("/50", [](){
    //pwm 50%
    DEBUG("\n50");
    setpwr(512);
    resp("OK");
  });

  //handler for +10% (+100 to pwm)
  server.on("/p10", [](){
    DEBUG("\n+10");
    setpwr(pwr+100);
    resp("OK");
  });

  //handler for -10% (-100 to pwm)
  server.on("/m10", [](){
    //pwm -10%
    DEBUG("\n-10");
    setpwr(pwr-100);
    resp("OK");
  });

  server.on("/upload", HTTP_POST, [](){ 
    server.send(200, "text/plain", "");
    }, handlePageUpload);

  //resp default config page
  server.on("/config", HTTP_GET, [](){
    resp(confpage);
    });
  //handler for config page    
  server.on("/config", HTTP_POST, [](){ 
      writeString(server.arg("ssid"),"/ssid");
      writeString(server.arg("pwd"),"/passwd");
      writeString(server.arg("my_name"),"/my_name");
      server.send(200, "text/plain", "OK, restarting"); 
      delay(5000);//need time for flush
      ESP.restart();
      return;
    });

  //handler for not found page
  server.onNotFound([](){ 
    server.send(404, "text/plain", server.uri());
  });

}

//reset configuration
void reset_all() {
    DEBUG("Reset ALL");
    digitalWrite(LED,1);
    SPIFFS.format();
    delay(2000);
    ESP.reset();
}

void loop(void){
  if (!digitalRead(RESET_IN)) reset_all();
  CheckConnect();
  server.handleClient();
  hook();
  delay(50);
}

//----- add-on functions ------
//handling motion detector

long lastChangedPower=0;
long t;

void hook_init(void) {
  pinMode(SENSOR_IN, INPUT);
}

void hook(void) {
  t=millis();
  if (digitalRead(SENSOR_IN)) {
    //increase power every 1 sec
    if (pwr<600) pwr=600;
    if (t-lastChangedPower>1000) {
      lastChangedPower=t;
      setpwr(pwr+100);
      DEBUG("\nmotion, +100");
    }
  } else { //no motion >8sec
    if (pwr>0) //only if some power
    if (t-lastChangedPower>5000) { //dim every 5 sec
      lastChangedPower=t;
      if (pwr<500) pwr=100;
      setpwr(pwr-100);
      DEBUG("\nno motion, -100");
    }
    
  }
}

