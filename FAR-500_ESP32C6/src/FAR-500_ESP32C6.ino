/* =============================================================================
   FAR-500  -  Force/Angle Recorder - 500 Newton
   Target : ESP32-C6 SuperMini (TinyTronics) - 18650 via schuifschakelaar

   v5 wijzigingen:
     - GEEN as-schakelen meer. De justering (0 gr / 45 gr) legt de kantelas +
       nulvector vast als volledige 3D-vectoren (ref0, axisV) i.p.v. 1 raw
       as + scalaire offset/gain. De hoek wordt live berekend als de
       (getekende) rotatiehoek van de actuele g-vector t.o.v. ref0, rond
       axisV. Daardoor blijft de gemeten hoek zuiver, ook als de sensor met
       een vaste scheefstand (bv. 10 gr op de X-as) gemonteerd zit.
     - Ruwe hoeken X/Y/Z worden meegestuurd naar de laptop, puur als diagnose
       om te zien of de sensor scheef gaat staan. (stream: ms,deg,N,bat,rx,ry,rz)
       Het live cijfer tijdens justeren (rechtsboven op het CAL-scherm) toont
       de ruwe Z-hoek (was X-as).

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

/* ---------- meetas -----------------------------------------------------------
   Geen vaste as meer. De justering (0 gr / 45 gr) legt de kantelas + nulvector
   vast als volledige 3D-vectoren (i.p.v. 1 as + scalaire offset/gain). Daardoor
   blijft de gemeten hoek zuiver, ook als de sensor met een vaste scheefstand
   (bv. 10 gr) op een andere as gemonteerd zit. --------------------------------*/

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

float ref0[3]={0,0,1};           // gekalibreerde nulvector (0 gr), persistent
float axisV[3]={0,1,0};          // gekalibreerde kantelas-richting, persistent
float gainV=1.0f;                // schaalcorrectie 2e justeerpunt (persistent)
float rawX=NAN, rawY=NAN, rawZ=NAN;   // ruwe hoeken (diagnose)
float curVec[3]={NAN,NAN,NAN};   // ruwe genormaliseerde g-vector (huidige meting)
float curRaw=NAN;                // ruwe Z-hoek, alleen live-indicatie tijdens justeren
float lastForce=NAN;
float batV=0; int batPct=0; int fsFreePct=100;
bool  logging=false; uint32_t runStartMs=0;
uint16_t measNum=0;   // meting-volgnummer, telt op vanaf 1 bij elke START, reset bij herstart (niet persistent)
File  logFile; bool logOpen=false;
volatile bool reqTare=false, reqDump=false, reqClear=false;

int   mode=M_NORMAL;
float calVec0[3]={0,0,0};
char  msgL1[16]="", msgL2[16]=""; uint32_t msgUntil=0;
bool  btnWas=false; uint32_t btnDownAt=0;

Preferences prefs;
char  sbuf[48]; uint8_t sidx=0;
uint32_t tLive=0,tLog=0,tOled=0,tBat=0,tFlush=0;

/* ============================ VECTORWISKUNDE =============================
   Rotatiehoek van v om axis, getekend, t.o.v. referentievector ref:
   angle = atan2( axis . (ref x v), ref . v ). Geldig voor élke axis-richting
   -> ongevoelig voor een vaste montage-scheefstand op een andere as, zolang
   de mechanische beweging een zuivere rotatie om 1 vaste as t.o.v. de sensor
   blijft (wat hier het geval is). ---------------------------------------- */
inline void vNorm(float v[3]){
  float m=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
  if(m>1e-6f){ v[0]/=m; v[1]/=m; v[2]/=m; }
}
inline float vDot(const float a[3], const float b[3]){ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline void vCross(const float a[3], const float b[3], float out[3]){
  out[0]=a[1]*b[2]-a[2]*b[1]; out[1]=a[2]*b[0]-a[0]*b[2]; out[2]=a[0]*b[1]-a[1]*b[0];
}

/* ============================ ADXL345 ===================================== */
void adxlW(uint8_t r,uint8_t v){ Wire.beginTransmission(ADXL_ADDR); Wire.write(r); Wire.write(v); Wire.endTransmission(); }
bool adxlBegin(){
  Wire.beginTransmission(ADXL_ADDR); if(Wire.endTransmission()!=0) return false;
  adxlW(ADXL_FMT,0x08); adxlW(ADXL_POWER,0x08); return true;
}
// Genormaliseerde g-vector (vx,vy,vz) voor de justering/hoekberekening, plus
// de ruwe kantelhoeken X/Y/Z t.o.v. horizontaal (diagnose, naar laptop).
bool adxlRead(float &vx, float &vy, float &vz, float &rx, float &ry, float &rz){
  Wire.beginTransmission(ADXL_ADDR); Wire.write(ADXL_DATA);
  if(Wire.endTransmission(false)!=0) return false;
  if(Wire.requestFrom(ADXL_ADDR,6)!=6) return false;
  int16_t x=Wire.read()|(Wire.read()<<8), y=Wire.read()|(Wire.read()<<8), z=Wire.read()|(Wire.read()<<8);
  float ax=x/256.0f, ay=y/256.0f, az=z/256.0f;
  rx=atan2f(ax, sqrtf(ay*ay+az*az))*180.0f/(float)M_PI;   // pitch
  ry=atan2f(ay, sqrtf(ax*ax+az*az))*180.0f/(float)M_PI;   // roll
  rz=atan2f(az, sqrtf(ax*ax+ay*ay))*180.0f/(float)M_PI;   // Z t.o.v. horizontaal (~90 als vlak)
  float m=sqrtf(ax*ax+ay*ay+az*az);
  if(m<1e-6f) return false;
  vx=ax/m; vy=ay/m; vz=az/m;
  return true;
}

/* ============================ SAUTER ====================================== */
/* De FH500 stuurt een kaal ASCII-frame (7 tekens) zonder CR/LF-afsluiter, met
   een stilte van >20ms tussen twee uitlezingen. Einde-frame wordt daarom
   gedetecteerd via die stilte i.p.v. via '\n'/'\r'.
   Eerste teken is een sign-flag: '1' = positief, alles anders = negatief.
   Rest is de zero-padded waarde met 1 decimaal, bv. "10005.5" = +5.5,
   "00003.3" = -3.3.
   Tijdelijke troubleshoot-hulp: print elke ruwe byte en elke ontvangen regel
   naar de USB-seriemonitor (115200), en waarschuw als er 3s niets binnenkomt.
   Zet SAUTER_DEBUG op 0 zodra de uitlezing stabiel werkt. */
#define SAUTER_DEBUG 1
#define SAUTER_FRAME_GAP_MS 20
uint32_t tLastSauterByte=0;
void sauterRequest(){ Serial1.write('9'); }
void sauterParse(){
  sbuf[sidx]='\0'; char* p=sbuf; while(*p==' ') p++;
#if SAUTER_DEBUG
  Serial.printf("[sauter] regel: \"%s\"\n", p);
#endif
  if(strlen(p)>=2){ char sg=p[0]; float mag=atof(p+1); lastForce=(sg=='1')?mag:-mag; }
}
void sauterPoll(){
  while(Serial1.available()){
    char c=(char)Serial1.read();
    uint32_t now=millis(); uint32_t dt=now-tLastSauterByte; tLastSauterByte=now;
#if SAUTER_DEBUG
    Serial.printf("[sauter] +%4lums byte: 0x%02X '%c'\n", (unsigned long)dt, (uint8_t)c, (c>=32&&c<127)?c:'.');
#endif
    if(c=='\n'||c=='\r'){ if(sidx>0){ sauterParse(); sidx=0; } }
    else if(sidx<sizeof(sbuf)-1) sbuf[sidx++]=c; else sidx=0; }
  uint32_t now=millis();
  if(sidx>0 && now-tLastSauterByte>=SAUTER_FRAME_GAP_MS){ sauterParse(); sidx=0; }
#if SAUTER_DEBUG
  static uint32_t tWarn=0;
  if(now-tLastSauterByte>3000 && now-tWarn>3000){
    tWarn=now; Serial.println("[sauter] WAARSCHUWING: al 3s geen byte ontvangen op Serial1 (RX=GPIO4)");
  }
#endif
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
void updateFsFree(){ size_t tot=LittleFS.totalBytes(), used=LittleFS.usedBytes();
  if(tot>0) fsFreePct=(int)(100.0*(tot-used)/tot); }

void toggleLogging(){
  if(!logging){
    logging=true; runStartMs=millis(); measNum++;
    logOpenW();
    // markeert het begin van deze meting in het gecombineerde logbestand, zodat
    // de laptop-UI de geschiedenis kan opsplitsen per meting bij het importeren.
    if(logOpen) logFile.printf("#MEAS,%u,%lu\n", measNum, (unsigned long)runStartMs);
    updateFsFree();
  } else { logging=false; logCloseF(); }
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
    else if(sh && !isnan(curVec[0])){
      calVec0[0]=curVec[0]; calVec0[1]=curVec[1]; calVec0[2]=curVec[2]; mode=M_CAL2;
    }
  } else if(mode==M_CAL2){
    if(lo){ showMsg("JUSTERING","GEANNULEERD",2000); }
    else if(sh && !isnan(curVec[0])){
      float axis[3]; vCross(calVec0,curVec,axis);
      float axisMag=sqrtf(vDot(axis,axis));
      float rawAngle=atan2f(axisMag, vDot(calVec0,curVec))*180.0f/(float)M_PI;
      if(rawAngle>2.0f){
        vNorm(axis);
        ref0[0]=calVec0[0]; ref0[1]=calVec0[1]; ref0[2]=calVec0[2];
        axisV[0]=axis[0]; axisV[1]=axis[1]; axisV[2]=axis[2];
        gainV=45.0f/rawAngle;
        prefs.putFloat("r0x",ref0[0]); prefs.putFloat("r0y",ref0[1]); prefs.putFloat("r0z",ref0[2]);
        prefs.putFloat("axx",axisV[0]); prefs.putFloat("axy",axisV[1]); prefs.putFloat("axz",axisV[2]);
        prefs.putFloat("gv",gainV);
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
  // meting-volgnummer: vierkant rechtsboven, naast de hoek, nummer rechts uitgelijnd erin
  oled.setFont(u8g2_font_5x7_tf);
  char mnum[6]; if(measNum>0) snprintf(mnum,sizeof(mnum),"%u",measNum); else strcpy(mnum,"-");
  const int mbW=26, mbH=12, mbX=127-mbW, mbY=12;
  oled.drawFrame(mbX,mbY,mbW,mbH);
  oled.drawStr(mbX+mbW-2-oled.getStrWidth(mnum), mbY+9, mnum);
  char f[16]; if(isnan(lastForce)) strcpy(f,"F  ---  N"); else snprintf(f,sizeof(f),"F %+.1f N",lastForce);
  oled.setFont(u8g2_font_7x13B_tf); oled.drawStr(0,54,f);
  if(logging){
    int phase=(millis()/1000)%3;                 // elke seconde wisselen
    oled.setFont(u8g2_font_5x7_tf);
    if(phase==0){ oled.drawDisc(3,60,2); oled.drawStr(9,63,"REC"); }
    else if(phase==1){ uint32_t s=(millis()-runStartMs)/1000;
      char tt[8]; snprintf(tt,sizeof(tt),"%u:%02u",(unsigned)(s/60),(unsigned)(s%60)); oled.drawStr(3,63,tt); }
    else { char mm[12]; snprintf(mm,sizeof(mm),"vrij %d%%",fsFreePct); oled.drawStr(3,63,mm); }
  }
  drawBluetooth(119,54,laptopConnected);
  oled.sendBuffer();
}

/* ============================ SETUP ====================================== */
void setup(){
  Serial.begin(115200); delay(150);
  analogReadResolution(12);
  pinMode(BTN_PIN, INPUT_PULLUP);

  prefs.begin("logger",false);
  ref0[0]=prefs.getFloat("r0x",ref0[0]); ref0[1]=prefs.getFloat("r0y",ref0[1]); ref0[2]=prefs.getFloat("r0z",ref0[2]);
  axisV[0]=prefs.getFloat("axx",axisV[0]); axisV[1]=prefs.getFloat("axy",axisV[1]); axisV[2]=prefs.getFloat("axz",axisV[2]);
  gainV=prefs.getFloat("gv",gainV);
  vNorm(ref0); vNorm(axisV);

  Wire.begin(I2C_SDA,I2C_SCL);
  oled.setBusClock(400000); oled.begin();
  oled.clearBuffer(); oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0,20,"FAR-500 v5"); oled.drawStr(0,40,"opstarten..."); oled.sendBuffer();
  if(!adxlBegin()) Serial.println("[WAARSCHUWING] ADXL345 niet gevonden (0x53)");

  Serial1.begin(SAUTER_BAUD, SERIAL_8N1, SAUTER_RX, SAUTER_TX);
  if(!LittleFS.begin(true)) Serial.println("[FOUT] LittleFS");
  else { updateFsFree(); size_t tot=LittleFS.totalBytes(), used=LittleFS.usedBytes();
    // ~22 byte/regel bij 5 Hz -> minuten die nog passen
    long minLeft=(long)((tot-used)/22.0/5.0/60.0);
    Serial.printf("[fs] totaal %u B, gebruikt %u B, vrij %d%%, ~%ld min log ruimte (5 Hz)\n",
                  (unsigned)tot,(unsigned)used,fsFreePct,minLeft); }

  NimBLEDevice::init(DEVICE_NAME); NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEServer* srv=NimBLEDevice::createServer(); srv->setCallbacks(new SrvCB());
  NimBLEService* svc=srv->createService(SERVICE_UUID);
  pData=svc->createCharacteristic(DATA_UUID, NIMBLE_PROPERTY::NOTIFY);
  pLog =svc->createCharacteristic(LOG_UUID,  NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* pCtrl=svc->createCharacteristic(CTRL_UUID,
        NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_NR);
  pCtrl->setCallbacks(new CtrlCB());
  srv->start();
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

  bool ok=adxlRead(curVec[0],curVec[1],curVec[2], rawX,rawY,rawZ);
  if(!ok){ curVec[0]=curVec[1]=curVec[2]=NAN; }
  curRaw = ok?rawZ:NAN;   // live diagnose-cijfer tijdens justeren: ruwe Z-hoek

  handleButton(now);
  if(mode==M_MSG && now>msgUntil) mode=M_NORMAL;

  if(reqTare){ reqTare=false; if(!isnan(curVec[0])){
      ref0[0]=curVec[0]; ref0[1]=curVec[1]; ref0[2]=curVec[2];
      prefs.putFloat("r0x",ref0[0]); prefs.putFloat("r0y",ref0[1]); prefs.putFloat("r0z",ref0[2]); } }
  if(reqClear){ reqClear=false; logClearF(); }
  if(reqDump){ reqDump=false; logDumpBle(); }

  float angle;
  if(isnan(curVec[0])) angle=NAN;
  else{
    float cr[3]; vCross(ref0,curVec,cr);
    angle = atan2f(vDot(axisV,cr), vDot(ref0,curVec)) * 180.0f/(float)M_PI * gainV;
  }

#if BAT_MONITOR
  if(now-tBat>=BAT_MS){ tBat=now; batV=readBatV(); batPct=voltToPct(batV);
    Serial.printf("[bat] %.3f V  %d%%\n", batV, batPct); }
#endif

  if(now-tLive>=LIVE_MS){ tLive=now; sauterRequest();
    if(laptopConnected){ uint32_t t=logging?(now-runStartMs):0;
      char line[80]; snprintf(line,sizeof(line),"%lu,%.1f,%.0f,%d,%.1f,%.1f,%.1f,%d",
        (unsigned long)t, angle, lastForce, batPct, rawX, rawY, rawZ, logging?1:0);
      pData->setValue(line); pData->notify(); } }
  if(logging && logOpen && now-tLog>=LOG_MS){ tLog=now;
    char row[48]; snprintf(row,sizeof(row),"%lu,%.1f,%.1f",(unsigned long)(now-runStartMs),angle,lastForce);
    logFile.println(row); }
  if(logOpen && now-tFlush>=FLUSH_MS){ tFlush=now; logFile.flush(); updateFsFree(); }
  if(now-tOled>=OLED_MS){ tOled=now; drawOled(angle); }

  delay(4);
}
