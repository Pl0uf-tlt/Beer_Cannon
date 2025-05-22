/*
 // MATRIX DECLARATION:
// Parameter 1 = width of NeoPixel matrix
// Parameter 2 = height of matrix
// Parameter 3 = pin number (most are valid)
// Parameter 4 = matrix layout flags, add together as needed:
//   NEO_MATRIX_TOP, NEO_MATRIX_BOTTOM, NEO_MATRIX_LEFT, NEO_MATRIX_RIGHT:
//     Position of the FIRST LED in the matrix; pick two, e.g.
//     NEO_MATRIX_TOP + NEO_MATRIX_LEFT for the top-left corner.
//   NEO_MATRIX_ROWS, NEO_MATRIX_COLUMNS: LEDs are arranged in horizontal
//     rows or in vertical columns, respectively; pick one or the other.
//   NEO_MATRIX_PROGRESSIVE, NEO_MATRIX_ZIGZAG: all rows/columns proceed
//     in the same order, or alternate lines reverse direction; pick one.
//   See example below for these values in action.
// Parameter 5 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_GRBW    Pixels are wired for GRBW bitstream (RGB+W NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
*/

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

// WiFi credentials
#define WIFI_SSID "Mango_WiFi"
#define WIFI_PASS "iotempire"

// MQTT server address
const char *mqtt_server = "192.168.14.1";

// MQTT client and variables
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (50)
char msg[MSG_BUFFER_SIZE];
char receivedMsg[MSG_BUFFER_SIZE]; // Buffer to store the received message
int value = 0;

#ifndef PSTR
#define PSTR // Make Arduino Due happy
#endif

// NeoPixel matrix configuration
#define PIN 26
#define CHAR_WIDTH 5 // Define the width of each character in pixels

unsigned long lastSecond = 0;
int simulatedHour = 12;
int simulatedMinute = 34;
bool colonVisible = true;

Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(
    64, 8, PIN, // Dimensions physiques : 64 pixels de large, 8 pixels de haut
    NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
    NEO_GRB + NEO_KHZ800);

// Function to remap 32x16 logical coordinates to 64x8 physical coordinates
void drawPixelRemapped(int x, int y, uint16_t color)
{
  int physicalX, physicalY;

  if (y < 8)
  {
    // Premier panneau (haut), normal
    physicalX = x;
    physicalY = y;
  }
  else
  {
    // Deuxième panneau (bas), inversé
    physicalX = 31 - x + 32; // miroir horizontal sur 32 colonnes, décalé de 32
    physicalY = 7 - (y - 8); // miroir vertical sur 8 lignes
  }

  matrix.drawPixel(physicalX, physicalY, color);
}

// Default colors
uint16_t currentColor = matrix.Color(255, 255, 255); // Default color (white)
uint16_t messageColor = matrix.Color(255, 255, 255); // Default message color (white)

// Variables for display and animation
int x = matrix.width();
int pass = 0;
bool displayOff = false; // Indicates if the display is turned off

// Function to configure OTA updates
void setup_ota()
{
  ArduinoOTA.onStart([]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("Start updating " + type); });

  ArduinoOTA.onEnd([]()
                   { Serial.println("\nEnd"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });

  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    } });

  ArduinoOTA.begin();
  Serial.println("OTA ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// Function to connect to WiFi
void setup_wifi()
{
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print("trying to connect to WiFi");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

// Function to parse RGB values from a string in the format "rgb(108, 147, 134)"
uint16_t parseColor(const char *colorMsg)
{
  int r, g, b;
  sscanf(colorMsg, "rgb(%d, %d, %d)", &r, &g, &b);
  return matrix.Color(r, g, b);
}

// Function to generate a color wheel (used for rainbow effects)
uint32_t Wheel(byte WheelPos)
{
  if (WheelPos < 85)
  {
    return matrix.Color(255 - WheelPos * 3, WheelPos * 3, 0); // Red to green
  }
  else if (WheelPos < 170)
  {
    WheelPos -= 85;
    return matrix.Color(0, 255 - WheelPos * 3, WheelPos * 3); // Green to blue
  }
  else
  {
    WheelPos -= 170;
    return matrix.Color(WheelPos * 3, 0, 255 - WheelPos * 3); // Blue to red
  }
}

// Function to display a rainbow snake animation
void rainbowSnake(uint8_t wait)
{
  int width = 32;
  int height = 16;
  int snakeLength = 32; // Length of the snake
  int totalPixels = width * height;

  // Store the positions of the snake
  int snake[snakeLength];
  for (int i = 0; i < snakeLength; i++)
  {
    snake[i] = -1; // Initialize outside the matrix
  }

  for (int head = 0; head < totalPixels + snakeLength; head++)
  {
    matrix.fillScreen(0); // Clear the screen

    // Shift the snake's body
    for (int i = snakeLength - 1; i > 0; i--)
    {
      snake[i] = snake[i - 1];
    }
    snake[0] = head;

    // Draw the snake
    for (int i = 0; i < snakeLength; i++)
    {
      int pos = snake[i];
      if (pos >= 0 && pos < totalPixels)
      {
        int x = pos / height;
        int y = (x % 2 == 0) ? pos % height : height - 1 - (pos % height);

        // Calculate a rainbow color based on the snake's position
        uint32_t color = Wheel((i * 256 / snakeLength) & 255);
        drawPixelRemapped(x, y, color); // Draw the pixel with the rainbow color
      }
    }

    matrix.show(); // Update the display
    delay(wait);   // Wait before the next frame
  }
}

// MQTT callback function to handle incoming messages
void callback(char *topic, byte *payload, unsigned int length)
{
  char tempMsg[MSG_BUFFER_SIZE];
  memset(tempMsg, 0, MSG_BUFFER_SIZE); // Clear the buffer
  for (unsigned int i = 0; i < length; i++)
  {
    tempMsg[i] = (char)payload[i];
  }
  tempMsg[length] = '\0'; // Null-terminate the string

  // Check the topic for color changes
  if (strcmp(topic, "panel_color") == 0)
  {
    if (strcmp(tempMsg, "red") == 0)
    {
      messageColor = matrix.Color(255, 0, 0); // Red
    }
    else if (strcmp(tempMsg, "white") == 0)
    {
      messageColor = matrix.Color(255, 255, 255); // White
    }
    else if (strcmp(tempMsg, "blue") == 0)
    {
      messageColor = matrix.Color(0, 0, 255); // Blue
    }
  }
  // Check the topic for messages
  else if (strcmp(topic, "panel_msg") == 0)
  {
    if (strcmp(tempMsg, "off") == 0) // If the message is "off"
    {
      displayOff = true;    // Turn off the display
      matrix.fillScreen(0); // Clear the LEDs
      matrix.show();        // Update the display
    }
    else if (strcmp(tempMsg, "rb") == 0) // If the message is "rb"
    {
      rainbowSnake(20); // Trigger the rainbow snake animation
    }
    else
    {
      displayOff = false;                             // Turn on the display
      strncpy(receivedMsg, tempMsg, MSG_BUFFER_SIZE); // Store the received message
    }
  }
  // Check the topic for brightness changes
  else if (strcmp(topic, "panel_ll") == 0)
  {
    int brightness = atoi(tempMsg);           // Convert the message to an integer
    if (brightness >= 0 && brightness <= 255) // Ensure the value is between 0 and 255
    {
      matrix.setBrightness(brightness); // Apply the brightness
      matrix.show();                    // Update the display
      Serial.print("Brightness set to: ");
      Serial.println(brightness);
    }
    else
    {
      Serial.println("Invalid brightness value. Must be between 0 and 255.");
    }
  }
}

// Function to reconnect to the MQTT server
void reconnect()
{
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str()))
    {
      Serial.println("connected");
      // Subscribe to topics once connected
      client.subscribe("panel_msg");
      client.subscribe("panel_color");
      client.subscribe("panel_ll"); // Subscribe to the brightness topic
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// Setup function
void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  setup_wifi(); // Connect to WiFi
  setup_ota();  // Configure OTA updates
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  client.subscribe("panel_msg");   // Subscribe to the message topic
  client.subscribe("panel_color"); // Subscribe to the color topic
  client.subscribe("panel_ll");    // Subscribe to the brightness topic
  matrix.begin();
  matrix.setTextWrap(false);
  matrix.setBrightness(3);           // Default brightness (10 out of 255)
  matrix.setTextColor(currentColor); // Set the default text color
}

// Main loop
void loop()
{
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  ArduinoOTA.handle(); // Handle OTA updates

  unsigned long now = millis();
  if (now - lastMsg > 2000)
  {
    lastMsg = now;
    client.publish("panel_pub", receivedMsg); // Publish the last received message
    Serial.print("Published message: ");
    Serial.println(receivedMsg);
  }

  if (displayOff)
  {
    return; // Do nothing if the display is off
  }

  // Clear the screen
  matrix.fillScreen(0);

  // Calculate the width of the text in pixels
  int textWidth = strlen(receivedMsg) * CHAR_WIDTH;

  // Calculate the X position to align the text just before "C"
  int firstThird = matrix.width() / 4 + 2;
  int textX = firstThird - textWidth - 2; // Adjust position for spacing

  // Display the main text aligned to the left of "C"
  matrix.setCursor(textX, 0);
  matrix.setTextColor(messageColor);
  matrix.print(receivedMsg);

  // Display "C" in white
  matrix.setCursor(firstThird, 0);                  // Position for "C"
  matrix.setTextColor(matrix.Color(255, 255, 255)); // White color
  matrix.print("C");

  // Update the display
  matrix.show();
  delay(100);
}
