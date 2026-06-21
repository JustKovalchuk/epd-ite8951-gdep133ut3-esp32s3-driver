#pragma once

#ifndef IT8951_H
#define IT8951_H

#include <Arduino.h>
#include <SPI.h>
#include <FS.h>

// ── Режими оновлення дисплея (Waveform Modes) ─────────────
#define UPDATE_MODE_INIT  0   // Init (повне оновлення, повільно, блимає)
#define UPDATE_MODE_DU    1   // Direct Update (швидко, ч/б, бінарне)
#define UPDATE_MODE_GC16  2   // Grayscale 16 (повна якість, повільно)
#define UPDATE_MODE_GL16  3   // GL16 (швидкий сірий)
#define UPDATE_MODE_GLR16 4   // GLR16
#define UPDATE_MODE_GLD16 5   // GLD16 (сірий з очищенням від гостінгу - РЕКОМЕНДОВАНО ДЛЯ ЧАСТКОВИХ ОНОВЛЕНЬ)
#define UPDATE_MODE_A2    6   // A2 (надшвидкий 2-рівневий чорно-білий)
#define UPDATE_MODE_DU4   7   // DU4 (4 рівні сірого)

// ── Команди IT8951 ────────────────────────────────────────
#define IT8951_TCON_SYS_RUN         0x0001
#define IT8951_TCON_STANDBY         0x0002
#define IT8951_TCON_SLEEP           0x0003
#define IT8951_TCON_REG_RD          0x0010
#define IT8951_TCON_REG_WR          0x0011
#define IT8951_TCON_LD_IMG          0x0020
#define IT8951_TCON_LD_IMG_AREA     0x0021
#define IT8951_TCON_LD_IMG_END      0x0022
#define IT8951_TCON_PMIC_CTL        0x0030
#define IT8951_TCON_BYPASS_I2C      0x0031
#define IT8951_TCON_GET_DEV_INFO    0x0302
#define IT8951_TCON_DPY_AREA        0x0034
#define IT8951_TCON_DPY_BUF_AREA    0x0037

// ── Регістри IT8951 ───────────────────────────────────────
#define REG_LISAR           0x0008  // Load Image Start Reg Low
#define REG_LISARH          0x000A  // Load Image Start Reg High
#define REG_I80CPCR         0x0004  // CPU packed pixel enable
#define REG_TEMP            0x0114  // Регістр температури

// ── SPI швидкість та Preamble ─────────────────────────────
#define SPI_FREQ            2000000  // 2 МГц
#define PREAMBLE_CMD        0x6000   // Перед кодом команди
#define PREAMBLE_WRITE      0x0000   // Перед даними (запис)
#define PREAMBLE_READ       0x1000   // Перед читанням


// ── Структури ─────────────────────────────────────────────
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

// ── Клас бібліотеки IT8951 ───────────────────────────────
class IT8951Class {
public:
    IT8951Class();
    ~IT8951Class();

    // Ініціалізація дисплея. Виділяє пам'ять під canvas у PSRAM.
    // Параметри:
    //   vcom - Значення VCOM у мілівольтах (наприклад: 2330 для -2.33V)
    //   sck, miso, mosi, cs - піни апаратного SPI
    //   hrdy - BUSY (HOST_HRDY) вхідний пін
    //   rst - RESET вихідний пін
    //   pwr - POWER_CTRL вихідний пін живлення
    bool begin(uint16_t vcom = 2330, 
               int8_t sck = 12, int8_t miso = 13, int8_t mosi = 11, int8_t cs = 10,
               int8_t hrdy = 6, int8_t rst = 7, int8_t pwr = 5);

    // ── ГРАФІЧНІ ФУНКЦІЇ CANVAS (Малювання у PSRAM буфері) ──
    void drawPixel(uint16_t x, uint16_t y, uint8_t color);
    uint8_t getPixel(uint16_t x, uint16_t y);
    void drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color, bool fill);
    void drawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint8_t color, bool fill = false);
    void drawChar(uint16_t x, uint16_t y, char c, uint8_t color, uint8_t bgColor, uint8_t scale = 1);
    void drawText(uint16_t x, uint16_t y, const char* str, uint8_t color, uint8_t bgColor, uint8_t scale = 1);
    void drawCyrillicText(uint16_t x, uint16_t y, const char* str, uint8_t color, uint8_t bgColor, uint8_t scale = 1, bool transparent = true);
    size_t getCyrillicTextWidth(const char* str, uint8_t scale = 1);
    void drawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* data);

    // ── ДИНАМІЧНИЙ TTF РЕНДЕРИНГ ──
    bool loadTTF(fs::FS &fs, const char* path, uint16_t pixelHeight);
    void drawTTFText(uint16_t x, uint16_t y, const char* str, uint8_t color, uint8_t bgColor, bool transparent = true);
    size_t getTTFTextWidth(const char* str);
    void unloadTTF();
    uint16_t getTTFHeight() const { return _ttfHeight; }

    // ── УПРАВЛІННЯ ДИСПЛЕЄМ (Взаємодія з платою TCON) ──────────
    
    // Заповнити весь canvas у PSRAM одним кольором без відправки на екран
    void fill(uint8_t color = 0xFF);

    // Залити весь canvas у PSRAM одним кольором та оновити екран повністю
    void clear(uint8_t color = 0xFF, uint8_t mode = UPDATE_MODE_GC16);

    // Зробити повний hardware INIT цикл очищення екрану (команда 0x0034 в INIT режимі)
    void clearScreen();

    // Передати весь canvas з PSRAM в IT8951 та оновити весь екран
    void display(uint8_t mode = UPDATE_MODE_GC16);

    // Оновити окрему область екрана (завантажує пікселі області та дає команду оновлення)
    void updateArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t mode = UPDATE_MODE_DU);

    // Дати команду оновлення області без завантаження пікселів (для низькорівневого контролю)
    void displayArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t mode = UPDATE_MODE_DU);

    // Перевести дисплей у режим сну та вимкнути живлення VCOM
    void sleep();

    // Пробудити дисплей після сну
    void wakeup();

    // ── НАЛАШТУВАННЯ ТА СТАТУС ───────────────────────────────
    
    // Примусово задати температуру навколишнього середовища (у градусах Цельсія)
    void setTemperature(int8_t temp);

    // Змінити тип endianness (0: Little, 1: Big Endian)
    void setEndianness(uint8_t endianVal) { _endianType = endianVal; }
    uint8_t getEndianness() const { return _endianType; }

    // Отримати ширину та висоту панелі
    uint16_t getWidth() const { return _panelW; }
    uint16_t getHeight() const { return _panelH; }

    // Отримати вказівник на локальний буфер у PSRAM
    uint8_t* getCanvas() { return _imgBuf; }

    // Отримати внутрішню адресу буфера зображення в пам'яті IT8951
    uint32_t getImgBufAddr() const { return _imgBufAddr; }

    // Зчитати поточне значення VCOM (у мілівольтах)
    uint16_t getVCOM();

    // Зчитати поточну температуру з термодатчика (у градусах Цельсія)
    int8_t readTemperature();

    // Перевірити, чи зайнятий рушій малювання LUT (регістр 0x1224)
    bool isEngineBusy();

    // Отримати інформацію про пристрій (фізична роздільна здатність, версії FW/LUT тощо)
    void getDeviceInfo(IT8951DevInfo* info);

private:
    // Піни
    int8_t _pinSCK, _pinMISO, _pinMOSI, _pinCS;
    int8_t _pinHRDY, _pinRST, _pinPWR;

    // Внутрішній стан
    uint8_t* _imgBuf;       // Вказівник на буфер у PSRAM (розмір PANEL_WIDTH * PANEL_HEIGHT / 2)
    uint32_t _imgBufAddr;   // Адреса буфера в пам'яті IT8951 (з DevInfo)
    uint8_t  _endianType;   // Порядок байт для передачі (1: Big, 0: Little)
    uint16_t _vcom;         // Значення VCOM
    uint16_t _panelW;       // Ширина екрану
    uint16_t _panelH;       // Висота екрану
    bool     _isPowered;    // Чи подано живлення на плату
    int8_t   _currentTemp;  // Поточна примусова температура (-99 якщо не задано)

    // Низькорівневий SPI драйвер
    void writeCmd(uint16_t cmd);
    void writeData(uint16_t data);
    uint16_t readData();
    void writeReg(uint16_t addr, uint16_t value);
    uint16_t readReg(uint16_t addr);
    void waitForReady(uint32_t timeoutMs = 100);
    void reset();
    void setVCOM(uint16_t vcom);

    void powerOn();
    void powerOff();

    void loadImageStart(IT8951LdImgInfo* imgInfo, IT8951AreaImgInfo* areaInfo);
    void loadImageEnd();
    void loadBulkPixels(const uint8_t* buf, uint32_t totalBytes);
    void loadAreaPixels_4bpp(const uint8_t* canvas, uint16_t x, uint16_t y, uint16_t w, uint16_t h);

    inline void csLow()   { digitalWrite(_pinCS, LOW);  }
    inline void csHigh()  { digitalWrite(_pinCS, HIGH); }

    // Стан dynamic TTF
    unsigned char* _ttfBuffer;
    bool _ttfLoaded;
    float _ttfScale;
    uint16_t _ttfHeight;
    void* _fontInfoRaw;
};

#endif // IT8951_H
