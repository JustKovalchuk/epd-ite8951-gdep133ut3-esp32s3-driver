#pragma once

#ifndef IT8951_H
#define IT8951_H

#include <Arduino.h>
#include <SPI.h>
#include <FS.h>

// ── Display Update Modes (Waveform Modes) ─────────────
#define UPDATE_MODE_INIT  0   // Init (full update, slow, blinking)
#define UPDATE_MODE_DU    1   // Direct Update (fast, black/white, binary)
#define UPDATE_MODE_GC16  2   // Grayscale 16 (full quality, slow)
#define UPDATE_MODE_GL16  3   // GL16 (fast gray)
#define UPDATE_MODE_GLR16 4   // GLR16
#define UPDATE_MODE_GLD16 5   // GLD16 (gray with ghosting clearing - RECOMMENDED FOR PARTIAL UPDATES)
#define UPDATE_MODE_A2    6   // A2 (ultra-fast 2-level black and white)
#define UPDATE_MODE_DU4   7   // DU4 (4 gray levels)

// ── IT8951 Commands ────────────────────────────────────────
#define IT8951_TCON_SYS_RUN         0x0001
#define IT8951_TCON_STANDBY         0x0002
#define IT8951_TCON_SLEEP           0x0003
#define IT8951_TCON_REG_RD          0x0010
#define IT8951_TCON_REG_WR          0x0011
#define IT8951_TCON_MEM_BST_RD_T    0x0012 // +
#define IT8951_TCON_MEM_BST_RD_S    0x0013 // +
#define IT8951_TCON_MEM_BST_WR      0x0014 // +
#define IT8951_TCON_MEM_BST_END     0x0015 // +
#define IT8951_TCON_LD_IMG          0x0020
#define IT8951_TCON_LD_IMG_AREA     0x0021
#define IT8951_TCON_LD_IMG_END      0x0022

//I80 User defined command code
#define IT8951_TCON_PMIC_CTL        0x0030
#define IT8951_TCON_BYPASS_I2C      0x0031
#define IT8951_TCON_GET_DEV_INFO    0x0302
#define IT8951_TCON_DPY_AREA        0x0034
#define IT8951_TCON_DPY_BUF_AREA    0x0037
#define USDEF_I80_CMD_PMIC_CTRL     0x0038 // +
#define USDEF_I80_CMD_SET_VCOM      0x0039 // +

//Rotate mode
#define IT8951_ROTATE_0     0 // +
#define IT8951_ROTATE_90    1 // +
#define IT8951_ROTATE_180   2 // +
#define IT8951_ROTATE_270   3 // +

//Pixel mode , BPP - Bit per Pixel
#define IT8951_2BPP   0 // +
#define IT8951_3BPP   1 // +
#define IT8951_4BPP   2 // +
#define IT8951_8BPP   3 // +

//Waveform Mode
#define IT8951_MODE_0   0 // +
#define IT8951_MODE_1   1 // +
#define IT8951_MODE_2   2 // +
#define IT8951_MODE_3   3 // +
#define IT8951_MODE_4   4 // +

//Endian Type
#define IT8951_LDIMG_L_ENDIAN   0 // +
#define IT8951_LDIMG_B_ENDIAN   1 // +

//Auto LUT
#define IT8951_DIS_AUTO_LUT   0 // +
#define IT8951_EN_AUTO_LUT    1 // +

//LUT Engine Status
#define IT8951_ALL_LUTE_BUSY 0xFFFF // +

// ── IT8951 Registers ───────────────────────────────────────
#define REG_LISAR           0x0008  // Load Image Start Reg Low
#define REG_LISARH          0x000A  // Load Image Start Reg High
#define REG_I80CPCR         0x0004  // (0x0001) CPU packed pixel enable 
#define REG_TEMP            0x0114  // ? Temperature register

#define REG_LUTAFSR         0x1224  // + LUT Status Reg (status of All LUT Engines)

// ── SPI Speed and Preamble ─────────────────────────────
#define SPI_FREQ            12000000 // 12 MHz for stable transfers
#define PREAMBLE_CMD        0x6000   // Before command code
#define PREAMBLE_WRITE      0x0000   // Before data (write)
#define PREAMBLE_READ       0x1000   // Before reading


// ── Structures ─────────────────────────────────────────────
struct IT8951DevInfo {
    uint16_t panelW;
    uint16_t panelH;
    uint16_t imgBufAddrL;
    uint16_t imgBufAddrH;
    uint16_t fwVersion[8];
    uint16_t lutVersion[8];
};

struct IT8951AreaImgInfo {
    uint16_t areaX;
    uint16_t areaY;
    uint16_t areaW;
    uint16_t areaH;
};

struct IT8951LdImgInfo {
    uint16_t endianType;   // 0=little, 1=big
    uint16_t pixelFormat;  // BPP (2, 3, 4, 8)
    uint16_t rotate;       // 0=0°
    void*    pSrcBufferAddr;
    uint16_t* pBufAddr;
};

// ── IT8951 Library Class ───────────────────────────────
class IT8951Class {
public:
    IT8951Class();
    ~IT8951Class();

    // Display initialization. Allocates memory for canvas in PSRAM.
    // Parameters:
    //   vcom - VCOM value in millivolts (e.g., 2330 for -2.33V)
    //   sck, miso, mosi, cs - hardware SPI pins
    //   hrdy - BUSY (HOST_HRDY) input pin
    //   rst - RESET output pin
    //   pwr - POWER_CTRL power output pin
    bool begin(uint16_t vcom = 2330, 
               int8_t sck = 12, int8_t miso = 13, int8_t mosi = 11, int8_t cs = 10,
               int8_t hrdy = 6, int8_t rst = 7, int8_t pwr = 5);

    // ── CANVAS GRAPHIC FUNCTIONS (Drawing in PSRAM buffer) ──
    void drawPixel(uint16_t x, uint16_t y, uint8_t color);
    uint8_t getPixel(uint16_t x, uint16_t y);
    void drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color, bool fill);
    void drawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint8_t color, bool fill = false);
    void drawChar(uint16_t x, uint16_t y, char c, uint8_t color, uint8_t bgColor, uint8_t scale = 1);
    void drawText(uint16_t x, uint16_t y, const char* str, uint8_t color, uint8_t bgColor, uint8_t scale = 1);
    void drawCyrillicText(uint16_t x, uint16_t y, const char* str, uint8_t color, uint8_t bgColor, uint8_t scale = 1, bool transparent = true);
    size_t getCyrillicTextWidth(const char* str, uint8_t scale = 1);
    void drawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* data);

    // ── DYNAMIC TTF RENDERING ──
    bool loadTTF(fs::FS &fs, const char* path, uint16_t pixelHeight);
    void drawTTFText(uint16_t x, uint16_t y, const char* str, uint8_t color, uint8_t bgColor, bool transparent = true);
    size_t getTTFTextWidth(const char* str);
    void unloadTTF();
    uint16_t getTTFHeight() const { return _ttfHeight; }

    // ── DISPLAY CONTROL (Interaction with TCON board) ──────────
    
    // Fill the entire canvas in PSRAM with a single color without sending to the screen
    void fill(uint8_t color = 0xFF);

    // Fill the entire canvas in PSRAM with a single color and fully update the screen
    void clear(uint8_t color = 0xFF, uint8_t mode = UPDATE_MODE_GC16);

    // Perform a full hardware INIT screen clearing cycle (command 0x0034 in INIT mode)
    void clearScreen();

    // Transfer the entire canvas from PSRAM to IT8951 and update the entire screen
    void display(uint8_t mode = UPDATE_MODE_GC16);

    // Update a specific screen area (loads area pixels and sends update command)
    void updateArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t mode = UPDATE_MODE_DU);

    // Send an area update command without loading pixels (for low-level control)
    void displayArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t mode = UPDATE_MODE_DU);

    // Put the display to sleep and turn off VCOM power
    void sleep();

    // Wake up the display after sleep
    void wakeup();

    // ── SETTINGS AND STATUS ───────────────────────────────
    
    // Force set the ambient temperature (in degrees Celsius)
    void setTemperature(int8_t temp);

    // Set canvas rotation (0: 0°, 1: 90° CW, 2: 180°, 3: 270° CW / 90° CCW)
    void setRotation(uint8_t rotation) { _rotation = rotation % 4; }
    uint8_t getRotation() const { return _rotation; }

    // Change endianness type (0: Little, 1: Big Endian)
    void setEndianness(uint8_t endianVal) { _endianType = endianVal; }
    uint8_t getEndianness() const { return _endianType; }

    // Get panel width and height
    uint16_t getWidth() const { return (_rotation % 2 == 1) ? _panelH : _panelW; }
    uint16_t getHeight() const { return (_rotation % 2 == 1) ? _panelW : _panelH; }

    // Get physical (hardware, unrotated) panel dimensions
    uint16_t getPanelWidth()  const { return _panelW; }
    uint16_t getPanelHeight() const { return _panelH; }

    // Get pointer to the local buffer in PSRAM
    uint8_t* getCanvas() { return _imgBuf; }

    // Get the internal image buffer address in IT8951 memory
    uint32_t getImgBufAddr() const { return _imgBufAddr; }

    // Read the current VCOM value (in millivolts)
    uint16_t getVCOM();

    // Read the current temperature from the thermal sensor (in degrees Celsius)
    int8_t readTemperature();

    // Check if the LUT drawing engine is busy (register 0x1224)
    bool isDisplayBusy();

    // Get device info (physical resolution, FW/LUT versions, etc.)
    void getDeviceInfo(IT8951DevInfo* info);

    // Read image data back from IT8951 memory (Burst Read)
    void memBurstReadProc(uint32_t memAddr, uint32_t readSizeWords, uint16_t* destBuf);

private:
    // Pins
    int8_t _pinSCK, _pinMISO, _pinMOSI, _pinCS;
    int8_t _pinHRDY, _pinRST, _pinPWR;
    SPIClass* _spi;
    uint32_t _spiTimeout;

    // Internal state
    uint8_t* _imgBuf;       // Pointer to buffer in PSRAM (size PANEL_WIDTH * PANEL_HEIGHT / 2)
    uint32_t _imgBufAddr;   // Address of the buffer in IT8951 memory (from DevInfo)
    uint8_t  _endianType;   // Byte order for transmission (1: Big, 0: Little)
    uint16_t _vcom;         // VCOM value
    uint16_t _panelW;       // Screen width
    uint16_t _panelH;       // Screen height
    bool     _isPowered;    // Whether power is supplied to the board
    int8_t   _currentTemp;  // Current forced temperature (-99 if not set)
    uint8_t  _rotation;     // Logical canvas rotation (0, 1, 2, 3)

    // Low-level SPI driver
    void writeCmd(uint16_t cmd);
    void writeData(uint16_t data);
    uint16_t readData();
    void writeReg(uint16_t addr, uint16_t value);
    uint16_t readReg(uint16_t addr);
    void waitForSPIReady(uint32_t timeoutMs = 0);
    void reset();
    void setVCOM(uint16_t vcom);

    void powerOn(bool forceReset = false);
    void powerOff();

    void loadImageStart(IT8951LdImgInfo* imgInfo);
    void loadImageAreaStart(IT8951LdImgInfo* imgInfo, IT8951AreaImgInfo* areaInfo);
    void loadImageEnd();
    void loadBulkPixels(const uint8_t* buf, uint32_t totalBytes);
    void loadAreaPixels_4bpp(const uint8_t* canvas, uint16_t x, uint16_t y, uint16_t w, uint16_t h);

    void memBurstReadStart();
    void memBurstReadTrigger(uint32_t memAddr, uint32_t readSizeWords);
    void memBurstEnd();

    inline void csLow()   { digitalWrite(_pinCS, LOW);  }
    inline void csHigh()  { digitalWrite(_pinCS, HIGH); }

    // Dynamic TTF state
    unsigned char* _ttfBuffer;
    bool _ttfLoaded;
    float _ttfScale;
    uint16_t _ttfHeight;
    void* _fontInfoRaw;
};

#endif // IT8951_H
