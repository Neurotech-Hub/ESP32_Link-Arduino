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

bool fileTransferInProgress = false;
bool allFilesTransferred = false;  // New flag to track if all files have been transferred
bool needFile = false;  // Global flag to track whether Pi needs the file
bool deviceConnected = false;  // Track BLE connection status
bool piReady = false;  // Track Pi readiness

class NeedFileCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = String(pCharacteristic->getValue().c_str());
    if (value == "SEND") {
      needFile = true;
      Serial.println("Pi needs this file.");
    } else if (value == "SKIP") {
      needFile = false;
      Serial.println("Pi does not need this file. Moving to next file.");
    }
    Serial.println("needFile flag updated: " + String(needFile ? "SEND" : "SKIP"));
  }
};

class ReadyCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = String(pCharacteristic->getValue().c_str());
    if (value == "READY") {
      piReady = true;
      Serial.println("Pi is ready to receive files.");
    }
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("Device connected.");
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    needFile = false;  // Reset state when disconnected
    piReady = false;   // Reset Pi readiness
    Serial.println("Device disconnected.");
  }
};

void transferFiles(File dir) {
  if (!piReady) {
    Serial.println("Waiting for Pi to signal readiness...");
    return;
  }

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      Serial.println("All files processed.");
      break;  // No more files
    }

    String fileName = entry.name();
    if (fileName.endsWith(".txt")) {
      Serial.printf("Sending filename: %s\n", fileName.c_str());
      pFilenameCharacteristic->setValue(fileName.c_str());
      pFilenameCharacteristic->notify();

      // Wait for the Pi's response: "SEND" or "SKIP"
      unsigned long startWaitTime = millis();
      while (deviceConnected && !needFile) {
        delay(10);  // Frequent check

        if (millis() - startWaitTime > 2000) {
          Serial.println("Timeout: No response from Pi.");
          break;  // Timeout waiting for Pi's response
        }
      }

      if (needFile && deviceConnected) {
        Serial.printf("Transferring file: %s\n", fileName.c_str());
        while (entry.available()) {
          String dataLine = entry.readStringUntil('\n');
          pFileTransferCharacteristic->setValue(dataLine.c_str());
          pFileTransferCharacteristic->notify();
          delay(50);  // Small delay between lines
        }

        // Send EOF marker
        pFileTransferCharacteristic->setValue("EOF");
        pFileTransferCharacteristic->notify();
        delay(50);  // Ensure EOF is sent
        entry.close();
        Serial.println("File transfer complete.");
      } else {
        Serial.println("Pi skipped this file.");
        entry.close();  // Move to the next file
      }

      needFile = false;  // Reset for the next file
    }
  }

  if (deviceConnected) {
    Serial.println("All files transferred. Disconnecting...");
    pServer->disconnect(0);  // Disconnect after file transfer
  }
}

void setup() {
  Serial.begin(115200);
  BLEDevice::init("ESP32_BLE_SD");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  // Create BLE services and characteristics
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pFilenameCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_FILENAME, BLECharacteristic::PROPERTY_NOTIFY);
  pFileTransferCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_FILETRANSFER, BLECharacteristic::PROPERTY_NOTIFY);
  BLECharacteristic *pNeedFileCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_NEEDFILE_RESPONSE, BLECharacteristic::PROPERTY_WRITE);
  pNeedFileCharacteristic->setCallbacks(new NeedFileCallback());

  // Ensure READY characteristic allows WRITE operations
  BLECharacteristic *pReadyCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_READY,
    BLECharacteristic::PROPERTY_WRITE);  // WRITE permission for Pi's ready signal
  pReadyCharacteristic->setCallbacks(new ReadyCallback());

  // Start BLE service
  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("BLE advertising started.");
}

void loop() {
  if (deviceConnected) {
    // Begin transferring files when connected and Pi is ready
    if (piReady) {
      File root = SD.open("/");
      transferFiles(root);
    }
  } else {
    // If not connected, keep advertising
    BLEDevice::getAdvertising()->start();
  }

  delay(1000);  // Main loop delay to avoid busy-waiting
}
