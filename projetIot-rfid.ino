#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPAsyncWebServer.h>
#include <time.h>

const char* ssid = "OnePlus 9 5G";
const char* password = "w3kvm68b";
const char* FIREBASE_HOST = "https://projetiot-rfid-default-rtdb.europe-west1.firebasedatabase.app";

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;      // UTC+1 (Paris)
const int daylightOffset_sec = 3600;  // Heure d'été

AsyncWebServer server(80);

#define SDA_PIN 9
#define SCL_PIN 10
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define SCK_PIN 2
#define MISO_PIN 6
#define MOSI_PIN 7
#define SS_PIN 3
#define RST_PIN 8

MFRC522 rfid(SS_PIN, RST_PIN);

#define LED_ROUGE 4
#define LED_VERTE 5

struct Badge {
  byte uid[4];
  String nom;
};

#define MAX_BADGES 10
Badge badges[MAX_BADGES] = {
  {{0xDE, 0xAD, 0xBE, 0xEF}, "Sylvain (Admin)"}
};
int nbBadges = 1;

unsigned long debutAjout = 0;
const unsigned long DUREE_AJOUT = 10000;
bool attenteAffichee = false;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Historique RFID - Temps Réel</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background: #f4f6f9; color: #333; }
    h2 { color: #1a73e8; }
    .status-bar { margin-bottom: 15px; font-size: 0.9em; color: #555; }
    table { width: 100%%; border-collapse: collapse; background: white; box-shadow: 0 2px 5px rgba(0,0,0,0.1); border-radius: 6px; overflow: hidden; }
    th, td { padding: 12px 15px; text-align: left; border-bottom: 1px solid #e0e0e0; }
    th { background-color: #1a73e8; color: white; }
    tr:hover { background-color: #f9f9f9; }
    .autorise { color: #188038; font-weight: bold; }
    .refuse { color: #d93025; font-weight: bold; }
  </style>
</head>
<body>
  <h2>Contrôle d'Accès RFID - Temps Réel</h2>
  <div class="status-bar" id="statusBar">Synchronisation avec Firebase...</div>
  <table>
    <thead>
      <tr>
        <th>Date et Heure</th>
        <th>UID du Badge</th>
        <th>Nom du Porteur</th>
        <th>Statut</th>
      </tr>
    </thead>
    <tbody id="logTable">
      <tr><td colspan="4">Chargement de l'historique...</td></tr>
    </tbody>
  </table>

  <script>
    const firebaseUrl = "%FIREBASE_URL%/passages.json";
    const tableBody = document.getElementById("logTable");
    const statusBar = document.getElementById("statusBar");

    async function chargerHistorique() {
      try {
        const res = await fetch(firebaseUrl);
        const data = await res.json();
        statusBar.innerText = "Connecté en temps réel à Firebase";

        if (!data) {
          tableBody.innerHTML = "<tr><td colspan='4'>Aucun passage enregistré dans Firebase.</td></tr>";
          return;
        }

        let html = "";
        const cles = Object.keys(data).reverse();
        cles.forEach(cle => {
          const item = data[cle];
          if (!item || !item.uid) return;
          const classe = item.statut === "Autorisé" ? "autorise" : "refuse";
          html += `<tr>
            <td>${item.dateHeure || "-"}</td>
            <td><code>${item.uid}</code></td>
            <td>${item.nom || "Inconnu"}</td>
            <td class="${classe}">${item.statut}</td>
          </tr>`;
        });
        tableBody.innerHTML = html;
      } catch (err) {
        statusBar.innerText = "Erreur de lecture Firebase";
        console.error(err);
      }
    }

    chargerHistorique();
    setInterval(chargerHistorique, 2000);
  </script>
</body>
</html>
)rawliteral";

String processor(const String& var) {
  if (var == "FIREBASE_URL") return FIREBASE_HOST;
  return "";
}

void afficher(const char* l1, const char* l2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(l1);
  if (strlen(l2) > 0) {
    display.setCursor(0, 30);
    display.println(l2);
  }
  display.display();
}

String uidToString(byte *uid, byte len) {
  String s = "";
  for (byte i = 0; i < len; i++) {
    if (i > 0) s += " ";
    if (uid[i] < 0x10) s += "0";
    s += String(uid[i], HEX);
  }
  s.toUpperCase();
  return s;
}

String getDateHeure() {
  struct tm t;
  if (!getLocalTime(&t)) return "Heure non synchro";
  char buf[25];
  strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", &t);
  return String(buf);
}

int trouverBadge(byte *uid, byte len) {
  if (len != 4) return -1;
  for (int i = 0; i < nbBadges; i++) {
    if (memcmp(uid, badges[i].uid, 4) == 0) return i;
  }
  return -1;
}

void envoyerFirebase(String uid, String nom, String statut) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(FIREBASE_HOST) + "/passages.json";

  if (http.begin(client, url)) {
    http.addHeader("Content-Type", "application/json");

    String json = "{\"uid\":\"" + uid + "\",\"nom\":\"" + nom + "\",\"dateHeure\":\"" + getDateHeure() + "\",\"statut\":\"" + statut + "\"}";

    int code = http.POST(json);
    if (code > 0) {
      Serial.printf("Firebase OK (%d)\n", code);
    } else {
      Serial.printf("Erreur Firebase: %s\n", http.errorToString(code).c_str());
    }
    http.end();
  }
}

void enregistrerBadge() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  int idx = trouverBadge(rfid.uid.uidByte, rfid.uid.size);

  if (idx != -1) {
    afficher("Deja enregistre", badges[idx].nom.c_str());
    digitalWrite(LED_VERTE, HIGH);
  } else if (nbBadges >= MAX_BADGES) {
    afficher("Erreur memoire", "Liste pleine !");
    digitalWrite(LED_ROUGE, HIGH);
  } else {
    memcpy(badges[nbBadges].uid, rfid.uid.uidByte, 4);
    badges[nbBadges].nom = "Badge #" + String(nbBadges + 1);
    nbBadges++;

    afficher("Nouveau badge", "Ajoute !");
    digitalWrite(LED_VERTE, HIGH);
  }

  delay(1500);
  digitalWrite(LED_VERTE, LOW);
  digitalWrite(LED_ROUGE, LOW);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void verifierAcces() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  String uid = uidToString(rfid.uid.uidByte, rfid.uid.size);
  int idx = trouverBadge(rfid.uid.uidByte, rfid.uid.size);

  String nom = (idx != -1) ? badges[idx].nom : "Inconnu";
  String statut = (idx != -1) ? "Autorisé" : "Refusé";

  if (idx != -1) {
    Serial.printf("Accès autorisé : %s (%s)\n", nom.c_str(), uid.c_str());
    afficher("Acces autorise", nom.c_str());
    digitalWrite(LED_VERTE, HIGH);
  } else {
    Serial.printf("Accès refusé (%s)\n", uid.c_str());
    afficher("Acces refuse", "Badge inconnu");
    digitalWrite(LED_ROUGE, HIGH);
  }

  envoyerFirebase(uid, nom, statut);

  delay(1500);
  digitalWrite(LED_VERTE, LOW);
  digitalWrite(LED_ROUGE, LOW);
  afficher("En attente...", "Approchez un tag");

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_ROUGE, OUTPUT);
  pinMode(LED_VERTE, OUTPUT);
  digitalWrite(LED_ROUGE, LOW);
  digitalWrite(LED_VERTE, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR)) {
    Serial.println("Erreur OLED");
    while (1);
  }
  afficher("Connexion WiFi...");

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi connecte! IP: %s\n", WiFi.localIP().toString().c_str());

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html, processor);
  });
  server.begin();

  debutAjout = millis();
}

void loop() {
  unsigned long elapsed = millis() - debutAjout;

  if (elapsed < DUREE_AJOUT) {
    int sec = (DUREE_AJOUT - elapsed) / 1000 + 1;
    char buf[25];
    snprintf(buf, sizeof(buf), "Scannez (%d s)", sec);
    afficher("Ajout de badge :", buf);
    enregistrerBadge();
  } else {
    if (!attenteAffichee) {
      String ip = "IP: " + WiFi.localIP().toString();
      afficher("En attente...", ip.c_str());
      attenteAffichee = true;
    }
    verifierAcces();
  }
}
