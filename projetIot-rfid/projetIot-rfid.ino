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
MFRC522::MIFARE_Key key;

// ---------- LED Multicolore ----------
#define LED_ROUGE_PIN 4
#define LED_VERT_PIN  5

// ---------- Liste des UID autorisés (4 octets) ----------
// Remplacez ces valeurs par l'UID réel de votre badge (visible dans le moniteur série)
const byte badgesAutorises[][4] = {
  {0xDE, 0xAD, 0xBE, 0xEF}, // Badge 1
  {0x12, 0x34, 0x56, 0x78}  // Badge 2
};
const int nbBadgesAutorises = sizeof(badgesAutorises) / sizeof(badgesAutorises[0]);

// Variables de gestion du temps (10 secondes d'écriture au lancement)
unsigned long debutProgramme = 0;
const unsigned long DUREE_MODE_ECRITURE = 10000; // 10 000 ms = 10 s
bool messageAttenteAffiche = false;

// Fonction utilitaire pour afficher facilement sur l'OLED
void afficherMessage(const char* ligne1, const char* ligne2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
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
  
  // Préparation de la clé par défaut MIFARE (0xFF partout) pour l'écriture
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
}

// Vérifie si l'UID scanné fait partie de la liste autorisée
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

// ---------- Fonction d'écriture (active les 10 premières secondes) ----------
void writeDataToRFIDTag() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  Serial.println(F("Badge détecté en mode écriture..."));
  afficherMessage("Mode Ecriture", "Ecriture en cours...");

  byte bloc = 4; // Bloc 4 (Secteur 1) : zone de données utilisateur sans risque
  byte donneesAEcrire[16] = {"Badge_IoT_2026"}; // 16 octets max par bloc

  // 1. Authentification sur le secteur
  MFRC522::StatusCode status = rfid.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A, bloc, &key, &(rfid.uid)
  );
  
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Erreur d'authentification : "));
    Serial.println(rfid.GetStatusCodeName(status));
    afficherMessage("Erreur Ecriture", "Auth echouee");
    digitalWrite(LED_ROUGE_PIN, HIGH);
    delay(1500);
    digitalWrite(LED_ROUGE_PIN, LOW);
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  // 2. Écriture des 16 octets dans le bloc 4
  status = rfid.MIFARE_Write(bloc, donneesAEcrire, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Échec de l'écriture : "));
    Serial.println(rfid.GetStatusCodeName(status));
    afficherMessage("Erreur Ecriture", "Echec ecriture");
    digitalWrite(LED_ROUGE_PIN, HIGH);
  } else {
    Serial.println(F("Données écrites avec succès dans le bloc 4 !"));
    afficherMessage("Mode Ecriture", "Succes !");
    digitalWrite(LED_VERT_PIN, HIGH);
  }

  delay(2000);
  digitalWrite(LED_ROUGE_PIN, LOW);
  digitalWrite(LED_VERT_PIN, LOW);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ---------- Fonction de lecture / contrôle d'accès (après 10 secondes) ----------
void readingDataFromRFIDTag() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Affichage de l'UID dans le moniteur série pour vous aider à récupérer vos codes
  Serial.print(F("UID scanné :"));
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0x0" : " 0x");
    Serial.print(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();

  // Comparaison avec la liste des badges autorisés
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

  // Extinction des LED et retour à l'écran d'attente
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
  Serial.println(F("=== Mode écriture actif pendant 10 secondes ==="));
}

void loop() {
  unsigned long tempsEcoule = millis() - debutProgramme;

  // Phase 1 : Les 10 premières secondes -> Mode écriture
  if (tempsEcoule < DUREE_MODE_ECRITURE) {
    int secondesRestantes = (DUREE_MODE_ECRITURE - tempsEcoule) / 1000 + 1;
    char buffer[25];
    snprintf(buffer, sizeof(buffer), "Reste : %d s", secondesRestantes);
    afficherMessage("Mode Ecriture...", buffer);
    
    writeDataToRFIDTag();
  } 
  // Phase 2 : Après 10 secondes -> Mode contrôle d'accès
  else {
    if (!messageAttenteAffiche) {
      Serial.println(F("=== Fin du mode écriture. Passage en contrôle d'accès ==="));
      afficherMessage("En attente...", "Approchez un tag");
      messageAttenteAffiche = true;
    }
    
    readingDataFromRFIDTag();
  }
}