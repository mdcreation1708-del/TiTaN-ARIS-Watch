/*
 * TiTaN Lab of Innovation - ARIS Watch OS V2 Lite
 * Target: classic ESP32 DevKit V1 + GC9A01 240x240 + 3 buttons
 * Libraries: Adafruit GFX, Adafruit GC9A01A, ESP32 Arduino BLE library
 *
 * Based on review of V1.3: removes Wi-Fi/NTP and demo boot animation to keep
 * the first phone-connected build compact. This source has NOT been compiled
 * or tested on hardware in this environment.
 *
 * BLE protocol (UTF-8 text, newline not required):
 * Phone writes to COMMAND characteristic:
 *   PING, STATUS, HOME, MENU, NOTE|title|body, VOICE|transcript, CLEAR
 * Watch notifies EVENT characteristic: READY, PONG, STATUS|..., ACK|..., ERR|...
 *
 * Battery percentage is deliberately not shown: no divider/sensor is installed.
 */
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define TFT_SCLK 18
#define TFT_MOSI 23
#define TFT_CS    5
#define TFT_DC   16
#define TFT_RST  17
#define TFT_BL   -1
#define BTN_UP   25
#define BTN_OK   26
#define BTN_DOWN 27

static const char *SERVICE_UUID = "9f8c0001-7b5a-4d2e-9a11-3c6a7b8c0001";
static const char *COMMAND_UUID = "9f8c0002-7b5a-4d2e-9a11-3c6a7b8c0001";
static const char *EVENT_UUID   = "9f8c0003-7b5a-4d2e-9a11-3c6a7b8c0001";
Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);
BLEServer *bleServer = nullptr;
BLECharacteristic *commandChar = nullptr;
BLECharacteristic *eventChar = nullptr;
volatile bool phoneConnected = false;
bool bleEnabled = true;
bool displaySleeping = false;
bool sleepEnabled = true;
uint32_t lastActivity = 0;
uint32_t downStart = 0;
bool downLongDone = false;
uint8_t page = 0; // 0 home, 1 menu, 2 quick settings, 3 notifications, 4 ARIS
uint8_t cursor = 0;
String noteTitle = "No notifications";
String noteBody = "Connect ARIS app";
String voiceText = "Ready for phone";
String statusLine = "BLE starting";
uint32_t lastClockDraw = 0;

struct Button { uint8_t pin; bool raw; bool stable; uint32_t changed; bool press; };
Button upB{BTN_UP,HIGH,HIGH,0,false}, okB{BTN_OK,HIGH,HIGH,0,false}, downB{BTN_DOWN,HIGH,HIGH,0,false};

bool pressed(Button &b) {
  bool v = digitalRead(b.pin); b.press = false;
  if (v != b.raw) { b.raw = v; b.changed = millis(); }
  if (millis() - b.changed > 25 && v != b.stable) {
    b.stable = v; if (v == LOW) b.press = true;
  }
  return b.press;
}
void centerText(const String &s, int y, uint8_t size, uint16_t color) {
  tft.setTextSize(size); tft.setTextColor(color); int16_t x1,y1; uint16_t w,h;
  tft.getTextBounds(s,0,y,&x1,&y1,&w,&h); tft.setCursor((240-(int)w)/2,y); tft.print(s);
}
void frame(const String &heading) {
  tft.fillScreen(GC9A01A_BLACK); tft.drawCircle(120,120,103,GC9A01A_CYAN);
  tft.drawCircle(120,120,100,GC9A01A_BLUE); centerText(heading,37,2,GC9A01A_CYAN);
}
void sendEvent(String s) {
  if (!eventChar) return;
  eventChar->setValue(s.c_str());
  if (phoneConnected) eventChar->notify();
  Serial.println("BLE event: " + s);
}
void drawHome() {
  frame("TiTaN ARIS");
  centerText("Hello MD Sir !!",62,1,GC9A01A_YELLOW);
  centerText(phoneConnected ? "PHONE CONNECTED" : "PHONE DISCONNECTED",88,1,phoneConnected?GC9A01A_GREEN:GC9A01A_RED);
  centerText("ARIS WATCH OS",113,2,GC9A01A_WHITE);
  centerText("Notifications",151,1,GC9A01A_CYAN);
  centerText(noteTitle.substring(0,22),168,1,GC9A01A_WHITE);
  centerText("OK: MENU",198,1,GC9A01A_YELLOW);
  centerText("HOLD DOWN: HOME",214,1,GC9A01A_WHITE);
}
void drawMenu() {
  frame("MAIN MENU"); const char *labels[] = {"Quick Settings","Notifications","ARIS Voice","About"};
  for(int i=0;i<4;i++){ int y=72+i*30; bool a=i==cursor;
    if(a)tft.fillRoundRect(45,y-4,150,24,5,GC9A01A_CYAN);
    else tft.drawRoundRect(45,y-4,150,24,5,GC9A01A_BLUE);
    tft.setTextSize(1); tft.setTextColor(a?GC9A01A_BLACK:GC9A01A_WHITE); tft.setCursor(55,y+4); tft.print(labels[i]); }
  centerText("UP/DOWN MOVE  OK OPEN",204,1,GC9A01A_YELLOW);
}
void drawQuick() {
  frame("QUICK SETTINGS");
  centerText(String("BLE: ")+(bleEnabled?"ON":"OFF"),78,2,bleEnabled?GC9A01A_GREEN:GC9A01A_RED);
  centerText(String("PHONE: ")+(phoneConnected?"LINKED":"NOT LINKED"),108,1,GC9A01A_WHITE);
  centerText(String("DISPLAY SLEEP: ")+(sleepEnabled?"ON":"OFF"),133,1,GC9A01A_WHITE);
  centerText("OK: TOGGLE SLEEP",165,1,GC9A01A_YELLOW);
  centerText("UP: TOGGLE BLE",184,1,GC9A01A_YELLOW);
  centerText("DOWN: BACK",207,1,GC9A01A_WHITE);
}
void drawNote() {
  frame("NOTIFICATIONS");
  tft.setTextSize(1); tft.setTextColor(GC9A01A_YELLOW); tft.setCursor(35,76); tft.print(noteTitle.substring(0,26));
  tft.setTextColor(GC9A01A_WHITE); int y=100; String rest=noteBody;
  while(rest.length() && y<180){ int cut=rest.length()>27?rest.lastIndexOf(' ',27):-1; if(cut<1)cut=min((int)rest.length(),27); tft.setCursor(35,y);tft.print(rest.substring(0,cut));rest=rest.substring(cut);rest.trim();y+=16; }
  centerText("DOWN: BACK",208,1,GC9A01A_CYAN);
}
void drawVoice() {
  frame("ARIS VOICE"); centerText("PHONE-BASED",75,1,GC9A01A_CYAN);
  String s=voiceText; int y=104;
  while(s.length() && y<174){ int cut=s.length()>26?s.lastIndexOf(' ',26):-1;if(cut<1)cut=min((int)s.length(),26);centerText(s.substring(0,cut),y,1,GC9A01A_WHITE);s=s.substring(cut);s.trim();y+=16; }
  centerText("Use MIC in Android app",185,1,GC9A01A_YELLOW); centerText("DOWN: BACK",207,1,GC9A01A_WHITE);
}
void drawAbout(){frame("ABOUT");centerText("TiTaN LAB",83,2,GC9A01A_WHITE);centerText("OF INNOVATION",108,1,GC9A01A_CYAN);centerText("By MD",135,2,GC9A01A_YELLOW);centerText("Battery sensing not fitted",167,1,GC9A01A_WHITE);centerText("DOWN: BACK",207,1,GC9A01A_CYAN);}
void render(){ if(displaySleeping)return; switch(page){case 0:drawHome();break;case 1:drawMenu();break;case 2:drawQuick();break;case 3:drawNote();break;case 4:drawVoice();break;default:drawAbout();break;} }

class ServerCallbacks : public BLEServerCallbacks {
 void onConnect(BLEServer*) override { phoneConnected=true; statusLine="Phone connected"; sendEvent("READY"); if(!displaySleeping)render(); }
 void onDisconnect(BLEServer *s) override { phoneConnected=false; statusLine="Phone disconnected"; s->startAdvertising(); if(!displaySleeping)render(); }
};
class CommandCallbacks : public BLECharacteristicCallbacks {
 void onWrite(BLECharacteristic *c) override {
  String cmd=c->getValue().c_str(); cmd.trim(); Serial.println("BLE command: "+cmd);
  if(cmd=="PING") sendEvent("PONG");
  else if(cmd=="STATUS") sendEvent(String("STATUS|BLE=")+(bleEnabled?"ON":"OFF")+"|PHONE="+(phoneConnected?"CONNECTED":"DISCONNECTED")+"|BATTERY=NOT_CONFIGURED");
  else if(cmd=="HOME"){page=0;displaySleeping=false;render();sendEvent("ACK|HOME");}
  else if(cmd=="MENU"){page=1;cursor=0;displaySleeping=false;render();sendEvent("ACK|MENU");}
  else if(cmd=="CLEAR"){noteTitle="No notifications";noteBody="Notification list cleared";render();sendEvent("ACK|CLEAR");}
  else if(cmd.startsWith("NOTE|")){int p=cmd.indexOf('|',5); if(p>0){noteTitle=cmd.substring(5,p);noteBody=cmd.substring(p+1);if(noteTitle.length()>80)noteTitle=noteTitle.substring(0,80);if(noteBody.length()>220)noteBody=noteBody.substring(0,220);page=3;displaySleeping=false;render();sendEvent("ACK|NOTE");}else sendEvent("ERR|NOTE_FORMAT");}
  else if(cmd.startsWith("VOICE|")){voiceText=cmd.substring(6);if(voiceText.length()>140)voiceText=voiceText.substring(0,140);page=4;displaySleeping=false;render();sendEvent("ACK|VOICE_TEXT");}
  else sendEvent("ERR|UNKNOWN_COMMAND");
 }
};
void startBle(){
 BLEDevice::init("TiTaN ARIS Watch"); bleServer=BLEDevice::createServer();bleServer->setCallbacks(new ServerCallbacks());
 BLEService *svc=bleServer->createService(SERVICE_UUID);
 commandChar=svc->createCharacteristic(COMMAND_UUID,BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_WRITE_NR);
 commandChar->setCallbacks(new CommandCallbacks());
 eventChar=svc->createCharacteristic(EVENT_UUID,BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_NOTIFY);
 eventChar->addDescriptor(new BLE2902());eventChar->setValue("READY");svc->start();
 BLEAdvertising *adv=BLEDevice::getAdvertising();adv->addServiceUUID(SERVICE_UUID);adv->setScanResponse(true);BLEDevice::startAdvertising();
 bleEnabled=true;statusLine="BLE advertising";
}
void stopBle(){if(!bleEnabled)return; BLEDevice::deinit(true);bleServer=nullptr;commandChar=nullptr;eventChar=nullptr;phoneConnected=false;bleEnabled=false;statusLine="BLE off";}
void handleButtons(){
 bool u=pressed(upB),o=pressed(okB),d=pressed(downB);
 if(u||o||d){lastActivity=millis();if(displaySleeping){displaySleeping=false;render();return;}}
 if(digitalRead(BTN_DOWN)==LOW){if(!downStart){downStart=millis();downLongDone=false;}if(!downLongDone&&page!=0&&millis()-downStart>=1000){page=0;cursor=0;downLongDone=true;render();}}else{downStart=0;downLongDone=false;}if(downLongDone)d=false;
 if(page==0){if(o){page=1;cursor=0;render();}}
 else if(page==1){if(u){cursor=(cursor+3)%4;render();}if(d){cursor=(cursor+1)%4;render();}if(o){page=(cursor==0?2:cursor==1?3:cursor==2?4:5);render();}}
 else if(page==2){if(u){if(bleEnabled)stopBle();else startBle();render();}if(o){sleepEnabled=!sleepEnabled;render();}if(d){page=1;render();}}
 else if(page>=3&&page<=5){if(d||o){page=1;render();}}
}
void setup(){Serial.begin(115200);pinMode(BTN_UP,INPUT_PULLUP);pinMode(BTN_OK,INPUT_PULLUP);pinMode(BTN_DOWN,INPUT_PULLUP);
 SPI.begin(TFT_SCLK,-1,TFT_MOSI,TFT_CS);tft.begin();tft.setRotation(0);tft.fillScreen(GC9A01A_BLACK);
 centerText("TiTaN LAB",78,2,GC9A01A_CYAN);centerText("ARIS WATCH OS",108,1,GC9A01A_WHITE);delay(450);
 lastActivity=millis();startBle();page=0;render();Serial.println("TiTaN ARIS Watch OS V2 Lite started. Battery sensing not configured.");}
void loop(){handleButtons();if(sleepEnabled&&!displaySleeping&&page==0&&millis()-lastActivity>30000UL){tft.fillScreen(GC9A01A_BLACK);displaySleeping=true;}
 if(displaySleeping){delay(10);return;}if(page==0&&millis()-lastClockDraw>60000UL){lastClockDraw=millis();render();}delay(8);}
