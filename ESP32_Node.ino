#include "/Users/gaidica/Documents/Arduino/hardware/espressif/esp32/libraries/BLE/src/BLEDevice.h"
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define RGB_BRIGHTNESS 10  // Change white brightness (max 255)
#ifdef RGB_BUILTIN
#undef RGB_BUILTIN
#endif
#define RGB_BUILTIN 21

#define SERVICE_UUID "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID_FILELIST "87654321-4321-4321-4321-abcdefabcdf1"      // To receive file list from Pi
#define CHARACTERISTIC_UUID_FILETRANSFER "87654321-4321-4321-4321-abcdefabcdf2"  // For file data

BLECharacteristic *pFileListCharacteristic;
BLECharacteristic *pFileTransferCharacteristic;
BLEServer *pServer;  // Global BLE server object
bool deviceConnected = false;

// Server callback functions to track connection status
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("Device connected.");
  }

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("Device disconnected.");
  }
};

// Callback to handle the file list sent by the Pi
class FileListCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = String(pCharacteristic->getValue().c_str());  // Convert std::string to Arduino String
    if (value.length() > 0) {
      Serial.print("File list received from Pi: ");
      Serial.println(value);

      // Parse received file list (assumes filenames are separated by commas)
      // fileListOnPi.clear();
      // char *token = strtok((char *)value.c_str(), ",");
      // while (token != NULL) {
      //   fileListOnPi.push_back(String(token));  // Using Arduino String
      //   token = strtok(NULL, ",");
      // }
    }
  }
};

void setup() {
  neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS);  // Blue
  Serial.begin(115200);
  delay(2000);
  Serial.println("Starting ESP32 BLE...");

  // Initialize BLE
  BLEDevice::init("ESP32_BLE_SD");
  pServer = BLEDevice::createServer();  // Assign to global pServer
  pServer->setCallbacks(new ServerCallbacks());

  // Create BLE service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Setup characteristic to receive file list from Pi
  pFileListCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_FILELIST,
    BLECharacteristic::PROPERTY_WRITE);
  pFileListCharacteristic->setCallbacks(new FileListCallback());

  // Setup characteristic for file transfer
  pFileTransferCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_FILETRANSFER,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pFileTransferCharacteristic->addDescriptor(new BLE2902());

  // Start the BLE service
  pService->start();

  // Start advertising using legacy BLE advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->start();
  Serial.println("BLE advertising started. Waiting for client connection...");
}

void loop() {
  neopixelWrite(RGB_BUILTIN, 0, RGB_BRIGHTNESS, 0);  // Green
  delay(200);                                        // Adjust loop delay as needed
  neopixelWrite(RGB_BUILTIN, 0, 0, 0);               // Turn off LED
  delay(200);
}