/********************************************************************************
/ Solar Sprinkler                                                               /
/ REV 01.03                                                                     /
/ 2016-06-23                                                                    /
/                                                                               /
/ Control rain-barrel water pump with empty barrel sensor.                      /
/ Connect to predefined local network to provide interface.                     /
/                                                                               /
/ ESP8266                                                                       /
/ .01: Modified board (trigger Rx instead of GPIO0 for relay)                   /
/ .02: Improve connecting with wifi manager library                             /
/ .03: Start improving UI, use AJAX to update page                              /
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
int pumpFreq = 16;         //... every how long

int sysOn = 1;               //System is 'On'

int pumpOnTime;
int pumpOffTime;

const int dryPumpComfort = 10;          //How many seconds are you comfortable running the pump dry? 
int nextPollTimeSec = dryPumpComfort;

int debugCount = 0;                    //store debug msgs

String root, javaScript, XML;

const int sliderMAX=2;              //sliders for pf and pd
int sliderVal[sliderMAX] = {60};





MDNSResponder mdns;

ESP8266WebServer server ( 80 );


int p = 0;
int swState = 0;


void buildRoot() {
  //char temp[1600];
//  int sec = millis() / 1000;    //variables to hold Uptime
//  int min = sec / 60;
//  int hr  = min / 60;
//  int nSec = pumpOnTime;        //variables to hold Next Pump Event time 
//  int nMin = nSec / 60;
//  int nHr  = nMin / 60;
  
  //snprintf ( root, 800,

  buildJavascript();
  root="<!DOCTYPE HTML>\n";
  root+="<HTML>\n";
  root+="<TITLE>Solar Sprinkler</TITLE>\n";
  root+="<META name='viewport' content='width=device-width, initial-scale=1'>\n";
  root+=javaScript;
  root+="<BODY onload='process()'>\n";
  root+="<h1>Solar Sprinkler 001.03</h1>\n";
  root+="<p>Server Uptime: <A ID='runtime'></A></p>\n";
  root+="<p>System: <A ID='powerval'>"+(String)sysOn+"</A>\n";
  root+="<form>\n";
  root+="   <INPUT NAME='power' ID='powerOff' type='radio' value='0' ";     //this html line is broken up to keep the correct radio button checked upon page reload
  if (!sysOn) root+= "checked ";
  root+="onclick='Power()'>OFF</INPUT>";
  root+="   <INPUT NAME='power' ID='powerOn' type='radio' value='1' ";
  if (sysOn) root+= "checked ";
  root+="onclick='Power()'>ON</INPUT>\n";
  root+="</form>\n";
  root+="<p>Pump:   <A ID='pump'>"+(String)p+"</A> (run <A ID='Sliderval1'>"+(String)pumpDur+"</A> secs every <A ID='Sliderval0'>"+(String)pumpFreq+"</A> secs.)</p>\n";
  root+="<p>Next Pump Event:   <A ID='nextpumpevent'></A> (begins in <A ID='beginsin'></A>)</p>\n";
  root+="<p>Water Level: "+(String)swState+" (Last tested during pumping)</p>\n";
  root+="<p>Debug Count: "+(String)debugCount+"</p>\n";
  root+="<p></p>\n";
  root+="<INPUT ID='slider0' TYPE='range' MIN='1' MAX='86400' ONINPUT='Slider(0)'>\n";  //pf every sec to every day
  root+="pumpFreq (Slidervalue0) = <A ID='Sliderval0'>"+(String)pumpFreq+"</A>\n";
  root+="<INPUT ID='slider1' TYPE='range' MIN='1' MAX='3600' ONINPUT='Slider(1)'>\n";   //pd for one sec to one hour
  root+="pumpDur  (Slidervalue1) = <A ID='Sliderval1'>"+(String)pumpDur+"</A>\n";

  root+="</BODY>\n";
  root+="</HTML>\n";
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
  javaScript+="  document.getElementById('Sliderval'+cnt).innerHTML=sliderVal;\n";
  //javaScript+="  document.getElementById('ESPval'+cnt).innerHTML=9*(100-sliderVal)+100;\n";
  javaScript+="  if(xmlHttp.readyState==0||xmlHttp.readyState==4){\n";
  javaScript+="    xmlHttp.open('PUT','setESPval?cnt='+cnt+'&val='+sliderVal,true);\n";
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

void buildXML(){
  XML= "<?xml version='1.0'?>";
  XML+="<xml>";
  XML+="<uptime>";
  XML+=secs2time((int)(millis()/1000));       //pass number of seconds running...
  XML+="</uptime>";
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
  
//  XML+="<sliderval1>";
//  XML+=String(sliderVal[1]);
//  XML+="</sliderval1>";


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

  pumpFreq=sliderVal[0];    //First Slider is pumpFreq
  pumpDur=sliderVal[1];     //Second Slider is pumpDur

  buildXML();
  server.send(200,"text/xml",XML);
}

void handleSystem(){
    sysOn = server.arg("state").toInt();
    if (sysOn == 0) {
      digitalWrite(pumpPin, LOW);  
      p=0;   
    };
    buildXML();                                   //not even using XML in this ?
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
  server.on ( "/xml", handleXML );
  server.on ( "/setESPval", handleESPval);
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
  server.on ( "/inline", []() {
    server.send ( 200, "text/plain", "..." );
  } );
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

    int currentTimeSec = now();  //call time to get in seconds

    if (currentTimeSec >= pumpOnTime){  //time to turn pump on
      digitalWrite(pumpPin, HIGH);  
      p=1;    
      pumpOffTime = currentTimeSec + pumpDur;
      pumpOnTime = currentTimeSec + pumpFreq;  
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
  pumpOnTime = currentTimeSec + pumpFreq;       // set new on time

}
