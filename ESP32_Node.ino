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
#define CHARACTERISTIC_UUID_FILENAME "87654321-4321-4321-4321-abcdefabcdf3"   // R/W/N characteristic for filenames
#define CHARACTERISTIC_UUID_FILETRANSFER "87654321-4321-4321-4321-abcdefabcdf2"  // R/N characteristic for file transfer

BLECharacteristic *pFilenameCharacteristic;
BLECharacteristic *pFileTransferCharacteristic;
BLEServer *pServer;

bool piReadyForFilenames = false;   // Flag to check if Pi has enabled notifications
bool deviceConnected = false;       // Track connection status
bool fileTransferInProgress = false; // Track if file transfer is in progress
String currentFileName = "";        // Store current file being transferred
bool allFilesSent = false;          // Flag to track if all filenames have been sent
String macAddress;                  // MAC address for prepending filenames

// BLE Server Callbacks to handle connection and disconnection
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("Device connected.");
    neopixelWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0);  // Red for active connection
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    piReadyForFilenames = false;
    fileTransferInProgress = false;
    allFilesSent = false;
    Serial.println("Device disconnected. Restarting advertising...");
    neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS);  // Blue for idle
    BLEDevice::getAdvertising()->start(); // Restart advertising
  }
};

// Callback to handle filename requests from the Pi
class FilenameCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    std::string rxValue = pCharacteristic->getValue();
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
  macAddress = BLEDevice::getAddress().toString().c_str();
  macAddress.replace(":", "");

  // Initialize RGB LED
  neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS);  // Blue for idle

  // Initialize SPI and SD card
  SPI.begin(sck, miso, mosi, cs);
  while (!SD.begin(cs, SPI, 500000)) {
    Serial.println("SD Card initialization failed!");
    delay(1000);
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
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
  );
  pFilenameCharacteristic->addDescriptor(new BLE2902()); // Enable notifications
  pFilenameCharacteristic->setCallbacks(new FilenameCallback());

  // Create the FILETRANSFER characteristic (R/N)
  pFileTransferCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_FILETRANSFER,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pFileTransferCharacteristic->addDescriptor(new BLE2902()); // Enable notifications

  // Start the BLE service
  pService->start();
  BLEDevice::getAdvertising()->start();
  Serial.println("BLE advertising started.");
}

// Main loop to handle file listing and transfer
void loop() {
  if (deviceConnected && !fileTransferInProgress && !allFilesSent) {
    // Start sending filenames to the Pi if connected and ready
    sendFilenames();
  }

  // If no device is connected, restart advertising
  if (!deviceConnected) {
    BLEDevice::getAdvertising()->start();
  }

  delay(1000); // Avoid busy waiting
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
    if (fileName.endsWith(".txt")) {
      String fullFileName = macAddress + "_" + fileName; // Prepend MAC address
      Serial.printf("Sending filename: %s\n", fullFileName.c_str());
      pFilenameCharacteristic->setValue(fullFileName.c_str());
      pFilenameCharacteristic->notify();
      delay(500);  // Delay to prevent overwhelming the Pi
    }
  }
}

// Function to transfer a file line by line over FILETRANSFER characteristic
void transferFile(String fileName) {
  File file = SD.open(fileName);
  if (!file) {
    Serial.printf("Failed to open file: %s\n", fileName.c_str());
    return;
  }

  Serial.printf("Transferring file: %s\n", fileName.c_str());
  while (file.available()) {
    String dataLine = file.readStringUntil('\n');
    pFileTransferCharacteristic->setValue(dataLine.c_str());
    pFileTransferCharacteristic->notify();
    delay(100);  // Small delay between lines
  }

  // Send EOF marker to signal end of file transfer
  pFileTransferCharacteristic->setValue("EOF");
  pFileTransferCharacteristic->notify();
  file.close();
  Serial.println("File transfer complete.");
  fileTransferInProgress = false;
}
