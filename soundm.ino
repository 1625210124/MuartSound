#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>

LGFX_Sprite canvas(&M5.Display);

String title = "Muzik Bekleniyor";
String artist = "-";
int progress = 0;
int duration = 0;
int volume = 0;
bool isPlaying = false;
unsigned long lastTick = 0;

int scrollX = 0;
int scrollDir = -1; 
unsigned long lastScrollTime = 0;
unsigned long waitStart = 0;
bool isWaiting = true;

bool showStatus = false;
unsigned long statusDisplayStartTime = 0;
unsigned long buttonHoldStartTime = 0;

// Ekran Acik/Kapali Kontrolu
bool isScreenOn = true;
bool btnBHoldHandled = false;
unsigned long btnBPressTime = 0;

int btnB_clicks = 0;
unsigned long lastClickTimer = 0;

// Jiroskop Ses Kontrol Degiskenleri
float volAccumulator = 0.0;
unsigned long btnAPressTime = 0;
bool volChanged = false;
unsigned long lastImuTime = 0;

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
BLECharacteristic *pRxCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define RX_CHAR_UUID           "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define TX_CHAR_UUID           "828917c1-ea55-4d4a-a66e-fd202cea0645"

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { deviceConnected = true; }
    void onDisconnect(BLEServer* pServer) { deviceConnected = false; }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue();
      if (rxValue.length() > 0) {
        StaticJsonDocument<512> doc;
        if (!deserializeJson(doc, rxValue)) {
          title = doc["title"].as<String>();
          artist = doc["artist"].as<String>();
          progress = doc["progress"].as<int>();
          duration = doc["duration"].as<int>();
          volume = doc["volume"].as<int>();
          isPlaying = doc["playing"].as<bool>();
          
          // MAVI BAR BUG FIX 1: PC'den veri geldiginde sayaci bilgisayarla senkronize et
          lastTick = millis(); 
        }
      }
    }
};

void sendCommand(String cmd) {
  if (deviceConnected) {
    pTxCharacteristic->setValue(cmd.c_str());
    pTxCharacteristic->notify();
  }
}

void updateScroll(int textWidth, int maxWidth) {
  if (textWidth <= maxWidth) { scrollX = 0; return; }
  if (isWaiting) {
    if (millis() - waitStart > 1500) isWaiting = false;
  } else {
    if (millis() - lastScrollTime > 30) { 
      lastScrollTime = millis();
      scrollX += scrollDir;
      if (scrollX < -(textWidth - maxWidth)) {
        scrollX = -(textWidth - maxWidth); scrollDir = 1; isWaiting = true; waitStart = millis();
      } else if (scrollX > 0) {
        scrollX = 0; scrollDir = -1; isWaiting = true; waitStart = millis();
      }
    }
  }
}

void drawScreen() {
  canvas.fillScreen(TFT_BLACK);
  canvas.setTextWrap(false); 
  
  canvas.setTextSize(2.3);
  canvas.setTextColor(TFT_WHITE);
  int maxW = canvas.width() - 10;
  int tW = canvas.textWidth(title);
  updateScroll(tW, maxW);
  canvas.setClipRect(5, 5, maxW, 26);
  canvas.setCursor(5 + scrollX, 5);
  canvas.print(title);
  canvas.clearClipRect();

  canvas.setTextSize(1.7);
  canvas.setTextColor(TFT_LIGHTGRAY);
  canvas.setCursor(5, 36);
  canvas.print(artist);

  int symX = canvas.width() - 20; 
  int symY = 36; 
  if (isPlaying) {
    canvas.fillTriangle(symX, symY, symX, symY + 12, symX + 10, symY + 6, TFT_GREEN);
  } else {
    canvas.fillRect(symX, symY, 4, 12, TFT_ORANGE);
    canvas.fillRect(symX + 6, symY, 4, 12, TFT_ORANGE);
  }

  // Alt Kismin (Barlar) Cizimi
  int barW = canvas.width() - 10;
  int barH = 18; 
  int progY = canvas.height() - barH - 5;
  int volY = progY - barH - 8;
  
  canvas.drawRect(5, volY, barW, barH, TFT_PURPLE);
  canvas.fillRect(5, volY, (volume * barW) / 100, barH, TFT_PURPLE);
  canvas.setTextDatum(middle_center);
  canvas.setTextSize(1.3);
  canvas.setTextColor(TFT_GREEN);
  canvas.drawString("%" + String(volume), 5 + (barW / 2), volY + (barH / 2));

  uint16_t blueColor = canvas.color565(0, 150, 255);
  canvas.drawRect(5, progY, barW, barH, TFT_WHITE);
  if (duration > 0) {
    canvas.fillRect(5, progY, (progress * barW) / duration, barH, blueColor);
  }
  
  int remaining = (duration > progress) ? (duration - progress) : 0;
  char timeStr[10];
  sprintf(timeStr, "-%d'%02d\"", remaining / 60, remaining % 60);
  canvas.setTextColor(TFT_GREEN);
  canvas.drawString(timeStr, 5 + (barW / 2), progY + (barH / 2));
  canvas.setTextDatum(top_left);

  if (showStatus) {
    int bandY = (canvas.height() / 2) - 20;
    canvas.fillRect(0, bandY, canvas.width(), 40, TFT_BLUE);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(1.5);
    canvas.setCursor(10, bandY + 5);
    canvas.print(deviceConnected ? "BLE: Bagli" : "BLE: Baglanti Yok");
    canvas.setCursor(10, bandY + 22);
    canvas.printf("Pil: %%%d", M5.Power.getBatteryLevel());
  }
  canvas.pushSprite(0, 0);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg); 
  M5.Display.setRotation(1); 
  M5.Display.setBrightness(128); // Baslangicta varsayilan ekran parlakligi
  canvas.createSprite(M5.Display.width(), M5.Display.height());

  BLEDevice::init("M5-Media-Remote");
  BLEDevice::setMTU(512);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pTxCharacteristic = pService->createCharacteristic(TX_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());
  pRxCharacteristic = pService->createCharacteristic(RX_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pRxCharacteristic->setCallbacks(new MyCallbacks());
  pService->start();
  pServer->getAdvertising()->start();
}

void loop() {
  M5.update();
  M5.Imu.update(); 

  unsigned long currentTime = millis();
  float dt = (currentTime - lastImuTime) / 1000.0;
  if (dt > 0.1) dt = 0.01; 
  lastImuTime = currentTime;

  if (!deviceConnected && oldDeviceConnected) {
      delay(500); pServer->startAdvertising(); oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
      oldDeviceConnected = deviceConnected;
  }

  // --- HAREKETLI SES KONTROL ---
  if (M5.BtnA.wasPressed()) {
      btnAPressTime = currentTime;
      volChanged = false;
      volAccumulator = 0.0;
  }

  if (M5.BtnA.isPressed()) {
      auto imu = M5.Imu.getImuData();
      float turnRate = imu.gyro.y; 
      
      volAccumulator += (turnRate * dt); 
      float threshold = 3.0; 
      
      if (volAccumulator > threshold) {
          sendCommand("VOL_UP");
          volAccumulator = 0.0;
          volChanged = true;
      } else if (volAccumulator < -threshold) {
          sendCommand("VOL_DOWN");
          volAccumulator = 0.0;
          volChanged = true;
      }
  }

  if (M5.BtnA.wasReleased()) {
      if (!volChanged && (currentTime - btnAPressTime < 400)) {
          sendCommand("TOGGLE");
      }
  }

  // --- B TUSU KOMUTLARI & 2 SANIYE EKRAN KAPATMA ---
  if (M5.BtnB.wasPressed()) {
      btnBPressTime = millis();
      btnBHoldHandled = false;
  }

  if (M5.BtnB.isPressed()) {
      if (M5.BtnA.isPressed()) {
          btnBHoldHandled = true; // A+B basiliyorsa ekran islemini iptal et
      } else if (!btnBHoldHandled && (millis() - btnBPressTime >= 2000)) {
          btnBHoldHandled = true;
          isScreenOn = !isScreenOn;
          M5.Display.setBrightness(isScreenOn ? 128 : 0); // Ekran parlakligini 0 yap (uyku)
      }
  }

  if (M5.BtnB.wasReleased()) {
      if (!btnBHoldHandled) {
          btnB_clicks++;
          lastClickTimer = millis();
      }
  }

  if (btnB_clicks > 0 && (millis() - lastClickTimer > 300)) {
    if (btnB_clicks == 1) sendCommand("NEXT");
    else if (btnB_clicks >= 2) sendCommand("PREV");
    btnB_clicks = 0;
  }

  if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
    if (buttonHoldStartTime == 0) buttonHoldStartTime = millis();
    else if (millis() - buttonHoldStartTime >= 1000 && !showStatus) {
      showStatus = true; statusDisplayStartTime = millis();
    }
  } else {
    buttonHoldStartTime = 0; 
  }

  if (showStatus && (millis() - statusDisplayStartTime >= 5000)) showStatus = false;

  // Sadece ekran aciksa çizim yap (Batarya tasarrufu saglar)
  if (isScreenOn) {
      drawScreen();
  }

  // MAVI BAR BUG FIX 2: Milisaniye kayiplarini onleyen dogru zamanlayici
  if (isPlaying && duration > 0) {
    unsigned long elapsed = millis() - lastTick;
    if (elapsed >= 1000) {
      int secondsPassed = elapsed / 1000;
      progress += secondsPassed;
      if (progress > duration) progress = duration;
      
      // Kalan küsuratli milisaniyeleri koruyarak tam zamaninda artmasini sagla
      lastTick += secondsPassed * 1000; 
    }
  }
}