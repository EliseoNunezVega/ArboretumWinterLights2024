#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <ArtnetWifi.h>
#include "SparkFun_VL53L1X.h"
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <math.h>
#include <Preferences.h>
// #include <cmath>

// Used to store rail dependent parameters
Preferences preferences;

// Pin definitions and sensor setups
#define IRQ_PIN1 7
#define IRQ_PIN2 10
#define XSHUT_PIN1 13
#define XSHUT_PIN2 16
#define LED_PIN 2
#define LED_BRIGHT 255

unsigned long ota_progress_millis = 0;

// Used to share Serial output with HTML server
String serialOutput = "";  // Store serial output
bool webInputAvailable = 0;
String webInput = "";

// Debugging flags
bool printDistancesEnabled = 0;
bool displayArtnetData = 0;
bool useFilter = 0;
bool printHowManyHands = 0;
bool displayParsedData = 0;
bool twoHandEffectActive = 1;

// EMA Filter Parameters
int previous_distance1 = 0;
int previous_distance2 = 0;
float old_weight = 0.5;
float new_weight = 0.5;

// Rail specific parameters
int RAIL_LENGTH = 1143;
int LED_COUNT = 18;

// Error tracking
bool sensor1Errored = 0;
bool sensor2Errored = 0;

// Instantiating distance sensor classes & LED class
SFEVL53L1X distanceSensor1(Wire, XSHUT_PIN1, IRQ_PIN1);
SFEVL53L1X distanceSensor2(Wire, XSHUT_PIN2, IRQ_PIN2);
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_RWGB + NEO_KHZ800);

// Global color channel variables: changed by lightjams via Artnet
uint8_t r_channel = 255;
uint8_t g_channel = 255;
uint8_t b_channel = 255;
uint8_t w_channel = 10;
uint8_t intensity = 0;
uint8_t function = 0;

// Party Mode Parameters
int partySpeed = 600;  // Speed of the comet effect (milliseconds)
int partyBrightness = 500;
int tailLength = 20;

// Manual Mode Parameters
int manualR = 0;
int manualG = 0;
int manualB = 0;
int manualW = 0;

// IPAddress local_IP(192,168,0,50);   // Your desired static IP
// IPAddress gateway(192,168,0,100);      // Your router's gateway IP
// IPAddress subnet(255,255,255,0);     // Your subnet mask

// WiFi credentials
String ssid = "ArboretumWinterLights";  // Default SSID
String password = "winterlights2024";   // Default password

// Asynchronous configuration web server
AsyncWebServer server(80);

// Artnet settings
ArtnetWifi artnet;
int defaultUniverse = 0;
int thisUniverse = 0;                 // CHANGE FOR YOUR SETUP most software this is 1, some software send out artnet first universe as 0.
const char host[] = "192.168.0.200";  // CHANGE FOR YOUR SETUP your destination

int sensor1TimeoutCount = 0;  // Counter for Sensor 1 timeouts
int sensor2TimeoutCount = 0;  // Counter for Sensor 2 timeouts

// Ranging check variables
unsigned long rangeStatus1ZeroTime = 0;
unsigned long rangeStatus2ZeroTime = 0;
bool rangeStatus1ZeroStarted = false;
bool rangeStatus2ZeroStarted = false;
bool range1Errored = false;
bool range2Errored = false;
bool gotError1Flag = false;
bool gotError2Flag = false;


// Used to keep track of min/max distances for both sensors
int s1_min_dist = 10;
int s2_min_dist = 10;
int s1_max_dist = 1000;
int s2_max_dist = 1000;

// Overloaded print function for integer types: allows hexadecimal formatting
template<typename T>
typename std::enable_if<std::is_integral<T>::value>::type print(const T &message, bool hex = false) {
  if (hex) {
    Serial.print(message, HEX);            // Print in hexadecimal for integers
    serialOutput += String(message, HEX);  // Append in hexadecimal for HTML output
  } else {
    Serial.print(message);            // Print to serial console
    serialOutput += String(message);  // Append to string for HTML output
  }
}

// Overloaded print function for non-integer types: no hexadecimal formatting
void print(const String &message, bool hex = false) {
  Serial.print(message);    // Print to serial console
  serialOutput += message;  // Append to string for HTML output
}

// Overloaded println function for integer types: allows hexadecimal formatting
template<typename T>
typename std::enable_if<std::is_integral<T>::value>::type println(const T &message, bool hex = false) {
  if (hex) {
    Serial.println(message, HEX);                 // Print in hexadecimal for integers
    serialOutput += String(message, HEX) + "\n";  // Append in hexadecimal for HTML output
  } else {
    Serial.println(message);                 // Print to serial console
    serialOutput += String(message) + "\n";  // Append to string for HTML output
  }
}

// Overloaded println function for non-integer types: no hexadecimal formatting
void println(const String &message, bool hex = false) {
  Serial.println(message);         // Print to serial console
  serialOutput += message + "\n";  // Append to string for HTML output
}

// Overloaded println for IPAddress
void println(const IPAddress &ip) {
  Serial.println(ip);                    // Print IP address to serial console
  serialOutput += ip.toString() + "\n";  // Append IP address as string for HTML output
}

void println(const char message[]) {
  println(String(message));
}


// OTA Functionality
void onOTAStart() {
  // Log when OTA has started
  println("OTA update started!");
  // <Add your own code here>
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
  }
}

void onOTAEnd(bool success) {
  // Log when OTA has finished
  if (success) {
    println("OTA update finished successfully!");
  } else {
    println("There was an error during OTA update!");
  }
  // <Add your own code here>
}

// Function to connect to WiFi
bool ConnectWifi() {

  WiFi.begin(ssid.c_str(), password.c_str());
  println("Connecting to WiFi...");

  int i = 0;
  while (WiFi.status() != WL_CONNECTED && i < 20) {
    delay(500);
    print(".");
    i++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    println("Connected to WiFi.");
    print("IP Address: ");
    println(WiFi.localIP());
    return true;
  } else {
    println("Failed to connect to WiFi.");
    return false;
  }
}

#define MAXLONG 2147483647
long NewMap(long val, long in_min, long in_max, long out_min, long out_max) {
  // TEST: in_min must be lower than in_max => flip the ranges
  // must be done before out of range test
  if (in_min > in_max) return NewMap(val, in_max, in_min, out_max, out_min);
  // TEST: if input value out of range it is mapped to border values. By choice.
  if (val <= in_min) return out_min;
  if (val >= in_max) return out_max;
  // TEST: special range cases
  if (out_min == out_max) return out_min;
  if (in_min == in_max) return out_min / 2 + out_max / 2;  // out_min or out_max? better
  // test if there will be an overflow with well known (unbalanced) formula
  if (((MAXLONG / abs(out_max - out_min)) < (val - in_min))  // multiplication overflow test
      || ((MAXLONG - in_max) < -in_min))                     // division overflow test
  {
    // if overflow would occur that means the ranges are too big
    // To solve this we divide both the input & output range in two
    // alternative is to throw an error.
    // print(" >> "); // just to see the dividing
    long mid = in_min / 2 + in_max / 2;
    long Tmid = out_min / 2 + out_max / 2;
    if (val > mid) {
      // map with upper half of original range
      return NewMap(val, mid, in_max, Tmid, out_max);
    }
    // map with lower half of original range
    return NewMap(val, in_min, mid, out_min, Tmid);
  }
  // finally we have a range that can be calculated
  // unbalanced
  // return out_min + ((out_max - out_min) * (val - in_min)) / (in_max - in_min);
  // or balanced
  // return BalancedMap(val, in_min, in_max, out_min, out_max);
  unsigned long niv = in_max - in_min + 1;               // number input valuer
  unsigned long nov = abs(out_max - out_min) + 1;        // number output values
  unsigned long pos = val - in_min + 1;                  // position of val
  unsigned long newpos = ((pos * nov) + niv - 1) / niv;  // new position with rounding
  if (out_min < out_max) return out_min + newpos - 1;
  return out_min - newpos + 1;
}

//HTML Content for config page
const char *wifi_page = R"rawliteral(
  <!DOCTYPE HTML>
  <html>
  <head>
    <title>ESP32 WiFi Manager</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body { font-family: Arial, sans-serif; }
      .button { padding: 10px 20px; margin: 5px; }

      /* Increase the height of the text box */
      #consoleOutput {
          width: 100%;
          height: 500px; /* Increased height */
          overflow-y: auto;
          font-family: monospace; /* Use a monospaced font for better readability */
      }
      #consoleInput {
          width: calc(100% - 100px);
      }
      button {
          margin-left: 10px;
      }

    </style>
  </head>
  <body>
    <h2>Universe and Rail Specific Settings</h2>
    <label>This Universe:</label>
    <input type="number" id="thisUniverse" value="0" min="0">
    <button onclick="setThisUniverse()" class="button">Set Universe</button>
    <br>

    <label>Rail Length:</label>
    <input type="number" id="railLength" value="1143" min="0">
    <button onclick="setRailLength()" class="button">Set Max Distance</button>
    <br>
    
    <label>LED Count:</label>
    <input type="number" id="ledCount" value="18" min="1">
    <button onclick="setLedCount()" class="button">Set LED Count</button>
    <br>

    <h2>Sensor Thresholding Setting</h2>

    <label>Sensor 1 Min Distance:</label>
    <input type="number" id="sensor1MinDist" value="10" min="0">
    <button onclick="setSensor1MinDist()" class="button">Set Sensor 1 Min Distance</button>
    <br>

    <label>Sensor 1 Max Distance:</label>
    <input type="number" id="sensor1MaxDist" value="1000" min="0">
    <button onclick="setSensor1MaxDist()" class="button">Set Sensor 1 Max Distance</button>
    <br>

    <label>Sensor 2 Min Distance:</label>
    <input type="number" id="sensor2MinDist" value="10" min="0">
    <button onclick="setSensor2MinDist()" class="button">Set Sensor 2 Min Distance</button>
    <br>

    <label>Sensor 2 Max Distance:</label>
    <input type="number" id="sensor2MaxDist" value="1000" min="0">
    <button onclick="setSensor2MaxDist()" class="button">Set Sensor 2 Max Distance</button>
    <br>
    <h2>ESP32 Serial Console</h2>
    <h5>Enter i or I for more info on available commands</h5>
    <textarea id="consoleOutput" readonly></textarea>
    <br>
    <input type="text" id="consoleInput" placeholder="Type command here...">
    <button onclick="sendCommand()">Send</button>
    <button onclick="clearConsole()">Clear</button>

    <h2>WiFi Configuration</h2>
    <form action="/get" method="GET">
      SSID: <input type="text" name="ssid"><br>
      Password: <input type="password" name="password"><br>
      <input type="submit" value="Save" class="button">
    </form>
    <br>
    <h2>Calibrate Sensors</h2>
    <button onclick="calibrate()" class="button">Calibrate</button>
    <br><br>
    <h2>Party Mode Settings</h2>
    <label>Speed:</label>
    <input type="number" id="speed" value="2" min="1" max="1200">
    <button onclick="setSpeed()" class="button">Set Speed</button>
    <br>
    <label>Brightness:</label>
    <input type="number" id="brightness" value="20" min="0" max="512">
    <button onclick="setBrightness()" class="button">Set Brightness</button>
    <br>
    <label>Tail Length:</label>
    <input type="number" id="tailLength" value="20" min="1" max="100">
    <button onclick="setTailLength()" class="button">Set Tail Length</button>
    <h2>Manual Mode Settings</h2>
    <label>Red:</label>
    <input type="number" id="manualR" value="0" min="0" max="255">
    <button onclick="setManualR()" class="button">Set Red</button>
    <br>
    <label>Green:</label>
    <input type="number" id="manualG" value="0" min="0" max="255">
    <button onclick="setManualG()" class="button">Set Green</button>
    <br>
    <label>Blue:</label>
    <input type="number" id="manualB" value="0" min="0" max="255">
    <button onclick="setManualB()" class="button">Set Blue</button>
    <br>
    <label>White:</label>
    <input type="number" id="manualW" value="0" min="0" max="255">
    <button onclick="setManualW()" class="button">Set White</button>

    <script>

    function sendCommand() {
      const input = document.getElementById("consoleInput").value;
      fetch(`/send?cmd=${encodeURIComponent(input)}`)
        .then(response => response.text())
        .then(data => {
          if (data) { // Check if data is not empty
              document.getElementById("consoleOutput").value += data + "\n"; // Append data with a newline
          }
          document.getElementById("consoleInput").value = '';
        });
        
      // Add input to consoleOutput
      const output = document.getElementById('consoleOutput');

      // Auto-scroll to the bottom
      output.scrollTop = output.scrollHeight;

      // Clear input field
      document.getElementById('consoleInput').value = '';

      // Add additional logic here to handle the command if needed
    }

    function clearConsole() {
      document.getElementById('consoleOutput').value = ''; // Clear the console output
    }

    setInterval(() => {
      fetch('/output')
        .then(response => response.text())
        .then(data => {
          if(data){
            document.getElementById("consoleOutput").value += data + "\\n";
          }
          // Add input to consoleOutput
          const output = document.getElementById('consoleOutput');

          // Auto-scroll to the bottom
          output.scrollTop = output.scrollHeight;
        });
    }, 1000);

    window.onload = function() {
      fetch('/getValues')
        .then(response => response.json())
        .then(data => {
          document.getElementById("thisUniverse").value = data.thisUniverse;
          document.getElementById("railLength").value = data.railLength;
          document.getElementById("ledCount").value = data.ledCount;
          document.getElementById("sensor1MinDist").value = data.sensor1MinDist;
          document.getElementById("sensor1MaxDist").value = data.sensor1MaxDist;
          document.getElementById("sensor2MinDist").value = data.sensor2MinDist;
          document.getElementById("sensor2MaxDist").value = data.sensor2MaxDist;
          document.getElementById("speed").value = data.speed;
          document.getElementById("brightness").value = data.brightness;
          document.getElementById("tailLength").value = data.tailLength;
          document.getElementById("manualR").value = data.manualR;
          document.getElementById("manualG").value = data.manualG;
          document.getElementById("manualB").value = data.manualB;
          document.getElementById("manualW").value = data.manualW;
        })
        .catch(error => console.error("Error fetching values:", error));
    }

     function setThisUniverse() {
        const universeValue = document.getElementById("thisUniverse").value;
        fetch(`/setUniverse?thisUniverse=${universeValue}`)
          .then(response => response.text())
          .then(data => alert(data));
      }

      function setSensor1MinDist() {
        const sensor1MinValue = document.getElementById("sensor1MinDist").value;
        fetch(`/setSensor1MinDist?sensor1MinDist=${sensor1MinValue}`)
          .then(response => response.text())
          .then(data => alert(data));
      }

      function setSensor1MaxDist() {
        const sensor1MaxValue = document.getElementById("sensor1MaxDist").value;
        fetch(`/setSensor1MaxDist?sensor1MaxDist=${sensor1MaxValue}`)
          .then(response => response.text())
          .then(data => alert(data));
      }

      function setSensor2MinDist() {
        const sensor2MinValue = document.getElementById("sensor2MinDist").value;
        fetch(`/setSensor2MinDist?sensor2MinDist=${sensor2MinValue}`)
          .then(response => response.text())
          .then(data => alert(data));
      }

      function setSensor2MaxDist() {
        const sensor2MaxValue = document.getElementById("sensor2MaxDist").value;
        fetch(`/setSensor2MaxDist?sensor2MaxDist=${sensor2MaxValue}`)
          .then(response => response.text())
          .then(data => alert(data));
      }

      function calibrate() {
        fetch('/calibrate')
          .then(response => response.text())
          .then(data => alert(data));
      }

      function setSpeed() {
        const speedValue = document.getElementById("speed").value;
        fetch(`/setSpeed?speed=${speedValue}`)
          .then(response => response.text())
          .then(data => alert(data));
      }

      function setRailLength() {
        const railLength = document.getElementById("railLength").value;
        fetch(`/setRailLength?railLength=${railLength}`)
          .then(response => response.text())
          .then(data => alert(data));
      }

      function setLedCount() {
        const ledCount = document.getElementById("ledCount").value;
        fetch(`/setLedCount?ledCount=${ledCount}`)
          .then(response => response.text())
          .then(data => alert(data));
      }



      function setBrightness() {
        const brightnessValue = document.getElementById("brightness").value;
        fetch(`/setBrightness?brightness=${brightnessValue}`)
          .then(response => response.text())
          .then(data => alert(data));
      }

      function setTailLength() {
        const tailLengthValue = document.getElementById("tailLength").value;
        fetch(`/setTailLength?tailLength=${tailLengthValue}`)
          .then(response => response.text())
          .then(data => alert(data));
      }

      function setManualR() {
        const manualRValue = document.getElementById("manualR").value;
        fetch(`/setManualR?manualR=${manualRValue}`)
          .then(response => response.text())
          .then(data => alert(data));
      }

      function setManualG() {
        const manualGValue = document.getElementById("manualG").value;
        fetch(`/setManualG?manualG=${manualGValue}`)
          .then(response => response.text())
          .then(data => alert(data));
      }

      function setManualB() {
        const manualBValue = document.getElementById("manualB").value;
        fetch(`/setManualB?manualB=${manualBValue}`)
          .then(response => response.text())
          .then(data => alert(data));
      }

      function setManualW() {
        const manualWValue = document.getElementById("manualW").value;
        fetch(`/setManualW?manualW=${manualWValue}`)
          .then(response => response.text())
          .then(data => alert(data));
      }
    </script>
  </body>
  </html>
)rawliteral";

void handleSend(AsyncWebServerRequest *request) {
  if (request->hasParam("cmd")) {
    String command = request->getParam("cmd")->value();
    webInputAvailable = true;
    webInput = command;
    // println(command);  // Send command to the serial console
    // serialOutput += "Sent: " + command + "\n";  // Log the command
    request->send(200, "text/plain", "Command sent: " + command);
  } else {
    request->send(400, "text/plain", "Bad Request");
  }
}

void handleOutput(AsyncWebServerRequest *request) {
  String output = serialOutput;  // Get the output string
  serialOutput = "";             // Clear after sending
  request->send(200, "text/plain", output);
}

// Callback function to handle form submission
void handleWifiConfig(AsyncWebServerRequest *request) {
  if (request->hasParam("ssid") && request->hasParam("password")) {
    ssid = request->getParam("ssid")->value();
    password = request->getParam("password")->value();
    request->send(200, "text/html", "WiFi settings updated. Reconnecting...");

    // Reconnect with new WiFi settings
    ConnectWifi();
  } else {
    request->send(200, "text/html", "Missing SSID or Password.");
  }
}

void printSerialCommandInfo() {
  println("Available Serial Commands:");
  println("q = restart ESP");
  println("p = getHTMLParameterValues");
  println("h = printHowManyHands");
  println("d = print distances");
  println("a = display ArtNet data");
  println("f = useFilter");
  println("n = new value weight for EMA filter");
  println("o = old value weight for EMA filter");
  println("c = display parsed data");
  println("t = toggle two hand effect");
}

// Calibration function
void calibrate() {
  // Example calibration process: Reset distance sensors or recalibrate parameters
  println("Calibrating sensors...");
  // Add actual calibration logic here

  // // Example: Resetting sensor data (you can modify based on your specific needs)
  // distanceSensor1.startRanging();
  // distanceSensor2.startRanging();
  // delay(100); // Simulate calibration delay

  println("Calibration complete!");
}

// Handle the calibration request from the web page
void handleCalibrateRequest(AsyncWebServerRequest *request) {
  calibrate();  // Call the calibration function
  request->send(200, "text/plain", "Calibration complete!");
}

// Callback function for processing incoming DMX messages
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t *data) {
  // set brightness of the whole strip
  if (universe == thisUniverse) {
    // print("Got data: ");
    for (int i = 0; i < length; i++) {
      if (i == 0)
        r_channel = data[i];
      if (i == 1)
        g_channel = data[i];
      if (i == 2)
        b_channel = data[i];
      if (i == 3)
        w_channel = data[i];
      if (i == 4)
        intensity = data[i];
      if (i == 5)
        function = data[i];

      if (displayArtnetData) {
        print(data[i], true);
      }
    }
    if (displayArtnetData) {
      println("");
      println("Got frame");
    }
    if (displayParsedData) {
      print("r_channel: ");
      println(r_channel);
      print("g_channel: ");
      println(g_channel);
      print("b_channel: ");
      println(b_channel);
      print("w_channel: ");
      println(w_channel);
      print("intensity: ");
      println(intensity);
      print("function: ");
      println(function);
    }
  }
}

void startupSequence(bool wifiConnected) {
  // Step 1: Flash all LEDs white at 20% brightness for 1 second
  strip.clear();
  uint8_t brightness = LED_BRIGHT * 0.2;  // 20% brightness

  // Set all LEDs to white at 20% brightness
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(255, 255, 255, brightness));  // White color with low brightness
  }
  strip.show();
  delay(1000);  // Wait for 1 second

  // Step 2: Flash green or red based on WiFi connection status
  strip.clear();  // Clear the strip

  uint32_t color;
  if (wifiConnected) {
    color = strip.Color(0, 220, 0, LED_BRIGHT * 0.2);  // Green if connected
  } else {
    color = strip.Color(220, 0, 0, LED_BRIGHT * 0.2);  // Red if not connected
  }

  // Set all LEDs to the selected color (green or red)
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
  delay(2000);  // Flash for 1 second

  // Clear LEDs after the startup sequence
  strip.clear();
  strip.show();
}

void setupSensors() {
  pinMode(XSHUT_PIN1, OUTPUT);
  digitalWrite(XSHUT_PIN1, LOW);

  pinMode(XSHUT_PIN2, OUTPUT);
  digitalWrite(XSHUT_PIN2, LOW);

  println("Setting up sensors");

  digitalWrite(XSHUT_PIN1, HIGH);
  delay(100);
  distanceSensor1.setI2CAddress(0x01);
  delay(100);

  digitalWrite(XSHUT_PIN2, HIGH);
  delay(100);
  distanceSensor2.setI2CAddress(0x02);
  delay(100);

  if (distanceSensor1.begin() != 0)  //Begin returns 0 on a good init
  {
    println("Sensor 1 failed to begin. Please check wiring. Freezing...");
    sensor1Errored = 1;
    // while (1)
    //   ;
  }
  distanceSensor1.setTimingBudgetInMs(33);
  distanceSensor1.setDistanceModeLong();
  // distanceSensor1.setDistanceThreshold(20, 1100, 3);
  println("Sensor1 online!");

  if (distanceSensor2.begin() != 0)  //Begin returns 0 on a good init
  {
    println("Sensor 2 failed to begin. Please check wiring. Freezing...");
    sensor2Errored = 1;
    // while (1)
    //   ;
  }
  println("Sensor2 online!");

  distanceSensor2.setTimingBudgetInMs(33);
  distanceSensor2.setDistanceModeLong();
  // distanceSensor2.setIntermeasurementPeriod(10);
  // distanceSensor2.setDistanceThreshold(30, 1000, 3);



  print(F("Sensor 1 ID: 0x"));
  println(distanceSensor1.getI2CAddress(), true);
  print(F("Sensor 2 ID: 0x"));
  println(distanceSensor2.getI2CAddress(), true);
}


void getHTMLParameterValues() {
  // Retrieve saved values or set to default if none found
  thisUniverse = preferences.getInt("thisUniverse", defaultUniverse);
  s1_min_dist = preferences.getInt("s1_min_dist", s1_min_dist);
  s2_min_dist = preferences.getInt("s2_min_dist", s2_min_dist);
  s1_max_dist = preferences.getInt("s1_max_dist", s1_max_dist);
  s2_max_dist = preferences.getInt("s2_max_dist", s2_max_dist);
  RAIL_LENGTH = preferences.getInt("RAIL_LENGTH", RAIL_LENGTH);
  LED_COUNT = preferences.getInt("LED_COUNT", LED_COUNT);

  strip.updateLength(LED_COUNT);

  print("Universe: ");
  println(thisUniverse);
  print("Rail Length: ");
  println(RAIL_LENGTH);
  print("LED Count: ");
  println(LED_COUNT);

  print("S1 Min Dist: ");
  println(s1_min_dist);
  print("S2 Min Dist: ");
  println(s2_min_dist);
  print("S1 Max Dist: ");
  println(s1_max_dist);
  print("S2 Max Dist: ");
  println(s2_max_dist);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  println("Setting up...");

  // Initialize sensors, LEDs, and connect to WiFi
  Wire.begin();
  setupSensors();
  strip.begin();
  strip.show();

  // Start WiFi and web server
  bool isConnected = ConnectWifi();

  // Getting stored preferences from NVM
  preferences.begin("rail", false);  // Namespace is "rail"
  delay(100);
  getHTMLParameterValues();

  // Run the startup sequence
  startupSequence(isConnected);

  // preferences.end();

  // Begin Artnet
  artnet.begin(host);
  artnet.setLength(11);
  artnet.setUniverse(thisUniverse);

  // Set up the web server
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", wifi_page);
  });

  server.on("/get", HTTP_GET, handleWifiConfig);

  // Set up endpoint to handle the calibration button press
  server.on("/calibrate", HTTP_GET, handleCalibrateRequest);

  // New endpoints for party mode settings
  server.on("/setSpeed", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("speed")) {
      partySpeed = request->getParam("speed")->value().toInt();
      request->send(200, "text/plain", "Speed updated to " + String(partySpeed));
    } else {
      request->send(400, "text/plain", "Missing speed parameter");
    }
  });

  server.on("/send", HTTP_GET, handleSend);
  server.on("/output", HTTP_GET, handleOutput);

  server.on("/setBrightness", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("brightness")) {
      partyBrightness = request->getParam("brightness")->value().toInt();
      request->send(200, "text/plain", "Brightness updated to " + String(partyBrightness));
    } else {
      request->send(400, "text/plain", "Missing brightness parameter");
    }
  });

  server.on("/setTailLength", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("tailLength")) {
      tailLength = request->getParam("tailLength")->value().toInt();
      request->send(200, "text/plain", "Tail length updated to " + String(tailLength));
    } else {
      request->send(400, "text/plain", "Missing tail length parameter");
    }
  });

  server.on("/setManualR", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("manualR")) {
      manualR = request->getParam("manualR")->value().toInt();
      request->send(200, "text/plain", "Manual Red updated to " + String(manualR));
    } else {
      request->send(400, "text/plain", "Missing manualR parameter");
    }
  });

  server.on("/setManualG", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("manualG")) {
      manualG = request->getParam("manualG")->value().toInt();
      request->send(200, "text/plain", "Manual Green updated to " + String(manualG));
    } else {
      request->send(400, "text/plain", "Missing manualG parameter");
    }
  });

  server.on("/setManualB", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("manualB")) {
      manualB = request->getParam("manualB")->value().toInt();
      request->send(200, "text/plain", "Manual Blue updated to " + String(manualB));
    } else {
      request->send(400, "text/plain", "Missing manualB parameter");
    }
  });

  server.on("/setManualW", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("manualW")) {
      manualW = request->getParam("manualW")->value().toInt();
      request->send(200, "text/plain", "Manual White updated to " + String(manualW));
    } else {
      request->send(400, "text/plain", "Missing manualW parameter");
    }
  });

  server.on("/setRailLength", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("railLength")) {
      RAIL_LENGTH = request->getParam("railLength")->value().toInt();

      // Saving to NVM
      // preferences.begin("my-app", false);
      preferences.putInt("RAIL_LENGTH", RAIL_LENGTH);
      // delay(100);
      // preferences.end();

      // strip.updateLength(LED_COUNT);   // Update the strip length
      strip.begin();  // Reinitialize the strip
      request->send(200, "text/plain", "Max distance updated to " + String(RAIL_LENGTH) + " and LED count updated to " + String(LED_COUNT));
    } else {
      request->send(400, "text/plain", "Missing railLength parameter");
    }
  });

  server.on("/setLedCount", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ledCount")) {
      LED_COUNT = request->getParam("ledCount")->value().toInt();

      // Saving to NVM
      // preferences.begin("my-app", false);
      preferences.putInt("LED_COUNT", LED_COUNT);
      // delay(100);
      // preferences.end();

      strip.updateLength(LED_COUNT);  // Update the strip length
      strip.begin();                  // Reinitialize the strip
      request->send(200, "text/plain", "LED count updated to " + String(LED_COUNT));
    } else {
      request->send(400, "text/plain", "Missing ledCount parameter");
    }
  });

  server.on("/setUniverse", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("thisUniverse")) {
      thisUniverse = request->getParam("thisUniverse")->value().toInt();
      // preferences.begin("my-app", false);
      preferences.putInt("thisUniverse", thisUniverse);
      artnet.setUniverse(thisUniverse);
      // delay(100);
      // preferences.end();
      request->send(200, "text/plain", "Universe updated to " + String(thisUniverse));
    } else {
      request->send(400, "text/plain", "Missing thisUniverse parameter");
    }
  });

  server.on("/setSensor1MinDist", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("sensor1MinDist")) {
      s1_min_dist = request->getParam("sensor1MinDist")->value().toInt();
      // preferences.begin("my-app", false);
      preferences.putInt("s1_min_dist", s1_min_dist);
      // delay(100);
      // preferences.end();
      request->send(200, "text/plain", "Sensor 1 Min Distance updated to " + String(s1_min_dist));
    } else {
      request->send(400, "text/plain", "Missing sensor1MinDist parameter");
    }
  });

  server.on("/setSensor1MaxDist", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("sensor1MaxDist")) {
      s1_max_dist = request->getParam("sensor1MaxDist")->value().toInt();
      // preferences.begin("my-app", false);
      preferences.putInt("s1_max_dist", s1_max_dist);
      // delay(100);
      // preferences.end();
      request->send(200, "text/plain", "Sensor 1 Max Distance updated to " + String(s1_max_dist));
    } else {
      request->send(400, "text/plain", "Missing sensor1MaxDist parameter");
    }
  });

  server.on("/setSensor2MinDist", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("sensor2MinDist")) {
      s2_min_dist = request->getParam("sensor2MinDist")->value().toInt();
      // preferences.begin("my-app", false);
      preferences.putInt("s2_min_dist", s2_min_dist);
      // delay(100);
      // preferences.end();
      request->send(200, "text/plain", "Sensor 2 Min Distance updated to " + String(s2_min_dist));
    } else {
      request->send(400, "text/plain", "Missing sensor2MinDist parameter");
    }
  });

  server.on("/setSensor2MaxDist", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("sensor2MaxDist")) {
      s2_max_dist = request->getParam("sensor2MaxDist")->value().toInt();
      // preferences.begin("my-app", false);
      preferences.putInt("s2_max_dist", s2_max_dist);
      // delay(100);
      // preferences.end();
      request->send(200, "text/plain", "Sensor 2 Max Distance updated to " + String(s2_max_dist));
    } else {
      request->send(400, "text/plain", "Missing sensor2MaxDist parameter");
    }
  });

  server.on("/getValues", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"thisUniverse\":" + String(preferences.getInt("thisUniverse", thisUniverse)) + ",";
    json += "\"railLength\":" + String(preferences.getInt("RAIL_LENGTH", RAIL_LENGTH)) + ",";
    json += "\"ledCount\":" + String(preferences.getInt("LED_COUNT", LED_COUNT)) + ",";
    json += "\"sensor1MinDist\":" + String(preferences.getInt("s1_min_dist", s1_min_dist)) + ",";
    json += "\"sensor1MaxDist\":" + String(preferences.getInt("s1_max_dist", s1_max_dist)) + ",";
    json += "\"sensor2MinDist\":" + String(preferences.getInt("s2_min_dist", s2_min_dist)) + ",";
    json += "\"sensor2MaxDist\":" + String(preferences.getInt("s2_max_dist", s2_max_dist));
    // json += "\"speed\":" + String(preferences.getInt("speed", 2)) + ",";
    // json += "\"brightness\":" + String(preferences.getInt("brightness", 20)) + ",";
    // json += "\"tailLength\":" + String(preferences.getInt("tailLength", 20)) + ",";
    // json += "\"manualR\":" + String(preferences.getInt("manualR", 0)) + ",";
    // json += "\"manualG\":" + String(preferences.getInt("manualG", 0)) + ",";
    // json += "\"manualB\":" + String(preferences.getInt("manualB", 0)) + ",";
    // json += "\"manualW\":" + String(preferences.getInt("manualW", 0));
    json += "}";
    request->send(200, "application/json", json);
  });


  ElegantOTA.begin(&server);  // Start ElegantOTA
  // ElegantOTA callbacks
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);

  server.begin();

  artnet.setArtDmxCallback(onDmxFrame);
  printSerialCommandInfo();
}

void partyMode() {
  static int cometPos = 0;   // Position of the comet on the LED strip
  static int direction = 1;  // Direction of comet movement (1 = forward, -1 = backward)
  static uint16_t hue = 0;   // Hue value for the comet's color
  // uint8_t tailLength = 20;              // Length of the comet's tail
  float fadeFactor = 0.8;  // Factor by which tail pixels will fade

  // Clear the strip to start fresh
  strip.clear();

  // Set the comet pixel and its tail with a rainbow color
  for (int i = 0; i < tailLength; i++) {
    int pos = cometPos - i * direction;  // Position of each tail pixel
    if (pos >= 0 && pos < LED_COUNT) {   //strip.numPixels()) {
      // Use a shifting hue for the comet's main color
      uint32_t color = strip.ColorHSV(hue + i * 1000, 255, 255);  // Shifting hue creates a rainbow effect

      // Fade the color of the tail pixels
      uint32_t fadedColor = strip.Color(
        uint8_t((color >> 16 & 0xFF) * pow(fadeFactor, i)),  // Fade red
        uint8_t((color >> 8 & 0xFF) * pow(fadeFactor, i)),   // Fade green
        uint8_t((color & 0xFF) * pow(fadeFactor, i)),        // Fade blue
        uint8_t(w_channel * pow(fadeFactor, i)));            // Fade white
      strip.setPixelColor(pos, fadedColor);
    }
  }

  // Move the comet's position along the strip
  cometPos += direction * partySpeed;
  if (cometPos >= strip.numPixels() || cometPos < 0) {
    direction *= -1;        // Reverse direction when reaching the ends
    cometPos += direction;  // Move the comet back inside the strip
  }

  // Increment hue to cycle through colors for the next comet
  hue += partyBrightness;  // Adjust this for the speed of the color change

  strip.show();
  delay(1);  // Adjust this value to change the comet's speed
}

void manualMode() {
  strip.clear();
  // Set all LEDs to white at 20% brightness
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(manualR, manualG, manualB, manualW));  // White color with low brightness
  }
  strip.show();
  delay(1000);
}


void standbyMode() {
}

void twoHandrailsTouched() {
}

void threeHandrailsTouched() {
}

void fourHandrailsTouched() {
}

void allHandrailsTouched() {
}

int howManyHands(int distance1, int distance2) {
  // If both sensors are recording max distance, there is no hand
  if ((distance1 >= s1_max_dist) || (distance2 >= s2_max_dist)) {
    return 0;
  }

  // If the sum of sensor distances is less than the rail lentgh, then two hands are touching it
  if ((distance1 + distance2) < s1_max_dist) {
    return 2;
  }

  // If they are equal, then 1 hand is touching it
  return 1;
}

// Breath mode parameters
float breathTime = 2500.0;  // Total time for one breath cycle (in milliseconds)
float breathOffset = 10;    // Time offset to keep track of breathing state


void breathMode() {
  // println("In Breathing Mode");
  // Calculate current time in the breathing cycle (normalized to 0.0 to 1.0)
  float cyclePos = fmod((millis() + breathOffset), breathTime);                      // Use fmod for float modulus
  float intensityFactor = (sin((cyclePos / breathTime) * 2 * PI - PI / 2) + 1) / 2;  // Generates a sine wave (0 to 1)

  // Set brightness based on the intensity factor
  uint8_t brightness = uint8_t(intensityFactor * LED_BRIGHT);  // Scale to max brightness

  // Apply the same color and brightness to the whole strip
  strip.setBrightness(brightness);
  uint32_t color = strip.Color(r_channel, g_channel, b_channel, w_channel);

  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, color);
  }

  strip.show();
}

// float* hsv2rgb(float h, float s, float b, float* rgb) {
//   rgb[0] = b * mix(1.0, constrain(abs(fract(h + 1.0) * 6.0 - 3.0) - 1.0, 0.0, 1.0), s);
//   rgb[1] = b * mix(1.0, constrain(abs(fract(h + 0.6666666) * 6.0 - 3.0) - 1.0, 0.0, 1.0), s);
//   rgb[2] = b * mix(1.0, constrain(abs(fract(h + 0.3333333) * 6.0 - 3.0) - 1.0, 0.0, 1.0), s);
//   return rgb;
// }

// float* rgb2hsv(float r, float g, float b, float* hsv) {
//   float s = step(b, g);
//   float px = mix(b, g, s);
//   float py = mix(g, b, s);
//   float pz = mix(-1.0, 0.0, s);
//   float pw = mix(0.6666666, -0.3333333, s);
//   s = step(px, r);
//   float qx = mix(px, r, s);
//   float qz = mix(pw, pz, s);
//   float qw = mix(r, px, s);
//   float d = qx - min(qw, py);
//   hsv[0] = abs(qz + (qw - py) / (6.0 * d + 1e-10));
//   hsv[1] = d / (qx + 1e-10);
//   hsv[2] = qx;
//   return hsv;
// }

void normalMode(int distance1, int distance2, int howManyHands) {
  strip.setBrightness(255);
  uint32_t color = strip.Color(r_channel, g_channel, b_channel, w_channel);

  // Convert RGB to HSV
  float r = r_channel / 255.0f;
  float g = g_channel / 255.0f;
  float b = b_channel / 255.0f;
  float h, s, v;
  rgb_to_hsv(r, g, b, &h, &s, &v);

  // Calculate offset hue (opposite color)
  float offset_h = fmod(h + 128.0f, 255.0f);

  // Convert offset HSV back to RGB
  float offset_r, offset_g, offset_b;
  hsv_to_rgb(offset_h, s, v, &offset_r, &offset_g, &offset_b);

  // Convert back to 0-255 range
  uint8_t offset_r_channel = static_cast<uint8_t>(offset_r * 255);
  uint8_t offset_g_channel = static_cast<uint8_t>(offset_g * 255);
  uint8_t offset_b_channel = static_cast<uint8_t>(offset_b * 255);



  // uint8_t offset_r = min(max(r_channel - 100, 0), min(r_channel + 100, 255));
  // uint8_t offset_g = min(max(g_channel - 100, 0), min(g_channel + 100, 255));
  // uint8_t offset_b = min(max(b_channel - 100, 0), min(b_channel + 100, 255));
  uint32_t offset_color = strip.Color(offset_r_channel, offset_g_channel, offset_b_channel, w_channel);

  float dist2pix = RAIL_LENGTH / float(LED_COUNT);
  int numPix1 = float(distance1) / dist2pix;
  int numPix2 = float(LED_COUNT - 1) - (float(distance2) / dist2pix);

  int buffer = 1;

  // Setting sensor 1
  strip.setPixelColor(numPix1, color);

  // Setting sensor 2
  if (twoHandEffectActive) {
    if (howManyHands == 2) {
      strip.setPixelColor(numPix2, offset_color);
    } else {
      strip.setPixelColor(numPix2, color);
    }
  } else {
    strip.setPixelColor(numPix2, color);
  }


  int minVal = min(numPix1, numPix2);
  int maxVal = max(numPix1, numPix2);



  for (int p = 0; p < int(minVal - 1); p++) {
    strip.setPixelColor(p, strip.Color(0, 0, 0));
  }

  for (int p = minVal + 1; p < maxVal; p++) {
    strip.setPixelColor(p, strip.Color(0, 0, 0));
  }

  for (int p = maxVal + 1; p < LED_COUNT; p++) {
    strip.setPixelColor(p, strip.Color(0, 0, 0));
  }


  strip.show();
}

void handleSerialCommands() {
  char input;

  if (Serial.available() || webInputAvailable) {
    if (webInputAvailable) {
      input = webInput[0];
    } else {
      input = Serial.read();
    }
    if (input == 'i' || input == 'I') {
      printSerialCommandInfo();
    }
    if (input == 'q' || input == 'Q') {
      ESP.restart();
    }
    if (input == 'p' || input == 'P') {
      getHTMLParameterValues();
    }

    if (input == 'h' || input == 'H') {
      printHowManyHands = !printHowManyHands;
    }

    if (input == 'd' || input == 'D') {
      printDistancesEnabled = !printDistancesEnabled;
    }

    if (input == 'a' || input == 'A') {
      displayArtnetData = !displayArtnetData;
    }

    if (input == 'f' || input == 'F') {
      useFilter = !useFilter;
    }
    if (input == 'n' || input == 'N') {
      new_weight = Serial.parseFloat();
      print("Received float value: ");
      println(new_weight);
    }
    if (input == 'o' || input == 'O') {
      old_weight = Serial.parseFloat();
      print("Received float value: ");
      println(old_weight);
    }

    if (input == 'c' || input == 'C') {
      displayParsedData = !displayParsedData;
    }

    if (input == 't' || input == 'T') {
      twoHandEffectActive = !twoHandEffectActive;
      print("Toggled twoHandEffectActive: ");
      println(twoHandEffectActive);
    }

    webInputAvailable = false;
  }
}



void loop() {

  ElegantOTA.loop();

  int maxTimeouts = 80;  // Maximum number of allowed timeouts
  int startTime = millis();
  int distance1 = 0;
  int distance2 = 0;
  int filtered_distance1 = 0;
  int filtered_distance2 = 0;
  int rangeStatus1 = 0;
  int rangeStatus2 = 0;


  handleSerialCommands();

  // Check Sensor 1
  if (!sensor1Errored && sensor1TimeoutCount < maxTimeouts) {
    distanceSensor1.startRanging();  // Initiate measurement
    unsigned long startTime1 = millis();
    const unsigned long timeout1 = 50;  // Timeout of 100 ms

    while (!distanceSensor1.checkForDataReady()) {
      // delay(1);
      if (millis() - startTime1 > timeout1) {
        println("Sensor 1 timeout!");
        sensor1TimeoutCount++;
        break;
      }
    }

    rangeStatus1 = distanceSensor1.getRangeStatus();

    if (rangeStatus1 != 0) {
      if (!rangeStatus1ZeroStarted) {
        rangeStatus1ZeroTime = millis();
        rangeStatus1ZeroStarted = true;
      }
      // Check if rangeStatus has stayed non-zero for 1 second
      if (rangeStatus1ZeroStarted && (millis() - rangeStatus1ZeroTime >= 100)) {
        // println("RangeStatus 1 has been non-zero for 1 second!");
        range1Errored = true;
        gotError1Flag = true;
      }
      distance1 = 0;
    } else {
      if (range1Errored) {
        if (gotError1Flag) {
          rangeStatus1ZeroTime = millis();
          gotError1Flag = false;
        }
        // Check if rangeStatus has stayed zero for 1 second after error
        if (millis() - rangeStatus1ZeroTime >= 100) {
          // println("RangeStatus 1 has been zero for 1 second after error!");
          range1Errored = false;  // Clear error
        }
        distance1 = 0;
      } else {
        rangeStatus1ZeroStarted = false;  // Reset timer if rangeStatus is not zero
        if (distanceSensor1.checkForDataReady()) {
          distance1 = distanceSensor1.getDistance();
          sensor1TimeoutCount = 0;  // Reset timeout counter
        }
      }
    }

    distanceSensor1.clearInterrupt();
    distanceSensor1.stopRanging();

  } else if (sensor1TimeoutCount >= maxTimeouts) {
    println("Sensor 1 has reached the maximum number of timeouts.");
    sensor1Errored = true;
  }

  // Check Sensor 2 (similar logic)
  if (!sensor2Errored && sensor2TimeoutCount < maxTimeouts) {
    distanceSensor2.startRanging();
    unsigned long startTime2 = millis();
    const unsigned long timeout2 = 50;

    while (!distanceSensor2.checkForDataReady()) {
      // delay(1);
      if (millis() - startTime2 > timeout2) {
        println("Sensor 2 timeout!");
        sensor2TimeoutCount++;
        break;
      }
    }

    rangeStatus2 = distanceSensor2.getRangeStatus();

    if (rangeStatus2 != 0) {
      if (!rangeStatus2ZeroStarted) {
        rangeStatus2ZeroTime = millis();
        rangeStatus2ZeroStarted = true;
      }
      // Check if rangeStatus has stayed non-zero for 1 second
      if (rangeStatus2ZeroStarted && (millis() - rangeStatus2ZeroTime >= 100)) {
        // println("RangeStatus 2 has been non-zero for 1 second!");
        range2Errored = true;
        gotError2Flag = true;
      }
      distance2 = 0;
    } else {
      if (range2Errored) {
        if (gotError2Flag) {
          rangeStatus2ZeroTime = millis();
          gotError2Flag = false;
        }
        // Check if rangeStatus has stayed zero for 1 second after error
        if (millis() - rangeStatus2ZeroTime >= 100) {
          // println("RangeStatus 2 has been zero for 1 second after error!");
          range2Errored = false;  // Clear error
        }
        distance2 = 0;
      } else {
        rangeStatus2ZeroStarted = false;  // Reset timer if rangeStatus is not zero
        if (distanceSensor2.checkForDataReady()) {
          distance2 = distanceSensor2.getDistance();
          sensor2TimeoutCount = 0;
        }
      }
    }

    distanceSensor2.clearInterrupt();
    distanceSensor2.stopRanging();
  } else if (sensor2TimeoutCount >= maxTimeouts) {
    println("Sensor 2 has reached the maximum number of timeouts.");
    sensor2Errored = true;
  }

  filtered_distance1 = distance1 * new_weight + previous_distance1 * old_weight;
  filtered_distance2 = distance2 * new_weight + previous_distance2 * old_weight;

  if (useFilter) {
    distance1 = filtered_distance1;
    distance2 = filtered_distance2;
  }

  previous_distance1 = filtered_distance1;
  previous_distance2 = filtered_distance2;

  int howManyHandsTouching = howManyHands(distance1, distance2);
  artnet.read();

  int dmx_value1 = NewMap(distance1, 0, RAIL_LENGTH, 0, 255);
  int dmx_value2 = NewMap(distance2, 0, RAIL_LENGTH, 0, 255);

  if (printDistancesEnabled) {
    print("Distance1(mm): ");
    print(filtered_distance1);
    print(" ");
    print(rangeStatus1);
    println("");
    print("Distance2(mm): ");
    print(filtered_distance2);
    print(" ");
    print(rangeStatus2);
    println("");
  }

  if (printHowManyHands) {
    print("How Many Hands: ");
    println(howManyHandsTouching);
  }
  // Sends the first distances in two bytes, high byte first
  artnet.setByte(6, dmx_value1);
  artnet.setByte(7, dmx_value2);
  artnet.setByte(8, howManyHandsTouching);
  artnet.setByte(9, sensor1Errored);
  artnet.setByte(10, sensor2Errored);
  // artnet.setByte(1, 0);
  artnet.write();
  delay(10);


  // If both sensors are errored, default to partyMode
  if (sensor1Errored && sensor2Errored) {
    breathMode();
    return;
  }

  // If both range1Errored and range2Errored have persisted, trigger breathMode
  if (range1Errored && range2Errored) {
    breathMode();
    return;
  }

  if (((distance1 > s1_max_dist) || (distance1 < s1_min_dist) || rangeStatus1 != 0) && ((distance2 > s2_max_dist) || (distance2 < s2_min_dist) || rangeStatus2 != 0)) {
    breathMode();
    return;
  }

  // print("EndTime: "); println(millis() - startTime);


  if (function == 0x00)
    normalMode(distance1, distance2, howManyHandsTouching);
  else if (function >= 0x19)  // 10%
    partyMode();
  else if (function >= 0x33)  // 20%
    standbyMode();
  else if (function >= 0x4C)  // 30%
    twoHandrailsTouched();
  else if (function >= 0x66)  // 40%
    threeHandrailsTouched();
  else if (function >= 0x7F)  // 50%
    fourHandrailsTouched();
  else if (function >= 0x99)  // 60%
    allHandrailsTouched();
  else if (function >= 0xB2)  // 70%
    breathMode();
  else if (function >= 0xCC)  // 80%
    manualMode();
}

void rgb_to_hsv(float r, float g, float b, float *h, float *s, float *v) {
  float min = fmin(fmin(r, g), b);
  float max = fmax(fmax(r, g), b);
  *v = max;

  float delta = max - min;
  if (max != 0)
    *s = delta / max;
  else {
    *s = 0;
    *h = -1;
    return;
  }

  if (r == max)
    *h = (g - b) / delta;
  else if (g == max)
    *h = 2 + (b - r) / delta;
  else
    *h = 4 + (r - g) / delta;

  *h *= 60;
  if (*h < 0)
    *h += 360;

  // Convert hue to 0-255 range
  *h = (*h / 360.0f) * 255.0f;
}

void hsv_to_rgb(float h, float s, float v, float *r, float *g, float *b) {
  // Convert hue from 0-255 range to 0-360 range
  h = (h / 255.0f) * 360.0f;

  int i;
  float f, p, q, t;

  if (s == 0) {
    *r = *g = *b = v;
    return;
  }

  h /= 60;
  i = floor(h);
  f = h - i;
  p = v * (1 - s);
  q = v * (1 - s * f);
  t = v * (1 - s * (1 - f));

  switch (i) {
    case 0:
      *r = v;
      *g = t;
      *b = p;
      break;
    case 1:
      *r = q;
      *g = v;
      *b = p;
      break;
    case 2:
      *r = p;
      *g = v;
      *b = t;
      break;
    case 3:
      *r = p;
      *g = q;
      *b = v;
      break;
    case 4:
      *r = t;
      *g = p;
      *b = v;
      break;
    default:
      *r = v;
      *g = p;
      *b = q;
      break;
  }
}
