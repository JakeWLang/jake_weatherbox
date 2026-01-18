#include <WiFi.h>;
#include <HTTPClient.h>;
#include <ArduinoJson.h>;

const char* ssid = "{myWifiSsid}";
const char* password = "{myWifiPass}";

const char* timeApi = "http://worldtimeapi.org/api/timezone/America/Chicago";
const char* sunsetApi = "https://api.sunrise-sunset.org/json?lat={myLat}&lng={myLong}&tzid=America/Chicago"; // Need to add the date if we want tomorrow or something &date=2025-11-22
const char* seasonsApi = "https://aa.usno.navy.mil/api/seasons";

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("\nConnected to the WiFi network");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

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

int getUnixTimeFromStr(String data) {
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
  Serial.print("og deserializeJson() returned ");
  Serial.println(error.c_str());

  if (doubleDeserialize == 1) {
    DeserializationError doubleError = deserializeJson(doc1, doc.as<const char*>());
    Serial.print("double deserializeJson() returned ");
    Serial.println(doubleError.c_str());
  }

  if (doubleDeserialize == 0) {
    Serial.println("returning normal doc");
    return doc;
  } else {
    Serial.println("I am returning doc1, not doc");
    return doc1;
  }
}


JsonDocument apiCall(String apiString, String params, int doubleDeserialize) {
    HTTPClient client;
    client.begin(apiString + params);
    int httpCode = client.GET();

    if (httpCode > 0) {
      String payload = client.getString();
      Serial.println("\nStatuscode: " + String(httpCode));
      Serial.println(payload);
      client.end();
      JsonDocument processedJson = localDeserialize(payload, doubleDeserialize);
      return processedJson;

    } else {
        Serial.println("Error on HTTP Request due to code: " + String(httpCode));
    }
}


// JsonObject getMatchingObject(jsonArray, nKeys, keys, values) {
//   for (JsonObject dataItem : jsonArray.as<JsonArray>()) {
//     // Find the JsonObject that matches the criteria
//     int matches = 0
//     for (int i = 0; i < nKeys, i++) {
//       const char* selKey = String(keys[i]);
//       const char* selVal = String(values[i]);
//       const char* itemVal = String(dataItem[selKey]);
//       Serial.println("This is selkey: " + selKey + " and selVal: " + selVal + " and the itemVal: " + itemVal)
//       if (itemVal = selVal) {
//         matches++
//       }
//       Serial.println("We have" + String(matches) + " matches!")
//     }
//     if (matches == nKeys) {
//       return dataItem;
//     }
//   }
// }


void loop() {
  if ((WiFi.status() == WL_CONNECTED)) {

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
    Serial.println("Today's sunset is at: " + String(sunset) + " and the current time is " + String(currentTime));

    // Testing Section
    int sunsetTime = getUnixTimeFromStr(String(sunset));
    double sunlightHrsInDay = (sunsetTime - currentHour) / 3600.0;
    Serial.println("sunsetTime is : " + String(sunsetTime));
    Serial.println("There are " + String(sunlightHrsInDay) + " hrs of sunlight left in the day");


    // JsonDocument seasons = apiCall(seasonsApi, "?year=2025");
    // JsonObject winterSolstice = getMatchingObject(seasons["data"], 2, {"month", "phenom"}, {"12", "Solstice"});
  } else {
    Serial.println("Connection lost");
  }
  delay(15000);

}
