/* =============================================================================
   FH500_Logger v2  -  Force/Angle datalogger HUB
   Target : ESP32-C6 SuperMini (TinyTronics) - 18650 gevoed via schuifschakelaar
   ============================================================================= */

#include <Wire.h>
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <U8g2lib.h>
#include <math.h>

/* ---------- PINNEN  (controleer tegen je board; vermijd 8,9,15 en 12,13) ---- */
#define I2C_SDA       6      // ADXL345 + OLED
#define I2C_SCL       7
#define SAUTER_RX     4      // ESP RX <- TXD Sauter pin5 (via MAX3232)
#define SAUTER_TX     5      // ESP TX -> RXD Sauter pin8 (via MAX3232)
#define SAUTER_BAUD   9600
#define BAT_MONITOR   1      // 1 = accu-% meten (externe deler vereist), 0 = uit -> toont "USB"
#define BAT_ADC       1      // ADC1-pin, vrij op dit board (GPIO1); niet 4/5/6/7 gebruiken
#define DIVIDER_RATIO 2.0f   // 2x 100k van B+ -> ADC -> GND geeft ratio 2.0

/* ---------- timing ---------------------------------------------------------- */
#define LIVE_MS   100        // 10 Hz: Sauter-poll + BLE-notify
#define LOG_MS    200        // 5 Hz naar flash
#define OLED_MS   150
#define BAT_MS    5000
#define FLUSH_MS  1000

/* ---------- BLE (laptop) ----------------------------------------------------- */
#define SERVICE_UUID "9a8b0001-1d4c-4f6e-9b2a-2f3e4d5c6b7a"
#define DATA_UUID    "9a8b0002-1d4c-4f6e-9b2a-2f3e4d5c6b7a"
#define CTRL_UUID    "9a8b0003-1d4c-4f6e-9b2a-2f3e4d5c6b7a"
#define LOG_UUID     "9a8b0004-1d4c-4f6e-9b2a-2f3e4d5c6b7a"
#define DEVICE_NAME  "FAR-500"

/* ---------- ADXL345 ---------------------------------------------------------- */
#define ADXL_ADDR 0x53
#define ADXL_POWER 0x2D
#define ADXL_FMT   0x31
#define ADXL_DATA  0x32

#define LOG_PATH "/log.csv"
#define CSV_HEADER "ms,angle_deg,force_N"

/* ============================ GLOBALS ===================================== */
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
// 1.3" = meestal SH1106. SSD1306-paneel? -> U8G2_SSD1306_128X64_NONAME_F_HW_I2C

NimBLECharacteristic *pData=nullptr, *pLog=nullptr;
bool laptopConnected=false;

const char* AXIS_NAME[2]={"X","Y"};
int   axis=0;                 // 0=X (pitch), 1=Y (roll)
float offs[2]={0,0};          // tare-offset per as (persistent)
float lastForce=NAN;
float batV=0; int batPct=0;
bool  logging=false; uint32_t runStartMs=0;
File  logFile; bool logOpen=false;
volatile bool reqTare=false, reqDump=false, reqClear=false;

Preferences prefs;
char  sbuf[48]; uint8_t sidx=0;
uint32_t tLive=0,tLog=0,tOled=0,tBat=0,tFlush=0,tBleDbg=0;

/* ============================ ADXL345 ===================================== */
void adxlW(uint8_t r,uint8_t v){ Wire.beginTransmission(ADXL_ADDR); Wire.write(r); Wire.write(v); Wire.endTransmission(); }
bool adxlBegin(){
  Wire.beginTransmission(ADXL_ADDR); if(Wire.endTransmission()!=0) return false;
  adxlW(ADXL_FMT,0x08); adxlW(ADXL_POWER,0x08); return true;   // FULL_RES +-2g, MEASURE
}
// Vult pitch (kanteling X-as) en roll (kanteling Y-as) in graden.
bool adxlAngles(float &pitchX, float &rollY){
  Wire.beginTransmission(ADXL_ADDR); Wire.write(ADXL_DATA);
  if(Wire.endTransmission(false)!=0) return false;
  if(Wire.requestFrom(ADXL_ADDR,6)!=6) return false;
  int16_t x=Wire.read()|(Wire.read()<<8), y=Wire.read()|(Wire.read()<<8), z=Wire.read()|(Wire.read()<<8);
  float ax=x/256.0f, ay=y/256.0f, az=z/256.0f;
  pitchX=atan2f(ax, sqrtf(ay*ay+az*az))*180.0f/(float)M_PI;
  rollY =atan2f(ay, sqrtf(ax*ax+az*az))*180.0f/(float)M_PI;
  return true;
}

/* ============================ SAUTER ====================================== */
void sauterRequest(){ Serial1.write('9'); }    // vraag meting op
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

/* ============================ BLE SERVER (NimBLE 2.x) ===================== */
class SrvCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& connInfo) override { laptopConnected=true; }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& connInfo, int reason) override {
    laptopConnected=false; NimBLEDevice::startAdvertising(); }
};
class CtrlCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    String cmd=String(c->getValue().c_str()); cmd.trim(); cmd.toUpperCase();
    if(cmd=="START"){ logging=true; runStartMs=millis(); logOpenW(); }
    else if(cmd=="STOP"){ logging=false; logCloseF(); }
    else if(cmd=="TARE"){ reqTare=true; }
    else if(cmd=="DUMP"){ reqDump=true; }
    else if(cmd=="CLEAR"){ reqClear=true; }
    else if(cmd=="AXIS:X"){ axis=0; prefs.putInt("ax",0); }
    else if(cmd=="AXIS:Y"){ axis=1; prefs.putInt("ax",1); }
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
void drawOled(float angle){
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  char top[12]; snprintf(top,sizeof(top),"AS %s",AXIS_NAME[axis]); oled.drawStr(0,9,top);
  char b[8]; if(batPct<0) strcpy(b,"USB"); else snprintf(b,sizeof(b),"%d%%",batPct);
  oled.drawStr(128-oled.getStrWidth(b),9,b);
  char a[12]; if(isnan(angle)) strcpy(a,"--.-"); else snprintf(a,sizeof(a),"%.1f",angle);
  oled.setFont(u8g2_font_logisoso24_tn); oled.drawStr(0,42,a);
  oled.setFont(u8g2_font_6x10_tf); oled.drawStr(oled.getStrWidth(a)+2,24,"\xb0");
  char f[16]; if(isnan(lastForce)) strcpy(f,"F  ---  N"); else snprintf(f,sizeof(f),"F %+.0f N",lastForce);
  oled.setFont(u8g2_font_7x13B_tf); oled.drawStr(0,58,f);
  oled.setFont(u8g2_font_5x7_tf);
  const char* s = laptopConnected?"BT":"--"; oled.drawStr(128-oled.getStrWidth(s),58,s);
  oled.sendBuffer();
}

/* ============================ SETUP ====================================== */
void setup(){
  Serial.begin(115200); delay(150);
  analogReadResolution(12);
  prefs.begin("logger",false);
  offs[0]=prefs.getFloat("oX",0); offs[1]=prefs.getFloat("oY",0); axis=prefs.getInt("ax",0);

  Wire.begin(I2C_SDA,I2C_SCL);
  oled.setBusClock(400000); oled.begin();
  oled.clearBuffer(); oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0,20,"FH500 Logger"); oled.drawStr(0,40,"opstarten..."); oled.sendBuffer();
  if(!adxlBegin()) Serial.println("[WAARSCHUWING] ADXL345 niet gevonden (0x53)");

  Serial1.begin(SAUTER_BAUD, SERIAL_8N1, SAUTER_RX, SAUTER_TX);
  if(!LittleFS.begin(true)) Serial.println("[FOUT] LittleFS");

  Serial.printf("[BLE] reset reason: %d\n", (int)esp_reset_reason());
  NimBLEDevice::init(DEVICE_NAME); NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  Serial.printf("[BLE] MAC: %s\n", NimBLEDevice::getAddress().toString().c_str());
  NimBLEServer* srv=NimBLEDevice::createServer(); srv->setCallbacks(new SrvCB());
  NimBLEService* svc=srv->createService(SERVICE_UUID);
  pData=svc->createCharacteristic(DATA_UUID, NIMBLE_PROPERTY::NOTIFY);
  pLog =svc->createCharacteristic(LOG_UUID,  NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* pCtrl=svc->createCharacteristic(CTRL_UUID,
        NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_NR);
  pCtrl->setCallbacks(new CtrlCB());
  bool svcOk=srv->start();
  Serial.printf("[BLE] service start: %s\n", svcOk?"OK":"FOUT");

  NimBLEAdvertising* adv=NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID); adv->setName(DEVICE_NAME);
  adv->setConnectableMode(BLE_GAP_CONN_MODE_UND);
  adv->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
  adv->setMinInterval(160); adv->setMaxInterval(240);   // 100-150 ms
  bool advOk=adv->start();
  Serial.printf("[BLE] advertising '%s' start: %s\n", DEVICE_NAME, advOk?"OK":"FOUT");

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

  float pX,rY; bool ok=adxlAngles(pX,rY);
  float raw = !ok?NAN : (axis==0?pX:rY);

  if(reqTare){ reqTare=false; if(!isnan(raw)){ offs[axis]=raw; prefs.putFloat(axis==0?"oX":"oY",raw); } }
  if(reqClear){ reqClear=false; logClearF(); }
  if(reqDump){ reqDump=false; logDumpBle(); }

  float angle = isnan(raw)? NAN : (raw-offs[axis]);

#if BAT_MONITOR
  if(now-tBat>=BAT_MS){ tBat=now; batV=readBatV(); batPct=voltToPct(batV); }
#endif

  if(now-tLive>=LIVE_MS){ tLive=now; sauterRequest();
    if(laptopConnected){ uint32_t t=logging?(now-runStartMs):0;
      char line[56]; snprintf(line,sizeof(line),"%lu,%.1f,%.0f,%d",(unsigned long)t,angle,lastForce,batPct);
      pData->setValue(line); pData->notify(); } }
  if(logging && logOpen && now-tLog>=LOG_MS){ tLog=now;
    char row[48]; snprintf(row,sizeof(row),"%lu,%.1f,%.1f",(unsigned long)(now-runStartMs),angle,lastForce);
    logFile.println(row); }
  if(logOpen && now-tFlush>=FLUSH_MS){ tFlush=now; logFile.flush(); }
  if(now-tOled>=OLED_MS){ tOled=now; drawOled(angle); }

  if(now-tBleDbg>=3000){ tBleDbg=now;
    NimBLEAdvertising* adv=NimBLEDevice::getAdvertising();
    Serial.printf("[BLE] adv=%s conn=%d heap=%lu\n",
      adv->isAdvertising()?"aan":"UIT", laptopConnected?1:0, (unsigned long)ESP.getFreeHeap()); }

  delay(4);
}
