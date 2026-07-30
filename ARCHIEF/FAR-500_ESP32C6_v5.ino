/* =============================================================================
   FAR-500_Logger v5  -  Force/Angle datalogger HUB
   Target : ESP32-C6 SuperMini (TinyTronics) - 18650 via schuifschakelaar

   v5 wijzigingen:
     - GEEN as-schakelen meer. Meetas ligt vast (MEAS_AXIS). Na justering telt
       alleen de gekalibreerde hoek.
     - Ruwe hoeken X/Y/Z worden meegestuurd naar de laptop, puur als diagnose
       om te zien of de sensor scheef gaat staan. (stream: ms,deg,N,bat,rx,ry,rz)

   Drukknop (GPIO2 -> GND, INPUT_PULLUP):
     - KORT (0,5-2 s) : meting START / STOP
     - LANG (>=3 s)   : twee-punts justering (stap1 = 0 gr, stap2 = 45 gr)
     - 2-3 s          : dode zone

   LIBRARIES: "NimBLE-Arduino" v2.x (getest 2.5.0), "U8g2"
   BOARD: esp32 by Espressif >= 3.0.x, "ESP32C6 Dev Module", USB CDC On Boot = On
   ============================================================================= */

#include <Wire.h>
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <U8g2lib.h>
#include <math.h>

/* ---------- meetas (geen schakelaar; wijzig hier als je Y wilt) ------------- */
#define MEAS_AXIS   0        // 0 = X (pitch), 1 = Y (roll)

/* ---------- PINNEN (vermijd 8,9,15 en 12,13) -------------------------------- */
#define I2C_SDA       6
#define I2C_SCL       7
#define SAUTER_RX     4
#define SAUTER_TX     5
#define SAUTER_BAUD   9600
#define BAT_MONITOR   1
#define BAT_ADC       1
#define DIVIDER_RATIO 2.268f
#define BTN_PIN       2

/* ---------- knop-timing (ms) ------------------------------------------------ */
#define BTN_SHORT_MIN 500
#define BTN_SHORT_MAX 2000
#define BTN_LONG      3000

/* ---------- overige timing -------------------------------------------------- */
#define LIVE_MS 100
#define LOG_MS  200
#define OLED_MS 150
#define BAT_MS  5000
#define FLUSH_MS 1000

/* ---------- BLE ------------------------------------------------------------- */
#define SERVICE_UUID "9a8b0001-1d4c-4f6e-9b2a-2f3e4d5c6b7a"
#define DATA_UUID    "9a8b0002-1d4c-4f6e-9b2a-2f3e4d5c6b7a"
#define CTRL_UUID    "9a8b0003-1d4c-4f6e-9b2a-2f3e4d5c6b7a"
#define LOG_UUID     "9a8b0004-1d4c-4f6e-9b2a-2f3e4d5c6b7a"
#define DEVICE_NAME  "FAR-500"

/* ---------- ADXL345 --------------------------------------------------------- */
#define ADXL_ADDR 0x53
#define ADXL_POWER 0x2D
#define ADXL_FMT   0x31
#define ADXL_DATA  0x32

#define LOG_PATH "/log.csv"
#define CSV_HEADER "ms,angle_deg,force_N"

/* ---------- modi ------------------------------------------------------------ */
#define M_NORMAL 0
#define M_CAL1   1
#define M_CAL2   2
#define M_MSG    3

/* ============================ GLOBALS ===================================== */
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
// SSD1306-paneel? -> U8G2_SSD1306_128X64_NONAME_F_HW_I2C

NimBLECharacteristic *pData=nullptr, *pLog=nullptr;
bool laptopConnected=false;

float offs=0.0f, gain=1.0f;      // justering meetas (persistent)
float rawX=NAN, rawY=NAN, rawZ=NAN;   // ruwe hoeken (diagnose)
float curRaw=NAN;                // ruwe hoek van de meetas
float lastForce=NAN;
float batV=0; int batPct=0;
bool  logging=false; uint32_t runStartMs=0;
File  logFile; bool logOpen=false;
volatile bool reqTare=false, reqDump=false, reqClear=false;

int   mode=M_NORMAL;
float calRaw0=0;
char  msgL1[16]="", msgL2[16]=""; uint32_t msgUntil=0;
bool  btnWas=false; uint32_t btnDownAt=0;

Preferences prefs;
char  sbuf[48]; uint8_t sidx=0;
uint32_t tLive=0,tLog=0,tOled=0,tBat=0,tFlush=0;

/* ============================ ADXL345 ===================================== */
void adxlW(uint8_t r,uint8_t v){ Wire.beginTransmission(ADXL_ADDR); Wire.write(r); Wire.write(v); Wire.endTransmission(); }
bool adxlBegin(){
  Wire.beginTransmission(ADXL_ADDR); if(Wire.endTransmission()!=0) return false;
  adxlW(ADXL_FMT,0x08); adxlW(ADXL_POWER,0x08); return true;
}
// Ruwe kantelhoeken van X-, Y- en Z-as t.o.v. horizontaal.
bool adxlRaw(float &rx, float &ry, float &rz){
  Wire.beginTransmission(ADXL_ADDR); Wire.write(ADXL_DATA);
  if(Wire.endTransmission(false)!=0) return false;
  if(Wire.requestFrom(ADXL_ADDR,6)!=6) return false;
  int16_t x=Wire.read()|(Wire.read()<<8), y=Wire.read()|(Wire.read()<<8), z=Wire.read()|(Wire.read()<<8);
  float ax=x/256.0f, ay=y/256.0f, az=z/256.0f;
  rx=atan2f(ax, sqrtf(ay*ay+az*az))*180.0f/(float)M_PI;   // pitch
  ry=atan2f(ay, sqrtf(ax*ax+az*az))*180.0f/(float)M_PI;   // roll
  rz=atan2f(az, sqrtf(ax*ax+ay*ay))*180.0f/(float)M_PI;   // Z t.o.v. horizontaal (~90 als vlak)
  return true;
}

/* ============================ SAUTER ====================================== */
void sauterRequest(){ Serial1.write('9'); }
void sauterParse(){
  sbuf[sidx]='\0'; char* p=sbuf; while(*p==' ') p++;
  if(strlen(p)>=2){ char sg=p[0]; float mag=atof(p+1); lastForce=(sg=='1')?mag:-mag; }
}
void sauterPoll(){
  while(Serial1.available()){ char c=(char)Serial1.read();
    if(c=='\n'||c=='\r'){ if(sidx>0){ sauterParse(); sidx=0; } }
    else if(sidx<sizeof(sbuf)-1) sbuf[sidx++]=c; else sidx=0; }
}

/* ============================ LITTLEFS ==================================== */
void logOpenW(){ bool ex=LittleFS.exists(LOG_PATH);
  logFile=LittleFS.open(LOG_PATH, ex?FILE_APPEND:FILE_WRITE);
  if(logFile){ if(!ex||logFile.size()==0) logFile.println(CSV_HEADER); logOpen=true; } }
void logCloseF(){ if(logOpen){ logFile.flush(); logFile.close(); logOpen=false; } }
void logClearF(){ logCloseF(); LittleFS.remove(LOG_PATH); }
void logDumpBle(){ if(!laptopConnected||!pLog) return;
  File f=LittleFS.open(LOG_PATH,FILE_READ);
  if(!f){ pLog->setValue("<<EOF>>"); pLog->notify(); return; }
  while(f.available()){ String l=f.readStringUntil('\n'); pLog->setValue(l.c_str()); pLog->notify(); delay(12); }
  f.close(); pLog->setValue("<<EOF>>"); pLog->notify(); }

/* ============================ MEET-STEUR ================================== */
void showMsg(const char* l1,const char* l2,uint32_t ms){
  strncpy(msgL1,l1,sizeof(msgL1)-1); msgL1[sizeof(msgL1)-1]=0;
  strncpy(msgL2,l2,sizeof(msgL2)-1); msgL2[sizeof(msgL2)-1]=0;
  msgUntil=millis()+ms; mode=M_MSG;
}
void toggleLogging(){
  if(!logging){ logging=true; runStartMs=millis(); logOpenW(); }
  else       { logging=false; logCloseF(); }
}

/* ============================ DRUKKNOP ==================================== */
void onBtn(uint32_t dur){
  if(dur<120) return;
  bool sh=(dur>=BTN_SHORT_MIN && dur<=BTN_SHORT_MAX), lo=(dur>=BTN_LONG);
  if(mode==M_NORMAL){
    if(lo){ if(!logging) mode=M_CAL1; }
    else if(sh){ toggleLogging(); }
  } else if(mode==M_CAL1){
    if(lo){ showMsg("JUSTERING","GEANNULEERD",2000); }
    else if(sh && !isnan(curRaw)){ calRaw0=curRaw; mode=M_CAL2; }
  } else if(mode==M_CAL2){
    if(lo){ showMsg("JUSTERING","GEANNULEERD",2000); }
    else if(sh && !isnan(curRaw)){
      float r45=curRaw;
      if(fabsf(r45-calRaw0)>1.0f){
        gain=45.0f/(r45-calRaw0); offs=calRaw0;
        prefs.putFloat("gn",gain); prefs.putFloat("off",offs);
        showMsg("JUSTERING","GESLAAGD",5000);
      } else showMsg("JUSTERING","MISLUKT",3000);
    }
  }
}
void handleButton(uint32_t now){
  bool pressed=(digitalRead(BTN_PIN)==LOW);
  if(pressed && !btnWas){ btnWas=true; btnDownAt=now; }
  else if(!pressed && btnWas){ btnWas=false; onBtn(now-btnDownAt); }
}

/* ============================ BLE SERVER (NimBLE 2.x) ===================== */
class SrvCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& ci) override { laptopConnected=true; }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& ci, int reason) override {
    laptopConnected=false; NimBLEDevice::startAdvertising(); }
};
class CtrlCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& ci) override {
    String cmd=String(c->getValue().c_str()); cmd.trim(); cmd.toUpperCase();
    if(cmd=="START"){ if(mode==M_NORMAL && !logging) toggleLogging(); }
    else if(cmd=="STOP"){ if(logging) toggleLogging(); }
    else if(cmd=="TARE"){ if(mode==M_NORMAL) reqTare=true; }   // alleen via laptop-UI
    else if(cmd=="DUMP"){ reqDump=true; }
    else if(cmd=="CLEAR"){ reqClear=true; }
    // as-schakelen bestaat niet meer
  }
};

/* ============================ BATTERIJ =================================== */
float readBatV(){ return analogReadMilliVolts(BAT_ADC)/1000.0f*DIVIDER_RATIO; }
int voltToPct(float v){
  const float V[]={3.00,3.30,3.50,3.60,3.70,3.80,3.90,4.00,4.10,4.20};
  const float P[]={0,10,20,35,50,62,74,85,93,100};
  if(v<=V[0])return 0; if(v>=V[9])return 100;
  for(int i=1;i<10;i++) if(v<V[i]) return (int)(P[i-1]+(P[i]-P[i-1])*(v-V[i-1])/(V[i]-V[i-1]));
  return 100;
}

/* ============================ OLED ======================================= */
void drawCentered(int y,const char* s){ oled.drawStr((128-oled.getStrWidth(s))/2, y, s); }
void drawBluetooth(int x,int y,bool connected){
  oled.drawLine(x+3,y,x+3,y+8); oled.drawLine(x+3,y,x+6,y+2); oled.drawLine(x+6,y+2,x,y+6);
  oled.drawLine(x+3,y+8,x+6,y+6); oled.drawLine(x+6,y+6,x,y+2);
  if(!connected) oled.drawLine(x-1,y+9,x+7,y-1);
}
void drawOled(float angle){
  oled.clearBuffer();
  if(mode==M_CAL1 || mode==M_CAL2){
    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(0,10, mode==M_CAL1?"JUSTEREN 1/2":"JUSTEREN 2/2");
    oled.drawStr(0,28, mode==M_CAL1?"Stap 1: 0 graden":"Stap 2: 45 graden");
    oled.drawStr(0,42, "waterpas, kort=OK");
    oled.drawStr(0,58, "3s = annuleren");
    char r[12]; if(!isnan(curRaw)) snprintf(r,sizeof(r),"%.1f",curRaw); else strcpy(r,"--");
    oled.drawStr(128-oled.getStrWidth(r),10,r);
    oled.sendBuffer(); return;
  }
  if(mode==M_MSG){
    oled.setFont(u8g2_font_7x13B_tf); drawCentered(30,msgL1); drawCentered(48,msgL2);
    oled.sendBuffer(); return;
  }
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0,9,"HOEK");
  char b[8]; if(batPct<0) strcpy(b,"USB"); else snprintf(b,sizeof(b),"%d%%",batPct);
  oled.drawStr(128-oled.getStrWidth(b),9,b);
  char a[12]; if(isnan(angle)) strcpy(a,"--.-"); else snprintf(a,sizeof(a),"%.1f",angle);
  oled.setFont(u8g2_font_logisoso24_tn); oled.drawStr(0,40,a);
  oled.setFont(u8g2_font_6x10_tf); oled.drawStr(oled.getStrWidth(a)+2,22,"\xb0");
  char f[16]; if(isnan(lastForce)) strcpy(f,"F  ---  N"); else snprintf(f,sizeof(f),"F %+.0f N",lastForce);
  oled.setFont(u8g2_font_7x13B_tf); oled.drawStr(0,54,f);
  if(logging){ oled.drawDisc(3,60,2); oled.setFont(u8g2_font_5x7_tf); oled.drawStr(9,63,"REC"); }
  drawBluetooth(119,54,laptopConnected);
  oled.sendBuffer();
}

/* ============================ SETUP ====================================== */
void setup(){
  Serial.begin(115200); delay(150);
  analogReadResolution(12);
  pinMode(BTN_PIN, INPUT_PULLUP);

  prefs.begin("logger",false);
  offs=prefs.getFloat("off",0.0f); gain=prefs.getFloat("gn",1.0f);

  Wire.begin(I2C_SDA,I2C_SCL);
  oled.setBusClock(400000); oled.begin();
  oled.clearBuffer(); oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0,20,"FAR-500 Logger v5"); oled.drawStr(0,40,"opstarten..."); oled.sendBuffer();
  if(!adxlBegin()) Serial.println("[WAARSCHUWING] ADXL345 niet gevonden (0x53)");

  Serial1.begin(SAUTER_BAUD, SERIAL_8N1, SAUTER_RX, SAUTER_TX);
  if(!LittleFS.begin(true)) Serial.println("[FOUT] LittleFS");

  NimBLEDevice::init(DEVICE_NAME); NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEServer* srv=NimBLEDevice::createServer(); srv->setCallbacks(new SrvCB());
  NimBLEService* svc=srv->createService(SERVICE_UUID);
  pData=svc->createCharacteristic(DATA_UUID, NIMBLE_PROPERTY::NOTIFY);
  pLog =svc->createCharacteristic(LOG_UUID,  NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* pCtrl=svc->createCharacteristic(CTRL_UUID,
        NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_NR);
  pCtrl->setCallbacks(new CtrlCB()); svc->start();
  NimBLEAdvertising* adv=NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID); adv->setName(DEVICE_NAME); adv->start();

#if BAT_MONITOR
  batV=readBatV(); batPct=voltToPct(batV);
#else
  batPct=-1;
#endif
}

/* ============================ LOOP ======================================= */
void loop(){
  sauterPoll();
  uint32_t now=millis();

  bool ok=adxlRaw(rawX,rawY,rawZ);
  curRaw = !ok?NAN : (MEAS_AXIS==0?rawX:rawY);

  handleButton(now);
  if(mode==M_MSG && now>msgUntil) mode=M_NORMAL;

  if(reqTare){ reqTare=false; if(!isnan(curRaw)){ offs=curRaw; prefs.putFloat("off",offs); } }
  if(reqClear){ reqClear=false; logClearF(); }
  if(reqDump){ reqDump=false; logDumpBle(); }

  float angle = isnan(curRaw)? NAN : (curRaw-offs)*gain;

#if BAT_MONITOR
  if(now-tBat>=BAT_MS){ tBat=now; batV=readBatV(); batPct=voltToPct(batV);
    Serial.printf("[bat] %.3f V  %d%%\n", batV, batPct); }
#endif

  if(now-tLive>=LIVE_MS){ tLive=now; sauterRequest();
    if(laptopConnected){ uint32_t t=logging?(now-runStartMs):0;
      char line[80]; snprintf(line,sizeof(line),"%lu,%.1f,%.0f,%d,%.1f,%.1f,%.1f",
        (unsigned long)t, angle, lastForce, batPct, rawX, rawY, rawZ);
      pData->setValue(line); pData->notify(); } }
  if(logging && logOpen && now-tLog>=LOG_MS){ tLog=now;
    char row[48]; snprintf(row,sizeof(row),"%lu,%.1f,%.1f",(unsigned long)(now-runStartMs),angle,lastForce);
    logFile.println(row); }
  if(logOpen && now-tFlush>=FLUSH_MS){ tFlush=now; logFile.flush(); }
  if(now-tOled>=OLED_MS){ tOled=now; drawOled(angle); }

  delay(4);
}
