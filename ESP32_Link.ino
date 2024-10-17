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
#define CHARACTERISTIC_UUID_FILENAME "87654321-4321-4321-4321-abcdefabcdf3"      // R/W/I characteristic for filenames
#define CHARACTERISTIC_UUID_FILETRANSFER "87654321-4321-4321-4321-abcdefabcdf2"  // R/I characteristic for file transfer
uint16_t mtuSize = 20;  // Default MTU size, updated after negotiation
#define NOTIFICATION_DELAY 20  // ms

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

// BLE Server Callbacks to handle connection and disconnection
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    neopixelWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0);  // Red for active connection
    Serial.println("Device connected");
    // Request a larger MTU after connection
    BLEDevice::setMTU(512);
    mtuSize = BLEDevice::getMTU();
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

// Callback to handle filename requests from the Pi
class FilenameCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = String(pCharacteristic->getValue().c_str());
    currentFileName = String(rxValue.c_str());
    Serial.printf("Requested file: %s\n", currentFileName.c_str());

    // Set flag to start the file transfer in the main loop
    if (currentFileName != "") {
      fileTransferInProgress = true;
    }
  }
};

bool isValidFile(String fileName) {
  String lowerFileName = fileName;
  lowerFileName.toLowerCase();  // Convert filename to lowercase

  for (int i = 0; i < numExtensions; i++) {
    String lowerExtension = validExtensions[i];
    lowerExtension.toLowerCase();  // Convert valid extension to lowercase

    if (lowerFileName.endsWith(lowerExtension)) {
      return true;  // Return true if any valid extension matches
    }
  }
  return false;  // Return false if no match found
}

// Initialize BLE and setup the characteristics
void setup() {
  // Request to increase MTU size
  BLEDevice::setMTU(512);
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

  // Create the FILENAME characteristic (R/W/I)
  pFilenameCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_FILENAME,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pFilenameCharacteristic->addDescriptor(new BLE2902());  // Enable notifications
  pFilenameCharacteristic->setCallbacks(new FilenameCallback());

  // Create the FILETRANSFER characteristic (R/I)
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
  if (deviceConnected && fileTransferInProgress && currentFileName != "") {
    transferFile(currentFileName);
    currentFileName = "";
    fileTransferInProgress = false;
  }

  if (deviceConnected && !fileTransferInProgress && !allFilesSent && pFilenameCharacteristic->getDescriptorByUUID(BLEUUID((uint16_t)0x2902))->getValue()[0] == 1) {
    Serial.println("Notifications enabled, send filenames...");
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
      pFilenameCharacteristic->setValue("EOF");  // Signal end of filenames
      pFilenameCharacteristic->notify();
      allFilesSent = true;
      break;
    }

    String fileName = entry.name();
    if (isValidFile(fileName)) {
      Serial.printf("Sending filename and size: %s|%d\n", fileName.c_str(), entry.size());
      String fileInfo = fileName + "|" + String(entry.size());

      // Split fileInfo into chunks if it exceeds mtuSize
      int index = 0;
      while (index < fileInfo.length()) {
        String chunk = fileInfo.substring(index, index + mtuSize);
        pFilenameCharacteristic->setValue(chunk.c_str());
        pFilenameCharacteristic->notify();
        index += mtuSize;
        delay(NOTIFICATION_DELAY);
      }

      // Send end of name marker
      pFilenameCharacteristic->setValue("EON");  // Signal end of name
      pFilenameCharacteristic->notify();
      delay(NOTIFICATION_DELAY);
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

  Serial.printf("Transferring %s (MTU=%i)\n", fileName.c_str(), mtuSize);
  uint8_t buffer[mtuSize];

  while (file.available()) {
    int bytesRead = file.read(buffer, mtuSize);
    if (bytesRead > 0) {
      pFileTransferCharacteristic->setValue(buffer, bytesRead);
      pFileTransferCharacteristic->notify();
      delay(NOTIFICATION_DELAY);
    } else {
      Serial.println("Error reading from file.");
      break;
    }
  }

  // Send EOF marker to signal end of file transfer
  pFileTransferCharacteristic->setValue("EOF");
  pFileTransferCharacteristic->notify();

  file.close();
  Serial.println("File transfer complete.");
}
