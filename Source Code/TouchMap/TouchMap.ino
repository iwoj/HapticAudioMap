// =========================
// Must upload with No OTA (Large APP) partition scheme


// WiFi Stuff ---------
#include <WiFi.h>
#include <WebServer.h>
#include <AutoConnect.h>
WebServer   Server;
AutoConnect Portal(Server);
WiFiClient httpClient;
WiFiClient touchEventClient;

// TODO:
// It would be nice if this could be autodiscovered on the local network
// instead of hardcoded.
#define SERVER_HOST        "designcards.mooo.com"
#define SERVER_HTTP_PORT    3000
#define SERVER_EVENT_PORT   9000


// BLE Stuff ---------
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// TODO:
// - Load these variables continuously from server.
#define PROXIMITY_LIMIT_RSSI  -60
#define MAX_CLOSE_DEVICES     10
char myMACAddress[25];

byte scanTime = 1; // In seconds.
BLEScan* pBLEScan;
bool runningScan = false;
byte deviceCounter = 0;
BLEAdvertisedDevice closeDevices[MAX_CLOSE_DEVICES];
BLEAdvertisedDevice previousCloseDevices[MAX_CLOSE_DEVICES];
BLEAdvertisedDevice nullDevice = BLEAdvertisedDevice();

// Capacitive Touch Stuff ---------
const int TOUCH_SENSOR_THRESHOLD = 70;
int touchSensor1Value = 0;
int touchSensor2Value = 0;
int touchSensor3Value = 0;
boolean touch1Start = false;
boolean touch2Start = false;
boolean touch3Start = false;
#define UNSIGNED_LONG_MAX 4294967295
unsigned long sequence = 1;

#include <Queue.h>
struct jsonPost {
  String route;
  String payload;
};
DataQueue<jsonPost> jsonConnectionQueue(10);

const byte LED_PIN = 5; // Thing's onboard LED

#define DEBUG     false



class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      if (advertisedDevice.haveRSSI()) {
        if ((int)advertisedDevice.getRSSI() > PROXIMITY_LIMIT_RSSI) {
          //          Serial.printf("Advertised Device: %s", advertisedDevice.toString().c_str());
          //          Serial.printf(", Rssi: %d \n", (int)advertisedDevice.getRSSI());
          if (deviceCounter < MAX_CLOSE_DEVICES) {
            closeDevices[deviceCounter] = advertisedDevice;
            deviceCounter++;
          }
        }
      }
    }
};



void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  connectWiFi();
  registerExhibit();
  connectEventServer();
  setupBLE();
  clearDevices(previousCloseDevices);
  clearDevices(closeDevices);
  runBLEScan();
}



void loop() {
  Portal.handleClient();
  readSensors();
  senseTouchEvents();
  handleConnectionQueue();
}



void handleConnectionQueue() {
  // Handle connection queue
  while(jsonConnectionQueue.item_count() > 0) {
    Serial.print("Connection queue item count: ");
    Serial.println(jsonConnectionQueue.item_count());
    if (!httpClient.connected()) { // Block on connections and handle each one in sequence.
      jsonPost postThis = jsonConnectionQueue.dequeue();
      postJSONData(postThis.route, postThis.payload);
      while (httpClient.available()) {
        char c = httpClient.read();
        Serial.write(c);
      }
      httpClient.stop();
    }
  }
}

void rootPage() {
  char content[] = "Hello, world";
  Server.send(200, "text/plain", content);
}



void connectWiFi()
{
  Server.on("/", rootPage);
  if (Portal.begin()) {
    if (DEBUG) Serial.println("WiFi connected: " + WiFi.localIP().toString());
  }
}



void setupBLE() {
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
}



void runBLEScan() {
  if (DEBUG) Serial.println("Scanning BLE...");
  clearDevices(previousCloseDevices);
  bleadCopy(closeDevices, previousCloseDevices, MAX_CLOSE_DEVICES);
  clearDevices(closeDevices);
  deviceCounter = 0;
  runningScan = true;
  pBLEScan->start(scanTime, scanComplete);
}



void scanComplete(BLEScanResults foundDevices) {
  runningScan = false;
  if (DEBUG) sortDevices(closeDevices, deviceSortByRSSI, MAX_CLOSE_DEVICES);
  if (DEBUG) Serial.println("closeDevices:");
  if (DEBUG) printDeviceList(closeDevices, MAX_CLOSE_DEVICES);
  if (DEBUG) Serial.println("previousCloseDevices:");
  if (DEBUG) printDeviceList(previousCloseDevices, MAX_CLOSE_DEVICES);
  handleBLEProximity();
  pBLEScan->clearResults();
  if (DEBUG) Serial.println("");
  runBLEScan();
}



BLEAdvertisedDevice *arrSubtract(BLEAdvertisedDevice a[], BLEAdvertisedDevice b[]) {
  BLEAdvertisedDevice combined[MAX_CLOSE_DEVICES*2];
  BLEAdvertisedDevice duplicates[MAX_CLOSE_DEVICES];
  byte duplicatesIndex = 0;
  static BLEAdvertisedDevice differences[MAX_CLOSE_DEVICES];
  byte differencesIndex = 0;
  
  clearDevices(duplicates);
  clearDevices(differences);
  
  // Combine a + b
  for (byte i = 0; i < MAX_CLOSE_DEVICES; i++) {
    combined[i] = a[i];
  }
  for (byte i = 0; i < MAX_CLOSE_DEVICES; i++) {
    combined[MAX_CLOSE_DEVICES+i] = b[i];
  }
  
  sortDevices(combined, deviceSortByAddress, MAX_CLOSE_DEVICES*2);
  
  // Find duplicates
  for (byte i = 0; i < MAX_CLOSE_DEVICES*2 - 1; i++) {
    if (combined[i].getAddress().toString() == combined[i+1].getAddress().toString()) {
      duplicates[duplicatesIndex] = combined[i];
      duplicatesIndex++;
      i++;
    }
  }
  
  // Perform subtraction by storing non-duplicates
  boolean isDuplicate;
  for (byte i = 0; i < MAX_CLOSE_DEVICES; i++) {
    if (a[i].getAddress().toString() == nullDevice.getAddress().toString()) continue; // Ignore nullDevices
    isDuplicate = false;
    for (byte j = 0; j < MAX_CLOSE_DEVICES; j++) {
      if (duplicates[j].getAddress().toString() == nullDevice.getAddress().toString()) continue; // Ignore nullDevices
      if (a[i].getAddress().toString() == duplicates[j].getAddress().toString()) {
        isDuplicate = true;
      }
    }
    if (!isDuplicate) {
      differences[differencesIndex] = a[i];
      differencesIndex++;
    }
  }
  
  return differences;
}



void printDeviceList(BLEAdvertisedDevice a[], byte arrLen) {
  byte maxDevices = deviceCount(a);
  byte deviceCounter = 0;
  for (byte i = 0; i < arrLen - 1; i++) {
    if (a[i].getAddress().toString() != nullDevice.getAddress().toString()) {
      deviceCounter++;
      Serial.print(a[i].getAddress().toString().c_str());
      if (deviceCounter != maxDevices) Serial.print(", ");
    }
  }
  if (arrLen > 0 && a[arrLen - 1].getAddress().toString() != nullDevice.getAddress().toString()) {
    Serial.print(a[arrLen - 1].getAddress().toString().c_str());
  }
  Serial.println("");
}



void handleBLEProximity() {
  if (!runningScan) {
    byte numCloseDevices = deviceCount(closeDevices);
    
    if (numCloseDevices == 0) {
      Serial.println("No devices close by.");
      return;
    }
    
    sortDevices(closeDevices, deviceSortByRSSI, MAX_CLOSE_DEVICES);
    
    Serial.println(deviceCount(closeDevices) + (String)" device(s) close by.");
    Serial.printf("Closest device: %s\n", closeDevices[0].getAddress().toString().c_str());
    
    if (DEBUG) Serial.print("closeDevices: ");
    if (DEBUG) printDeviceList(closeDevices, MAX_CLOSE_DEVICES);
    if (DEBUG) Serial.print("\n");
    if (DEBUG) Serial.print("previousCloseDevices: ");
    if (DEBUG) printDeviceList(previousCloseDevices, MAX_CLOSE_DEVICES);
    if (DEBUG) Serial.print("\n");
    
    BLEAdvertisedDevice *newDevices = arrSubtract(closeDevices, previousCloseDevices);
    Serial.print("Number of new devices: ");
    Serial.println(deviceCount(newDevices));
    if (DEBUG) Serial.print("New devices: ");
    if (DEBUG) printDeviceList(newDevices, MAX_CLOSE_DEVICES);
    if (DEBUG) Serial.print("\n");

    
    BLEAdvertisedDevice *lostDevices = arrSubtract(previousCloseDevices, closeDevices);
    Serial.print("Number of devices lost: ");
    Serial.println(deviceCount(lostDevices));
    if (DEBUG) Serial.print("Lost devices: ");
    if (DEBUG) printDeviceList(lostDevices, MAX_CLOSE_DEVICES);
    if (DEBUG) Serial.print("\n");
    
    byte numDevices = deviceCount(closeDevices);
    String payload = "[{\"exhibitMACAddress\": \"" + String(myMACAddress) + "\", ";
    payload += "\"devices\":[";
    for (byte i = 0; i < numDevices; i++){
      payload += "{\"address\": \"" + String(closeDevices[i].getAddress().toString().c_str()) + "\",";
//      payload += "\"serviceDataUUID\": \"" + String(closeDevices[i].getServiceDataUUID().toString().c_str()) + "\",";
//      payload += "\"serviceUUID\": \"" + String(closeDevices[i].getServiceUUID().toString().c_str()) + "\",";
//      payload += "\"name\": \"" + String(closeDevices[i].getName().c_str()) + "\",";
      payload += "\"string\": \"" + String(closeDevices[i].toString().c_str()) + "\",";
//      payload += "\"manufacturerData\": \"" + String(closeDevices[i].getManufacturerData().c_str()) + "\",";
      // See: https://forum.arduino.cc/index.php?topic=626200.0
      payload +="\"signalStrength\": " + String(closeDevices[i].getRSSI()) + "}";
      if (i < numDevices - 1)
        payload += ",";
    }
    payload += "]}]";
    
    
    jsonConnectionQueue.enqueue((jsonPost){"/methods/exhibitdevices.addSample", payload});
  }
}



void readSensors() {
  byte numberOfReadings = 10;
  
  touchSensor1Value = 0;
  touchSensor2Value = 0;
  touchSensor3Value = 0;
  
  for (byte i = 0; i < numberOfReadings; i++)
  {
    touchSensor1Value += touchRead(T0);
    touchSensor2Value += touchRead(T2);
    touchSensor3Value += touchRead(T3);
  }
  
  touchSensor1Value = touchSensor1Value / numberOfReadings;
  touchSensor2Value = touchSensor2Value / numberOfReadings;
  touchSensor3Value = touchSensor3Value / numberOfReadings;
}



void senseTouchEvents() {
  sortDevices(closeDevices, deviceSortByRSSI, MAX_CLOSE_DEVICES);
  
  if (sequence >= UNSIGNED_LONG_MAX - 6) {
    jsonConnectionQueue.enqueue((jsonPost){"/methods/resetbuttonsequences", "[{\"macAddress\": \"" + String(myMACAddress) + "\"}]"});
    sequence = 1;
  }
  
  if (touchSensor1Value < TOUCH_SENSOR_THRESHOLD && !touch1Start) {
    Serial.print("Touch1 start (");
    Serial.print(touchSensor1Value);
    Serial.println(")");
    touch1Start = true;
    String payload = "[{\"exhibitMACAddress\": \"" + String(myMACAddress) + "\", ";
    payload += "\"deviceString\": \"" + String(closeDevices[0].toString().c_str()) + "\",";
    payload += "\"buttonID\": 1, ";
    payload += "\"buttonState\": \"down\",";
    payload += "\"sequence\": " + String(sequence++);
    payload += "}]";
    sendEventData(payload);
  }
  else if (touchSensor1Value > TOUCH_SENSOR_THRESHOLD && touch1Start) {
    Serial.println("Touch1 end");
    touch1Start = false;
    String payload = "[{\"exhibitMACAddress\": \"" + String(myMACAddress) + "\", ";
    payload += "\"deviceString\": \"" + String(closeDevices[0].toString().c_str()) + "\",";
    payload += "\"buttonID\": 1, ";
    payload += "\"buttonState\": \"up\",";
    payload += "\"sequence\": " + String(sequence++);
    payload += "}]";
    sendEventData(payload);
  }
  
  if (touchSensor2Value < TOUCH_SENSOR_THRESHOLD && !touch2Start) {
    Serial.print("Touch2 start (");
    Serial.print(touchSensor2Value);
    Serial.println(")");
    touch2Start = true;
    String payload = "[{\"exhibitMACAddress\": \"" + String(myMACAddress) + "\", ";
    payload += "\"deviceString\": \"" + String(closeDevices[0].toString().c_str()) + "\",";
    payload += "\"buttonID\": 2, ";
    payload += "\"buttonState\": \"down\",";
    payload += "\"sequence\": " + String(sequence++);
    payload += "}]";
    sendEventData(payload);
  }
  else if (touchSensor2Value > TOUCH_SENSOR_THRESHOLD && touch2Start) {
    Serial.println("Touch2 end");
    touch2Start = false;
    String payload = "[{\"exhibitMACAddress\": \"" + String(myMACAddress) + "\", ";
    payload += "\"deviceString\": \"" + String(closeDevices[0].toString().c_str()) + "\",";
    payload += "\"buttonID\": 2, ";
    payload += "\"buttonState\": \"up\",";
    payload += "\"sequence\": " + String(sequence++);
    payload += "}]";
    sendEventData(payload);
  }

  if (touchSensor3Value < TOUCH_SENSOR_THRESHOLD && !touch3Start) {
    Serial.print("Touch3 start (");
    Serial.print(touchSensor3Value);
    Serial.println(")");
    touch3Start = true;
    String payload = "[{\"exhibitMACAddress\": \"" + String(myMACAddress) + "\", ";
    payload += "\"deviceString\": \"" + String(closeDevices[0].toString().c_str()) + "\",";
    payload += "\"buttonID\": 3, ";
    payload += "\"buttonState\": \"down\",";
    payload += "\"sequence\": " + String(sequence++);
    payload += "}]";
    sendEventData(payload);
  }
  else if (touchSensor3Value > TOUCH_SENSOR_THRESHOLD && touch3Start) {
    Serial.println("Touch3 end");
    touch3Start = false;
    String payload = "[{\"exhibitMACAddress\": \"" + String(myMACAddress) + "\", ";
    payload += "\"deviceString\": \"" + String(closeDevices[0].toString().c_str()) + "\",";
    payload += "\"buttonID\": 3, ";
    payload += "\"buttonState\": \"up\", ";
    payload += "\"sequence\": " + String(sequence++);
    payload += "}]";
    sendEventData(payload);
  }
}



void sortDevices(BLEAdvertisedDevice devices[], int8_t (*comparator)(BLEAdvertisedDevice &a, BLEAdvertisedDevice &b), byte arrLen) {
  boolean swapped;
  BLEAdvertisedDevice temp;
  do
  {
      swapped = false;
      for (byte i = 0; i < arrLen - 1; i++)
      {
          if (comparator(devices[i], devices[i+1]) > 0)
          {
              temp = devices[i];
              devices[i] = devices[i + 1];
              devices[i + 1] = temp;
              swapped = true;
          }
      }
  } while (swapped);
}



int8_t deviceSortByAddress(BLEAdvertisedDevice &a, BLEAdvertisedDevice &b) {
  int8_t result = 0;
  if (b.getAddress().toString() == "00:00:00:00:00:00") result = -1; // Keep empty devices at the end of list.
  else if (a.getAddress().toString() == "00:00:00:00:00:00") result = 1; // Keep empty devices at the end of list.
  else result = strcmp(a.getAddress().toString().c_str(),
                      b.getAddress().toString().c_str());
  return result;
}

int8_t deviceSortByRSSI(BLEAdvertisedDevice &a, BLEAdvertisedDevice &b) {
  int8_t result = 0;
  if (a.getRSSI() > b.getRSSI()) result = -1;
  else if (a.getRSSI() < b.getRSSI()) result = 1;
  return result;
}



void clearDevices(BLEAdvertisedDevice devices[]) {
  for (byte i = 0; i < MAX_CLOSE_DEVICES; i++) {
    devices[i] = nullDevice;
  }
}



byte deviceCount(BLEAdvertisedDevice devices[]) {
  byte count = 0;
  for (byte i = 0; i < MAX_CLOSE_DEVICES; i++) {
    if (devices[i].getAddress().toString() != nullDevice.getAddress().toString())
    {
      count++;
    }
  }
  return count;
}


void bleadCopy(BLEAdvertisedDevice arrayOriginal[], BLEAdvertisedDevice arrayCopy[], byte arraySize){ //Copy function
  for(byte i=0; i<arraySize; i++){
    arrayCopy[i]=arrayOriginal[i];  
  }
}


void connectEventServer() {
  if (touchEventClient.connect(SERVER_HOST, SERVER_EVENT_PORT) > 0) {
    Serial.print("Connected to event server.");
  }
  else {
    Serial.print("Failed to connect to event server. ");
  }
}

bool sendEventData(String payload) {
  if (!touchEventClient.connected()) {
    connectEventServer();
  }
  if (DEBUG) Serial.println("sendEventData");
  if (DEBUG) Serial.println(payload);
  touchEventClient.print(payload);
  Serial.println("sent.");
}


bool postJSONData(String route, String payload) {
  if (DEBUG) Serial.println("postJSONData");
  // close any connection before send a new request.
  // This will free the socket on the WiFi shield
  // httpClient.stop();
  
  // if there's a successful connection:
  if (httpClient.connect(SERVER_HOST, SERVER_HTTP_PORT) > 0) {
    Serial.println("Connected to HTTP server.");
    // send the HTTP POST request:
    
    // Build HTTP request.
    String toSend = "";
    toSend += "POST ";
    toSend += route;
    toSend += " HTTP/1.1\r\n";
    toSend += "Host:";
    toSend += SERVER_HOST;
    toSend += "\r\n" ;
    toSend += "Content-Type: application/json\r\n";
    toSend += "User-Agent: Arduino\r\n";
    toSend += "Accept-Version: ~0\r\n";
    toSend += "Connection: close\r\n";
    toSend += "Content-Length: "+String(payload.length())+"\r\n";
    toSend += "\r\n";
    toSend += payload;
    if (DEBUG) Serial.println(toSend);
    httpClient.println(toSend);
    Serial.println("JSON data sent.");
    return true;
  } else {
    // if you couldn't make a connection:
    Serial.println("Failed to connect to HTTP server.");
    return false;
  }
}


void registerExhibit() {
  uint8_t address[6];
  esp_efuse_mac_get_default(address);
  Serial.println("");
  Serial.printf("My MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n", address[0], address[1], address[2], address[3], address[4], address[5]);
  sprintf(myMACAddress, "%02x:%02x:%02x:%02x:%02x:%02x", address[0], address[1], address[2], address[3], address[4], address[5]);
  jsonConnectionQueue.enqueue((jsonPost){"/methods/registerexhibit", "[{\"macAddress\": \"" + String(myMACAddress) +"\", \"buttons\": [{\"state\":\"up\", \"id\":\"1\", \"sequence\": 0}, {\"state\":\"up\", \"id\":\"2\", \"sequence\": 0}, {\"state\":\"up\", \"id\":\"3\", \"sequence\": 0}]}]"});
}
