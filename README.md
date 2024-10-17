### Interacting with the ESP32 BLE File Transfer Device

This document describes the proper way to interact with the ESP32-based BLE device to list files and receive file contents via BLE indications.

#### Overview
The device uses BLE to transfer files stored on an SD card. It provides two main characteristics to interact with:
1. **Filename Characteristic** (`57617368-5502-0001-8000-00805f9b34fb`): Used to list available filenames and request specific file transfers.
2. **File Transfer Characteristic** (`57617368-5503-0001-8000-00805f9b34fb`): Used to receive the actual file data.

### Steps to Interact

#### 1. Connect to the BLE Device
- Search for and connect to the BLE device named **"ESP32_BLE_SD"**.
- After establishing the connection, ensure that your BLE central (e.g., smartphone app or computer program) has enabled **indications** for both characteristics.

#### 2. List Available Files
- To list the files available on the SD card, the central device must enable indications on the **Filename Characteristic**.
- Once indications are enabled, the ESP32 will start sending file information.
  - Each file's name and size will be sent as a formatted string: `"filename|filesize"`.
  - The end of each filename will be marked with **"EON"** (End of Name), and when all filenames are sent, **"EOF"** (End of Files) will be indicated.

#### 3. Request a Specific File
- To request a file, write the desired filename to the **Filename Characteristic**.
- The filename must match one of the available filenames listed by the device.

#### 4. Receive File Contents
- Once a filename is written, the ESP32 will begin transferring the file using the **File Transfer Characteristic**.
- The file will be split into multiple chunks if needed, each chunk fitting within the MTU size negotiated during connection.
- When the entire file has been transferred, an **"EOF"** marker will be indicated to signal the end of the transfer.

### BLE Characteristic UUIDs
- **Service UUID**: `57617368-5501-0001-8000-00805f9b34fb`
  - Contains `"WashU"` to indicate association with Washington University in St. Louis.
- **Filename Characteristic UUID**: `57617368-5502-0001-8000-00805f9b34fb`
- **File Transfer Characteristic UUID**: `57617368-5503-0001-8000-00805f9b34fb`

### Important Notes
- **Indications vs. Notifications**: This device uses **indications** instead of notifications. Indications require acknowledgment from the central device, ensuring data reliability. Make sure your BLE central device supports and properly acknowledges indications.
- **MTU Size**: The default MTU size is set to 20 bytes, but the device attempts to negotiate an MTU of up to 512 bytes for efficient file transfer. The actual MTU size will depend on the capabilities of the central device.

### Example Workflow
1. **Connect** to the BLE device.
2. **Enable indications** on the **Filename Characteristic** to receive the list of files.
3. **Write** the desired filename to the **Filename Characteristic** to initiate file transfer.
4. **Enable indications** on the **File Transfer Characteristic** to receive file contents.

This setup allows reliable file transfers from the ESP32 to any BLE-enabled central device.
 
### Installation Guide
 
 - [Installing for Arduino IDE](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html#installing-using-arduino-ide)

### ESP32-S3-Zero (Waveshare)

- [ESP32-S3-Zero](https://www.waveshare.com/wiki/ESP32-S3-Zero#Arduino)

**Note:** ESP32-S3-Zero does not have a USB to UART chip mounted, you need to use the USB of the ESP32-S3 as the download interface and the Log print interface, and you need to enable the USB CDC when using the Arduino IDE.