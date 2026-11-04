#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MFRC522.h>
#include <SPI.h>

// ---------- Écran ----------
#define SDA_PIN 9   
#define SCL_PIN 10  

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDR   0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --------- Lecteur RFID RC522 -------------
#define SCK_PIN  2
#define MISO_PIN 6
#define MOSI_PIN 7
#define SS_PIN   3
#define RST_PIN  8

// ---- LED ----
#define LED_ROUGE_PIN 4
#define LED_VERT_PIN  5

MFRC522 rfid(SS_PIN, RST_PIN);

void setupEcran() {
  // Init
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR)) {
    Serial.println(F("Échec !"));
    while (true); 
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("En attente ... ");
  display.display();
}

void setupRFID() {
  // 1. Initialiser le bus SPI matériel avec les broches libres
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);

  // 2. Initialiser la puce RC522
  rfid.PCD_Init();
  delay(10); // Léger délai de stabilisation de l'antenne

  // 3. (Optionnel) Vérifier la communication avec la puce RC522
  byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  if (version == 0x00 || version == 0xFF) {
    Serial.println(F("Erreur de liaison SPI avec le RC522 (vérifiez câblage/alimentation 3.3V)"));
  } else {
    Serial.print(F("MFRC522 initialisé avec succès ! Version du firmware : 0x"));
    Serial.println(version, HEX);
    Serial.println(F("Approche une carte..."));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Initialisation...");

  pinMode(LED_ROUGE_PIN, OUTPUT);
  pinMode(LED_VERT_PIN, OUTPUT);
  digitalWrite(LED_ROUGE_PIN, LOW);
  digitalWrite(LED_VERT_PIN, LOW);

  setupEcran();
  setupRFID();
}

void loop() {
  // Attente d'un badge
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Action : Allumer la LED verte et afficher l'UID
  digitalWrite(LED_VERT_PIN, HIGH);
  digitalWrite(LED_ROUGE_PIN, HIGH);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Badge detecte !");
  display.print("UID: ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    display.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
    display.print(rfid.uid.uidByte[i], HEX);
  }
  display.display();

  delay(1500);

  // Réinitialisation
  digitalWrite(LED_VERT_PIN, LOW);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("En attente...");
  display.display();

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}