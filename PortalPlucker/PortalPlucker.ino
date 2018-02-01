#include "ESP8266WiFi.h"
#include <SPI.h>
#include <SD.h>

#define DEBUG_ESP_HTTP_CLIENT
#define DEBUG_ESP_PORT  Serial

#include <ESP8266HTTPClient.h>

// On the ESP8266, connect the SD card this way...
// SD card -> ESP pin
// DI(MOSI) -> IO13
// DO(MISO) -> IO12
// SCLK -> IO14
// CS -> IO2 (this just needs to be a free IO pin)

SDClass gCard;
const int kSDChipSelectPin = 2;

// On the Sparkfun ESP8266 Thing there's an LED on pin 5
const int kStatusLEDPin = 5;

// Where the portal results will be stored
char* kResultsDir = "/portals";

// Simple error reporting by counts of short flashes
// with a bigger pause between them
void statusFlash(int aCount)
{
  for (int i = 0; i < aCount; i++)
  {
    digitalWrite(kStatusLEDPin, HIGH);
    delay(300);
    digitalWrite(kStatusLEDPin, LOW);
    delay(300);
  }
  delay(1000);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Let's go!");

  pinMode(kStatusLEDPin, OUTPUT);

  // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.print("\nSetting up SD card...");
  if (!gCard.begin(kSDChipSelectPin, SPI_HALF_SPEED))
  {
    Serial.println("Failed");
    // Failed.  Not worth going any further
    while (1)
    {
      statusFlash(1);
    }
  }

#if 0 // Fails to create the folder, let's not bother with it...
  // Check for the results directory
  if (!gCard.exists(kResultsDir)) {
    Serial.print(kResultsDir);
    Serial.println(" not found.  Creating.");
    if (!gCard.mkdir(kResultsDir))
    {
      Serial.println("Failed.");
//      while (1)
      {
        statusFlash(2);
      }
    }
  }
#endif

  Serial.println("Setup done");
}

bool recordedYet(const char* anSSID)
{
  return gCard.exists(anSSID);
}

const size_t kHeadersOfInterestCount = 4;
const char* kHeadersOfInterest[kHeadersOfInterestCount] = {
  "Location",
  "P3P",
  "Cache-Control",
  "Server"
};

void loop() {
  Serial.println("scan start");

  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0)
    Serial.println("no networks found");
  else
  {
    int chosenNetwork = -1;
    int strongestSignal = -1000;
    uint32_t SSIDcrc32; // So we can fit arbitrary SSID names
                        // into 8.3 filenames
    char filename[14]; // space for 8.3 plus \0
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      SSIDcrc32 = crc32(WiFi.SSID(i).c_str(), WiFi.SSID(i).length());
      sprintf(filename, "/%02X.nfo", SSIDcrc32);
Serial.print(" [crc32 0x");
Serial.print(filename);
Serial.print("]");
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*");
      if ((WiFi.encryptionType(i) == ENC_TYPE_NONE) && !recordedYet(filename))
      {
        // This is a candidate to connect to...
        if (WiFi.RSSI(i) > strongestSignal)
        {
          // It's the strongest signal so far...
          strongestSignal = WiFi.RSSI(i);
          chosenNetwork = i;
        }
      }
      delay(10);
    }
    if (chosenNetwork > -1)
    {
      Serial.print("We should check out ");
      Serial.print(WiFi.SSID(chosenNetwork));
      WiFi.begin(WiFi.SSID(chosenNetwork).c_str());

      digitalWrite(kStatusLEDPin, HIGH);
      unsigned long start = millis();
      while ((millis()-start < 5000UL) && (WiFi.status() != WL_CONNECTED)) 
      {
        delay(500);
        Serial.print(".");
      }
      if (millis()-start < 5000UL)
      {
        // We've connected
        Serial.println("Connected to WiFi");
        // Let's start logging details
        SSIDcrc32 = crc32(WiFi.SSID().c_str(), WiFi.SSID().length());
        sprintf(filename, "/%02X.log", SSIDcrc32);
        // Open the file
        File logFile = gCard.open(filename, FILE_WRITE);

        if (logFile)
        {
          // Start by trying to get to the Internet proper
          String url = "http://detectportal.firefox.com/success.txt";
          HTTPClient http;
  
          int httpCode = 1;
  
          // Loop while we're getting continues or redirects from
          // the server
          // FIXME Should probably limit how many redirects we do too
          while ( ((httpCode > 0) && (httpCode < 200)) 
            || ((httpCode > 299) && (httpCode < 400)) )
          {
            Serial.print("Connecting to ");
            Serial.println(url);
            logFile.print("Connecting to ");
            logFile.println(url);
            if (url.indexOf("https") == -1)
            {
              // Plain HTTP
              http.begin(url);
            }
            else
            {
// Dummy https fingerprint, coupled 
              http.begin(url, "*");
            }
            http.collectHeaders(kHeadersOfInterest, kHeadersOfInterestCount);
            // start connection and send HTTP header
            httpCode = http.GET();
            Serial.printf("[HTTP] GET... code: %d\n", httpCode);
            logFile.printf("[HTTP] GET... code: %d\n", httpCode);
            if(httpCode) {
              // HTTP header has been send and Server response header has been handled
              // Dump the headers
              Serial.print("Headers:");
              Serial.println(http.headers());
              logFile.print("Headers:");
              logFile.println(http.headers());
              for (int i =0; i < http.headers(); i++)
              {
                Serial.println(http.header(i));
                logFile.println(http.header(i));
              }
              while (http.connected())
              {
                WiFiClient* client = http.getStreamPtr();
                if (client->available())
                {
                  char c = client->read();
                  Serial.print(c);
                  logFile.print(c);
                  delay(1);
                }
              }
              Serial.println();
              logFile.println();
  
              if ((httpCode >= 300) && (httpCode < 400))
              {
                // It's a redirect
                // Location header will be something like...
                // https://portal.moovmanage.com/setup/28002/2?res=notyet&uamip=10.0.0.1&uamport=3990&challenge=0a19759645a230339353930b2856da3d&called=04-F0-21-11-0A-FB&mac=5C-CF-7F-8A-F8-D8&ip=10.0.0.103&nasid=28002&sessionid=5a7109c300000004&ssl=https%3a%2f%2f1.0.0.1%3a4990%2f&userurl=http%3a%2f%2fdetectportal.firefox.com%2fsuccess.txt&md=782E10B091AAEE6A19AB28ADB4F899F6
                // ...or...
                // https://www.btopenzone.com:8443/home?CPURL=http%3A%2F%2Fhome.bt.com
                // ...or...
                // https://virgin.passengerwifi.com/index.php?OriginalURL=http%3A%2F%2Fdetectportal.firefox.com%2Fsuccess.txt
                // ...or...
                // https://www.bitbuzz.net/cp/?redirect=http%253A%252F%252Fdetectportal%252Efirefox%252Ecom%252Fsuccess%252Etxt&timeout=604800&gateway=62%2e253%2e196%2e26%3a5280&mac=5c%3acf%3a7f%3a8a%3af8%3ad8&token=%241%2413516515%24d1TIySGj4bsQDpSQ8saao%2f
                Serial.print("Redirecting to ");
                Serial.println(http.header("Location"));
                logFile.print("Redirecting to ");
                logFile.println(http.header("Location"));
  
                url = http.header("Location");
              }
            }
          }
          logFile.close();
        }
        else
        {
          Serial.print("Failed to open file ");
          Serial.println(filename);
        }

        // Record some stats too
        sprintf(filename, "/%02X.nfo", SSIDcrc32);
        File infoFile = gCard.open(filename, FILE_WRITE);
        if (infoFile)
        {
          infoFile.print("Our IP: ");
          infoFile.println(WiFi.localIP());
          infoFile.print("Gateway IP: ");
          infoFile.println(WiFi.gatewayIP());
          infoFile.print("DNS IP: ");
          infoFile.println(WiFi.dnsIP());
          infoFile.print("BSSID: ");
          infoFile.println(WiFi.BSSIDstr());
          infoFile.print("SSID: ");
          infoFile.println(WiFi.SSID());
          infoFile.print("RSSI: ");
          infoFile.println(WiFi.RSSI());
          infoFile.close();
        }
        else
        {
          Serial.print("Failed to open file ");
          Serial.println(filename);
        }
        Serial.print("Our IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("Gateway IP: ");
        Serial.println(WiFi.gatewayIP());
        Serial.print("DNS IP: ");
        Serial.println(WiFi.dnsIP());
        Serial.print("BSSID: ");
        Serial.println(WiFi.BSSIDstr());
        Serial.print("SSID: ");
        Serial.println(WiFi.SSID());
        Serial.print("RSSI: ");
        Serial.println(WiFi.RSSI());
      }
      else
      {
        Serial.println("Timed out");
      }
      WiFi.disconnect();
      digitalWrite(kStatusLEDPin, LOW);
    }
    else
    {
      Serial.println("No useful networks to look into");
    }
  }
  Serial.println("");

  // Wait a bit before scanning again
  //delay(5000);
// FIXME Only try a number of loops, then drop to deep sleep with
   Serial.println("Sleeping...");
   ESP.deepSleep(2*60*1000*1000UL); // sleep for 2 mins
   // NB: Need to hook GPIO16(DTR on the Sparkfun Thing) to 
   // RST too for this to work
}
