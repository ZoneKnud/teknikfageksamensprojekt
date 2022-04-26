
// Import required libraries
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <Wire.h> // Library for I2C communication
#include <RTClib.h>
#include <SD.h>
#include <SPI.h>
#include <LittleFS.h>
#include <SoftwareSerial.h>




#define SD_CS_PIN D8 // CS pin til SD-kort

//HC12
SoftwareSerial HC12(D3, D4); // HC-12 TX Pin, HC-12 RX Pin

// Definer rtc objekt.
RTC_DS3231 rtc;

char daysOfTheWeek[7][12] = {"Søndag", "Mandag", "Tirsdag", "Onsdag", "Torsdag", "Fredag", "Lørdag"}; // Fortæl RTClib omkring ugedage.

DateTime lastBroadcast; // Variabel til at gemme den sidste gang tiden blev sendt ud til foderautomaterne.

// Allocate a JsonDocument
// Don't forget to change the capacity to match your requirements.
// Use arduinojson.org/v6/assistant to compute the capacity.
DynamicJsonDocument configDoc(5120);

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

String dataToBeSent = ""; // Variabel til at holde data som skal sendes til foderautomater.


// struct Foderautomat {
//     unsigned int ID;
//     char navn[64];
//     unsigned int fodertidspunkter[48];
//     unsigned int doseringer;
// };

// Foderautomat foderautomater[100];


bool sendData(String jsondata, bool expectResponse = true) {
    // funktinen sender given data med HC-12 og afhængig af om expectResponse er sat, -
    // tjekker om data er modtaget ved at vente på et svar fra foderautomat
    bool finish = false;
    // Sender data
    Serial.print("Sender: ");
    Serial.println(jsondata);
    int i = 0;
    // Send hver byte for sig med HC12. Da MC har en lille buffer kan den ikke sende hele string på én gang.
    while(!finish) {
        HC12.write(jsondata.charAt(i)); 
        if(jsondata.charAt(i) == '}') {
            // Når } er nået. Stop.
            finish = true;
        }
        delay(100); // Delay mellem hver bytes for stabilitet.
        i++;
    }

    if(expectResponse == false) {
        return true; // Stop funktionen og retuner true som indikerer at data er sendt og alt er i orden.
    } else {
        // Venter på svar fra foderautomat
        finish = false;
        unsigned long timeBegin = millis();

        char allowedChars[] = {'O','K','F','A','I','L'}; // Tilladte chars for at forhindre inteferrens.

        Serial.println("Lytter efter besked fra foderautomat");

        byte incomingByte; //Byte som bliver læst af HC-12
        String readBuffer = ""; //Lang tekst som byte bliver tilføjet til.

        while(!finish) { // Så længe finish er false.
            // Arduino som har modtaget data skal have tid til at bearbejde og sende et svar.
            if((millis()-timeBegin) <= 2000) { // Kør følgende i 2 sekunder.
                while (HC12.available()) { // Hvis HC-12 har data
                    incomingByte = HC12.read(); // byte fra HC-12

                    for(unsigned int i=0; i < sizeof(allowedChars) -1; i++) { // Gå alle tilladte chars igennem.
                        if(allowedChars[i] == char(incomingByte)) { // Hvis HC.read indeholder tilladt char
                            readBuffer += char(incomingByte);    // Tilføj byte til string.
                            break; // Slå ud af for-loop og læs igen fra HC-12. Ingen grund til at gå i gennem resten af bogstaverne.
                        }
                    }
                }
                if (readBuffer != "") {
                    Serial.println(readBuffer);
                    if (readBuffer == "OK") {
                        // Data er sendt og modtaget
                        finish = true;
                        Serial.println("Data sendt");
                        return true;
                    }
                    if (readBuffer == "FAIL") {
                        // Der opstod en fejl på Arduinoens side
                        finish = false;
                        Serial.println("Data FAIL");
                        return false; // Retuner false som indikerer at sendData fejlede.
                    }
                }
            } else { // Hvis der er gået mere end 2 sekunder uden gyldigt svar fra HC 12
                Serial.println("Der gik for lang tid for et svar");
                return false;
            }
        }
    }

    // Hvis noget går galt undervejs:
    // Det er ikke meningen at programmet nogensinde når til dette punkt, men hvis der opstår en ukendt fejl er det godt at have en "fail-safe"
    Serial.println("Noget gik galt undervejs");
    return false;
}

bool broadcastTime() {
    // Funktion til at broadcast tid til alle foderautomater 
    // så de kan fodre synkont på korrekte tidspunkt.
    // Lav JSON String.
    String timedata = "{'t':";
    timedata += rtc.now().unixtime();
    timedata += "}";
    Serial.println("Sender tid!");
    Serial.println(timedata);

    if(sendData(timedata, false)) { // Send data  -  Hvis sendData returnerer true
        Serial.println("Tid sendt"); 
    } else { // Hvis sendData returner false.
        Serial.println("!!!Kunne ikke sende tid!!!");
        return false;
    }

    // Hvis sendData returnerer true
    lastBroadcast = rtc.now();
    return true;
}

// Loads the configuration from a file from SD-card
bool loadConfiguration(const char *filename = "config.json") {

    // Hvis config filen ikke findes.
    if (!SD.exists(filename)) {
        // fil eksisterer ikke - lav en.
        File configfile = SD.open(filename, FILE_WRITE);
        configfile.print("\"tid\":0,\"foderautomater\":[]}"); // Lav et tomt JSON object.
        configfile.close();
    }

    // Open file for reading
    File file = SD.open(filename);

    // Deserialize the JSON document
    DeserializationError error = deserializeJson(configDoc, file);
    if (error) {
        Serial.println(F("Failed to read file, using default configuration"));
        return false;
    }

    // Close the file (Curiously, File's destructor doesn't close the file)
    file.close();
    return true;
}

// Saves the configuration to a file
bool saveConfiguration(const char *filename = "config.json") {
  // Delete existing file, otherwise the configuration is appended to the file
  SD.remove(filename);

  // Open file for writing
  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to create file"));
    return false;
  }

  // Serialize JSON to file
  if (serializeJson(configDoc, file) == 0) {
    Serial.println(F("Failed to write to file"));
    return false;
  }

  // Close the file
  file.close();

  return true;
}


void setup(){
    #ifndef ESP8266
         while (!Serial); // for Leonardo/Micro/Zero
    #endif
    // Serial port for debugging purposes
    Serial.begin(115200);

    // Start HC12 kommunikation med 9600 baudrate.
    HC12.begin(9600);


    Serial.print("Setting AP (Access Point)…");

    // Indstil wifi accesspoint uden kode.
    WiFi.softAP("weFeed foderautomat");

    IPAddress IP = WiFi.softAPIP(); // Indstil IP-adresse.
    Serial.print("AP IP address: ");
    Serial.println(IP);

    // Print ESP8266 Local IP Address
    Serial.println(WiFi.localIP());

    Serial.print("\r\nWaiting for SD card to initialise...");
    if (!SD.begin(SD_CS_PIN)) { // CS
        Serial.println("Initialising failed!");
        return;
    }
    Serial.println("SD Initialisation completed");

    // Indlæs den nuværende konfiguration
    if(!loadConfiguration()) {
        Serial.println("Kunne ikke indlæse config!");
    }

    // Tilgå filsystem som lagre alle statiske web sider.
    if (LittleFS.begin()) {
        Serial.println("Filesystem mounted");
    }
    else {
        Serial.println("ERROR!!! Filesystem not mounted...");
    }


    delay(3000); // Tilføj lidt delay til RTC indlæsning i håb om at det virker bedre.

    if (! rtc.begin()) { // Indlæs RTC.
        Serial.println("Couldn't find RTC");
        while (1); // Soft reset ESP.
    }

    if (rtc.lostPower()) {
        Serial.println("RTC lost power, lets set the time!");
        // following line sets the RTC to the date & time this sketch was compiled
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    // server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    //     request->send(LittleFS, "/index.html", String(), false, processor);
    // });


    // Route for root / web page
    // server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    //     request->send_P(200, "text/html", index_html, processor);
    // });
    // server.on("/clockpicker.js", HTTP_GET, [](AsyncWebServerRequest *request){
    //     request->send(LittleFS, "/clockpicker.js", "text/javascript");
    // });
    // server.on("/clockpicker.css", HTTP_GET, [](AsyncWebServerRequest *request){
    //     request->send(LittleFS, "/clockpicker.css", "text/css");
    // });
    // server.serveStatic("/page.htm", LittleFS, "/www/page.htm");


    // De næste mange linjer kode opsætter webserverens forskellige sider og respons.

    // Bruger LittleFS (FS = filesystem) til at lavere websider. Hvor 192.168.4.1/ automatisk vil åbne index.html.
    // Alle filer er sat med en cache på 43200 sekunder = 60 minutter * 12 = 12 timer. Dette gør at server ikke bliver særlig meget belastet.
    server.serveStatic("/", LittleFS, "//").setDefaultFile("index.html").setCacheControl("max-age=43200");;


    // Funktion til at sende foderautomatdata til brugeren.
    // URL: /getdata, Request Method: GET.
    server.on("/getdata", HTTP_GET, [](AsyncWebServerRequest *request){
        String jsondata;
        configDoc["tid"] = rtc.now().unixtime(); // Sæt den nuværende tid i config dokumentet.
        serializeJson(configDoc, jsondata); // Konverter JSON dokument til String.
        request->send(200, "application/json", jsondata); // Respons jsonData.
    });

    // Funktion til at modtage tid for at justere ESP'ens tid.
    // URL: /updatetime, Request Method: POST, Body-format: JSON
    server.addHandler(new AsyncCallbackJsonWebHandler("/updatetime",[](AsyncWebServerRequest* request, JsonVariant& json) {
        // Tjek om der bliver modtaget JSON
        if (not json.is<JsonObject>()) {
            request->send(400, "text/plain", "Not an object");
            return;
        }
        // Få tiden fra request og indstil den som den nuværende tid i RTC'en.
        auto&& data = json.as<JsonObject>();

        uint32_t time = data["time"].as<uint32_t>();
        rtc.adjust(DateTime(time));

        request->send(200, "text/plain", "OK");

        broadcastTime(); // Send data ud til foderautomater.
    }));


    // Funktion til at modtage nyt foderautomat data til at opdatere en foderautomats indstillinger.
    // URL: /edit, Request Method: POST, Body-format: JSON
    server.addHandler(new AsyncCallbackJsonWebHandler("/edit",[](AsyncWebServerRequest* request, JsonVariant& json) {
        // Tjek om der bliver modtaget JSON
        if (not json.is<JsonObject>()) {
            request->send(400, "text/plain", "Not an object");
            return;
        }
        auto&& updateData = json.as<JsonObject>();

        // Indlæs foderautomater fra den nuværende config.
        auto&& foderautomater = configDoc["foderautomater"].as<JsonArray>();

        // For-loop med alle foderautomater.
        for (JsonArray::iterator it=foderautomater.begin(); it!=foderautomater.end(); ++it) {
            // Hvis foderautomat[i]'s id er lig den modtagets updatedata's id. Hvis ikke gå videre til næste.
            if ((*it)["id"] == updateData["id"]) {
                // Fjern den nuværnde foderautomat indstillinger.
                foderautomater.remove(it);

                // Lav ny foderautomat indstilling.
                JsonObject object = foderautomater.createNestedObject();
                object["id"] = updateData["id"].as<String>(); // Foderautomatens ID
                object["n"] = updateData["n"].as<String>(); // Foderautomatens navn
                object.createNestedArray("f"); // Foderautomatens fodertidspunkter
                object["f"] = updateData["f"].as<JsonArray>(); // Foderautomatens fodertidspunkter
                object["d"] = updateData["d"].as<long>(); // Foderautomatens doseringer pr. fodring.

                saveConfiguration();

                // {'id':'ID1234','f':[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48],'d':1}
                // Send data til foderautomat i formatet som står ovenfor.
                String jsonData = "{'id':'";
                jsonData += object["id"].as<String>();
                jsonData += "','f':";
                jsonData += object["f"].as<String>();
                jsonData += ",'d':";
                jsonData += object["d"].as<int>();
                jsonData += "}";

                dataToBeSent = jsonData; // Tilføj til global variabel. Data bliver send i loop() funktionen. Se loop()

                request->send(200, "text/plain", "Foderautomat bliver opdateret!");

                return;
            }
        }
        // Hvis ID ikke kunne findes blandt kendte foderautomater;
        request->send(400, "text/plain", "Kunne ikke finde ID!");

    }));


    // Funktion til at aktivere en foderautomat på kommando til test eller andet.
    // URL: /manual, Request Method: POST, Body-format: form-data
    server.on("/manual", HTTP_POST, [](AsyncWebServerRequest *request){
        String id;
        // Hvis der i requesten er et form-input der hedder manualInputID.
        if (request->hasParam("manualInputID", true)) {
            // Få id fra manualInputID.
            id = request->getParam("manualInputID", true)->value();

            // {'now':'1234'}
            // Send data til foderautomater i formatet som står ovenfor.
            String jsonData = "{'now':'";
            jsonData += id;
            jsonData += "'}";

            sendData(jsonData, false); // Tilføj til global variabel. Data bliver send i loop() funktionen. Se loop()

            request->send(200, "text/plain", "Foderautomat er aktiveret!");
            
        } else {
            request->send(400, "text/plain", "Intet ID er sendt!");
        }
        request->send(400);
    });


    // Funktion til at tilføje en ny foderautomat til systemet.
    // URL: /add, Request Method: POST, Body-format: form-data
    server.on("/add", HTTP_POST, [](AsyncWebServerRequest *request){
        String id;
        // Hvis der i requesten er et form-input der hedder addInputID.
        if (request->hasParam("addInputID", true)) {
            // Få id fra addInputID.
            id = request->getParam("addInputID", true)->value();
            // Indlæs foderautomater fra den nuværende config.
            auto&& foderautomater = configDoc["foderautomater"].as<JsonArray>();

            // for-loop med alle nuværende foderautomater for at se om den allerede er i systemet.
            for (JsonVariant value : foderautomater) {
                // Serial.println(value["id"].as<String>().c_str());
                if(value["id"].as<String>() == id) { // Hvis den nuværende foderautomat har samme ID som den som prøves at tilføjes.
                    request->send(400, "text/plain", "Foderautomat findes allerede!");
                    return;
                }
            }
            // Der blev ikke fundet en foderautomat med dette ID.
            // Tilføj den til systemet.
            JsonObject object = foderautomater.createNestedObject();
            object["id"] = id; // Foderautomatens ID
            object["n"] = ""; // Foderautomatens Navn
            object.createNestedArray("f"); // Foderautomatens fodertidspunkter
            object["d"] = 1; // Foderautomatens doseringer pr fodring.
            // Alle værdierne er tomme.

            saveConfiguration();

            request->send(200, "text/plain", "Foderautomat er tilføjet!");
            
        } else { // Hvis intet er sendt.
            request->send(400, "text/plain", "Intet ID er sendt!");
        }
        request->send(400);
    });

    // Funktion til at tilføje en ny foderautomat til systemet.
    // URL: /delete, Request Method: POST, Body-format: form-data
    server.on("/delete", HTTP_POST, [](AsyncWebServerRequest *request){
        String id;
        // Hvis der i requesten er et form-input der hedder deleteID.
        if (request->hasParam("deleteID", true)) {
            // Få id fra deleteID.
            id = request->getParam("deleteID", true)->value();

            // Indlæs foderautomater fra den nuværende config.
            JsonArray foderautomater = configDoc["foderautomater"].as<JsonArray>();
            
            // for-loop med alle nuværende foderautomater for at se om den allerede er i systemet.
            for (JsonArray::iterator it=foderautomater.begin(); it!=foderautomater.end(); ++it) {
                // Hvis foderautomat[i]'s id er lig den modtagets updatedata's id. Hvis ikke gå videre til næste.
                if ((*it)["id"] == id) {
                    // Fjern foderautomat.
                    foderautomater.remove(it);
                    saveConfiguration();
                    request->send(200, "text/plain", "Foderautomat er fjernet");
                    return;
                }
            }

            request->send(400, "text/plain", "Kunne ikke finde ID!");            
            
        } else {
            request->send(400, "text/plain", "Intet ID er sendt!");
        }
        // request->redirect("/");
        request->send(400);
    });

    // Hvis brugeren forespørger et URL som har en handler. Send en 404.
    server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Not found");
    });

    // Start server
    server.begin();

    // Print den nuværende tid for DEBUG.

    DateTime now = rtc.now();

    Serial.print("Time is: ");
    Serial.print(now.year(), DEC);
    Serial.print('/');
    Serial.print(now.month(), DEC);
    Serial.print('/');
    Serial.print(now.day(), DEC);
    Serial.print(" (");
    Serial.print(daysOfTheWeek[now.dayOfTheWeek()]);
    Serial.print(") ");
    Serial.print(now.hour(), DEC);
    Serial.print(':');
    Serial.print(now.minute(), DEC);
    Serial.print(':');
    Serial.print(now.second(), DEC);
    Serial.println();

    Serial.print(" since midnight 1/1/1970 = ");
    Serial.print(now.unixtime());
    Serial.print("s = ");
    Serial.print(now.unixtime() / 86400L);
    Serial.println("d");
    
    broadcastTime(); // Send den nuværende tid ud til alle foderautomater.

    // delay(2000);
}




void loop() {
    // Sørger for at tid bliver broadcastet hver 10 minutes = 600 seconds
    if(rtc.now().unixtime() - lastBroadcast.unixtime() >= 600) {
        broadcastTime();
    }

    // Sørger for at sende tid hvis der er blevet tilføjet data til dataToBeSent
    // Hvis dataToBeSent ikke er tom:
    if (dataToBeSent != "") {
        int counter = 0; // Denne counter fungerer som en tæller for hvor mange gange der er forsøgt at sende data ud til foderautomat.
        while(true) { // Kør til den bliver stoppet med break;
            if (counter < 5) { // Hvis counter er mindre end 5.
                Serial.println("Sender data"); 
                if(sendData(dataToBeSent)) { // Hvis sendData returnerer true
                    Serial.println("Data er sendt med sucess!!");
                    dataToBeSent = "";
                    break;
                }
                // Hvis der ikke kunne sendes data tilføj en til counter.
                counter+=1;
            } else {
                //Fejl i at sende data til enhed
                Serial.println("!!!Kunne ikke sende data!!!");
                dataToBeSent = ""; // Nulstil.
                break;
            }
            
        }
    }

    
    
    

    // delay(120000);
    

    



    // while (HC12.available()) {             // Hvis HC-12 har data
    //     incomingByte = HC12.read();          // Gem byte fra HC-12
    //     readBuffer += char(incomingByte);    // Tilføj byte til string.
    //     // delay(5);
    // }
    // delay(100);

    // // if(readBuffer != "") {
    // //     Serial.println("readbuffer er ikke ingenting!");
    // //     Serial.println(readBuffer.charAt(0));
    // //     Serial.println(readBuffer.charAt(readBuffer.length()-1));
    // //     if(readBuffer.charAt(0) == '{' && readBuffer.charAt(readBuffer.length()-1) == '}') {
    // //         Serial.println("if(readBuffer.charAt(0) == '{' && readBuffer.charAt(readBuffer.length()-1) == '}')");
    // //     }
    // // }



    // // if(readBuffer.charAt(0) == '{' && readBuffer.charAt(readBuffer.length()-1) == '}') {
    // if(readBuffer != "") {
    //     Serial.print(readBuffer);

    //     Serial.println("readbuffer er ikke ingenting!");
    //     Serial.println(readBuffer.charAt(0));
    //     Serial.println(readBuffer.charAt(readBuffer.length()-1));
    //     tmp1 = readBuffer.charAt(0);
    //     tmp2 = readBuffer.charAt(readBuffer.length()-1);

    //     if(tmp1 == '{') {
    //         // lastTime = millis();
            
    //     //     Serial.println("readBuffer.startsWith({)");
    //         // if( (millis()-lastTime) >= 500) {
    //             // if(tmp2 == '}') {
    //             // if(char(incomingByte) == '}'){
    //             //     Serial.println("readBuffer.endsWith(})");
    //                 //     Serial.println("if(readBuffer.charAt(0) == '{' && readBuffer.charAt(readBuffer.length()-1) == '}')");

    //                 // Deserialize the JSON document
    //                 DeserializationError error = deserializeJson(doc, readBuffer);

    //                 // Test if parsing succeeds.
    //                 if (error) {
    //                     Serial.print(F("deserializeJson() failed: "));
    //                     Serial.println(error.f_str());
    //                     //Serial.println(readBuffer);
    //                     return;
    //                 }

    //                 String output;
    //                 serializeJson(doc, output);
    //                 Serial.println(output);

    //                 //readBuffer = "";

    //                 //Serial.println(readBuffer);
                    
    //                 // Serial.println("readbuffer clear!!");
    //                 // readBuffer = "";// Nulstil string.
    //                 // Serial.println();
    //             // }
    //         // }
    //     }
    // }
    // Serial.println("readbuffer clear!!");
    // readBuffer = "";// Nulstil string.
    // // Serial.println();



    // // digitalWrite(D5, HIGH);
    // // delay(1000);
    // // digitalWrite(D5, LOW);
    // // delay(1000);

    // //  HC12.write("Open lock");

    // // while (HC12.available()) {             // Hvis HC-12 har data
    // //     incomingByte = HC12.read();          // Gem byte fra HC-12
    // //     readBuffer += char(incomingByte);    // Tilføj byte til string.
    // // }

    // delay(100); // Delay for stabilitet
    // // // Hvis der er kommandoer fra Arduinoen - send til HC-12.
    // while (Serial.available()) {
    //     HC12.write(Serial.read());
    // }

    // if(readBuffer == "Open lock") { // Hvis HC-12 får en besked som hedder "Open Lock".
    //     Serial.println("åbner lås");
    //     digitalWrite(7, HIGH); //Tænd LED som symboliserer en oplåsningsmekanisme.
    //     delay(3000);
    //     digitalWrite(7, LOW);
    //     readBuffer = ""; // Nulstil string.
    // }
    // readBuffer = "";// Nulstil string.



}



  // Send a GET request to <ESP_IP>/update?output=<inputMessage1>&state=<inputMessage2>
  // server.on("/update", HTTP_GET, [] (AsyncWebServerRequest *request) {
  //   String inputMessage1;
  //   String inputMessage2;
  //   // GET input1 value on <ESP_IP>/update?output=<inputMessage1>&state=<inputMessage2>
  //   if (request->hasParam(PARAM_INPUT_1) && request->hasParam(PARAM_INPUT_2)) {
  //     inputMessage1 = request->getParam(PARAM_INPUT_1)->value();
  //     inputMessage2 = request->getParam(PARAM_INPUT_2)->value();
  //     digitalWrite(inputMessage1.toInt(), inputMessage2.toInt());
  //   }
  //   else {
  //     inputMessage1 = "No message sent";
  //     inputMessage2 = "No message sent";
  //   }
  //   Serial.print("GPIO: ");
  //   Serial.print(inputMessage1);
  //   Serial.print(" - Set to: ");
  //   Serial.println(inputMessage2);
  //   request->send(200, "text/plain", "OK");
  // });

  // Send a POST request to <IP>/post with a form field message set to <message>
  // server.on("/post", HTTP_POST, [](AsyncWebServerRequest *request){
  //   String message;
  //   if (request->hasParam("data", true)) {
  //     message = request->getParam("data", true)->value();
  //   } else {
  //     message = "No message sent";
  //   }
  //   request->send(200, "text/plain", "Hello, POST: " + message);
  // });

  // AsyncCallbackJsonWebHandler* handler = new AsyncCallbackJsonWebHandler("/post3", [](AsyncWebServerRequest *request, JsonVariant &jsondata) {
  // JsonObject& jsonObj = jsondata.as<JsonObject>();
  //   Serial.println(jsonObj);
  // });
  // server.addHandler(handler);

  // server.onRequestBody(
  //   [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
  //   {
  //     String message;
  //     if ((request->url() == "/post2") && (request->method() == HTTP_POST))
  //     {
  //         const size_t        JSON_DOC_SIZE   = 8192;
  //         DynamicJsonDocument jsonDoc(JSON_DOC_SIZE);
          
  //         if (DeserializationError::Ok == deserializeJson(jsonDoc, (const char*)data))
  //         {
  //             JsonArray obj = jsonDoc.as<JsonArray>();

  //             message = "Besked modtaget";

  //             for (JsonVariant value : obj) {
  //                 Serial.println(value.as<String>().c_str());
  //             }

  //             Serial.println("færdig med itterate");

  //             //Serial.println(obj[0].as<String>().c_str());

  //         } else {
  //           message = "No message sent";
  //         }
  //         request->send(200, "text/plain", "Hello, POST: " + message);
  //         // request->send(200, "application/json", "{ \"status\": 0 }");
  //     }
  //   }
  // );

  // Send a GET request to <ESP_IP>/update?output=<inputMessage1>&state=<inputMessage2>
    // server.on("/updatetime", HTTP_GET, [] (AsyncWebServerRequest *request) {
    //     uint32_t time;
    //     // GET input1 value on <ESP_IP>/update?output=<inputMessage1>&state=<inputMessage2>
    //     if (request->hasParam("time")) {
    //         time = request->getParam("time")->value();
    //         rtc.adjust(DateTime(time));
    //         Serial.println(time);
    //         timeIsSet = true;
    //         Serial.print("Time now:");
    //         Serial.print(":");
            
    //     }
    //     else {
    //         time = "No time sent";
    //     }
    //     //Serial.println(time);
    //     request->send(200, "text/plain", "OK");
    // });

    // // Send a POST request to <IP>/post with a form field message set to <message>
    // server.on("/edit", HTTP_POST, [](AsyncWebServerRequest *request){
    //     String fodertidspunkter;
    //     if (request->hasParam("fodertidspunkter[]", true)) {
    //         fodertidspunkter = request->getParam("fodertidspunkter[]", true)->value();
    //         Serial.println(fodertidspunkter);

    //         request->send(200, "text/plain", "Kunne ikke finde ID!");
            
    //     }
    //     if (request->hasParam("fodertidspunkter", true)) {
    //         fodertidspunkter = request->getParam("fodertidspunkter", true)->value();
    //         Serial.println(fodertidspunkter);

    //         request->send(200, "text/plain", "Kunne ikke finde ID!");
    //     }
    //     else {
    //         request->send(400, "text/plain", "Intet ID er sendt!");
    //     }
    //     // request->redirect("/");
    //     request->send(400);
    // });

    // server.addHandler(new AsyncCallbackJsonWebHandler("/post3",[](AsyncWebServerRequest* request, JsonVariant& json) {
    //     if (not json.is<JsonObject>()) {
    //         request->send(400, "text/plain", "Not an object");
    //         return;
    //     }
    //     auto&& data = json.as<JsonObject>();

    //     auto&& foderautomater = data["foderautomater"].as<JsonArray>();

    //     Serial.println("Hej fra post3");
    //     Serial.println(data);
    //     for (JsonVariant value : foderautomater) {
    //         Serial.println(value.as<String>().c_str());
    //     }


    //     DateTime now = rtc.now();

    //     Serial.print("Time is: ");
    //     Serial.print(now.year(), DEC);
    //     Serial.print('/');
    //     Serial.print(now.month(), DEC);
    //     Serial.print('/');
    //     Serial.print(now.day(), DEC);
    //     Serial.print(" (");
    //     Serial.print(daysOfTheWeek[now.dayOfTheWeek()]);
    //     Serial.print(") ");
    //     Serial.print(now.hour(), DEC);
    //     Serial.print(':');
    //     Serial.print(now.minute(), DEC);
    //     Serial.print(':');
    //     Serial.print(now.second(), DEC);
    //     Serial.println();

    //     Serial.print(" since midnight 1/1/1970 = ");
    //     Serial.print(now.unixtime());
    //     Serial.print("s = ");
    //     Serial.print(now.unixtime() / 86400L);
    //     Serial.println("d");

    //     // if (not data["name"].is<String>()) {
    //     //   request->send(400, "text/plain", "name is not a string");
    //     //   return;
    //     // }
    //     // String name = data["name"].as<String>();

    //     request->send(200, "text/plain", "OK");

    // }));