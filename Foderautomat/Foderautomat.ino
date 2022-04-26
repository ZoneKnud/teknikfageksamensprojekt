#include <SoftwareSerial.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <EEPROM.h>

// HC-12
SoftwareSerial HC12(4, 2); // HC-12 TX Pin, HC-12 RX Pin

String id = "1234"; // Foderautomatens unikke ID.

bool hasBeenSetup = false; // Fortæller om data er blevet læst fra EEPROM

unsigned long tidModtaget = 0; // Unix-tiden som bliver sendt ud af ESP'en
unsigned long tidMillisVedModtaget = 0; // Arduinoens millis() ved modtagelse af unixtiden.

StaticJsonDocument<512> configDoc; // JSON dokumentet hvor konfigurationen bliver gemt.

const int buttonpin = 5;

const int motorPin1 = 7;
const int motorPin2 = 6;
const int motorRead = A1;

bool timeIsSet = false; // Fortæller om tiden er blevet modtaget fra styreenheden/ESP'en

int doseringer;


// Funktionen skriver til EEPROM med given data.
void writeToEEPROM(const String &strToWrite)
{
  byte len = strToWrite.length(); // Tæller hvor mange bytes der er i String.
  EEPROM.write(0, len); // Første byte fortæller hvor mange bytes der er skrevet. Til når den skal læse igen.
  for (int i = 0; i < len; i++)
  {
    EEPROM.write(1 + i, strToWrite[i]); // Skriver teksten til EEPROM
  }
}

// Funktionen læser fra EEPROM
String readFromEEPROM()
{
  int strLen = EEPROM.read(0); // Læs første byte som fortæller
  // hvor mange bytes der er skrevet i EEPROM
  char data[strLen + 1];
  for (int i = 0; i < strLen; i++)
  {
    data[i] = EEPROM.read(1 + i); // Læs fra EEPROM.
  }
  data[strLen] = '\0'; // Filter output.
  return String(data);
}

// Funktion til at ryde EEPROM for data.
void reset() {
  // Clear EEPROM
  for (int i = 0 ; i < EEPROM.length() ; i++) {
    EEPROM.write(i, 0); // Skriv alle bytes til 0.
  }

  configDoc.clear(); // Ryd JSON dokumentet.

  Serial.println("Done");
}

// Funktionen indlæser data gemt i EEPROM.
void loadData() {
  String input = readFromEEPROM(); // Læs fra EEPROM

  // String til JSON objekt som ArduinoJSON kan bearbejde.
  DeserializationError error = deserializeJson(configDoc, input);

  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());

  } else {
    doseringer = configDoc["d"];
    // Hvis indlæsningen lykkedes. Print den nuværende konfiguration til Serial.
    String output;
    serializeJson(configDoc, output);
    Serial.print("output: ");
    Serial.println(output);

    Serial.print("configDoc[f]: ");
    Serial.println(configDoc["f"].as<String>().c_str());
    Serial.print("configDoc[d]: ");
    Serial.println(configDoc["d"].as<String>().c_str());

    hasBeenSetup = true;
  }
}

// Funktionen returnerer den nuværende tid i unix-sekunder ved brug af Arduinoens millis() funktion.
unsigned long timeNow() {
  // Tid nu = udgangspunkt + (millis() - Millis ved udgangspunkt)
  if (tidModtaget == 0) {
    return 0;
  }
  return tidModtaget + (millis() / 1000 - tidMillisVedModtaget / 1000); 
  // Dividerer med 100 for at få tiden i sekunder.
}

void giveFood(bool single = false) {
  Serial.println("Feed me");
  Serial.println(doseringer);
  int count = doseringer;
  if (single) {
    count = 1;
  }

  for (int i = 0; i < count; i++) { // kør antal gange
    digitalWrite(motorPin2, HIGH); // Kør motoren ud.
    delay(100); // Vent 100 milli med at læse for at undgå at læse spike
    while (true) {
      Serial.print("reading..");
      Serial.println(analogRead(motorRead));
      if (analogRead(motorRead) > 80) { // Når ADC måler 80 - ca. 0.5V
        digitalWrite(motorPin2, LOW); // Gør motorpin lav.
        delay(200);
        digitalWrite(motorPin1, HIGH); // Kør motor tilbage igen.
        break;
      }
    }
    delay(100);
    while (true) {
      if (analogRead(motorRead) > 80) { // Når ADC måler 80 - ca. 0.5V
        digitalWrite(motorPin1, LOW); // Gør motor lav.
        delay(200);
        break;
      }
    }

  }

}


void setup() {
  Serial.begin(9600);
  HC12.begin(9600);
  Serial.println("Tændt");

  pinMode(buttonpin, INPUT);
  pinMode(motorPin1, OUTPUT);
  pinMode(motorPin2, OUTPUT);

  //Indlæs data
  loadData();

}



byte incomingByte; //Byte som bliver læst af HC-12
String readBuffer = ""; //Lang tekst som bliver sendt til HC-12.

// Tilladte chars for at undgå interferens.
char allowedChars[] = {'i', 'd', 'f', 't', 'n', 'o', 'w', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '[', ']', '{', '}', ':', ',', 39, '\''};

unsigned long lastTime = millis(); // Sidste gang der blev givet en dosering.
unsigned long lastBtnPressMillis; // Første gang der blev trykket på reset knappen.

unsigned int lastFeedDone;

void loop() {

  // Reset knap funktion.
  if (digitalRead(buttonpin) == LOW) {    // assumes btn is HIGH when pressed
    lastBtnPressMillis = millis();        // btn not pressed so reset clock
  }
  if (millis() - lastBtnPressMillis >= 10000) {
    // Hvis knappen har været holdt nede i 10 sekunder.
    reset();
  }

  // Læs fra HC-12
  while (HC12.available()) {             // Hvis HC-12 har data
    incomingByte = HC12.read();          // Gem byte fra HC-12

    for (int i = 0; i < sizeof(allowedChars) - 1; i++) { // Gå alle tilladte chars igennem.
      if (allowedChars[i] == char(incomingByte)) { // Hvis HC.read indeholder tilladt char

        if (char(incomingByte) == '{') { // Hvis start-byte er læst. Nulstil readbuffer
          // Det betyder at en ny besked er ved at blive sendt eller den tidligere har været en fejl.
          readBuffer = "";
        }

        readBuffer += char(incomingByte);    // Tilføj byte til string.
        break; // Slå ud af for-loop og læs igen fra HC-12.
        //Ingen grund til at gå i gennem resten af bogstaverne.
      }
    }
  }

  if (readBuffer != "") { // Hvis readBuffer har data.
    Serial.print("rB:  ");
    Serial.print(readBuffer);
    Serial.println();

    if (readBuffer.charAt(0) != '{') { // Hvis det første bogstav i readbuffer ikke er { - Nulstil.
      readBuffer = "";
    } else {
      if (readBuffer.indexOf("}") == -1) { // Hvis readBuffer IKKE indeholder  et }
        return;
      } else { // Hvis readBuffer indeholder  et }
        Serial.println("Prøver at lave nyt dokument");
        delay(300);
        // Nulstil det nuværende dokument og parse readBuffer til dokumentet.
        // Deserialize the JSON document
        DeserializationError error = deserializeJson(configDoc, readBuffer);

        if (error) { // Hvis der opstod en fejl.
          readBuffer = ""; // Nulstil readBuffer.
          configDoc.clear();
          loadData(); // Da vi nulstillede dokumentet indlæses den nuværende konfiguration igen fra EEPROM.
          delay(600);
          HC12.write("FAIL"); // Send FAIL tilbage til ESP'en.
          return;
        }

        // Hvis der ikke opstod en fejl:
        String output;
        serializeJson(configDoc, output);
        if (configDoc.containsKey("id")) { 
          // Hvis dokumentet indeholder key ID betyder 
          // det at en ny konfiguration er blevet sendt.
          if (configDoc["id"] = id) {
            if (configDoc.containsKey("f")) { // Fodertidspunkter
              if (configDoc.containsKey("d")) { // Doseringer pr. fodring.

                doseringer = configDoc["d"];

                Serial.println(output);

                writeToEEPROM(output); // Skriv den nye konfiguration til EEPROM.
                Serial.println("Gemt i EEPROM");
                delay(600);

                HC12.write("OK"); // Send OK tilbage til ESP'en

              } else {
                // Data transmissionen er ikke lykkedes da doseringer mangler
                Serial.println("Ingen 'd'");
                configDoc.clear();
                loadData(); // Da vi nulstillede dokumentet indlæses den nuværende konfiguration igen fra EEPROM.
                HC12.write("FAIL");
              }
            } else {
              // Data transmissionen er ikke lykkedes da fodertidspunkter mangler
              Serial.println("Ingen 'f'");
              configDoc.clear();
              loadData();
              HC12.write("FAIL");
            }
          } else {
            // ID er ikke foderautomatens ID.
            Serial.println("Ikke til mig");
            configDoc.clear();
            loadData(); // Da vi nulstillede dokumentet indlæses den nuværende konfiguration igen fra EEPROM.
          }
        } else if (configDoc.containsKey("t")) {
          // Hvis der er t betyder det at en unixtid bliver sendt fra ESP'en.
          // Set time.
          Serial.println("Gemmer tid!");
          tidModtaget = configDoc["t"];
          tidMillisVedModtaget = millis();

          timeIsSet = true;

          configDoc.clear();
          loadData(); // Da vi nulstillede dokumentet -
          // indlæses den nuværende konfiguration igen fra EEPROM.

        } else if (configDoc.containsKey("now")) { // Hvis en manuel er blevet sendt ud.
          // Giv foder på kommando hvis ID er lig foderautomatens ID.
          if (configDoc["now"] = id) {
            giveFood(true);
          }

          configDoc.clear();
          loadData(); // Da vi nulstillede dokumentet - 
          // indlæses den nuværende konfiguration igen fra EEPROM.

        }

      }
    }
  }


  if (timeIsSet) { // Hvis unix-tid er modtaget fra ESP.
    // Printer den nuværende tid hver 5. sekund.
    if ((millis() - lastTime) > 5000) { // Hvis der er gået 5 sekunder.
      Serial.println(timeNow());

      Serial.print(hour(timeNow())); // Brug TimeLib til at konvertere unix-tid til den nuværende time.
      Serial.print(":");
      Serial.println(minute(timeNow())); // Brug TimeLib til at konvertere unix-tid til det nuværende minut.

      lastTime = millis();
    }

    // Følgende sørger for at give foder på rigtige tidspunkter
    if (hasBeenSetup) { // Hvis foderautomaten har en konfiguration.

      unsigned int hourNow = hour(timeNow());
      unsigned int minuteNow = minute(timeNow());

      // Konverter unix-tid til tiden i halve timer.
      unsigned int time30Now = hourNow * 2; // Timer gange to for at få halve timer.

      if (minuteNow >= 30) { // Hvis der er gået mere end 30 minutter siden klokken slog hel.
        time30Now++; // Lig en til.
      }

      JsonArray arr = configDoc["f"].as<JsonArray>();
      // Gå igennem alle fodertidspunkter for den nuværende konfiguration.
      for (JsonVariant value : arr) {
        unsigned int time30Feed = value.as<unsigned int>(); // Fodertidspunktet i halve timer.


        if (time30Now == time30Feed) { // Hvis fodertidspunktet er nået.

          if (lastFeedDone != time30Feed) { // Hvis fodertidspunktet ikke allerede er blevet givet.
            Serial.println("Feed me hoe");
            Serial.println();
            giveFood();
            lastFeedDone = time30Feed; // Gem at fodertidspunktet er blevet aktivteret/givet.
          }

        }

      }

    }

  }

  readBuffer = "";// Nulstil string.
}
