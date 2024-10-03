#include "/Users/gaidica/Documents/Arduino/hardware/espressif/esp32/libraries/BLE/src/BLEDevice.h"
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
// #include <SD.h>
// #include <SPI.h>

#define RGB_BRIGHTNESS 10  // Change white brightness (max 255)
#ifdef RGB_BUILTIN
#undef RGB_BUILTIN
#endif
#define RGB_BUILTIN 21

#define NOTIFICATION_WAIT_TIMEOUT 5000  // 5 seconds timeout for notifications
#define SERVICE_UUID "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID_FILELIST "87654321-4321-4321-4321-abcdefabcdf1"      // To receive file list from Pi
#define CHARACTERISTIC_UUID_FILETRANSFER "87654321-4321-4321-4321-abcdefabcdf2"  // For file data
#define SD_CS_PIN 5                                                              // Pin for SD card

BLECharacteristic *pFileListCharacteristic;
BLECharacteristic *pFileTransferCharacteristic;
BLEServer *pServer;  // Global BLE server object
bool deviceConnected = false;
std::vector<String> fileListOnPi;

// File dataFile;  // Commented until SD functionality is needed

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("Device connected.");
  }

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("Device disconnected.");

    // Restart advertising after disconnection
    BLEDevice::getAdvertising()->start();
    Serial.println("Advertising restarted.");
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
      fileListOnPi.clear();
      char *token = strtok((char *)value.c_str(), ",");
      while (token != NULL) {
        fileListOnPi.push_back(String(token));  // Using Arduino String
        token = strtok(NULL, ",");
      }
    }
  }
};

void setup() {
  neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS);  // Blue
  Serial.begin(115200);
  delay(2000);
  Serial.println("Starting ESP32 BLE...");

  // Initialize the SD card
  // if (!SD.begin(SD_CS_PIN)) {
  //   Serial.println("SD Card initialization failed!");
  //   return;
  // }
  // Serial.println("SD Card initialized.");

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
  Serial.println("BLE advertising started.");
}

void loop() {
  if (deviceConnected) {
    Serial.println("Device Connected!");

    // Wait for notifications to be enabled (with a timeout)
    unsigned long startTime = millis();
    while (millis() - startTime < NOTIFICATION_WAIT_TIMEOUT) {
      // Check if notifications are enabled for the file transfer characteristic
      if (pFileTransferCharacteristic->getDescriptorByUUID(BLEUUID((uint16_t)0x2902))->getValue()[0] == 1) {
        Serial.println("Notifications enabled, starting transfer...");
        delay(200);  // Brief delay to ensure everything is ready before transfer

        // Start the file transfer
        //   File root = SD.open("/");
        transferFiles();
        break;
      }

      delay(100);  // Check every 100ms to avoid busy-waiting
    }

    // If the loop finishes and notifications are still not enabled
    if (millis() - startTime >= NOTIFICATION_WAIT_TIMEOUT) {
      Serial.println("Timeout: Notifications not enabled.");
    }
  }

  neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS);  // Blue
  delay(200);                                        // Adjust loop delay as needed
  neopixelWrite(RGB_BUILTIN, 0, 0, 0);               // Turn off LED
  delay(200);
}

// !! Function to simulate file transfer
void transferFiles() {
  // Simulated file names and contents
  String fakeFiles[] = { "file1.txt", "file2.txt", "file3.txt" };
  String fileContents[] = {
    "This is the content of file1.txt.\nLine 2 of file1.txt.\n",
    "Here is file2.txt data.\nAnother line in file2.txt.\n",
    "file3.txt contains this text.\nMore text here in file3.txt.\n"
  };

  // Loop through the simulated files
  for (int i = 0; i < 3; i++) {
    Serial.printf("Transferring file: %s\n", fakeFiles[i].c_str());

    // Send filename to the Pi
    pFileTransferCharacteristic->setValue(fakeFiles[i].c_str());
    pFileTransferCharacteristic->notify();
    delay(100);  // Let Pi register filename

    // Send file contents line by line
    String content = fileContents[i];
    int lineStart = 0;
    int lineEnd = content.indexOf('\n');

    // Loop through each line of the file content
    while (lineEnd != -1) {
      String dataLine = content.substring(lineStart, lineEnd + 1);  // Get the next line
      pFileTransferCharacteristic->setValue(dataLine.c_str());
      pFileTransferCharacteristic->notify();
      delay(100);  // Adjust delay based on transfer speed

      // Move to the next line
      lineStart = lineEnd + 1;
      lineEnd = content.indexOf('\n', lineStart);
    }
  }

  // Optionally disconnect to conserve power after all files are transferred
  // pServer->disconnect(0);
}

// Function to handle file transfer
// void transferFiles(/*File dir*/) {
//   while (true) {
//     File entry = dir.openNextFile();
//     if (!entry) break;

//     // Check if the filename is in the fileListOnPi vector
//     if (std::find(fileListOnPi.begin(), fileListOnPi.end(), entry.name()) == fileListOnPi.end()) {
//       Serial.printf("Transferring file: %s\n", entry.name());

//       // Send filename to the Pi
//       pFileTransferCharacteristic->setValue(entry.name());
//       pFileTransferCharacteristic->notify();
//       delay(100);  // Let Pi register filename

//       // Send file contents
//       while (entry.available()) {
//         String dataLine = entry.readStringUntil('\n');
//         pFileTransferCharacteristic->setValue(dataLine.c_str());
//         pFileTransferCharacteristic->notify();
//         delay(100);  // Adjust delay based on transfer speed
//       }

//       entry.close();
//     }
//   }

//   // Optionally disconnect to conserve power after all files are transferred
//   pServer->disconnect(0);
// }
