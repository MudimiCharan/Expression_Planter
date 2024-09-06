#include <SPI.h>
//#define USE_SDFAT 
#include <SD.h> 
#include <Adafruit_GFX.h> 
#include <MCUFRIEND_kbv.h> 

MCUFRIEND_kbv tft; 
#include <TouchScreen.h> 

const int XP = 8, XM = A2, YP = A3, YM = 9; 
const int TS_LEFT = 180, TS_RT = 805, TS_TOP = 935, TS_BOT = 130; 
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300); 
TSPoint tp; 
int buz = A5; 
int scr = 1; 
unsigned char itt[8]; 

#define MINPRESSURE 200 
#define MAXPRESSURE 1000 

int sub = 0, x; 
char cnfm = 0;

#if defined(ESP32)
  #define SD_CS 5
#else
  #define SD_CS 10
#endif

#define NAMEMATCH ""
//#define NAMEMATCH "tiger" // tiger.bmp 
#define PALETTEDEPTH 0 // do not support Palette modes 
//#define PALETTEDEPTH 8 // support 256-colour Palette 

char namebuf[6] = "/"; // BMP files in root directory 
//char namebuf[32] = "/bitmaps/"; // BMP directory e.g. files in /bitmaps/*.bmp 

File root; 
int pathlen, prv = 0; 
unsigned char it11, it21, it31, it41, cnt = 0, it12, it22, it32, it42, canc = 0; 
int tot, cnt1 = 0, cnt2 = 0;

void setup() { 
  uint16_t ID; 
  Serial.begin(9600); 
  ID = tft.readID(); 
  pinMode(buz, OUTPUT); 
  digitalWrite(buz, 1); 
  delay(300); 
  digitalWrite(buz, 0); 
  if (ID == 0x0D3D3) ID = 0x9481; 
  tft.begin(ID); 
  tft.fillScreen(0x001F); 
  tft.setTextColor(0xFFFF, 0x0000); 

  bool good = SD.begin(SD_CS); 
  if (!good) { 
    while (1); 
  } 
  root = SD.open(namebuf); 
  pathlen = strlen(namebuf); 
  delay(1000); 
  int x = showBMP("/welcome.bmp", 5, 5);
}

void loop() {
  if (Serial.available()) {
    int x = Serial.read();
    Serial.write(x);
    if (x != prv) {
      if (x == '4') {
        Serial.write("Happy");
        prv = x;
        int x = showBMP("/happy.bmp", 5, 5);
      } else if (x == '2') {
        Serial.write("Hot");
        prv = x;
        int x = showBMP("/hot.bmp", 5, 5);
      } else if (x == '3') {
        Serial.write("Dark");
        prv = x;
        int x = showBMP("/1.bmp", 5, 5);
      } else if (x == '1') {
        Serial.write("Thrust");
        prv = x;
        int x = showBMP("/1.bmp", 5, 5);
      }
    }
  }
}

#define BMPIMAGEOFFSET 54 
#define BUFFPIXEL 20

uint16_t read16(File& f) {
  uint16_t result; // read little-endian 
  f.read((uint8_t*)&result, sizeof(result)); 
  return result;
}

uint32_t read32(File& f) {
  uint32_t result;
  f.read((uint8_t*)&result, sizeof(result)); 
  return result;
}

uint8_t showBMP(char *nm, int x, int y) { 
  File bmpFile; 
  int bmpWidth, bmpHeight; // W+H in pixels 
  uint8_t bmpDepth; // Bit depth (currently must be 24, 16, 8, 4, 1) 
  uint32_t bmpImageoffset; // Start of image data in file 
  uint32_t rowSize; // Not always = bmpWidth; may have padding 
  uint8_t sdbuffer[3 * BUFFPIXEL]; // pixel in buffer (R+G+B per pixel) 
  uint16_t lcdbuffer[(1 << PALETTEDEPTH) + BUFFPIXEL], *palette = NULL; 
  uint8_t bitmask, bitshift; 
  boolean flip = true; // BMP is stored bottom-to-top 
  int w, h, row, col, lcdbufsiz = (1 << PALETTEDEPTH) + BUFFPIXEL, buffidx; 
  uint32_t pos; 
  boolean is565 = false; // seek position 

  if ((x >= tft.width()) || (y >= tft.height())) return 1; // off screen 

  bmpFile = SD.open(nm); 
  uint16_t bmpID = read16(bmpFile); // BMP signature 
  (void) read32(bmpFile); // Read & ignore file size 
  (void) read32(bmpFile); // Read & ignore creator bytes 
  bmpImageoffset = read32(bmpFile); // Start of image data 
  (void) read32(bmpFile); // Read & ignore DIB header size 
  bmpWidth = read32(bmpFile); 
  bmpHeight = read32(bmpFile); 
  uint16_t n = read16(bmpFile); // # planes -- must be '1' 
  bmpDepth = read16(bmpFile); // bits per pixel 
  pos = read32(bmpFile); // format 

  if (bmpID != 0x4D42) return 2; // bad ID 
  if (n != 1) return 3; // too many planes 
  if (pos != 0 && pos != 3) return 4; // format: 0 = uncompressed, 3 = 565 
  if (bmpDepth < 16 && bmpDepth > PALETTEDEPTH) return 5; // palette

  if (bmpDepth <= PALETTEDEPTH) { // these modes have separate palette 
    bmpFile.seek(bmpImageoffset - (4 << bmpDepth)); // palette is always @ 54 
    bitmask = 0xFF; 
    if (bmpDepth < 8) bitmask >>= bmpDepth; 
    bitshift = 8 - bmpDepth; 
    n = 1 << bmpDepth; 
    lcdbufsiz -= n; 
    palette = lcdbuffer + lcdbufsiz; 
    for (col = 0; col < n; col++) { 
      pos = read32(bmpFile); // map palette to 5-6-5 
      palette[col] = ((pos & 0x0000F8) >> 3) | ((pos & 0x00FC00) >> 5) | ((pos & 0xF80000) >> 8); 
    }
  }

  // Set TFT address window to clipped image bounds 
  tft.setAddrWindow(x, y, x + w - 1, y + h - 1); 
  for (row = 0; row < h; row++) { // For each scanline... 
    uint8_t r, g, b, *sdptr; 
    int lcdidx, lcdleft; 

    if (flip) pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize; // Bitmap is stored bottom-to-top order (normal BMP) 
    else pos = bmpImageoffset + row * rowSize; // Bitmap is stored top-to-bottom 

    if (bmpFile.position() != pos) bmpFile.seek(pos); // Need seek? 
    buffidx = sizeof(sdbuffer); // Force buffer reload 

    for (col = 0; col < w; ) { // pixels in row 
      lcdleft = w - col; 
      if (lcdleft > lcdbufsiz) lcdleft = lcdbufsiz; 

      for (lcdidx = 0; lcdidx < lcdleft; lcdidx++) { // buffer at a time 
        uint16_t color; 
        if (buffidx >= sizeof(sdbuffer)) bmpFile.read(sdbuffer, sizeof(sdbuffer)); buffidx = 0; // Time to read more pixel data? 

        switch (bmpDepth) { 
          case 24: 
            b = sdbuffer[buffidx++]; 
            g = sdbuffer[buffidx++]; 
            r = sdbuffer[buffidx++]; 
            color = tft.color565(r, g, b); 
            break; 
          case 16: 
            b = sdbuffer[buffidx++]; 
            r = sdbuffer[buffidx++]; 
            if (is565) color = (r << 8) | (b); 
            else color = (r << 9) | ((b & 0xE0) << 1) | (b & 0x1F); 
            break; 
          case 1: 
          case 4: 
          case 8: 
            if (r == 0) b = sdbuffer[buffidx++], r = 8; 
            color = palette[(b >> bitshift) & bitmask]; 
            r -= bmpDepth; 
            b <<= bmpDepth; 
            break; 
        }
        lcdbuffer[lcdidx] = color; 
      }
      tft.pushColors(lcdbuffer, lcdidx, first); 
      first = false; 
      col += lcdidx; 
    }
  }

  tft.setAddrWindow(0, 0, tft.width() - 1, tft.height() - 1); 
  bmpFile.close(); 
  return 0; 
}
