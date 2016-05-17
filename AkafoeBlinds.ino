/*
  AkafoeBlinds

 A simple web server that shows the state of and controls the blinds in our office.
 using an Arduino Wiznet Ethernet shield.

 Circuit:
 * Ethernet shield attached to pins 10, 11, 12, 13
 * Digital outputs attached to pins 2-5 (Relais)

 created 20 March 2016
 by Thorsten Schroepel

 */

#include <SPI.h>
#include <Ethernet2.h>
#include <WebServer.h>

// Zuordnung an welchem Pin das entsprechende Relais angeschlossen ist
#define RELAIS_LINKS_HOCH 2
#define RELAIS_LINKS_RUNTER 3
#define RELAIS_RECHTS_HOCH 4
#define RELAIS_RECHTS_RUNTER 5

// Länge der Einschaltzeiten für langes und kurzes "Drücken"
#define SHORTPULSE 500
#define LONGPULSE 2500

// Länge der Fahrtzeit der Rolladen für die benutzerdefinierte Position (von oben gemessen!)
#define CUSTOMPULSE 48000-LONGPULSE

// Standardwerte für Webserver (Pfad "/" und Port 80)
#define PREFIX "/"
WebServer webserver(PREFIX, 80);

// Definition der MAC-Adresse
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x48, 0xF1 };

// Aktueller Status
//
//  0 = wartet auf Befehl von Webinterface
//  1 = Relais angezogen, wieder freigeben in "timer" Millisekunden
// 21 = Relais angezogen, Custom-Fahrt Links wurde angestartet
// 22 = Relais freigegeben, Wartezeit für Custom-Stellung Links hat angefangen
// 31 = Relais angezogen, Custom-Fahrt Rechts wurde angestartet
// 32 = Wartezeit für Custom-Stellung Rechts hat angefangen
// 41 = Relais angezogen, Custom-Fahrt Beide wurde angestartet
// 42 = Wartezeit für Custom-Stellung Beide hat angefangen
byte state = 0;

unsigned long timer = 0;

// Gibt alle Relais frei (lowaktiv!)
void relaisAllOff() {
  digitalWrite(RELAIS_LINKS_HOCH, HIGH);
  digitalWrite(RELAIS_LINKS_RUNTER, HIGH);
  digitalWrite(RELAIS_RECHTS_HOCH, HIGH);
  digitalWrite(RELAIS_RECHTS_RUNTER, HIGH);
}

// Zieht Relais "relais" an
void relaisOn(byte relais) {
  digitalWrite(relais, LOW);
}

// Zieht Relais "relais" für Zeit SHORTPULSE (in ms) an
void relaisShortOn(byte relais) {
  timer = millis() + SHORTPULSE;
  relaisOn(relais);
  Serial.println(F("State: 1"));
  state = 1;
}

// Zieht Relais "relais" für Zeit LONGPULSE (in ms) an
void relaisLongOn(byte relais, boolean custom) {
  timer = millis() + LONGPULSE;
  relaisOn(relais);
  if (!custom) {
    Serial.println(F("State: 1"));
    state = 1;
  }
}

// Überprüft, ob Timer bereits abgelaufen und ob ein Überlauf von millis() vorliegt
boolean checkMillis(unsigned long limit) {
  // Auf Überlauf von millis() prüfen
  if (timer - millis() > limit) {
    Serial.println(F("millis()-Ueberlauf festgestellt. Setze Relais zurück..."));
    Serial.println(F("State: 0"));
    state = 0;
    relaisAllOff();
    return false;
  // Ist der Timer schon abgelaufen?
  } else if (timer <= millis()) {
    return true;
  }
  return false;
}

// Wertet den übergebenen Button aus und reagiert entsprechend
void handleButton(char button[16]) {
  if (strcmp(button, "lup") == 0) {            // links hoch
    relaisLongOn(RELAIS_LINKS_HOCH, false);
  } else if (strcmp(button, "lstop") == 0) {   // links stop
    relaisShortOn(RELAIS_LINKS_HOCH);
  } else if (strcmp(button, "ldown") == 0) {   // links runter
    relaisLongOn(RELAIS_LINKS_RUNTER, false);
  } else if (strcmp(button, "rup") == 0) {     // rechts hoch
    relaisLongOn(RELAIS_RECHTS_HOCH, false);
  } else if (strcmp(button, "rstop") == 0) {   // rechts stop
    relaisShortOn(RELAIS_RECHTS_HOCH);
  } else if (strcmp(button, "rdown") == 0) {   // rechts runter
    relaisLongOn(RELAIS_RECHTS_RUNTER, false);
  } else if (strcmp(button, "bup") == 0) {     // beide hoch
    relaisLongOn(RELAIS_LINKS_HOCH, false);
    relaisLongOn(RELAIS_RECHTS_HOCH, false);
  } else if (strcmp(button, "bstop") == 0) {   // beide stop
    relaisShortOn(RELAIS_LINKS_HOCH);
    relaisShortOn(RELAIS_RECHTS_HOCH);
  } else if (strcmp(button, "bdown") == 0) {   // beide runter
    relaisLongOn(RELAIS_LINKS_RUNTER, false);
    relaisLongOn(RELAIS_RECHTS_RUNTER, false);
  } else if (strcmp(button, "lcustom") == 0) { // links custom
    Serial.println(F("State: 21"));
    state = 21;
    relaisLongOn(RELAIS_LINKS_RUNTER, true);
  } else if (strcmp(button, "rcustom") == 0) { // rechts custom
    Serial.println(F("State: 31"));
    state = 31;
    relaisLongOn(RELAIS_RECHTS_RUNTER, true);
  } else if (strcmp(button, "bcustom") == 0) { // beide custom
    Serial.println(F("State: 41"));
    state = 41;
    relaisLongOn(RELAIS_LINKS_RUNTER, true);
    relaisLongOn(RELAIS_RECHTS_RUNTER, true);
  } else {                                     // unbekannter Befehl
    Serial.println(F("Unbekannter Button wurde per POST uebermittelt!"));
  }
}

// Verarbeitet ankommende HTTP-Anfragen
void blindCmd(WebServer &server, WebServer::ConnectionType type, char *, bool) {
  Serial.print(F("HTTP-Verbindungsanfrage empfangen: "));
  switch (type) {
    case WebServer::POST: Serial.println(F("POST")); break;
    case WebServer::GET: Serial.println(F("GET")); break;
    default: Serial.println(F("Unbekannt"));
  }
  if (type == WebServer::POST) { // Anfrage vom Typ POST (normalerweise nur, wenn ein Button gedrückt wurde)
    bool repeat;
    char param[16], value[16];
    do {
      repeat = server.readPOSTparam(param, 16, value, 16);
      if (strcmp(param, "button") == 0) {
        Serial.print(F("Aktion \"button\" per POST empfangen: "));
        Serial.println(value);
        handleButton(value);
      }
    } while (repeat);
    return;
  }
  server.httpSuccess();
  if (type == WebServer::GET) { // Anfrage vom Typ GET (normale Anfrage - sende Startseite)
    Serial.println(F("Sende HTML-Seite an Client..."));
    sendWebPage(server);
  }
}

// Setup
void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  Serial.println(F("Rolladensteuerung v1.0 - (C)2016 by Thorsten Schroepel"));

  // Relais initialisieren
  Serial.println(F("Setze Relais auf definierten Zustand (AUS)..."));
  pinMode(RELAIS_LINKS_HOCH, OUTPUT);
  pinMode(RELAIS_LINKS_RUNTER, OUTPUT);
  pinMode(RELAIS_RECHTS_HOCH, OUTPUT);
  pinMode(RELAIS_RECHTS_RUNTER, OUTPUT);
  relaisAllOff();

  // Ethernet initialiseren (IP per DHCP)
  Serial.println(F("Initialisiere Ethernet-Port..."));
  if (Ethernet.begin(mac) == 0) {
    Serial.println(F("Fehler: Keine Adresse per DHCP zugewiesen!"));
  }
  // Webserver starten
  Serial.print(F("Starte HTTP-Server auf IP "));
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    Serial.print(Ethernet.localIP()[thisByte], DEC);
    if (thisByte < 3) Serial.print("."); else Serial.println("...");
  }
  webserver.setDefaultCommand(&blindCmd);
  webserver.begin();
}

// Loop
void loop() {
  Ethernet.maintain(); // DHCP-Lease erneuern
  webserver.processConnection(); // HTTP-Anfragen bearbeiten

  // "Mini-Statemachine"
  // Zeit ist abgelaufen - alle Relais freigeben und auf Ausgangszustand zurückkehren
  if (state == 1) {
    // Auf Überlauf von millis() aufpassen!
    if (checkMillis(10000)) {
      relaisAllOff();
      Serial.println(F("State: 0"));
      state = 0;
    }
  // Fahrt wurde gestartet, warte die Custom-Zeit ab 
  } else if ((state == 21) || (state == 31) || (state == 41)) {
    if (checkMillis(10000)) {
      relaisAllOff();
      timer = millis() + CUSTOMPULSE;
      Serial.print(F("State: "));
      Serial.println(state+1);
      state++;
    }
  } else if (state == 22) { // Custom-Zeit abgelaufen, Fahrt stoppen (links)
   if (checkMillis(100000)) relaisShortOn(RELAIS_LINKS_HOCH);
  } else if (state == 32) { // Custom-Zeit abgelaufen, Fahrt stoppen (rechts)
    if (checkMillis(100000)) relaisShortOn(RELAIS_RECHTS_HOCH);
  } else if (state == 42) { // Custom-Zeit abgelaufen, Fahrt stoppen (beide)
    if (checkMillis(100000)) {
      relaisShortOn(RELAIS_LINKS_HOCH);
      relaisShortOn(RELAIS_RECHTS_HOCH);
    }
  }
}

// Sende Standard-Webseite
void sendWebPage(WebServer &server) {
  P(message) =
    "<!DOCTYPE html><html><head>\n"
    "<title>Akafoe EDV-Rolladensteuerung</title>\n"
    "<link href='http://ajax.googleapis.com/ajax/libs/jqueryui/1.8.16/themes/base/jquery-ui.css' rel=stylesheet />\n"
    "<script src='http://ajax.googleapis.com/ajax/libs/jquery/1.6.4/jquery.min.js'></script>\n"
    "<script src='http://ajax.googleapis.com/ajax/libs/jqueryui/1.8.16/jquery-ui.min.js'></script>\n"
    "<script>\n"
    "$(document).ready(function(){\n"
    "$('#lup, #rup, #bup').button({text: false, icons: { primary: 'ui-icon-triangle-1-n'}});\n"
    "$('#lstop, #rstop, #bstop').button({label: 'Stop', icons: { primary: 'ui-icon-gear'}});\n"
    "$('#lcustom, #rcustom, #bcustom').button({label: 'Custom', icons: { primary: 'ui-icon-gear'}});\n"
    "$('#ldown, #rdown, #bdown').button({text: false, icons: { primary: 'ui-icon-triangle-1-s'}});\n"
    "$('#lup, #rup, #bup, #lstop, #rstop, #bstop, #ldown, #rdown, #bdown, #lcustom, #rcustom, #bcustom').click(function() {\n"
    "$.post('/', { button: $(this).attr('id')});\n"
    "});\n"
    "});\n"
    "</script>\n"
    "</head>\n"
    "<body style='font-size:62.5%;'>\n"
    "<table border='0'>\n"
    "<tr align='center'>\n"
    "<td width='150'>\n"
    "<h1>linke Rollos</h1>\n"
    "<div id=lup>.</div><br>\n"
    "<div id=lstop></div><br>\n"
    "<div id=lcustom></div><br>\n"
    "<div id=ldown>.</div>\n"
    "</td>\n"
    "<td width='150'>\n"
    "<h1>beide Rollos</h1>\n"
    "<div id=bup>.</div><br>\n"
    "<div id=bstop></div><br>\n"
    "<div id=bcustom></div><br>\n"
    "<div id=bdown>.</div>\n"
    "</td>\n"
    "<td width='150'>\n"
    "<h1>rechte Rollos</h1>\n"
    "<div id=rup>.</div><br>\n"
    "<div id=rstop></div><br>\n"
    "<div id=rcustom></div><br>\n"
    "<div id=rdown>.</div>\n"
    "</td>\n"
    "</tr>\n"
    "</table>\n"
    "</body>\n"
    "</html>\n";
    
  server.printP(message);
}

