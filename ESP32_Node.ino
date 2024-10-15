#include "C:\Users\Matt\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.0.5\libraries\BLE\src\BLEDevice.h"
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <SD.h>
#include <SPI.h>

#define RGB_BRIGHTNESS 10
#ifdef RGB_BUILTIN
#undef RGB_BUILTIN
#endif
#define RGB_BUILTIN 21

const int sck = 12;
const int miso = 13;
const int mosi = 11;
const int cs = 10;

#define SERVICE_UUID "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID_READY "87654321-4321-4321-4321-abcdefabcdf5"  // New characteristic for "READY"
#define CHARACTERISTIC_UUID_FILENAME "87654321-4321-4321-4321-abcdefabcdf3"
#define CHARACTERISTIC_UUID_FILETRANSFER "87654321-4321-4321-4321-abcdefabcdf2"
#define CHARACTERISTIC_UUID_NEEDFILE_RESPONSE "87654321-4321-4321-4321-abcdefabcdf4"

BLECharacteristic *pFilenameCharacteristic;
BLECharacteristic *pFileTransferCharacteristic;
BLEServer *pServer;

bool piReady = false;
bool needFile = false;
bool fileTransferInProgress = false;
bool allFilesTransferred = false;  // New flag to track if all files have been transferred
bool deviceConnected = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    allFilesTransferred = false;  // Reset when a new connection is made
    Serial.println("Device connected.");
  }

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    piReady = false;
    needFile = false;
    fileTransferInProgress = false;
    Serial.println("Device disconnected.");

    if (!allFilesTransferred) {
      // Restart advertising only if not all files were transferred
      BLEDevice::getAdvertising()->start();
      Serial.println("Advertising restarted.");
    }
  }
};

// Callback to handle "READY" signal from Pi
class ReadyCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = String(pCharacteristic->getValue().c_str());
    if (value == "READY") {
      piReady = true;
      Serial.println("Pi is ready for file transfer.");
    }
  }
};

// Callback to handle "SEND" or "SKIP" from Pi
class NeedFileCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = String(pCharacteristic->getValue().c_str());
    if (value == "SEND") {
      needFile = true;
      Serial.println("Pi needs this file.");
    } else if (value == "SKIP") {
      needFile = false;
      Serial.println("Pi does not need this file.");
    }
  }
};

void setup() {
  neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS);  // Blue
  Serial.begin(115200);
  delay(2000);
  Serial.println("Starting ESP32 BLE...");

  SPI.begin(sck, miso, mosi, cs);
  while (!SD.begin(cs, SPI, 500000)) {
    Serial.println("SD Card initialization failed!");
    delay(1000);
  }
  Serial.println("SD Card initialized.");

  BLEDevice::init("ESP32_BLE_SD");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create characteristic for "READY"
  BLECharacteristic *pReadyCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_READY,
    BLECharacteristic::PROPERTY_WRITE);
  pReadyCharacteristic->setCallbacks(new ReadyCallback());

  // Create characteristic to send filenames
  pFilenameCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_FILENAME,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);

  // Create characteristic for file transfer data
  pFileTransferCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_FILETRANSFER,
    BLECharacteristic::PROPERTY_NOTIFY);
  pFileTransferCharacteristic->addDescriptor(new BLE2902());

  // Create characteristic to receive file transfer response (SEND/SKIP)
  BLECharacteristic *pNeedFileResponseCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_NEEDFILE_RESPONSE,
    BLECharacteristic::PROPERTY_WRITE);
  pNeedFileResponseCharacteristic->setCallbacks(new NeedFileCallback());

  pService->start();
  BLEDevice::getAdvertising()->start();
  Serial.println("BLE advertising started.");
}

void loop() {
  if (deviceConnected && piReady && !fileTransferInProgress) {
    neopixelWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0);  // Red

    File root = SD.open("/");
    transferFiles(root);  // Process file transfers
  } else {
    neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS);  // Blue when idle
  }
}

// Function to handle file transfer logic
void transferFiles(File dir) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      allFilesTransferred = true;  // Mark all files as transferred
      break;
    }

    String fileName = entry.name();
    if (fileName.endsWith(".txt")) {
      Serial.printf("Sending filename: %s\n", fileName.c_str());
      pFilenameCharacteristic->setValue(fileName.c_str());
      pFilenameCharacteristic->notify();

      // Wait for Pi to respond with "SEND" or "SKIP"
      while (!needFile && deviceConnected) {
        delay(100);  // Wait until Pi responds
      }

      if (needFile && deviceConnected) {
        Serial.printf("Transferring file: %s\n", fileName.c_str());
        fileTransferInProgress = true;

        // Send file data
        while (entry.available()) {
          String dataLine = entry.readStringUntil('\n');
          pFileTransferCharacteristic->setValue(dataLine.c_str());
          pFileTransferCharacteristic->notify();
          delay(100);  // Adjust delay based on transfer speed
        }

        // After transferring all lines of the file, send the EOF marker
        pFileTransferCharacteristic->setValue("EOF");
        pFileTransferCharacteristic->notify();
        delay(100);  // Small delay to ensure EOF is sent

        fileTransferInProgress = false;
        entry.close();
      } else {
        Serial.println("File not needed by Pi.");
        entry.close();
      }

      // Reset needFile flag for the next file
      needFile = false;
    }
  }

  if (allFilesTransferred) {
    Serial.println("All files transferred. Disconnecting...");
    pServer->disconnect(0);  // Disconnect after all files are transferred
  }
}
