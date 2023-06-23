#include <MusicDefinitions.h>
#include <XT_DAC_Audio.h>
#include "lowbat.h"
#include "pumperror.h"
#include "tankfull.h"


//测试通过

#include <vector>
#include <SPI.h>
#include <SD.h>
#include <FS.h>

#include "ESP32_SPI_9341.h"
#include <driver/dac.h>

#define SD_SCK 18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS 5

#define LIGHT_ADC 34

 
// wired connections
#define HG7881_B_IA 22 // D10 --> Motor B Input A --> MOTOR B +
#define HG7881_B_IB 27 // D11 --> Motor B Input B --> MOTOR B -
//#define FL_PIN 22
// functional connections
#define MOTOR_B_PWM HG7881_B_IA // Motor B PWM Speed
#define MOTOR_B_DIR HG7881_B_IB // Motor B Direction
 
// the actual values for "fast" and "slow" depend on the motor
//#define PWM_SLOW 150  // arbitrary slow speed PWM duty cycle
//#define PWM_FAST 255 // arbitrary fast speed PWM duty cycle
#define DIR_DELAY 1000 // brief delay for abrupt motor changes

XT_Wav_Class pumperror(pumperrorwav);     // create an object of type XT_Wav_Class that is used by the dac audio class (below), passing wav data as parameter.
XT_Wav_Class lowbat(lowbatwav);
XT_Wav_Class tankfull(tankfullwav);
                                      
XT_DAC_Audio_Class DacAudio(26,0);    // Create the main player class object. 
                                      // Use GPIO 25, one of the 2 DAC pins and timer 0
XT_Sequence_Class Sequence;               // The sequence object, you add your sounds above to this object (see setup below)

int led_pin[3] = {17, 4, 16};
#define FL_PIN 35

SPIClass SD_SPI;

LGFX lcd;

//PImage up;

void setup(void)
{
    pinMode(led_pin[0], OUTPUT);
    pinMode(led_pin[1], OUTPUT);
    pinMode(led_pin[2], OUTPUT);

    Serial.begin(115200);
    pinMode( MOTOR_B_DIR, OUTPUT );
    pinMode( MOTOR_B_PWM, OUTPUT );
    digitalWrite( MOTOR_B_DIR, LOW );
    digitalWrite( MOTOR_B_PWM, LOW );
    pinMode(FL_PIN, INPUT_PULLUP);
    // pinMode(LCD_BL, OUTPUT);
    // digitalWrite(LCD_BL, HIGH);

    lcd.init();
    sd_init();
    sd_test();

    //up = lcd.loadImage("up.bmp");
    //down = lcd.loadImage("down.bmp");

    lcd.setRotation(1);
    print_img(SD, "/up.bmp", 240, 320);

    delay(1000);
    init_gui();
}

static int colors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW};

int i = 0;
long runtime_0 = 0;
long runtime_1 = 0;

void loop(void)
{
    init_gui();
    if ((millis() - runtime_0) > 1000)
    {
        led_set(i);
        lcd.fillScreen(colors[i++]);

        if (i > 3)
        {
            i = 0;
        }

        runtime_0 = millis();
    }

    if ((millis() - runtime_1) > 300)
    {
        int adc_value = analogRead(LIGHT_ADC);
        Serial.printf("ADC:%d\n", adc_value);
        if (adc_value > 50)
            lcd.setBrightness(50);
        else
            lcd.setBrightness(255);
        runtime_1 = millis();
    }
    delay(10);
}

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

boolean pumpState = false;
//represents status of battery, float switch, and pump errors respectively
boolean activeError = false;
String activeErrors[3] = {"", "", ""};
double curErr = 0;
int floatState;
int prevState = HIGH;

void init_gui(){
  int setpoint = 100;
  lcd.setFont(&fonts::FreeSansBold9pt7b);
  String errorMessage = "";
  update_gui(setpoint, "");
  int pos[2] = {0, 0};
  long timer = millis();
  while(true){
    DacAudio.FillBuffer();                // Fill the sound buffer with data
    floatState = digitalRead(FL_PIN);
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
    Sequence.RemoveAllPlayItems();            // Clear out any previous playlist
    curErr+=0.5;
    if (curErr>=3){
    curErr-=3;
    }
    //Serial.println(curErr);
        
    if(activeError){
      if((int)curErr == curErr){
      errorMessage = activeErrors[(int)curErr];
      if(errorMessage.length()!=0){
      switch((int)curErr)
      {
        case 0: Sequence.AddPlayItem(&lowbat);break;
        case 2: Sequence.AddPlayItem(&pumperror);break;
        case 1: Sequence.AddPlayItem(&tankfull);break;

      }
      update_gui(setpoint, errorMessage);
      }
      
      }
    }
    DacAudio.Play(&Sequence);
    timer = millis();
  }
  }

}
void update_gui(int setpoint, String error){
    lcd.fillScreen(TFT_WHITE);

    lcd.setTextColor(TFT_BLACK);
    if(pumpState){
      lcd.fillRoundRect( 10, 10, 180, 45, 4, TFT_GREEN);
      lcd.setCursor(20, 20);
      lcd.setTextSize(1.3);
      lcd.print("Pump Enabled");
      digitalWrite(led_pin[0], HIGH);
      digitalWrite(led_pin[1], HIGH);
      digitalWrite(led_pin[2], LOW);
    }
    else{
      lcd.fillRoundRect( 10, 10, 180, 45, 4, TFT_RED);
      lcd.setCursor(20, 20);
      lcd.setTextSize(1.3);
      lcd.setTextColor(TFT_WHITE);
      lcd.print("Pump Disabled");
      lcd.setTextColor(TFT_BLACK);
      digitalWrite(led_pin[0], HIGH);
      digitalWrite(led_pin[1], LOW);
      digitalWrite(led_pin[2], HIGH);
    }
    lcd.setTextSize(5);
    lcd.setCursor(10, 75);
    lcd.print(String(setpoint));
    if(setpoint<100){
      lcd.print("  ");
    }
    lcd.setTextSize(2);
    lcd.println(" mm");
    lcd.setTextSize(5);
    lcd.print("       ");
    lcd.setTextSize(2);
    lcd.println("Hg");

    //print_img(SD, "up.bmp", 60, 60);
    lcd.fillTriangle  ( 245, 70, 275, 25, 305, 70, TFT_BLACK);
    lcd.fillTriangle  ( 245, 160, 275, 205, 305, 160, TFT_BLACK);

        lcd.setTextColor(TFT_RED);
        lcd.setCursor(20, 165);
        lcd.setTextSize(2);
        lcd.print(error);

    delay(5);
}

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

void led_set(int i)
{
    if (i == 0)
    {
        digitalWrite(led_pin[0], LOW);
        digitalWrite(led_pin[1], HIGH);
        digitalWrite(led_pin[2], HIGH);
    }
    if (i == 1)
    {
        digitalWrite(led_pin[0], HIGH);
        digitalWrite(led_pin[1], LOW);
        digitalWrite(led_pin[2], HIGH);
    }
    if (i == 2)
    {
        digitalWrite(led_pin[0], HIGH);
        digitalWrite(led_pin[1], HIGH);
        digitalWrite(led_pin[2], LOW);
    }

    if (i == 3)
    {
        digitalWrite(led_pin[0], LOW);
        digitalWrite(led_pin[1], LOW);
        digitalWrite(led_pin[2], LOW);
    }
}
