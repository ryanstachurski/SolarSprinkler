/********************************************************************************
/ Solar Sprinkler                                                               /
/ REV 01.04                                                                     /
/ 2016-07-22                                                                   /
/                                                                               /
/ Control rain-barrel water pump with empty barrel sensor.                      /
/ Connect to predefined local network to provide interface.                     /
/                                                                               /
/ ESP8266                                                                       /
/ .01: Modified board (trigger Rx instead of GPIO0 for relay)                   /
/ .02: Improve connecting with wifi manager library                             /
/ .03: Start improving UI, use AJAX to update page                              /
/ .04: Implement clock time, pump start time, and pump cycles per day           /
/                                                                               /
/*******************************************************************************/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <Time.h>

const int pumpPin = 3;       // Turn on pump (Wimos pin [D4], originally using GPIO0 [0] on B.Rev.01, now using Rx pin [3], or Tx pin [1])
const int swPin = 2;         // Switch sensor (Wimos pin [D3])

// defaults for time keeping
int pumpDur = 8;             //seconds, pump remains on for how long...
int pumpFreq = 5400;         //... number of pump events in a day
int pumpFreqSec = 16;        //Time between each pump event
int sysOn = 1;               //System is 'On'
int startTime = 1;           //Start time in seconds

int pumpOnTime;
int pumpOffTime;
//int currentTimeHr, currentTimeMn, currentTimeSc, currentTimeSecs;     //current time in 24h day


int timeOffset;             //current time offset in seconds from 24h day

const int dryPumpComfort = 10;          //How many seconds are you comfortable running the pump dry? 
int nextPollTimeSec = dryPumpComfort;

int debugCount = 0;                    //store debug msgs

String root, settings, javaScript, XML;

const int sliderMAX=2;              //sliders for pf and pd
int sliderVal[sliderMAX] = {5400, 16};






MDNSResponder mdns;

ESP8266WebServer server ( 80 );


int p = 0;
int swState = 0;


void buildRoot() {

  buildJavascript();
  root="<!DOCTYPE HTML>\n";
  root+="<HTML>\n";
  root+="<TITLE>Solar Sprinkler</TITLE>\n";
  root+="<META name='viewport' content='width=device-width, initial-scale=1'>\n";
  root+=javaScript;
  root+="<BODY onload='process()'>\n";
  root+="<h1>Solar Sprinkler 001.04</h1>\n";
  root+="<p>Server Uptime: <A ID='runtime'></A></p>\n";
  root+="<p>Current System Time: <A ID='currenttime'></A></p>\n";
  root+="<p>Initial Start Time: <A ID='currentStartTime'></A></p>\n";
  root+="<p>System: <A ID='powerval'>"+(String)sysOn+"</A>\n";
  root+="<form>\n";
  root+="   <INPUT NAME='power' ID='powerOff' type='radio' value='0' ";     //this html line is broken up to keep the correct radio button checked upon page reload
  if (!sysOn) root+= "checked ";
  root+="onclick='Power()'>OFF</INPUT>";
  root+="   <INPUT NAME='power' ID='powerOn' type='radio' value='1' ";
  if (sysOn) root+= "checked ";
  root+="onclick='Power()'>ON</INPUT>\n";
  root+="</form>\n";
  root+="<p>Pump:   <A ID='pump'>"+(String)p+"</A> (run <A ID='Sliderval1'>"+(String)pumpDur+"</A> secs, <A ID='Sliderval0'>"+(String)pumpFreq+"</A> times per day.)</p>\n";
  root+="<p>Next Pump Event (in uptime):   <A ID='nextpumpevent'></A> (begins in <A ID='beginsin'></A>)</p>\n";
  root+="<p>Water Level: "+(String)swState+" (Last tested during pumping)</p>\n";
  root+="<p></p>\n";
  root+="<p><INPUT ID='slider0' TYPE='range' MIN='1' MAX='24' ONINPUT='Slider(0)'>\n";  //pf every sec to every day
  root+="pumpFreq: Number of cycles per day (SecondInstanceSliderval0) = <A ID='SecondInstanceSliderval0'></A></p>\n";   // between anchor previously: "+(String)pumpFreq+"
  root+="<p><INPUT ID='slider1' TYPE='range' MIN='1' MAX='60' ONINPUT='Slider(1)'>\n";   //pd for one sec to one hour
  root+="pumpDur: Slider in minutes  (SecondInstanceSliderval1) = <A ID='SecondInstanceSliderval1'></A></p>\n";   // ... "+(String)pumpDur+"
  root+="<p><form method='post' action='/settings'><button type='submit'>Settings</button></form></p>\n";
  root+="</BODY>\n";
  root+="</HTML>\n";
}

void buildSettings(){
  
  buildJavascript();
  settings="<!DOCTYPE HTML>\n";
  settings+="<HTML>\n";
  settings+="<TITLE>Settings</TITLE>\n";
  settings+="<META name='viewport' content='width=device-width, initial-scale=1'>\n";
  settings+=javaScript;
  settings+="<BODY onload='process()'>\n";
  settings+="<h1>Settings Adjustments</h1>\n";
  settings+="<p>Server Uptime: <A ID='runtime'></A> [<A ID='pump'></A> <A ID='beginsin'></A> <A ID='nextpumpevent'></A> <A ID='currenttime'></A>]</p>\n";
  settings+="<p>Adjust current time: <INPUT ID='currentTime' type='time' value='";  //Pre-populate field with current system time
  int hr = hour(); 
  if ( hr < 10 ) settings+="0"; //two decimals required
  settings+=hr;  
  settings+=":";
  int mn = minute();
  if ( mn < 10 ) settings+="0"; //two decimals required
  settings+=mn;
  settings+="'><button onclick='setTime()'>Set</button></INPUT></p>\n";

  settings+="<p>Adjust pump start time: <INPUT ID='currentStartTime' type='time' value='";  //Pre-populate field with current system time
  if ( hr < 10 ) settings+="0"; //two decimals required
  settings+=hr;  
  settings+=":";
  if ( mn < 10 ) settings+="0"; //two decimals required
  settings+=mn;
  settings+="'><button onclick='setStartTime()'>Set</button></INPUT></p>\n";
  
}

void buildJavascript(){
  javaScript="<SCRIPT>\n";
  javaScript+="xmlHttp=createXmlHttpObject();\n";
  
  javaScript+="function createXmlHttpObject(){\n";
  javaScript+="  if(window.XMLHttpRequest){\n";
  javaScript+="    xmlHttp=new XMLHttpRequest();\n";
  javaScript+="  }else{\n";
  javaScript+="    xmlHttp=new ActiveXObject('Microsoft.XMLHTTP');\n";
  javaScript+="  }\n";
  javaScript+="  return xmlHttp;\n";
  javaScript+="}\n";  

  javaScript+="function process(){\n";
  javaScript+="  if(xmlHttp.readyState==0||xmlHttp.readyState==4){\n";
  javaScript+="    xmlHttp.onreadystatechange=function(){\n";
  javaScript+="      if(xmlHttp.readyState==4&&xmlHttp.status==200){\n";
  javaScript+="        xmlDoc=xmlHttp.responseXML;\n";
  javaScript+="        xmlmsg=xmlDoc.getElementsByTagName('uptime')[0].firstChild.nodeValue;\n";
  javaScript+="        document.getElementById('runtime').innerHTML=xmlmsg;\n";
  javaScript+="        xmlmsg=xmlDoc.getElementsByTagName('currenttime')[0].firstChild.nodeValue;\n";
  javaScript+="        document.getElementById('currenttime').innerHTML=xmlmsg;\n";  
  javaScript+="        xmlmsg=xmlDoc.getElementsByTagName('currentstarttime')[0].firstChild.nodeValue;\n";
  javaScript+="        document.getElementById('currentStartTime').innerHTML=xmlmsg;\n";    
  javaScript+="        xmlmsg=xmlDoc.getElementsByTagName('nextpumpevent')[0].firstChild.nodeValue;\n";
  javaScript+="        document.getElementById('nextpumpevent').innerHTML=xmlmsg;\n"; 
  javaScript+="        xmlmsg=xmlDoc.getElementsByTagName('beginsin')[0].firstChild.nodeValue;\n";
  javaScript+="        document.getElementById('beginsin').innerHTML=xmlmsg;\n";    
  javaScript+="        xmlmsg=xmlDoc.getElementsByTagName('pump')[0].firstChild.nodeValue;\n";
  javaScript+="        document.getElementById('pump').innerHTML=xmlmsg;\n";  
  javaScript+="      }\n";
  javaScript+="    }\n";
  javaScript+="    xmlHttp.open('PUT','xml',true);\n";
  javaScript+="    xmlHttp.send(null);\n";
  javaScript+="  }\n";
  javaScript+="  setTimeout('process()',1000);\n";
  javaScript+="}\n";

  javaScript+="function Slider(cnt){\n";
  javaScript+="  sliderVal=document.getElementById('slider'+cnt).value;\n";
  javaScript+="  document.getElementById('SecondInstanceSliderval'+cnt).innerHTML=sliderVal;\n";
  javaScript+="  if (cnt == 1) sliderVal*=60;\n";  //stoopid hack to give the PumpDur slider a 60 sec multiplier
  javaScript+="  document.getElementById('Sliderval'+cnt).innerHTML=sliderVal;\n";
  javaScript+="  if(xmlHttp.readyState==0||xmlHttp.readyState==4){\n";
  javaScript+="    xmlHttp.open('PUT','setESPval?cnt='+cnt+'&val='+sliderVal,true);\n";
  javaScript+="    xmlHttp.send(null);\n";
  javaScript+="  }\n";
  javaScript+="}\n";

  javaScript+="function setTime(){\n";
  javaScript+="  timeVal=document.getElementById('currentTime').value;\n";  // timeVal is HH:MM -- rewrite function to  just get current time from user, calc day offset and set in xml
  javaScript+="  if(xmlHttp.readyState==0||xmlHttp.readyState==4){\n";
  javaScript+="    xmlHttp.open('PUT','setTimeVal?val='+timeVal,true);\n";
  javaScript+="    xmlHttp.send(null);\n";
  javaScript+="  }\n";
  javaScript+="}\n";


  javaScript+="function setStartTime(){\n";
  javaScript+="  timeVal=document.getElementById('currentStartTime').value;\n";  // timeVal is HH:MM -- rewrite function to  just get current time from user, calc day offset and set in xml
  javaScript+="  if(xmlHttp.readyState==0||xmlHttp.readyState==4){\n";
  javaScript+="    xmlHttp.open('PUT','setStartTimeVal?val='+timeVal,true);\n";
  javaScript+="    xmlHttp.send(null);\n";
  javaScript+="  }\n";
  javaScript+="}\n";

  javaScript+="function Power(){\n";
  javaScript+="  state=document.getElementById('powerOff').checked ? 0 : 1;\n";
  javaScript+="  document.getElementById('powerval').innerHTML=state;\n";
  javaScript+="  if(xmlHttp.readyState==0||xmlHttp.readyState==4){\n";
  javaScript+="    xmlHttp.open('PUT','system?state='+state,true);\n";
  javaScript+="    xmlHttp.send(null);\n";
  javaScript+="  }\n";
  javaScript+="}\n";



  javaScript+="</SCRIPT>\n";  
  
}

void handleRoot(){
  buildRoot();
  server.send ( 200, "text/html", root );
}

void handleSettings(){
  buildSettings();
  server.send ( 200, "text/html", settings );
}

void buildXML(){
  XML= "<?xml version='1.0'?>";
  XML+="<xml>";
  XML+="<uptime>";
  XML+=secs2time((int)(millis()/1000));       //pass number of seconds running...
  XML+="</uptime>";
  XML+="<currenttime>";
  XML+=secs2time((int)(hour()*3600 + minute()*60 + second()));
  XML+="</currenttime>";
  XML+="<currentstarttime>";
  XML+=secs2time(startTime);
  XML+="</currentstarttime>";
//  XML+="<currentTimeHr>";
//  XML+=currentTimeHr;
//  XML+=hour();
//  XML+="</currentTimeHr>";
//  XML+="<currentTimeMn>";
//  XML+=currentTimeMn;
//  XML+=minute();
//  XML+="</currentTimeMn>";
//  XML+="<currentTimeSc>";
//  XML+=currentTimeSc;
//  XML+=second();
//  XML+="</currentTimeSc>";
  XML+="<nextpumpevent>";
  XML+=secs2time(pumpOnTime);                 //The nextPumpEvent is a properly formatted pumpOnTime
  XML+="</nextpumpevent>";
  XML+="<beginsin>";
  XML+=secs2time( pumpOnTime - (int)(millis()/1000) );
  XML+="</beginsin>";
  XML+="<pump>";
  XML+=(String)p;
  XML+="</pump>";

  for(int i=0;i<sliderMAX;i++){
    XML+="<sliderval"+(String)i+">";
    XML+=String(sliderVal[i]);
    XML+="</sliderval"+(String)i+">";
  }

  XML+="<sysOn>";
  XML+=sysOn;
  XML+="</sysOn>";

  XML+="</xml>";  
}

String secs2time(int ss){
  String Time="";
  byte hh = ss/3600;
  byte mm = (ss-hh*3600)/60;
  ss = (ss-hh*3600)-mm*60;
  if(hh<10)Time+="0";
  Time+=(String)hh+":";
  if(mm<10)Time+="0";
  Time+=(String)mm+":";
  if(ss<10)Time+="0";
  Time+=(String)ss;
  return Time;
}

void handleXML(){
  buildXML();
  server.send(200,"text/xml",XML);
}


void handleESPval(){
  int sliderCNT=server.arg("cnt").toInt();
  sliderVal[sliderCNT]=server.arg("val").toInt();

  pumpFreq = sliderVal[0];   //First Slider is pumpFreq  (seconds in a day divided by watering times per day
  pumpFreqSec =86400 / pumpFreq;    
  pumpDur = sliderVal[1];     //Second Slider is pumpDur

  buildXML();
  server.send(200,"text/xml",XML);
}

void handleTimeVal(){
  String timeVal=server.arg("val");
  String hr = timeVal.substring(0,2);
  String mn = timeVal.substring(3,5);
  setTime(hr.toInt(),mn.toInt(),0,15,7,2016);                // Set time to user input of hours and minutes to some current day...
  
  buildXML();
  server.send(200,"text/xml",XML);
}

void handleStartTimeVal(){
  String timeVal=server.arg("val");
  String hr = timeVal.substring(0,2);
  String mn = timeVal.substring(3,5);
  startTime = (hr.toInt() * 3600) + (mn.toInt() * 60);                // Set start time to user input of hours and minutes of the day...

  int cursec = (hour()*3600) + (minute()*60) + second();
  pumpOnTime = (startTime > cursec) ? (startTime - cursec) + (millis() / 1000) : millis() / 1000;       // set new pump start time upon turning system back on (dif between clock and start + current uptime 

  //since we're adjusting the pump start time, we should turn it off now, before locking in the new start time
  digitalWrite(pumpPin, LOW);
  p=0;
  

  buildXML();
  server.send(200,"text/xml",XML);
  
}

void handleSystem(){
    sysOn = server.arg("state").toInt();
    if (sysOn == 0) {
      digitalWrite(pumpPin, LOW);  
      p=0;            //manually adjust pumpPin variable
    };
    if (sysOn == 1) {
      int cursec = (hour()*3600) + (minute()*60) + second();
      pumpOnTime = (startTime > cursec) ? (startTime - cursec) + (millis() / 1000) : millis() / 1000;       // set new pump start time upon turning system back on (dif between clock and start + current uptime 

    };
    buildXML();                                   
    server.send(200,"text/xml",XML);  
}


void handleNotFound() {
  String message = "uh-oh.\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }

  server.send ( 404, "text/plain", message );
}

void setup ( void ) {
  //Setup WiFi Manager to enable AP mode initially
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(60*60);  //wait an hour for response on power failure befpre entering default pumping
  wifiManager.autoConnect("SolaR", "sprinkler");
  

  if ( mdns.begin ( "SolarSprinkler", WiFi.localIP() ) ) {
    //Serial.println ( "MDNS responder started" );
  }

  //Setup webserver
  server.on ( "/", handleRoot );
  server.on ( "/settings", handleSettings );
  server.on ( "/xml", handleXML );
  server.on ( "/setESPval", handleESPval);
  server.on ( "/setTimeVal", handleTimeVal);
  server.on ( "/setStartTimeVal", handleStartTimeVal);
  server.on ( "/system", handleSystem );
/*  server.on ( "/syson", []() {              //Turn on or off:  <site>/syson?state=1
    String state=server.arg("state");
    if (state == "1") sysOn = 1;
    else if (state == "0") {
      sysOn = 0;
      digitalWrite(pumpPin, LOW);  
      p=0;   
    };
    handleRoot;
  });
  server.on ( "/pf", []() {               //Time adjust      <site>/pf?state=60
    String state=server.arg("state");
    if ( state.toInt() > 0 ) pumpFreq = state.toInt();
    handleRoot;  
  });
  server.on ( "/pd", []() {              
    String state=server.arg("state");
    if ( state.toInt() > 0 ) pumpDur = state.toInt();
    handleRoot;
  });
*/  
//  server.on ( "/inline", []() {
//    server.send ( 200, "text/plain", "..." );
//  } );
  server.onNotFound ( handleNotFound );
  server.begin();

  //setup hardware
  pinMode(pumpPin, OUTPUT);
  pinMode(swPin, INPUT);
  
  startupLightPump();  //Call this instead of the following each time

}



void loop ( void ) {
  mdns.update();
  server.handleClient();      // perhaps move this one down since it might take some time

  /////////////// Handle the Lights and Pump //////////////////
  if (sysOn) {

    int currentTimeSec = millis() / 1000; //     formerly:  now();  //call time to get in seconds

    if (currentTimeSec >= pumpOnTime){  //time to turn pump on
      digitalWrite(pumpPin, HIGH);  
      p=1;    
      pumpOffTime = currentTimeSec + pumpDur;
      pumpOnTime = currentTimeSec + pumpFreqSec;  
    }

    if (currentTimeSec >= pumpOffTime){   //time to turn pump off
      digitalWrite(pumpPin, LOW);
      p=0;
    }       

    if ( p == 1 ){             //if (digitalRead(pumpPin) == HIGH){   //If the pump is running, check if there is water
      //Pump Failsafe
      
      if (currentTimeSec >= nextPollTimeSec){     //time to poll pump sensor (don't poll needlessly)
        debugCount += 1;
        swState = digitalRead(swPin);      //Read water sensor state. Hardware requires pumpPin to be HIGH for this to be accurate
        if (swState == 0){
          debugCount = 0;
          digitalWrite(pumpPin, LOW);
          p=0;
          sysOn = 0;
        }
        nextPollTimeSec = currentTimeSec + dryPumpComfort;      
      }
    }

    
  }//if the system is 'On'...

    
}



void startupLightPump() {
  //Upon turning board on, run at default setings unless modified

  int currentTimeSec = now();  //call time to get in seconds
  
  digitalWrite(pumpPin, HIGH); 
  p=1;
  pumpOffTime = currentTimeSec + pumpDur;      // Whenever on, set to turn off
  pumpOnTime = currentTimeSec + pumpFreqSec;       // set new on time

}

