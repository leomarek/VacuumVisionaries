
//relevant import statements
#include <MusicDefinitions.h>
#include <XT_DAC_Audio.h>
#include "lowbat.h"
#include "pumperror.h"
#include "tankfull.h"
#include <vector>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include "ESP32_SPI_9341.h"
#include <driver/dac.h>

// Define Pins
#define SD_SCK 18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS 5
#define LIGHT_ADC 34
#define HG7881_B_IA 22 // IO22 --> Motor B Input A --> MOTOR B +
#define HG7881_B_IB 27 // IO27 --> Motor B Input B --> MOTOR B -
#define FL_PIN 35 // float switch pin
#define MOTOR_B_PWM HG7881_B_IA // Motor B PWM Speed
#define MOTOR_B_DIR HG7881_B_IB // Motor B Direction

// create an object of type XT_Wav_Class for each error to be used by the dac audio class (below), passing wav data as parameter.
XT_Wav_Class pumperror(pumperrorwav);     
XT_Wav_Class lowbat(lowbatwav);
XT_Wav_Class tankfull(tankfullwav);
                                      
XT_DAC_Audio_Class DacAudio(26,0); // Create the main player class object using GPIO 26, one of the DAC pins and timer 0
XT_Sequence_Class Sequence; // The sequence object to add sounds to

// Pins for integrated led on back of sunton board
int led_pin[3] = {17, 4, 16};

// Initialize SPI for SD card
SPIClass SD_SPI;

// Initialize lcd using LGFX from the ESP32_... .h
LGFX lcd;

// Global variables for method usage
boolean pumpState = false;

// Represents wether any error is active or no error is active
boolean activeError = false;

// Represents status of battery, float switch, and pump errors respectively
String activeErrors[3] = {"", "", ""};

// Ticker used to iterate through active errors
double curErr = 0;

// Float switch state (digital)
int floatState;
// Previous float switch state
int prevState = HIGH;

// Default pressure setpoint (mmHg)
int setpoint = 100;

// Default error message
String errorMessage = "";

// Array represents location of latest touch point
int pos[2] = {0, 0};

// Timer to apropriately trigger error messages
long timer = millis();

void setup(void)
{
    // Set led pins to output mode
    pinMode(led_pin[0], OUTPUT);
    pinMode(led_pin[1], OUTPUT);
    pinMode(led_pin[2], OUTPUT);

    // Begin serial
    Serial.begin(115200);

    // Set motor controller pins to output mode
    pinMode( MOTOR_B_DIR, OUTPUT );
    pinMode( MOTOR_B_PWM, OUTPUT );

    // Set motor pwm to low
    digitalWrite( MOTOR_B_DIR, LOW );
    digitalWrite( MOTOR_B_PWM, LOW );

    // Set float switch pin to input with internal pullup resistor
    pinMode(FL_PIN, INPUT_PULLUP);

    // Run lcd init method
    lcd.init();

    // Initialize and test SD card
    sd_init();
    sd_test();

    // set lcd to horizintal orientation
    lcd.setRotation(1);

    delay(1000);
    init_gui();
}

void loop(void)
{
    DacAudio.FillBuffer(); // Fill the sound buffer with data
    floatState = digitalRead(FL_PIN); // Read the float switch state
    Serial.println(floatState);
    if(floatState == LOW){
      activeErrors[1] = "Tank Full";
      activeError = true;
    }
    else if (floatState!=prevState){
      activeErrors[1] = "";
      activeError = false;
      update_gui(setpoint, "");
    }
    prevState = floatState;
    if (lcd.getTouch(&pos[0], &pos[1]))
        {
            if (pos[0] < 1100 && pos[1] < -2000){
                Serial.println("up");
                setpoint=setpoint+5;
            }
            if (pos[0] < 1100 && pos[1] > -1500){
                Serial.println("down");
                setpoint=setpoint-5;
            }
            if (pos[0] > 1750 && pos[1] < -3000){
                pumpState = !pumpState;
                delay(25);
            }
            setpoint = constrain(setpoint, 0, 150);
            if(pumpState){
              digitalWrite( MOTOR_B_DIR, LOW );
              int pwm = map(setpoint,0,150,0,255);
              digitalWrite( MOTOR_B_PWM, pwm);
            }
            else{
              digitalWrite( MOTOR_B_PWM, LOW);
            }
            if((int)curErr == curErr and activeError){
              errorMessage = activeErrors[(int)curErr];
            }
            else{
              errorMessage = "";
            }
            update_gui(setpoint,errorMessage);
            Serial.print(pos[0]);
            Serial.print(" - ");
            Serial.println(pos[1]);
            delay(100);
        }


  if(millis()-timer>=1200){
    Sequence.RemoveAllPlayItems(); // Clear out any previous playlist
    
    // Increment the error variable, ensure it never exceeds three
    curErr+=0.5; 
    if (curErr>=3){
    curErr-=3;
    }

    // If there is an active error
    if(activeError){
      
      // This is true for every other counter
      if((int)curErr == curErr){
        
      // For any non empty error message
      errorMessage = activeErrors[(int)curErr];
      if(errorMessage.length()!=0){
          
        // Add corresponding audio alert to sequence
        switch((int)curErr){
          case 0: Sequence.AddPlayItem(&lowbat);break;
          case 2: Sequence.AddPlayItem(&pumperror);break;
          case 1: Sequence.AddPlayItem(&tankfull);break;
        }
  
        // Update the GUI with relevant error message
        update_gui(setpoint, errorMessage);
        }
      }
    }

    // Play sequence and update timer
    DacAudio.Play(&Sequence);
    timer = millis();
  }
}

void init_gui(){
  // Set font and update GUI
  lcd.setFont(&fonts::FreeSansBold9pt7b);
  update_gui(setpoint, "");
}

void update_gui(int setpoint, String error){

    // Start with blank white screen
    lcd.fillScreen(TFT_WHITE);

    // Use black text
    lcd.setTextColor(TFT_BLACK);

    // If the pump is on, draw pump enabled bar
    if(pumpState){
      lcd.fillRoundRect( 10, 10, 180, 45, 4, TFT_GREEN);
      lcd.setCursor(20, 20);
      lcd.setTextSize(1.3);
      lcd.print("Pump Enabled");

      // Set internal LED to green
      digitalWrite(led_pin[0], HIGH);
      digitalWrite(led_pin[1], HIGH);
      digitalWrite(led_pin[2], LOW);
    }

    // If the pump is not on, draw pump disabled bar
    else{
      lcd.fillRoundRect( 10, 10, 180, 45, 4, TFT_RED);
      lcd.setCursor(20, 20);
      lcd.setTextSize(1.3);
      lcd.setTextColor(TFT_WHITE);
      lcd.print("Pump Disabled");
      lcd.setTextColor(TFT_BLACK);

      // Set internal led to red
      digitalWrite(led_pin[0], HIGH);
      digitalWrite(led_pin[1], LOW);
      digitalWrite(led_pin[2], HIGH);
    }

    // Print setpoint in large text
    lcd.setTextSize(5);
    lcd.setCursor(10, 75);
    lcd.print(String(setpoint));

    // Account for spacing on two digit numbers
    if(setpoint<100){
      lcd.print("  ");
    }

    // Draw mmHg label to right of reading
    lcd.setTextSize(2);
    lcd.println(" mm");
    lcd.setTextSize(5);
    lcd.print("       ");
    lcd.setTextSize(2);
    lcd.println("Hg");

    // Draw triangles for adjustment buttons
    lcd.fillTriangle  ( 245, 70, 275, 25, 305, 70, TFT_BLACK);
    lcd.fillTriangle  ( 245, 160, 275, 205, 305, 160, TFT_BLACK);

    // Print error message as given by the caller method
    lcd.setTextColor(TFT_RED);
    lcd.setCursor(20, 165);
    lcd.setTextSize(2);
    lcd.print(error);

    // Wait to prevent flashing
    delay(5);
}

// initialize SD card connection over SPI
void sd_init()
{
    SD_SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
    if (!SD.begin(SD_CS, SD_SPI, 40000000))
    {
        Serial.println("Card Mount Failed");
        lcd.setCursor(10, 10);
        lcd.println("SD Card Failed");
        while (1)
            delay(1000);
    }
    else
    {
        Serial.println("Card Mount Successed");
    }

    Serial.println("SD init over.");
}

//test sd card SPI connection
void sd_test()
{
    uint8_t cardType = SD.cardType();

    if (cardType == CARD_NONE)
    {
        Serial.println("No SD card attached");
        return;
    }

    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC)
    {
        Serial.println("MMC");
    }
    else if (cardType == CARD_SD)
    {
        Serial.println("SDSC");
    }
    else if (cardType == CARD_SDHC)
    {
        Serial.println("SDHC");
    }
    else
    {
        Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);

    listDir(SD, "/", 0);
}

// directory scanning sample method
void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if (!root)
    {
        Serial.println("Failed to open directory");
        return;
    }
    if (!root.isDirectory())
    {
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels)
            {
                listDir(fs, file.path(), levels - 1);
            }
        }
        else
        {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

// Display image from file
int print_img(fs::FS &fs, String filename, int x, int y)
{

    File f = fs.open(filename, "r");
    if (!f)
    {
        Serial.println("Failed to open file for reading");
        f.close();
        return 0;
    }

    f.seek(54);
    int X = x;
    int Y = y;
    uint8_t RGB[3 * X];
    for (int row = 0; row < Y; row++)
    {
        f.seek(54 + 3 * X * row);
        f.read(RGB, 3 * X);

        lcd.pushImage(0, row, X, 1, (lgfx::rgb888_t *)RGB);
    }

    f.close();
    return 0;
}

// example used for touchscreen calibration
void touch_calibration()
{
    lcd.fillScreen(TFT_YELLOW);

    lcd.setTextColor(TFT_BLACK);
    lcd.setTextSize(2);
    lcd.setCursor(70, 110);
    lcd.println("SCREEN");
    lcd.setCursor(70, 150);
    lcd.println("CALIBRATION");

    // タッチを使用する場合、キャリブレーションを行います。画面の四隅に表示される矢印の先端を順にタッチしてください。
    std::uint16_t fg = TFT_WHITE;
    std::uint16_t bg = TFT_BLACK;
    if (lcd.isEPD())
        std::swap(fg, bg);
    lcd.calibrateTouch(nullptr, fg, bg, std::max(lcd.width(), lcd.height()) >> 3);
}

// helper method for touch calibration
void touch_continue() 
{
    lcd.fillScreen(TFT_YELLOW);

    lcd.fillRect(60, 100, 120, 120, TFT_BLACK);
    lcd.setCursor(70, 110);
    lcd.println(" TOUCH");
    lcd.setCursor(70, 130);
    lcd.println("  TO");
    lcd.setCursor(70, 150);
    lcd.println("CONTINUE");

    int pos[2] = {0, 0};

    while (1)
    {
        if (lcd.getTouch(&pos[0], &pos[1]))
        {
            if (pos[0] > 60 && pos[0] < 180 && pos[1] > 120 && pos[1] < 240)
                break;
            delay(100);
        }
    }
}
}
