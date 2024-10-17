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
#define CHARACTERISTIC_UUID_FILENAME "87654321-4321-4321-4321-abcdefabcdf3"      // R/W/N characteristic for filenames
#define CHARACTERISTIC_UUID_FILETRANSFER "87654321-4321-4321-4321-abcdefabcdf2"  // R/N characteristic for file transfer

BLECharacteristic *pFilenameCharacteristic;
BLECharacteristic *pFileTransferCharacteristic;
BLEServer *pServer;

bool piReadyForFilenames = false;     // Flag to check if Pi has enabled notifications
bool deviceConnected = false;         // Track connection status
bool fileTransferInProgress = false;  // Track if file transfer is in progress
String currentFileName = "";          // Store current file being transferred
bool allFilesSent = false;            // Flag to track if all filenames have been sent
String macAddress;                    // MAC address for prepending filenames

String validExtensions[] = { ".txt", ".csv", ".log" };  // Array of valid file extensions
int numExtensions = sizeof(validExtensions) / sizeof(validExtensions[0]);
void transferFile(String fileName);

bool isValidFile(String fileName) {
    String lowerFileName = fileName;
    lowerFileName.toLowerCase(); // Convert filename to lowercase

    for (int i = 0; i < numExtensions; i++) {
        String lowerExtension = validExtensions[i];
        lowerExtension.toLowerCase(); // Convert valid extension to lowercase

        if (lowerFileName.endsWith(lowerExtension)) {
            return true; // Return true if any valid extension matches
        }
    }
    return false; // Return false if no match found
}

// BLE Server Callbacks to handle connection and disconnection
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("Device connected.");
    neopixelWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0);  // Red for active connection

    // Request to increase the MTU size
    pServer->updateConnParams(0, 0x20, 0x20, 400);  // Optional, update connection parameters for a better response
    pServer->getPeerDevice()->requestMtu(512);  // Maximum MTU size
  }

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    piReadyForFilenames = false;
    fileTransferInProgress = false;
    allFilesSent = false;
    Serial.println("Device disconnected. Restarting advertising...");
    neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS);  // Blue for idle
    BLEDevice::getAdvertising()->start();              // Restart advertising
  }
};

// Callback to handle MTU size negotiation
class MTUCallback : public BLEServerCallbacks {
  void onMtuChanged(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) {
    Serial.printf("MTU size updated to: %d\n", param->mtu);
  }
};

// Callback to handle filename requests from the Pi
class FilenameCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = String(pCharacteristic->getValue().c_str());
    currentFileName = String(rxValue.c_str());
    Serial.printf("Pi requested file: %s\n", currentFileName.c_str());

    // Start the file transfer if a valid filename is received
    if (currentFileName != "") {
      fileTransferInProgress = true;
      transferFile(currentFileName);
    }
  }
};

// Initialize BLE and setup the characteristics
void setup() {
  Serial.begin(115200);
  delay(2000);  // serial port

  // Initialize RGB LED
  neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS);  // Blue for idle

  // Initialize SPI and SD card
  SPI.begin(sck, miso, mosi, cs);
  while (!SD.begin(cs, SPI, 500000)) {
    Serial.println("SD Card initialization failed!");
    delay(500);
  }
  Serial.println("SD Card initialized.");

  // Initialize BLE
  BLEDevice::init("ESP32_BLE_SD");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create the FILENAME characteristic (R/W/N)
  pFilenameCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_FILENAME,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pFilenameCharacteristic->addDescriptor(new BLE2902());  // Enable notifications
  pFilenameCharacteristic->setCallbacks(new FilenameCallback());

  // Create the FILETRANSFER characteristic (R/N)
  pFileTransferCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_FILETRANSFER,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pFileTransferCharacteristic->addDescriptor(new BLE2902());  // Enable notifications

  // Start the BLE service
  pService->start();
  BLEDevice::getAdvertising()->start();
  Serial.println("BLE advertising started.");
}

// Main loop to handle file listing and transfer
void loop() {
  if (deviceConnected && !fileTransferInProgress && !allFilesSent && pFilenameCharacteristic->getDescriptorByUUID(BLEUUID((uint16_t)0x2902))->getValue()[0] == 1) {
    Serial.println("Notifications enabled, starting to send filenames...");

    // Start sending filenames to the Pi if connected and ready
    sendFilenames();
  }

  // If no device is connected, restart advertising
  if (!deviceConnected) {
    BLEDevice::getAdvertising()->start();
  }

  delay(100);  // Avoid busy waiting
}

// Function to send all filenames to the Pi
void sendFilenames() {
  File root = SD.open("/");
  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      Serial.println("All filenames sent.");
      allFilesSent = true;
      pFilenameCharacteristic->setValue("EOF");  // Signal end of filenames
      pFilenameCharacteristic->notify();
      break;
    }

    String fileName = entry.name();
    if (isValidFile(fileName)) {
      Serial.printf("Sending filename and size: %s|%d\n", fileName.c_str(), entry.size());
      String fileInfo = fileName + "|" + String(entry.size());
      pFilenameCharacteristic->setValue(fileInfo.c_str());
      pFilenameCharacteristic->notify();
      delay(50);  // !!optimize, Delay to prevent overwhelming the Pi
    }
  }
  root.close();
}

// Function to transfer a file byte by byte over FILETRANSFER characteristic
void transferFile(String fileName) {
  File file = SD.open("/" + fileName);
  if (!file) {
    Serial.printf("Failed to open file: %s\n", fileName.c_str());
    return;
  }

  Serial.printf("Transferring file: %s\n", fileName.c_str());
  int mtuSize = pServer->getPeerDevice()->getMtu() - 3;  // Get the current MTU size and subtract 3 for ATT overhead
  uint8_t buffer[mtuSize];
  while (file.available()) {
    int bytesRead = file.read(buffer, sizeof(buffer));
    pFileTransferCharacteristic->setValue(buffer, bytesRead);
    pFileTransferCharacteristic->notify();
    delay(10);  // Small delay to prevent overwhelming the BLE connection
  }

  // Send EOF marker to signal end of file transfer
  pFileTransferCharacteristic->setValue("EOF");
  pFileTransferCharacteristic->notify();
  file.close();
  Serial.println("File transfer complete.");
  fileTransferInProgress = false;
}
