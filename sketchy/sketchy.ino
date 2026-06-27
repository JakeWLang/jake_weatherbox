#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <set>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);

// const char* ssid = "RadioShack's Worst Nightmare";
const char* ssid = "wifi";
const char* password = "pwd";
const int yearEndOffset = 6 * 3600;

const char* timeApi = "https://time.now/developer/api/timezone/America/Chicago";  // "http://worldtimeapi.org/api/timezone/America/Chicago";
const char* sunsetApi = "https://api.sunrise-sunset.org/json?TZ"; // Need to add the date if we want tomorrow or something &date=2025-11-22
const char* seasonsApi = "https://aa.usno.navy.mil/api/seasons";
int reloads = 0;
int reloadTolerance = 3;


# define BUTTON_A 15
# define BUTTON_B 32
# define BUTTON_C 14

void setup() {
  Serial.begin(115200);
  Serial.println("128x64 OLED FeatherWing test");
  delay(250);
  display.begin(0x3C, true);

  display.display();
  delay(1000); // why the delay?

  display.clearDisplay();
  display.display();

  display.setRotation(1);

  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);

  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0,0);

  WiFi.begin(ssid, password);
  display.print("Connecting to SSID: '" + String(ssid) + "'");
  display.display();

  // Remember you can't escape a while loop until the condition is met
  while (WiFi.status() != WL_CONNECTED) {
    display.print(".");
    display.display();
    delay(500);
  }

  display.println("\nConnected to the WiFi network");
  display.print("IP Address: ");
  display.println(WiFi.localIP());
  display.display();
  delay(3500);
  display.clearDisplay();
  display.setCursor(0,0);

}

String splitString(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length();

  // Iterate over each character until you find the
  // target character (separator)
  for (int i = 0; i <= maxIndex && found <= index; i++) {
    // If the character at index i is the separator or the end of string
    if (data.charAt(i) == separator || i == maxIndex) {
      // Increment found
      found++;
      // Increment strIndex[0]
      strIndex[0] = strIndex[1] + 1;
      // Increment strIndex[1] by either i + 1 if end of string OR i?
      strIndex[1] = (i == maxIndex) ? i + 1 : i; // ternary operator
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}


int isLeapYear(int yr) {
  if (yr % 400 == 0) {
    return 1;
  }
  if (yr % 100 == 0) {
    return 0;
  }
  if (yr % 4 == 0) {
    return 1;
  }
  return 0;
}

int getSecondsInYear(int yr) {
  // Fuck leap years, all my homies hate leap years
  int isLeap = isLeapYear(yr);
  if (isLeap == 1) {
    return 86400 * 366;
  }
  return 86400 * 365;
}


auto getCurrentYear(int unixtime, int offsetTime=0, int lookaheadStop=100) {
  // Look for the range unixtime exists between to derive year of unixtime
  // offsetTime is offset applied in seconds at beginning/end of year (only one time ever)
  // E.g. America/Chicago is -6 at BOY and EOY
  struct result {int currentYear; int yearStart;};
  int startEpoch = 0 + offsetTime;
  int endEpoch = 0 + offsetTime;
  for (int iterYr = 1970; iterYr < iterYr + lookaheadStop; iterYr++) {
    int secInYr = getSecondsInYear(iterYr);
    endEpoch += secInYr;
    if (unixtime >= startEpoch && unixtime < endEpoch) {
      return result {iterYr, startEpoch};
    }
    startEpoch = endEpoch;
  }
}


int getBeginningOfYear(int startYr, int offsetTime=0) {
  // If you know the year
  int epoch = 0;
  for (int iterYr = 1970; iterYr < startYr; iterYr++) {
    int secInYr = getSecondsInYear(iterYr);
    epoch += secInYr;
  }
  return epoch + offsetTime;
}


int getMonthSeconds(int targetMo, int leapYear) {
  int curMoSeconds = 0;
  for (int iterMo = 1; iterMo <= 12; iterMo ++) {
    int days = 0;

    // February
    if (iterMo == 2) {
      if (leapYear == 1) {
        days = 29;
      } else {
        days = 28;
      }
    }

    // Those with 31
    if (std::set{1, 3, 5, 7, 8, 10, 12}.contains(iterMo)) {
      days = 31;
    }

    // Those with 30
    if (std::set{4, 6, 9, 11}.contains(iterMo)) {
      days = 30;
    }

    // Add days in seconds to curMo
    if (iterMo == targetMo) {
      return curMoSeconds;
    }
    curMoSeconds += 86400 * days;
  }
}


int dateToUnix(int year, int month, int day, int offsetTime=0) {
  int isLeap = isLeapYear(year);
  int bOY = getBeginningOfYear(year, offsetTime);
  int monthUnix = getMonthSeconds(month, isLeap);
  int dayUnix = (day - 1) * 86400; // Since this is BoD we need to - 1
  return bOY + monthUnix + dayUnix;
}


int getSecondsFromString(String data) {
  String hrStr = splitString(data, ':', 0);
  int hr = hrStr.toInt();
  String minStr = splitString(data, ':', 1);
  int min = minStr.toInt();
  String secUnsplit = splitString(data, ':', 2);
  String secStr = splitString(secUnsplit, ' ', 0);
  int sec = secStr.toInt();
  String amPm = splitString(secUnsplit, ' ', 1);

  int hrOffset = amPm == "PM" ? 12 : 0;
  return (hr + hrOffset) * 3600 + min * 60 + sec;
}


JsonDocument localDeserialize(String payload, int doubleDeserialize) {
  JsonDocument doc, doc1;
  DeserializationError error = deserializeJson(doc, payload);
  // Serial.print("og deserializeJson() returned ");
  // Serial.println(error.c_str());

  if (doubleDeserialize == 1) {
    DeserializationError doubleError = deserializeJson(doc1, doc.as<const char*>());
    Serial.print("double deserializeJson() returned ");
    Serial.println(doubleError.c_str());
  }

  // Honestly this may never be necessary
  if (doubleDeserialize == 0) {
    return doc;
  } else {
    return doc1;
  }
}


JsonDocument apiCall(String apiString, String params, int doubleDeserialize) {
    HTTPClient client;
    client.begin(apiString + params);
    int httpCode = client.GET();

    if (httpCode > 0) {
      String payload = client.getString();
      // Serial.println("\nStatuscode: " + String(httpCode));
      // Serial.println(payload);
      client.end();
      JsonDocument processedJson = localDeserialize(payload, doubleDeserialize);
      return processedJson;

    } else {
        Serial.println("Error on HTTP Request making API call to " + String(apiString) + " due to code: " + String(httpCode));
    }
}

// input: String seasonsResult = apiCall(seasonsApi, "?year=" + String(currentYear), 0)["data"];
auto findNextSolstice(String seasonsResult, int currentTime, int offsetTime=0) {
    struct result {int timeTillNext; char* solsticeName;};
    // Deserialize result from apiCall["data"] to iterate over array objects
    JsonDocument phenomArr = localDeserialize(seasonsResult, 0);
    // Iterate over array of phenomena
    for (int i = 0; i < 6; i++) {
      JsonDocument iterSeason = localDeserialize(phenomArr[i], 0);
      String thisPhenom = iterSeason["phenom"];
      if (thisPhenom == "Solstice") {
        int phenomUnix = dateToUnix(iterSeason["year"], iterSeason["month"], iterSeason["day"], offsetTime=offsetTime);
        if (phenomUnix > currentTime) {
          char* solstice;
          if (iterSeason["month"] == 6) {
            solstice = "Summer";
          } else {
            solstice = "Winter";
          }
          Serial.println("The next solstice is at " + String(phenomUnix));
          return result {phenomUnix - currentTime, solstice};
        }
      }
    }
    return result {-1, "junk"};
}


String formatSolsticeResults(int nextSolsticeSeconds, char* nextSolsticeType) {
  int nextSolsticeDays = ceil(nextSolsticeSeconds / 86400.0);
  String longerShorter = nextSolsticeType == "Winter" ? "shorter" : "longer";
  String fmtResult = "The " + String(nextSolsticeType) + " Solstice is in " + String(nextSolsticeDays) + " days.\n\nDays are getting " + String(longerShorter) + " until then...";
  return fmtResult;
}


void loop() {

  if (reloads < reloadTolerance) {
    if ((WiFi.status() == WL_CONNECTED)) {
      display.clearDisplay();
      display.setCursor(0,0);
      // WORKING SECTION!
      JsonDocument sunsetPayload = apiCall(sunsetApi, "", 0);
      JsonDocument results = localDeserialize(String(sunsetPayload["results"]), 0);
      const char* sunset = results["sunset"];

      // WORKING SECTION!
      JsonDocument timePayload = apiCall(timeApi, "", 0);
      const int rawTime = timePayload["unixtime"];
      const int timeOffset = timePayload["raw_offset"];
      const int currentTime = rawTime + timeOffset;
      const int currentHour = currentTime % 86400;
      auto [currentYear, beginningOfYear] = getCurrentYear(currentTime, yearEndOffset);
      const int dayOfYear =  (currentTime - beginningOfYear) / 86400;
      Serial.println("Today's sunset is at: " + String(sunset) + " and the current time is " + String(currentTime));
      Serial.println("This is the current year: " + String(currentYear));

      // WORKING SECTION!
      int sunsetTime = getSecondsFromString(String(sunset));
      double sunlightHrsInDay = (sunsetTime - currentHour) / 3600.0;
      Serial.println("sunsetTime is : " + String(sunsetTime));
      Serial.println("There are " + String(sunlightHrsInDay) + " hrs of sunlight left in the day");

      // WORKING SECTION!
      String seasonsResult = apiCall(seasonsApi, "?year=" + String(currentYear), 0)["data"];
      auto [nextSolsticeSeconds, nextSolsticeType] = findNextSolstice(seasonsResult, currentTime, yearEndOffset);
      // If next solstice not in this year, check next year
      if (nextSolsticeSeconds == -1) {
        String seasonsResult = apiCall(seasonsApi, "?year=" + String(currentYear + 1), 0)["data"];
        auto [nextSolsticeSeconds, nextSolsticeType] = findNextSolstice(seasonsResult, currentTime, yearEndOffset);
      }
      String solsticeInfoStr = formatSolsticeResults(nextSolsticeSeconds, nextSolsticeType);
      Serial.println(solsticeInfoStr);

    display.println("Today's sunset is at: " + String(sunset));
    display.display();
    delay(2500);
    display.println("There are " + String(sunlightHrsInDay) + " hrs of sunlight left in the day");
    display.display();
    delay(5000);
    display.clearDisplay();
    display.setCursor(0,0);
    display.println(solsticeInfoStr);
    display.display();

    } else {
      Serial.println("Connection lost");
      display.println("Connection lost, dummy!");
      display.display();
      delay(500);
      display.clearDisplay();
      display.setCursor(0,0);
    }
    delay(15000);
  }
  else {
    String reloadMsg = "You've surpassed the allowable # of reloads: " + String(reloadTolerance) + " (currently at " + String(reloads) + ")";
    Serial.println(reloadMsg);
    display.clearDisplay();
    display.setCursor(0,0);
    display.println(reloadMsg);
    display.display();
  }
  reloads += 1;

  if(!digitalRead(BUTTON_A)) display.print("A");
  if (!digitalRead(BUTTON_A)) {
    display.println("Pressed A button, resetting display mode");
    Serial.println("Pressed A button");
    display.display();
    reloads = 0;
    display.clearDisplay();
    display.setCursor(0,0);
  }

}
