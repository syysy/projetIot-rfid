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

// ---------- Wi-Fi & Firebase ----------
const char* ssid = "OnePlus 9 5G";
const char* password = "w3kvm68b";

// REMPLACEZ PAR L'URL DE VOTRE FIREBASE REALTIME DATABASE (sans "/" à la fin)
const char* FIREBASE_HOST = "https://projetiot-rfid-default-rtdb.europe-west1.firebasedatabase.app";

// Serveur NTP pour avoir la date et l'heure exactes (Heure de Paris)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;     // UTC+1
const int   daylightOffset_sec = 3600; // Heure d'été

AsyncWebServer server(80);

// ---------- Écran OLED (I2C) ----------
#define SDA_PIN 9   
#define SCL_PIN 10  
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDR   0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------- Lecteur RFID RC522 (SPI) ----------
#define SCK_PIN   2
#define MISO_PIN  6
#define MOSI_PIN  7
#define SS_PIN    3
#define RST_PIN   8

MFRC522 rfid(SS_PIN, RST_PIN);

// ---------- LED Multicolore ----------
#define LED_ROUGE_PIN 4
#define LED_VERT_PIN  5

// ---------- Structure des Badges (UID + Nom du porteur) ----------
struct BadgeInfo {
  byte uid[4];
  String nom;
};

#define MAX_BADGES 10
BadgeInfo badgesAutorises[MAX_BADGES] = {
  {{0xDE, 0xAD, 0xBE, 0xEF}, "Sylvain (Admin)"} // 1er badge par défaut
};
int nbBadgesAutorises = 1;

// Gestion du temps (10 secondes d'ajout de badges au démarrage)
unsigned long debutProgramme = 0;
const unsigned long DUREE_MODE_AJOUT = 10000;
bool messageAttenteAffiche = false;

// ---------- Page Web stockée en mémoire Flash (PROGMEM) ----------
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
        const reponse = await fetch(firebaseUrl);
        const data = await reponse.json();
        statusBar.innerText = "Connecté en temps réel à Firebase";

        if (!data) {
          tableBody.innerHTML = "<tr><td colspan='4'>Aucun passage enregistré dans Firebase.</td></tr>";
          return;
        }

        let html = "";
        const cles = Object.keys(data).reverse(); // Plus récent en haut
        cles.forEach(cle => {
          const item = data[cle];
          if (!item || !item.uid) return;
          const classeStatut = item.statut === "Autorisé" ? "autorise" : "refuse";
          html += `<tr>
            <td>${item.dateHeure || "-"}</td>
            <td><code>${item.uid}</code></td>
            <td>${item.nom || "Inconnu"}</td>
            <td class="${classeStatut}">${item.statut}</td>
          </tr>`;
        });
        tableBody.innerHTML = html;
      } catch (err) {
        statusBar.innerText = "Erreur de lecture Firebase : vérifiez l'URL ou les règles (.read: true)";
        console.error(err);
      }
    }

    // Premier chargement immédiat puis actualisation toutes les 2 secondes
    chargerHistorique();
    setInterval(chargerHistorique, 2000);
  </script>
</body>
</html>
)rawliteral";

// Remplace %FIREBASE_URL% dans le HTML par votre constante FIREBASE_HOST
String processor(const String& var) {
  if (var == "FIREBASE_URL") {
    return String(FIREBASE_HOST);
  }
  return String();
}

// ---------- Fonctions utilitaires ----------
void afficherMessage(const char* ligne1, const char* ligne2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(ligne1);
  if (strlen(ligne2) > 0) {
    display.setCursor(0, 30);
    display.println(ligne2);
  }
  display.display();
}

// Convertit l'UID en texte (ex: "DE AD BE EF")
String uidToString(byte *uid, byte taille) {
  String s = "";
  for (byte i = 0; i < taille; i++) {
    if (uid[i] < 0x10) s += "0";
    s += String(uid[i], HEX);
    if (i < taille - 1) s += " ";
  }
  s.toUpperCase();
  return s;
}

// Récupère la date et l'heure actuelles via NTP
String obtenirDateHeure() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Heure non synchro";
  }
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(buffer);
}

// Cherche l'indice du badge dans le tableau (-1 si inconnu)
int chercherIndiceBadge(byte *uid, byte tailleUid) {
  if (tailleUid != 4) return -1;
  for (int i = 0; i < nbBadgesAutorises; i++) {
    bool correspond = true;
    for (byte j = 0; j < 4; j++) {
      if (uid[j] != badgesAutorises[i].uid[j]) {
        correspond = false;
        break;
      }
    }
    if (correspond) return i;
  }
  return -1;
}

// ---------- Envoi automatique vers Firebase ----------
void envoyerVersFirebase(String uidStr, String nomPorteur, String statutAcces) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Erreur : Wi-Fi déconnecté, envoi Firebase annulé."));
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // Ignore le certificat SSL pour simplifier la requête HTTPS

  HTTPClient http;
  String url = String(FIREBASE_HOST) + "/passages.json";
  
  if (http.begin(client, url)) {
    http.addHeader("Content-Type", "application/json");
    String dateHeure = obtenirDateHeure();

    // Construction de l'objet JSON
    String jsonPayload = "{";
    jsonPayload += "\"uid\":\"" + uidStr + "\",";
    jsonPayload += "\"nom\":\"" + nomPorteur + "\",";
    jsonPayload += "\"dateHeure\":\"" + dateHeure + "\",";
    jsonPayload += "\"statut\":\"" + statutAcces + "\"";
    jsonPayload += "}";

    int httpResponseCode = http.POST(jsonPayload);
    if (httpResponseCode > 0) {
      Serial.print(F("Passage enregistré sur Firebase (Code HTTP "));
      Serial.print(httpResponseCode);
      Serial.println(F(")"));
    } else {
      Serial.print(F("Erreur HTTP Firebase : "));
      Serial.println(http.errorToString(httpResponseCode));
    }
    http.end();
  }
}

// ---------- Enregistrement d'un badge (10 premières secondes) ----------
void writeDataToRFIDTag() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  int indice = chercherIndiceBadge(rfid.uid.uidByte, rfid.uid.size);

  if (indice != -1) {
    afficherMessage("Deja enregistre", badgesAutorises[indice].nom.c_str());
    digitalWrite(LED_VERT_PIN, HIGH);
  } 
  else if (nbBadgesAutorises >= MAX_BADGES) {
    afficherMessage("Erreur Memoire", "Liste pleine !");
    digitalWrite(LED_ROUGE_PIN, HIGH);
  } 
  else {
    for (byte i = 0; i < 4; i++) {
      badgesAutorises[nbBadgesAutorises].uid[i] = rfid.uid.uidByte[i];
    }
    badgesAutorises[nbBadgesAutorises].nom = "Badge #" + String(nbBadgesAutorises + 1);
    nbBadgesAutorises++;

    afficherMessage("Nouveau badge", "Ajoute avec succes!");
    digitalWrite(LED_VERT_PIN, HIGH);
  }

  delay(1500);
  digitalWrite(LED_VERT_PIN, LOW);
  digitalWrite(LED_ROUGE_PIN, LOW);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ---------- Lecture, Vérification et Envoi Firebase (après 10 secondes) ----------
void readingDataFromRFIDTag() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  String uidStr = uidToString(rfid.uid.uidByte, rfid.uid.size);
  int indice = chercherIndiceBadge(rfid.uid.uidByte, rfid.uid.size);

  String nomPorteur;
  String statutAcces;

  if (indice != -1) {
    nomPorteur = badgesAutorises[indice].nom;
    statutAcces = "Autorisé";
    Serial.println("-> Accès autorisé : " + nomPorteur + " (" + uidStr + ")");
    afficherMessage("Acces autorise", nomPorteur.c_str());
    digitalWrite(LED_VERT_PIN, HIGH);
    digitalWrite(LED_ROUGE_PIN, LOW);
  } else {
    nomPorteur = "Inconnu";
    statutAcces = "Refusé";
    Serial.println("-> Accès refusé (" + uidStr + ")");
    afficherMessage("Acces refuse", "Badge inconnu");
    digitalWrite(LED_VERT_PIN, LOW);
    digitalWrite(LED_ROUGE_PIN, HIGH);
  }

  // Envoi immédiat dans Firebase Realtime Database
  envoyerVersFirebase(uidStr, nomPorteur, statutAcces);

  delay(1500);

  digitalWrite(LED_VERT_PIN, LOW);
  digitalWrite(LED_ROUGE_PIN, LOW);
  afficherMessage("En attente...", "Approchez un tag");

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ---------- Initialisations ----------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_ROUGE_PIN, OUTPUT);
  pinMode(LED_VERT_PIN, OUTPUT);
  digitalWrite(LED_ROUGE_PIN, LOW);
  digitalWrite(LED_VERT_PIN, LOW);

  // 1. Écran OLED
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR)) {
    Serial.println(F("Échec écran OLED"));
    while (true);
  }
  afficherMessage("Connexion WiFi...");

  // 2. Lecteur RFID
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();
  delay(10);

  // 3. Connexion Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connecté !");
  Serial.print("Adresse IP du serveur Web : ");
  Serial.println(WiFi.localIP());

  // 4. Synchronisation de l'heure NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // 5. Démarrage du serveur Web AsyncWebServer
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html, processor);
  });
  server.begin();
  Serial.println("Serveur HTTP démarré !");

  // Lancement du chronomètre de 10 secondes APRÈS la connexion Wi-Fi
  debutProgramme = millis();
}

void loop() {
  unsigned long tempsEcoule = millis() - debutProgramme;

  if (tempsEcoule < DUREE_MODE_AJOUT) {
    int secondesRestantes = (DUREE_MODE_AJOUT - tempsEcoule) / 1000 + 1;
    char buffer[25];
    snprintf(buffer, sizeof(buffer), "Scannez (%d s)", secondesRestantes);
    afficherMessage("Ajout de badge :", buffer);
    writeDataToRFIDTag();
  } else {
    if (!messageAttenteAffiche) {
      String ipStr = "IP: " + WiFi.localIP().toString();
      afficherMessage("En attente...", ipStr.c_str());
      messageAttenteAffiche = true;
    }
    readingDataFromRFIDTag();
  }
}