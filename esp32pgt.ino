#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <Ticker.h>
#include <Wire.h>
#include "misc.h"
#include "piclevel.h"
#include "mount.h"
#include "webserver.h"
#include "BluetoothSerial.h"
//Comment out undesired Feature
//---------------------------
#define NUNCHUCK_CONTROL
#define FIXED_IP
//#define OLED_DISPLAY
//--------------------------------
#ifdef  NUNCHUCK_CONTROL
#include "nunchuck.h"
#endif

#define BAUDRATE 19200
#define MAX_SRV_CLIENTS 3
#define SPEED_CONTROL_TICKER 10
#define COUNTERS_POLL_TICKER 100
#include <FS.h>
#include <SPIFFS.h>
//comment wifipass.h and uncomment for your  wifi parameters
//#include "wifipass.h"
const char* ssid = "MyWIFI";
const char* password = "Mypassword";
extern picmsg  msg;
extern volatile int state;
WiFiServer server(10001);
WiFiClient serverClients[MAX_SRV_CLIENTS];
WebServer serverweb(80);
BluetoothSerial SerialBT;
char buff[50] = "Waiting for connection..";
extern char  response[200];
mount_t *telescope;
String ssi;
String pwd;
Ticker speed_control_tckr, counters_poll_tkr;

extern long command( char *str );
time_t now;
#ifdef OLED_DISPLAY
#include "SSD1306.h"
//#include "SH1106.h"

#include "pad.h"
//SSD1306
SSD1306 display(0x3c, 21, 22);

void oledDisplay()
{
  char ra[20] = "";
  char de[20] = "";
  //write some information for debuging purpose to OLED display.
  display.clear();
  // display.drawString (0, 0, "ESP-8266 PicGoto++ 0.1");
  // display.drawString(0, 13, String(buff) + "  " + String(response));
  lxprintra(ra, sidereal_timeGMT_alt(telescope->longitude) * 15.0 * DEG_TO_RAD);
  display.drawString(0, 9, "LST " + String(ra));
  lxprintra(ra, calc_Ra(telescope->azmotor->pos_angle, telescope->longitude));
  lxprintde(de, telescope->altmotor->pos_angle);

  display.drawString(0, 50, "RA:" + String(ra) + " DE:" + String(de));
  lxprintde(de, telescope->azmotor->delta);
  display.drawString(0, 36, String(de)); // ctime(&now));
  display.drawString(0, 18, "MA:" + String(telescope->azmotor->counter) + " MD:" + String(telescope->altmotor->counter));
  //display.drawString(0, 27, "Dt:" + String(digitalRead(16)));//(telescope->azmotor->slewing));
  display.drawString(0, 27, "Dt:" + String(digitalRead(16))) + " Rate:" + String(telescope->srate));
  //unsigned int n= pwd.length();
  //display.drawString(0, 32,String(pw)+ " "+ String(n));
  display.drawString(0, 0, ctime(&now));
  display.display();
}
void oled_initscr(void)

{
  display.init();
  //  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.clear();
  display.drawString(0, 0, "Connecting to " + String(ssid));
  display.display();
}

void oled_waitscr(void)
{
  display.clear();
  display.drawString(0, 0, "Connecting to " + String(ssid));
  IPAddress ip = WiFi.localIP();
  String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
  display.drawString(0, 13, "Got IP! :" + ipStr);
  display.drawString(0, 26, "Waiting for Client");
  display.display();
}


#endif


int net_task(void)
{
  int lag = millis();
  size_t n;
  uint8_t i;
  //Sky Safari does not make a persistent connection, so each commnad query is managed as a single independent client.
  if (server.hasClient())
  {
    for (i = 0; i < MAX_SRV_CLIENTS; i++)
    {
      //find free/disconnected spot
      if (!serverClients[i] || !serverClients[i].connected())
      {
        if (serverClients[i]) serverClients[i].stop();
        serverClients[i] = server.available();
        continue;
      }
    }
    //Only one client at time, so reject
    WiFiClient serverClient = server.available();
    serverClient.stop();
  }
  //check clients for data
  for (i = 0; i < MAX_SRV_CLIENTS; i++)
  {
    if (serverClients[i] && serverClients[i].connected())
    {
      if (serverClients[i].available())
      {
        //get data from the  client and push it to LX200 FSM

        while (serverClients[i].available())
        {
          delay(1);
          size_t n = serverClients[i].available();
          serverClients[i].readBytes(buff, n);
          command( buff);
          buff[n] = 0;
          serverClients[i].write((char*)response, strlen(response));

          //checkfsm();
        }

      }
    }
  }
  return millis() - lag;
}
void bttask(void) {
  if (SerialBT.available()) {
    char n = 0;
    while (SerialBT.available())  buff[n++] = (char) SerialBT.read() ;
    command(buff);
    buff[n] = 0;
    SerialBT.write((const uint8_t* )response, strlen(response));

  }
}

void setup()
{

#ifdef OLED_DISPLAY
  oled_initscr();



#endif

#ifdef NUNCHUCK_CONTROL
  // nunchuck_init(D6, D5);
  nunchuck_init(2, 0);

#endif
  SerialBT.begin("PGT-BLUE");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("PGT_ESP", "boquerones");
  SPIFFS.begin();
  File f = SPIFFS.open("/wifi.config", "r");
  if (f)
  {
    ssi = f.readStringUntil('\n');
    pwd = f.readStringUntil('\n');
   
    f.close();
    char  ss [ssi.length() + 1];
    char  pw [pwd.length() + 1];
    ssi.toCharArray(ss, ssi.length() + 1);
    pwd.toCharArray(pw, pwd.length() + 1);
    pw[pwd.length() + 1] = 0;
    ss[ssi.length() + 1] = 0;

    WiFi.begin((const char*)ss, (const char*)pw);
  }
  else  WiFi.begin(ssid, password);
#ifdef FIXED_IP
  IPAddress ip(192, 168, 0, 14);
  IPAddress gateway(192, 168, 0, 1);
  IPAddress subnet(255, 255, 255, 0);
  // IPAddress DNS(8, 8, 8, 8);
  WiFi.config(ip, gateway, subnet);
#endif

  delay(500);
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 20) delay(500);
  if (i == 21)
  {
    //     while (1) delay(500);
    Serial.print(ssi);
    Serial.print(pwd);
  }
#ifdef OLED_DISPLAY
  oled_waitscr();
#endif

  //start UART and the server
  Serial.begin(BAUDRATE);
#ifdef OLED_DISPLAY
  // Serial.swap();
#endif
  //
  server.begin();
  server.setNoDelay(true);
  telescope = create_mount();
  readconfig(telescope);
  config_NTP(telescope->time_zone, 0);
  initwebserver();
  delay (2000) ;
  sdt_init(telescope->longitude, telescope->time_zone);
  speed_control_tckr.attach_ms(SPEED_CONTROL_TICKER, thread_motor, telescope);
  counters_poll_tkr.attach_ms(COUNTERS_POLL_TICKER, thread_counter, telescope);
#ifdef OLED_DISPLAY
  pad_Init();
#endif // OLED_DISPLAY

}

void loop()
{
  delay(10);
  net_task();
  bttask();
  now = time(nullptr);
  serverweb.handleClient();

#ifdef  NUNCHUCK_CONTROL
  nunchuck_read() ;
#endif

#ifdef OLED_DISPLAY
  doEvent();
  oledDisplay();
#endif

}



