extern "C"
{
#include "hal_i2c.h"
#include "hal_i2s.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "tinySh1106.h"
}
#include "Arduino.h"
#include <Audio.h>
#include <ETH.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "SPIFFS.h"
#include "IotWebConf.h"
#include "lwip/apps/sntp.h"
#include <sys/time.h>

#define I2S_DOUT      26
#define I2S_BCLK      5
#define I2S_LRC       25
#define I2S_DIN       35
#define I2SN (i2s_port_t)0
#define I2CN (i2c_port_t)0
#define SDA 18
#define SCL 23
#define MU GPIO_NUM_39      // Mode => mute / unmute
#define BK GPIO_NUM_36      // Rec  => station-
#define VM GPIO_NUM_32      // Play => vol-
#define VP GPIO_NUM_19      // Vol+ => vol +
#define FW GPIO_NUM_12      // Set  => station+
#define BOOT GPIO_NUM_0

#define CONFIG_PIN 1

#define PA GPIO_NUM_21      // Amp power ON

#define MAXSTATION 17
#define maxVol 60
#define ES8388_ADDR 0x10



// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "muse";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "musemuse";


DNSServer dnsServer;
WebServer server(80);

void playWav(char* n);
void beep(void);
char* Rlink(int st);
char* Rname(int st);
int maxStation(void);
int touch_get_level(int t);
void ES8388_Write_Reg(uint8_t reg, uint8_t val);
uint8_t ES8388_Read_Reg( uint8_t reg_add);
void ES8388vol_Set(uint8_t volx);
void ES8388_Init(void);

time_t now;
struct tm timeinfo;
int previousMin = -1;
char timeStr[10];
char comValue[16];
char newNameValue[16];
char newLinkValue[80];
// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "museRadio V0"


Audio audio;
//httpd_handle_t radio_httpd = NULL;

int station = 0;
int previousStation;
int vol = maxVol / 3;
int previousVol = -1;
int selectedVol;
int previousLevel = -1;
int retries = 0;
int vplus, vmoins, splus, smoins, vlevel, vmute;
nvs_handle my_handle;
esp_err_t err;
char ssid[32] = "a";
char pwd[32] = "b";
size_t lg;
int MS;
bool beepON = false;
bool muteON = false;
uint32_t sampleRate;
char* linkS;
bool timeON = false;
bool connected = true;
char mes[200];
int iMes ;
bool started = false;

void confErr(void)
{
  drawStrC(26, "Error...");
}
static void stop(void* data)
{
  static bool ON;
  ON = false;
  while (1)
  {
    if (gpio_get_level(MU) == 0) ON = true;
    if ((gpio_get_level(MU) == 1) && (ON == true))
    {

      esp_sleep_enable_ext0_wakeup(MU, LOW);

      esp_deep_sleep_start();
    }
    delay(500);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  param management (via local server)
//        add     : add a new station
//        del     : delete a station
//        mov     : move a station (change position)
////////////////////////////////////////////////////////////////////////////////////////////////////
void configRadio(void)
{
  char com[8];
  char comV[16];
  char* P;
  int n, m;
  char lf[] = {0x0A, 0x00};
  char li[80];
  char na[17];
  started = false;
  clearBuffer();
  drawStrC(16, "initializing...");
  sendBuffer();

  strcpy(comV, comValue);
  P = strtok(comV, ",");
  strcpy(com, P);
  P = strtok(NULL, ",");
  if ( P != NULL)
  {
    n = atoi(P);
    P = strtok(NULL, ",");
    if (P != NULL) m = atoi(P);
  }

  printf("xxxxxxxxxxxxxxxxxx %s   %d   %d\n", com, n, m);

  if (strcmp(com, "add") == 0)
  {
    printf("add\n");
    printf("link ==> %s\n", newLinkValue);
    printf("name ==> %s\n", newNameValue);


    File ln = SPIFFS.open("/linkS", "r+");
    ln.seek(0, SeekEnd);
    ln.write((const uint8_t*)newLinkValue, strlen(newLinkValue));
    ln.write((const uint8_t*)lf, 1);
    ln.close();
    ln = SPIFFS.open("/nameS", "r+");
    ln.seek(0, SeekEnd);
    ln.write((const uint8_t*)newNameValue, strlen(newNameValue));
    ln.write((const uint8_t*)lf, 1);
    ln.close();
  }
  else
  {

    if (strcmp(com, "del") == 0)
    {
      File trn = SPIFFS.open("/trn", "w");
      File trl = SPIFFS.open("/trl", "w");
      for (int i = 0; i < n; i++)
      {
        strcpy(li, Rlink(i));
        strcpy(na, Rname(i));
        trn.write((const uint8_t*)na, strlen(na));
        trn.write((const uint8_t*)lf, 1);
        trl.write((const uint8_t*)li, strlen(li));
        trl.write((const uint8_t*)lf, 1);
      }
      for (int i = n + 1; i <= MS; i++)
      {
        strcpy(li, Rlink(i));
        strcpy(na, Rname(i));
        trn.write((const uint8_t*)na, strlen(na));
        trn.write((const uint8_t*)lf, 1);
        trl.write((const uint8_t*)li, strlen(li));
        trl.write((const uint8_t*)lf, 1);
      }
      SPIFFS.remove("/nameS");
      SPIFFS.remove("/linkS");
      SPIFFS.rename("/trn", "/nameS");
      SPIFFS.rename("/trl", "/linkS");
    }
    else if (strcmp(com, "mov") == 0)
    {
      File trn = SPIFFS.open("/trn", "w");
      File trl = SPIFFS.open("/trl", "w");
      if (n > m)
      {
        for (int i = 0; i < m; i++)
        {
          strcpy(li, Rlink(i));
          strcpy(na, Rname(i));
          trn.write((const uint8_t*)na, strlen(na));
          trn.write((const uint8_t*)lf, 1);
          trl.write((const uint8_t*)li, strlen(li));
          trl.write((const uint8_t*)lf, 1);
        }
        strcpy(li, Rlink(n));
        strcpy(na, Rname(n));
        trn.write((const uint8_t*)na, strlen(na));
        trn.write((const uint8_t*)lf, 1);
        trl.write((const uint8_t*)li, strlen(li));
        trl.write((const uint8_t*)lf, 1);
        for (int i = m; i < n; i++)
        {
          strcpy(li, Rlink(i));
          strcpy(na, Rname(i));
          trn.write((const uint8_t*)na, strlen(na));
          trn.write((const uint8_t*)lf, 1);
          trl.write((const uint8_t*)li, strlen(li));
          trl.write((const uint8_t*)lf, 1);
        }
        for (int i = n + 1; i <= MS; i++)
        {
          strcpy(li, Rlink(i));
          strcpy(na, Rname(i));
          trn.write((const uint8_t*)na, strlen(na));
          trn.write((const uint8_t*)lf, 1);
          trl.write((const uint8_t*)li, strlen(li));
          trl.write((const uint8_t*)lf, 1);
        }
      }
      else
      {
        for (int i = 0; i < n; i++)
        {
          strcpy(li, Rlink(i));
          strcpy(na, Rname(i));
          trn.write((const uint8_t*)na, strlen(na));
          trn.write((const uint8_t*)lf, 1);
          trl.write((const uint8_t*)li, strlen(li));
          trl.write((const uint8_t*)lf, 1);
        }

        for (int i = n + 1; i < m + 1; i++)
        {
          strcpy(li, Rlink(i));
          strcpy(na, Rname(i));
          trn.write((const uint8_t*)na, strlen(na));
          trn.write((const uint8_t*)lf, 1);
          trl.write((const uint8_t*)li, strlen(li));
          trl.write((const uint8_t*)lf, 1);
        }
        strcpy(li, Rlink(n));
        strcpy(na, Rname(n));
        trn.write((const uint8_t*)na, strlen(na));
        trn.write((const uint8_t*)lf, 1);
        trl.write((const uint8_t*)li, strlen(li));
        trl.write((const uint8_t*)lf, 1);
        for (int i = m + 1; i <= MS; i++)
        {
          strcpy(li, Rlink(i));
          strcpy(na, Rname(i));
          trn.write((const uint8_t*)na, strlen(na));
          trn.write((const uint8_t*)lf, 1);
          trl.write((const uint8_t*)li, strlen(li));
          trl.write((const uint8_t*)lf, 1);
        }
      }
      SPIFFS.remove("/nameS");
      SPIFFS.remove("/linkS");
      SPIFFS.rename("/trn", "/nameS");
      SPIFFS.rename("/trl", "/linkS");
    }
    started = true;
  }
}

//////////////////////////////////////////////////////////////////
// local
// detects non ASCII chars and converts them (if possible...)
/////////////////////////////////////////////////////////////////
void convToAscii(char *s, char *t)
{
  int j = 0;
  for (int i = 0; i < strlen(s); i++)
  {
    if (s[i] < 128) t[j++] = s[i];
    else
    {
      if (s[i] == 0xC2)
      {
        t[j++] = '.';
        i++;
      }
      else if (s[i] == 0xC3)
      {
        i++;
        if ((s[i] >= 0x80) && (s[i] < 0x87)) t[j++] = 'A';
        else if ((s[i] >= 0x88) && (s[i] < 0x8C)) t[j++] = 'E';
        else if ((s[i] >= 0x8C) && (s[i] < 0x90)) t[j++] = 'I';
        else if ((s[i] >= 0x92) && (s[i] < 0x97)) t[j++] = 'O';
        else if ((s[i] >= 0x99) && (s[i] < 0x9D)) t[j++] = 'U';
        else if ((s[i] >= 0xA0) && (s[i] < 0xA7)) t[j++] = 'a';
        else if ((s[i] == 0xA7) ) t[j++] = 'c';
        else if ((s[i] >= 0xA8) && (s[i] < 0xAC)) t[j++] = 'e';
        else if ((s[i] >= 0xAC) && (s[i] < 0xB0)) t[j++] = 'i';
        else if ((s[i] >= 0xB2) && (s[i] < 0xB7)) t[j++] = 'o';
        else if ((s[i] >= 0xB9) && (s[i] < 0xBD)) t[j++] = 'u';
        else t[j++] = '.';
      }
    }
  }

  t[j] = 0;
}
/////////////////////////////////////////////////////////////////////
// play station task (core 0)
//
/////////////////////////////////////////////////////////////////////
static void playRadio(void* data)
{
  while (started == false) delay(100);
  while (1)
  {
    if ((station != previousStation) || (connected == false))
    {
      printf("station no %d\n", station);
      audio.stopSong();
      delay(200);
      err = nvs_set_i32(my_handle, "station", station);
      linkS = Rlink(station);
      mes[0] = 0;
      audio.connecttohost(linkS);
      previousStation = station;
    }
    // delay(100);
    if (connected == false) delay(500);
    if (beepON == false)audio.loop();
  }
}

/////////////////////////////////////////////////////////////////////////
// display task (core 1)
//
//////////////////////////////////////////////////////////////////////////
static void refreshDisplay(void* data)
{

  while (1)
  {
    if (started == true)
    {
      //clear ram
      clearBuffer();

      //displays station name
      drawStrC(1, Rname(station));
      drawHLine(14, 0, 128);
      drawHLine(50, 0, 128);

      //displays time (big chars)
      sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      drawBigStrC(24, timeStr);

      if (connected == false)
      {
        drawStrC(14, "Connection error");
      }

      // displays sound index (60x10, 10 values)
      drawIndexb(22, 119, 20, 8, 10, vol * 10 / maxVol);

      if (strlen(mes) != 0)
      {
        char mesa[17];
        strncpy(mesa, &mes[iMes], 16);
        if (strlen(mesa) < 16) iMes = 0; else iMes++;
        mesa[16] = 0;

        drawStr(56, 0, mesa);
      }
      sendBuffer();
    }
    delay(500);
  }
}
//////////////////////////////////////////////////////////////////////////
//
// plays .wav records (in SPIFFS file)
//////////////////////////////////////////////////////////////////////////
void playWav(char* n)
{
  struct header
  {
    uint8_t a[16];
    uint8_t cksize[4];
    uint8_t wFormatTag[2];
    uint8_t nChannels[2];
    uint8_t nSamplesPerSec[4];
    uint8_t c[16];
  };
  uint32_t rate;
  uint8_t b[46];
  int l;
  bool mono;
  size_t t;
  File f = SPIFFS.open(n, FILE_READ);
  // mono/stereo
  l = (int) f.read(b, sizeof(b));
  if (b[22] == 1) mono = true; else mono = false;
  // sample rate => init I2S
  rate =  (b[25] << 8) + b[24];
  printf(" rate = %d\n", rate);
  i2s_set_clk(I2SN, rate, (i2s_bits_per_sample_t)16, (i2s_channel_t)2);
  //writes samples (16 bits) to codec via I2S
  do
  {
    if (mono == true)
    {
      l = (int)f.read((uint8_t*)b, 2);
      b[2] = b[0]; b[3] = b[1];

    }
    else
      l = (int)f.read((uint8_t*)b, 4);

    i2s_write(I2SN, b, 4, &t, 1000);
  }
  while (l != 0);
  i2s_zero_dma_buffer((i2s_port_t)0);
  f.close();
}


/////////////////////////////////////////////////////////////////////
// beep....
/////////////////////////////////////////////////////////////////////
void beep(void)
{
#define volBeep 5
  ES8388vol_Set(volBeep);
  beepON = true;
  playWav("/Beep.wav");
  beepON = false;
  ES8388vol_Set(vol);
  i2s_set_clk(I2SN, sampleRate, (i2s_bits_per_sample_t)16, (i2s_channel_t)2);
}


/////////////////////////////////////////////////////////////////////////////
// gets station link from SPIFFS file "/linkS"
//
/////////////////////////////////////////////////////////////////////////////
char* Rlink(int st)
{
  int i;
  static char b[80];
  File ln = SPIFFS.open("/linkS", FILE_READ);
  i = 0;
  uint8_t c;
  while (i != st)
  {
    while (c != 0x0a)ln.read(&c, 1);
    c = 0;
    i++;
  }
  i = 0;
  do
  {
    ln.read((uint8_t*)&b[i], 1);
    i++;
  } while (b[i - 1] != 0x0a);
  b[i - 1] = 0;
  ln.close();
  return b;
}
/////////////////////////////////////////////////////////////////////////////////
//  gets station name from SPIFFS file "/namS"
//
/////////////////////////////////////////////////////////////////////////////////
char* Rname(int st)
{
  int i;
  static char b[20];
  File ln = SPIFFS.open("/nameS", FILE_READ);
  i = 0;
  uint8_t c;
  while (i != st)
  {
    while (c != 0x0a)ln.read(&c, 1);
    c = 0;
    i++;
  }
  i = 0;
  do
  {
    ln.read((uint8_t*)&b[i], 1);
    i++;
  } while (b[i - 1] != 0x0a);
  b[i - 1] = 0;
  ln.close();
  return b;
}
/////////////////////////////////////////////////////////////////////////
//  defines how many stations in SPIFFS file "/linkS"
//
////////////////////////////////////////////////////////////////////////
int maxStation(void)
{
  File ln = SPIFFS.open("/linkS", FILE_READ);
  uint8_t c;
  int m = 0;
  int t;
  t = ln.size();
  int i = 0;
  do
  {
    while (c != 0x0a) {
      ln.read(&c, 1);
      i++;
    }
    c = 0;
    m++;
  } while (i < t);
  ln.close();
  return m;
}

///////////////////////////////////////////////////////////////////////
// touch button value
///////////////////////////////////////////////////////////////////////
int touch_get_level(int t)
{
#define threshold 30
  if (((touchRead(t) + touchRead(t) + touchRead(t) + touchRead(t)) >> 2) > threshold) return 0;
  else return 1;
}

///////////////////////////////////////////////////////////////////////
// Write ES8388 register
///////////////////////////////////////////////////////////////////////
void ES8388_Write_Reg(uint8_t reg, uint8_t val)
{
  uint8_t buf[2];
  buf[0] = reg;
  buf[1] = val;
  hal_i2c_master_mem_write(I2CN, ES8388_ADDR, buf[0], buf + 1, 1);
  // ES8388_REGVAL_TBL[reg]=val;
}

////////////////////////////////////////////////////////////////////////
// Read ES8388 register
////////////////////////////////////////////////////////////////////////
uint8_t ES8388_Read_Reg( uint8_t reg_add)
{
  uint8_t val;
  hal_i2c_master_mem_read(I2CN, ES8388_ADDR, reg_add, &val, 1);
  return val;
}

////////////////////////////////////////////////////////////////////////
//
// manages volume (via vol xOUT1, vol DAC, and vol xIN2)
//
////////////////////////////////////////////////////////////////////////
void ES8388vol_Set(uint8_t volx)
{
#define M maxVol-33
  printf("volume ==> %d\n", volx);
  ES8388_Write_Reg(25, 0x00);
  if (volx > maxVol) volx = maxVol;
  if (volx == 0)
  {
    ES8388_Write_Reg(25, 0x04);
  }

  if (volx >= M)
  {
    ES8388_Write_Reg(46, volx - M);
    ES8388_Write_Reg(47, volx - M);
    ES8388_Write_Reg(26, 0x00);
    ES8388_Write_Reg(27, 0x00);
  }
  else
  {
    ES8388_Write_Reg(46, 0x00);
    ES8388_Write_Reg(47, 0x00);
    ES8388_Write_Reg(26, (M - volx) * 3);
    ES8388_Write_Reg(27, (M - volx) * 3);
  }
}

//////////////////////////////////////////////////////////////////
//
// init CCODEC chip ES8388
//
////////////////////////////////////////////////////////////////////
void ES8388_Init(void)
{
  hal_i2c_init(I2CN, SDA, SCL);
  hal_i2s_init(I2SN, I2S_DOUT, I2S_LRC, I2S_BCLK, I2S_DIN, 2);

  // reset
  ES8388_Write_Reg(0, 0x80);
  ES8388_Write_Reg(0, 0x00);
  // mute
  ES8388_Write_Reg(25, 0x04);
  ES8388_Write_Reg(1, 0x50);
  //powerup
  ES8388_Write_Reg(2, 0x00);
  // slave mode
  ES8388_Write_Reg(8, 0x00);
  // DAC powerdown
  ES8388_Write_Reg(4, 0xC0);
  // vmidsel/500k ADC/DAC idem
  ES8388_Write_Reg(0, 0x12);

  ES8388_Write_Reg(1, 0x00);
  // i2s 16 bits
  ES8388_Write_Reg(23, 0x18);
  // sample freq 256
  ES8388_Write_Reg(24, 0x02);
  // LIN2/RIN2 for mixer
  ES8388_Write_Reg(38, 0x09);
  // left DAC to left mixer
  ES8388_Write_Reg(39, 0x90);
  // right DAC to right mixer
  ES8388_Write_Reg(42, 0x90);
  // DACLRC ADCLRC idem
  ES8388_Write_Reg(43, 0x80);
  ES8388_Write_Reg(45, 0x00);
  // DAC volume max
  ES8388_Write_Reg(27, 0x00);
  ES8388_Write_Reg(26, 0x00);

  ES8388_Write_Reg(2 , 0xF0);
  ES8388_Write_Reg(2 , 0x00);
  ES8388_Write_Reg(29, 0x1C);
  // DAC power-up LOUT1/ROUT1 enabled
  ES8388_Write_Reg(4, 0x30);
  // unmute
  ES8388_Write_Reg(25, 0x00);

  // amp validation
  gpio_set_level(PA, 1);
}

/////////////////////////////////////////////////////////////////////////////////
//détection d'appui sur un bouton
// v valeur actuelle  0 => bouton appuyé  1 => bouton relevé
// l pointeur valeur précédente
//////////////////////////////////////////////////////////////////////////////////
int inc(int v, int *l)
{
  if ((v == 0) && (v != *l))
  {
    *l = v;
    return 1;
  }
  *l = v;
  return 0;

}


///////////////////////////////////////////////////////////////////////////////////
//
//  init. local server custom parameters
//
///////////////////////////////////////////////////////////////////////////////////

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);
IotWebConfParameter comParam = IotWebConfParameter("Action", "actionParam", comValue, 16, "action", "add, del or mov", NULL);
IotWebConfSeparator separator1 = IotWebConfSeparator();
IotWebConfParameter newNameParam = IotWebConfParameter("New Name", "nameParam", newNameValue, 16);
IotWebConfSeparator separator2 = IotWebConfSeparator();
IotWebConfParameter newLinkParam = IotWebConfParameter("New Link", "linkParam", newLinkValue, 80);

void setup() {
  Serial.begin(115200);

  if (!SPIFFS.begin())Serial.println("Erreur SPIFFS");
  // SPIFFS maintenance

  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.print("FILE: ");
    Serial.println(file.name());
    file = root.openNextFile();
  }
  printf("====> %d\n", (int)SPIFFS.totalBytes());
  printf("====> %d\n", (int)SPIFFS.usedBytes());
  //SPIFFS.format();

  printf(" SPIFFS used bytes  ====> %d of %d\n", (int)SPIFFS.usedBytes(), (int)SPIFFS.totalBytes());


  previousStation = -1;
  station = 0;
  // variables de travail
  vplus = 0;
  vmoins = 0;
  splus = 0;
  smoins = 0;
  vmute = 0;
  MS = maxStation() - 1;
  printf("max ===> %d\n", MS);

  /////////////////////////////////////////////////////////////
  // recovers params (station & vol)
  ///////////////////////////////////////////////////////////////
  char b[4];
  File ln = SPIFFS.open("/station", "r");
  ln.read((uint8_t*)b, 2);
  b[2] = 0;
  station = atoi(b);
  ln.close();
  ln = SPIFFS.open("/volume", "r");
  ln.read((uint8_t*)b, 2);
  b[2] = 0;
  vol = atoi(b);
  ln.close();

  ///////////////////////////////////////////////////////
  // initi gpios
  ////////////////////////////////////////////////////////////
  //gpio_reset_pin
  gpio_reset_pin(BK);
  gpio_reset_pin(PA);
  gpio_reset_pin(MU);
  gpio_reset_pin(VM);
  gpio_reset_pin(VP);



  //gpio_set_direction
  gpio_set_direction(BK, GPIO_MODE_INPUT);
  gpio_set_direction(MU, GPIO_MODE_INPUT);
  gpio_set_direction(PA, GPIO_MODE_OUTPUT);
  gpio_set_direction(VM, GPIO_MODE_INPUT);
  gpio_set_direction(VP, GPIO_MODE_INPUT);

  //gpio_set_pull_mode
  gpio_set_pull_mode(BK, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(MU, GPIO_PULLUP_ONLY);

  gpio_set_pull_mode(VM, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(VP, GPIO_PULLUP_ONLY);

  ////////////////////////////////////////////////////////////////
  // init ES8388
  //
  ////////////////////////////////////////////////////////////////
  ES8388_Init();
  ES8388vol_Set(vol);
  // init I2S
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  i2s_set_clk(I2SN, 44100, (i2s_bits_per_sample_t)16, (i2s_channel_t)2);
  // init screen handler
  tinySsd_init(SDA, SCL, 0, 0x3C, 1);
  clearBuffer();
  sendBuffer();
  drawStrC(16, "Starting up...");
  sendBuffer();


  /////////////////////////////////////////////////////////
  //init WiFi
  //////////////////////////////////////////////////////////////
  // init. local server main parameters
  //////////////////////////////////////////////////////////////
  iotWebConf.addParameter(&comParam);
  iotWebConf.addParameter(&separator1);
  iotWebConf.addParameter(&newNameParam);
  iotWebConf.addParameter(&separator2);
  iotWebConf.addParameter(&newLinkParam);
  //init custom parameters  management callbacks
  iotWebConf.setConfigSavedCallback(&configRadio);
  iotWebConf.setFormValidator(&formValidator);
  // -- Initializing the configuration.
  // pin for manual init
  iotWebConf.setConfigPin(CONFIG_PIN);
  iotWebConf.init();
  // init web server
  // -- Set up required URL handlers on the web server.
  server.on("/", handleRoot);
  server.on("/config", [] { iotWebConf.handleConfig(); });
  server.onNotFound([]() {
    iotWebConf.handleNotFound();
  });

  xTaskCreatePinnedToCore(refreshDisplay, "display", 5000, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(playRadio, "radio", 5000, NULL, 1, NULL, 0);
  xTaskCreate(stop, "stop", 5000, NULL, 1, NULL);
}

void loop() {

  iotWebConf.doLoop();
  if (WiFi.status() != WL_CONNECTED) return;
  started = true;


  if (timeON == false)
  {
    //////////////////////////////////////////////////////////////////
    // initialisation temps NTP
    //
    ////////////////////////////////////////////////////////////////////
    // time zone init
    setenv("TZ", "CEST-1", 1);
    tzset();
    //sntp init
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    int retry = 0;
    while ((timeinfo.tm_year < (2016 - 1900)) && (++retry < 20))
    {
      delay(500);
      time(&now);
      localtime_r(&now, &timeinfo);
    }
    timeON = true;
  }

  // prise en charge des boutons
  vol = vol + 5 * inc(touch_get_level(VP), &vplus);
  vol = vol - 5 * inc(touch_get_level(VM), &vmoins);
  if (vol > maxVol) vol = maxVol;
  if (vol < 0) vol = 0;

  station = station + inc(touch_get_level(FW), &splus);
  station = station - inc(gpio_get_level(BK), &smoins);
  if (station > MS) station = 0;
  if (station < 0) station = MS;
  if (station != previousStation)
  {
    beep();
    char b[4];
    sprintf(b, "%02d", station);
    File ln = SPIFFS.open("/station", "w");
    ln.write((uint8_t*)b, 2);
    ln.close();
  }


  if (inc(gpio_get_level(MU), &vmute) == 1)
  {
    if (muteON == false)
    {
      ES8388vol_Set(0);
      printf("mute on\n");
      muteON = true;
    }
    else
    {
      ES8388vol_Set(vol);
      muteON = false;
    }
  }

  //changement de volume
  if (vol != previousVol)
  {
    beep();
    muteON = false;
    previousVol = vol;
    ES8388vol_Set(vol);
    char b[4];
    sprintf(b, "%02d", vol);
    File ln = SPIFFS.open("/volume", "w");
    ln.write((uint8_t*)b, 2);
    ln.close();
  }

  delay(100);
  time(&now);
  localtime_r(&now, &timeinfo);
  if (timeinfo.tm_min != previousMin)
  {
    previousMin = timeinfo.tm_min;
  }
}

///////////////////////////////////////////////////////////////////////
//  stuff for  web server intialization
//       wifi credentials and other things...
//
/////////////////////////////////////////////////////////////////////////
void handleRoot()
{
  char b[6];
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    return;
  }
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>Muse Radio</title></head><body>--- MUSE Radio --- Ros & Co ---";
  s += "<li>---- Stations ----";
  s += "<ul>";

  for (int i = 0; i <= MS; i++)
  {
    s += "<li>";
    sprintf(b, "%02d  ", i);
    s += (String)b;
    s += (String) Rname(i);
  }
  s += "</ul>";
  s += "Go to <a href='config'>configure page</a> to change values (wifi credentials, stations names and links...)";
  s += "</body></html>\n";

  server.send(200, "text/html", s);

}
////////////////////////////////////////////////////////////////////////
// custom parameters verification
///////////////////////////////////////////////////////////////////
boolean formValidator()
{
  Serial.println("Validating form.");
  boolean valid = true;
  if (server.arg(comParam.getId()).length() > 0)
  {
    String buf;
    String com;
    String name;
    String link;
    int n, m;
    buf = server.arg(comParam.getId());
    com = server.arg(comParam.getId()).substring(0, 3);

    if ((com != "add") && (com != "del") && (com != "mov"))
    {
      comParam.errorMessage = "Action should be add, del or mov";
      valid = false;
      return valid;
    }
    if (com == "add")
    {
      name = server.arg(newNameParam.getId());
      link = server.arg(newLinkParam.getId());
      if ((name.length() == 0) || (name.length() > 16))
      {
        newNameParam.errorMessage = "add needs a station name (16 chars max)";
        valid = false;
        return valid;
      }
      if (link.length() == 0)
      {
        newLinkParam.errorMessage = "add needs a valid link";
        valid = false;
        return valid;
      }
    }
    if (com == "del")
    {
      int l = buf.indexOf(',');
      if (l == -1)
      {
        comParam.errorMessage = "incorrect del... del,[station to delete] (ie del,5)";
        valid = false;
        return valid;
      }
      sscanf(&buf[l + 1], "%d", &n);
      if ((n < 0) || (n >= MS))
      {
        comParam.errorMessage = "incorrect station number";
        valid = false;
        return valid;
      }

    }
    if (com == "mov")
    {
      int l = buf.indexOf(',');
      int k = buf.lastIndexOf(',');
      if ((l == -1) || (k == -1))
      {
        comParam.errorMessage = "incorrect mov... mov,[old position],[new position] (ie mov,5,7)";
        valid = false;
        return valid;
      }
      sscanf(&buf[l + 1], "%d", &n);
      sscanf(&buf[k + 1], "%d", &m);
      if ((n < 0) || (n > MS) || (m < 0) || (m > MS) || (m == n))
      {
        comParam.errorMessage = "incorrect station number";
        valid = false;
        return valid;
      }

    }
  }
  return valid;
}

// optional
void audio_info(const char *info) {
#define maxRetries 4
  // Serial.print("info        "); Serial.println(info);
  if (strstr(info, "SampleRate=") > 0)
  {
    sscanf(info, "SampleRate=%d", &sampleRate);
    printf("==================>>>>>>>>>>%d\n", sampleRate);
  }
  connected = true;
  if (strstr(info, "failed") > 0) {
    connected = false;
    printf("failed\n");
  }
}
void audio_id3data(const char *info) { //id3 metadata
  //Serial.print("id3data     ");Serial.println(info);
}
void audio_eof_mp3(const char *info) { //end of file
  //Serial.print("eof_mp3     ");Serial.println(info);
}
void audio_showstation(const char *info) {
  //Serial.print("station     ");Serial.println(info);
}
void audio_showstreaminfo(const char *info) {
  //  Serial.print("streaminfo  ");Serial.println(info);
}
void audio_showstreamtitle(const char *info) {
  Serial.print("streamtitle "); Serial.println(info);
  if (strlen(info) != 0)
  {
    convToAscii((char*)info, mes);
    iMes = 0;
  }
  else mes[0] = 0;
}
void audio_bitrate(const char *info) {
  // Serial.print("bitrate     ");Serial.println(info);
}
void audio_commercial(const char *info) { //duration in sec
  // Serial.print("commercial  ");Serial.println(info);
}
void audio_icyurl(const char *info) { //homepage
  // Serial.print("icyurl      ");Serial.println(info);
}
void audio_lasthost(const char *info) { //stream URL played
  //Serial.print("lasthost    ");Serial.println(info);
}
void audio_eof_speech(const char *info) {
  //Serial.print("eof_speech  ");Serial.println(info);
}
