#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <MFRC522.h>

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

// ---------- Liste dynamique des UID autorisés ----------
#define MAX_BADGES 10
byte badgesAutorises[MAX_BADGES][4] = {
  {0xDE, 0xAD, 0xBE, 0xEF} // 1er badge par défaut (optionnel)
};
int nbBadgesAutorises = 1; // Nombre actuel de badges dans la liste

// Gestion du temps (10 secondes d'enregistrement au démarrage)
unsigned long debutProgramme = 0;
const unsigned long DUREE_MODE_AJOUT = 10000; // 10 secondes
bool messageAttenteAffiche = false;

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

void setupEcran() {
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR)) {
    Serial.println(F("Échec d'initialisation de l'écran SSD1306"));
    while (true);
  }
  afficherMessage("Initialisation...");
}

void setupRFID() {
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();
  delay(10);
}

// Vérifie si l'UID est déjà présent dans le tableau
bool estBadgeAutorise(byte *uid, byte tailleUid) {
  if (tailleUid != 4) return false;
  for (int i = 0; i < nbBadgesAutorises; i++) {
    bool correspond = true;
    for (byte j = 0; j < 4; j++) {
      if (uid[j] != badgesAutorises[i][j]) {
        correspond = false;
        break;
      }
    }
    if (correspond) return true;
  }
  return false;
}

// ---------- Enregistrement d'un badge dans le tableau (10 premières secondes) ----------
void writeDataToRFIDTag() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  // 1. Vérifier si le badge est déjà enregistré
  if (estBadgeAutorise(rfid.uid.uidByte, rfid.uid.size)) {
    Serial.println(F("Ce badge est déjà dans la liste !"));
    afficherMessage("Deja enregistre", "Badge connu");
    digitalWrite(LED_VERT_PIN, HIGH);
  } 
  // 2. Vérifier s'il reste de la place dans le tableau
  else if (nbBadgesAutorises >= MAX_BADGES) {
    Serial.println(F("Tableau plein ! Impossible d'ajouter un badge."));
    afficherMessage("Erreur Memoire", "Liste pleine !");
    digitalWrite(LED_ROUGE_PIN, HIGH);
  } 
  // 3. Ajouter l'UID du nouveau badge dans le tableau
  else {
    for (byte i = 0; i < 4; i++) {
      badgesAutorises[nbBadgesAutorises][i] = rfid.uid.uidByte[i];
    }
    nbBadgesAutorises++;

    Serial.print(F("Nouveau badge ajouté ! Total enregistrés : "));
    Serial.println(nbBadgesAutorises);
    afficherMessage("Nouveau badge", "Ajoute avec succes!");
    digitalWrite(LED_VERT_PIN, HIGH);
  }

  delay(1500);
  digitalWrite(LED_VERT_PIN, LOW);
  digitalWrite(LED_ROUGE_PIN, LOW);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ---------- Lecture et vérification d'accès (après 10 secondes) ----------
void readingDataFromRFIDTag() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  if (estBadgeAutorise(rfid.uid.uidByte, rfid.uid.size)) {
    Serial.println(F("-> Accès autorisé"));
    afficherMessage("Acces autorise", "Bienvenue !");
    digitalWrite(LED_VERT_PIN, HIGH);
    digitalWrite(LED_ROUGE_PIN, LOW);
  } else {
    Serial.println(F("-> Accès refusé"));
    afficherMessage("Acces refuse", "Badge inconnu");
    digitalWrite(LED_VERT_PIN, LOW);
    digitalWrite(LED_ROUGE_PIN, HIGH);
  }

  delay(2000);

  digitalWrite(LED_VERT_PIN, LOW);
  digitalWrite(LED_ROUGE_PIN, LOW);
  afficherMessage("En attente...", "Approchez un tag");

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_ROUGE_PIN, OUTPUT);
  pinMode(LED_VERT_PIN, OUTPUT);
  digitalWrite(LED_ROUGE_PIN, LOW);
  digitalWrite(LED_VERT_PIN, LOW);

  setupEcran();
  setupRFID();

  debutProgramme = millis();
  Serial.println(F("=== Mode ajout de badges actif pendant 10 secondes ==="));
}

void loop() {
  unsigned long tempsEcoule = millis() - debutProgramme;

  // Pendant les 10 premières secondes : enregistrement dans le tableau
  if (tempsEcoule < DUREE_MODE_AJOUT) {
    int secondesRestantes = (DUREE_MODE_AJOUT - tempsEcoule) / 1000 + 1;
    char buffer[25];
    snprintf(buffer, sizeof(buffer), "Scannez (%d s)", secondesRestantes);
    afficherMessage("Ajout de badge :", buffer);
    
    writeDataToRFIDTag();
  } 
  // Après 10 secondes : contrôle d'accès normal
  else {
    if (!messageAttenteAffiche) {
      Serial.println(F("=== Fin du mode ajout. Passage en contrôle d'accès ==="));
      afficherMessage("En attente...", "Approchez un tag");
      messageAttenteAffiche = true;
    }
    
    readingDataFromRFIDTag();
  }
}