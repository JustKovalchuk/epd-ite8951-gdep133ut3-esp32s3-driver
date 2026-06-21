// ============================================================
//  IT8951 — Реалізація бібліотеки
//  Плата: ESP32-S3 + DEJA-TC103 + GDEP133UT3 (1600×1200)
// ============================================================

#include "IT8951.h"
#include "font8x16.h"
#include "irpin_font.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"


// Перетворює UTF-8 символ (до 2 байт) у кодування Windows-1251
static uint8_t utf8_to_cp1251(const char* &utf8_str) {
    uint8_t c = (uint8_t)*utf8_str++;
    
    if (c < 0x80) {
        return c; // ASCII
    }
    
    if (c == 0xD0 || c == 0xD1 || c == 0xD2) {
        if (!*utf8_str) return '?';
        uint8_t next = (uint8_t)*utf8_str++;
        
        if (c == 0xD0) {
            if (next >= 0x90 && next <= 0xBF) return next + 0x30; // А-п (192-239)
            if (next == 0x81) return 0xA8; // Ё
            if (next == 0x84) return 0xAA; // Є (укр)
            if (next == 0x86) return 0xAF; // І (укр)
            if (next == 0x87) return 0xBF; // Ї (укр)
        } 
        else if (c == 0xD1) {
            if (next >= 0x80 && next <= 0x8F) return next + 0x70; // р-я (240-255)
            if (next == 0x91) return 0xB8; // ё
            if (next == 0x94) return 0xBA; // є (укр)
            if (next == 0x96) return 0xB2; // і (укр)
            if (next == 0x97) return 0xB3; // ї (укр)
        }
        else if (c == 0xD2) {
            if (next == 0x90) return 0xA5; // Ґ (укр)
            if (next == 0x91) return 0xB4; // ґ (укр)
        }
    }
    return '?';
}


// Обмеження ESP32 SPI DMA: max ~4092 байт за один transferBytes
#define SPI_DMA_MAX  4092
static uint8_t _dmaBuf[SPI_DMA_MAX] __attribute__((aligned(4))); // SRAM буфер для DMA

// Конструктор
IT8951Class::IT8951Class() : 
    _imgBuf(nullptr), 
    _imgBufAddr(0), 
    _endianType(1), // Big Endian за замовчуванням
    _vcom(2330),
    _panelW(1600),
    _panelH(1200),
    _isPowered(false),
    _currentTemp(-99),
    _ttfBuffer(nullptr),
    _ttfLoaded(false),
    _ttfScale(0.0f),
    _ttfHeight(0),
    _fontInfoRaw(nullptr) {
}

// Деструктор
IT8951Class::~IT8951Class() {
    unloadTTF();
    if (_imgBuf) {
        free(_imgBuf);
        _imgBuf = nullptr;
    }
}

// Ініціалізація
bool IT8951Class::begin(uint16_t vcom, int8_t sck, int8_t miso, int8_t mosi, int8_t cs, int8_t hrdy, int8_t rst, int8_t pwr) {
    _pinSCK  = sck;
    _pinMISO = miso;
    _pinMOSI = mosi;
    _pinCS   = cs;
    _pinHRDY = hrdy;
    _pinRST  = rst;
    _pinPWR  = pwr;
    _vcom    = vcom;
    _isPowered = false;
    _currentTemp = -99;

    pinMode(_pinHRDY, INPUT);
    pinMode(_pinRST,  OUTPUT);
    pinMode(_pinCS,   OUTPUT);
    pinMode(_pinPWR,  OUTPUT);

    digitalWrite(_pinCS,  HIGH);
    digitalWrite(_pinRST, HIGH);
    digitalWrite(_pinPWR, LOW); // Спочатку живлення вимкнено

    // Виділення пам'яті під 4bpp буфер у PSRAM
    size_t bufSize = (size_t)_panelW * (_panelH / 2);
    _imgBuf = (uint8_t*)ps_malloc(bufSize);
    if (!_imgBuf) {
        Serial.println("[IT8951] ERROR: PSRAM allocation failed!");
        return false;
    }
    // Заливаємо білим
    memset(_imgBuf, 0xFF, bufSize);

    // Ініціалізація SPI
    SPI.begin(_pinSCK, _pinMISO, _pinMOSI, _pinCS);
    SPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));

    // Увімкнути живлення і налаштувати дисплей
    powerOn();

    // Отримуємо DevInfo
    IT8951DevInfo devInfo;
    getDeviceInfo(&devInfo);
    _panelW = devInfo.panelW;
    _panelH = devInfo.panelH;
    
    // Вимикаємо живлення екрана після повної ініціалізації (буде вмикатись динамічно)
    powerOff();

    Serial.println("[IT8951] Library initialized successfully");
    return true;
}

// ── Низькорівневі SPI функції ──────────────────────────────

void IT8951Class::waitForReady(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (digitalRead(_pinHRDY) == LOW) {
        if (millis() - start > timeoutMs) {
            Serial.printf("[IT8951] !! WaitForReady timeout (%lu ms) !!\n", timeoutMs);
            return;
        }
        delayMicroseconds(10);
    }
}

void IT8951Class::writeCmd(uint16_t cmd) {
    waitForReady();
    csLow();
    SPI.transfer16(PREAMBLE_CMD);
    waitForReady();
    SPI.transfer16(cmd);
    csHigh();
}

void IT8951Class::writeData(uint16_t data) {
    waitForReady();
    csLow();
    SPI.transfer16(PREAMBLE_WRITE);
    waitForReady();
    SPI.transfer16(data);
    csHigh();
}

uint16_t IT8951Class::readData() {
    waitForReady();
    csLow();
    SPI.transfer16(PREAMBLE_READ);
    waitForReady();
    SPI.transfer16(0x0000); // dummy
    waitForReady();
    uint16_t val = SPI.transfer16(0x0000);
    csHigh();
    return val;
}

void IT8951Class::writeReg(uint16_t addr, uint16_t value) {
    writeCmd(IT8951_TCON_REG_WR);
    writeData(addr);
    writeData(value);
}

uint16_t IT8951Class::readReg(uint16_t addr) {
    writeCmd(IT8951_TCON_REG_RD);
    writeData(addr);
    return readData();
}

void IT8951Class::reset() {
    digitalWrite(_pinRST, LOW);
    delay(200);
    digitalWrite(_pinRST, HIGH);
    delay(200);
    waitForReady(2000); // Дати контролеру до 2 секунд на запуск системного ПЗУ
}

void IT8951Class::getDeviceInfo(IT8951DevInfo* info) {
    writeCmd(IT8951_TCON_GET_DEV_INFO);

    waitForReady();
    csLow();
    SPI.transfer16(PREAMBLE_READ);
    waitForReady();
    SPI.transfer16(0x0000); // dummy
    waitForReady();

    uint16_t* p = (uint16_t*)info;
    size_t words = sizeof(IT8951DevInfo) / 2;
    for (size_t i = 0; i < words; i++) {
        waitForReady();
        p[i] = SPI.transfer16(0x0000);
    }
    csHigh();

    _imgBufAddr = ((uint32_t)info->imgBufAddrH << 16) | info->imgBufAddrL;
}

void IT8951Class::setVCOM(uint16_t vcom) {
    writeCmd(IT8951_TCON_PMIC_CTL);
    writeData(vcom);
    writeData(0x0001); // apply
    delay(100);
    Serial.printf("[IT8951] VCOM set to -%d mV\n", vcom);
}

// ── Живлення ──────────────────────────────────────────────────

void IT8951Class::powerOn() {
    if (_isPowered) return;
    
    // Вмикаємо лінію живлення (якщо підключена)
    digitalWrite(_pinPWR, HIGH);
    delay(50); // Дати живленню стабілізуватися
    
    bool needReset = false;
    
    // Якщо HRDY низький після подачі живлення, чіп точно не активний/потребує скидання
    if (digitalRead(_pinHRDY) == LOW) {
        needReset = true;
    } else {
        // HRDY високий, спробуємо розбудити та зчитати тестовий регістр
        // Використовуємо короткий таймаут 20ms, щоб не зависати якщо чіп мертвий
        waitForReady(20); 
        writeCmd(IT8951_TCON_SYS_RUN);
        delay(20);
        
        uint16_t testVal = readReg(REG_I80CPCR);
        if (testVal != 0x0001) {
            needReset = true;
        }
    }
    
    if (needReset) {
        Serial.println("[IT8951] Chip registers lost or unresponsive. Performing full hard reset...");
        reset();
        
        writeCmd(IT8951_TCON_SYS_RUN);
        delay(10);
        
        writeReg(REG_I80CPCR, 0x0001);
        setVCOM(_vcom);
    } else {
        // Контролер підключений окремо або зберіг стан у сні. 
        // Поновлюємо VCOM про всяк випадок (PMIC міг його вимкнути).
        setVCOM(_vcom);
    }
    
    // Відновлюємо налаштування температури, якщо вони були задані
    if (_currentTemp != -99) {
        writeReg(REG_TEMP, (uint16_t)_currentTemp);
        Serial.printf("[IT8951] Restored temperature setting to: %d C\n", _currentTemp);
    }
    
    _isPowered = true;
    Serial.println("[IT8951] Power ON sequence completed successfully.");
}

void IT8951Class::powerOff() {
    if (!_isPowered) return;
    
    Serial.println("[IT8951] Powering OFF...");
    writeCmd(IT8951_TCON_SLEEP);
    delay(50);
    digitalWrite(_pinPWR, LOW);
    
    _isPowered = false;
    Serial.println("[IT8951] Power OFF sequence completed successfully.");
}

void IT8951Class::sleep() {
    powerOff();
}

void IT8951Class::wakeup() {
    powerOn();
}

// ── Температура ────────────────────────────────────────────────

void IT8951Class::setTemperature(int8_t temp) {
    _currentTemp = temp;
    if (_isPowered) {
        writeReg(REG_TEMP, (uint16_t)temp);
        Serial.printf("[IT8951] Forced temperature set to: %d C\n", temp);
    } else {
        Serial.printf("[IT8951] Temperature set to %d C (will be applied on powerOn)\n", temp);
    }
}

uint16_t IT8951Class::getVCOM() {
    powerOn(); // Забезпечуємо увімкнене живлення
    writeCmd(0x0039);
    writeData(0x0000); // 0 = read mode
    uint16_t vcomVal = readData();
    return vcomVal;
}

int8_t IT8951Class::readTemperature() {
    powerOn(); // Забезпечуємо увімкнене живлення
    writeCmd(0x00A4);
    writeData(0x0000); // 0 = read option
    uint16_t rawTemp = readData();
    return (int8_t)(rawTemp & 0xFF);
}

bool IT8951Class::isEngineBusy() {
    if (!_isPowered) return false; // Якщо живлення вимкнено, рушій не може бути зайнятий
    return (readReg(0x1224) != 0);
}

// ── Завантаження пікселів ────────────────────────────────────

void IT8951Class::loadImageStart(IT8951LdImgInfo* imgInfo, IT8951AreaImgInfo* areaInfo) {
    powerOn(); // Забезпечуємо увімкнене живлення перед роботою

    writeReg(REG_LISAR,  (uint16_t)(_imgBufAddr & 0xFFFF));
    writeReg(REG_LISARH, (uint16_t)(_imgBufAddr >> 16));

    // Зміна 2, 3, 4, 8 BPP на внутрішній код IT8951 (0, 1, 2, 3)
    uint16_t bppCode = 2; // за замовчуванням 4bpp
    if (imgInfo->pixelFormat == 2) bppCode = 0;
    else if (imgInfo->pixelFormat == 3) bppCode = 1;
    else if (imgInfo->pixelFormat == 4) bppCode = 2;
    else if (imgInfo->pixelFormat == 8) bppCode = 3;

    writeCmd(IT8951_TCON_LD_IMG_AREA);
    writeData((imgInfo->endianType   << 8) |
              (bppCode               << 4) |
              (imgInfo->rotate));
    
    writeData(areaInfo->areaX);
    writeData(areaInfo->areaY);
    writeData(areaInfo->areaW);
    writeData(areaInfo->areaH);
}

void IT8951Class::loadImageEnd() {
    writeCmd(IT8951_TCON_LD_IMG_END);
}

void IT8951Class::loadBulkPixels(const uint8_t* buf, uint32_t totalBytes) {
    waitForReady();
    csLow();
    SPI.transfer16(PREAMBLE_WRITE);
    waitForReady();

    uint32_t sent = 0;
    while (sent < totalBytes) {
        uint32_t chunk = totalBytes - sent;
        if (chunk > SPI_DMA_MAX) chunk = SPI_DMA_MAX;
        
        memcpy(_dmaBuf, buf + sent, chunk);
        SPI.transferBytes(_dmaBuf, nullptr, chunk);
        sent += chunk;
    }
    csHigh();
}

void IT8951Class::loadAreaPixels_4bpp(const uint8_t* canvas, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    waitForReady();
    csLow();
    SPI.transfer16(PREAMBLE_WRITE);
    waitForReady();

    uint16_t w_bytes = w / 2; // 4bpp
    uint16_t row_stride_bytes = _panelW / 2;
    
    for (uint16_t r = 0; r < h; r++) {
        uint32_t srcIdx = (uint32_t)(y + r) * row_stride_bytes + (x / 2);
        
        // Копіюємо один рядок в SRAM DMA буфер
        memcpy(_dmaBuf, canvas + srcIdx, w_bytes);
        SPI.transferBytes(_dmaBuf, nullptr, w_bytes);
    }
    csHigh();
}

// ── Відображення (Refresh) ──────────────────────────────────

void IT8951Class::fill(uint8_t color) {
    uint8_t c4 = color >> 4;
    uint8_t byteColor = (c4 << 4) | c4;
    memset(_imgBuf, byteColor, (size_t)_panelW * (_panelH / 2));
}

void IT8951Class::clear(uint8_t color, uint8_t mode) {
    fill(color);
    display(mode);
}

void IT8951Class::clearScreen() {
    Serial.println("[IT8951] ClearScreen: loading white frame...");

    IT8951LdImgInfo imgInfo = {
        .endianType = 1, .pixelFormat = 4, .rotate = 0,
        .pSrcBufferAddr = nullptr, .pBufAddr = nullptr
    };
    IT8951AreaImgInfo areaInfo = {
        .areaX = 0, .areaY = 0,
        .areaW = _panelW, .areaH = _panelH
    };
    loadImageStart(&imgInfo, &areaInfo);

    static uint8_t whiteBuf[SPI_DMA_MAX] __attribute__((aligned(4)));
    static bool whiteReady = false;
    if (!whiteReady) { memset(whiteBuf, 0xFF, SPI_DMA_MAX); whiteReady = true; }

    uint32_t totalBytes = ((uint32_t)_panelW * _panelH) / 2; // ДІЛЕННЯ НА 2 ДЛЯ 4bpp!
    waitForReady();
    csLow();
    SPI.transfer16(PREAMBLE_WRITE);
    waitForReady();
    
    uint32_t sent = 0;
    while (sent < totalBytes) {
        uint32_t chunk = totalBytes - sent;
        if (chunk > SPI_DMA_MAX) chunk = SPI_DMA_MAX;
        SPI.transferBytes(whiteBuf, nullptr, chunk);
        sent += chunk;
    }
    csHigh();

    loadImageEnd();
    Serial.println("[IT8951] ClearScreen: INIT refresh (0x0034)...");
    displayArea(0, 0, _panelW, _panelH, UPDATE_MODE_INIT);
    Serial.println("[IT8951] ClearScreen done.");
}

void IT8951Class::display(uint8_t mode) {
    IT8951LdImgInfo imgInfo = {
        .endianType = _endianType, .pixelFormat = 4, .rotate = 0,
        .pSrcBufferAddr = nullptr, .pBufAddr = nullptr
    };
    IT8951AreaImgInfo areaInfo = {
        .areaX = 0, .areaY = 0,
        .areaW = _panelW, .areaH = _panelH
    };

    loadImageStart(&imgInfo, &areaInfo);
    
    uint32_t totalBytes = ((uint32_t)_panelW * _panelH) / 2; // ДІЛЕННЯ НА 2 ДЛЯ 4bpp!
    loadBulkPixels(_imgBuf, totalBytes);
    loadImageEnd();
    
    displayArea(0, 0, _panelW, _panelH, mode);
}

void IT8951Class::displayArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t mode) {
    uint16_t alignedX = x & ~1;
    uint16_t alignedW = (w + 1) & ~1;

    if (alignedX + alignedW > _panelW) alignedW = _panelW - alignedX;
    if (y + h > _panelH) h = _panelH - y;
    if (alignedW == 0 || h == 0) return;

    // Якщо це локальний updateArea, ми спочатку підвантажимо пікселі
    // (Але якщо це викликається після display() - loadImageStart вже увімкнув живлення)
    powerOn(); 

    waitForReady();
    writeCmd(IT8951_TCON_DPY_AREA);
    writeData(alignedX);
    writeData(y);
    writeData(alignedW);
    writeData(h);
    writeData(mode);

    uint32_t t0 = millis();
    delay(50); // дати час рушію оновлення запуститися
    uint32_t timeout = 10000; // Ліміт очікування 10 секунд
    while (isEngineBusy()) {
        if (millis() - t0 > timeout) {
            Serial.println("[IT8951] WARNING: display refresh timeout (Engine Busy)!");
            break;
        }
        delay(10);
    }
    Serial.printf("[IT8951] Refresh area done in %lu ms\n", millis() - t0);

    // АВТОМАТИЧНЕ ВИМКНЕННЯ VCOM ПІСЛЯ ЗАВЕРШЕННЯ ОНОВЛЕННЯ
    powerOff();
}

// Завантаження та виведення окремої області кадру без динамічної алокації
void IT8951Class::updateArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t mode) {
    uint16_t alignedX = x & ~1;
    uint16_t alignedW = (w + 1) & ~1;

    if (alignedX + alignedW > _panelW) alignedW = _panelW - alignedX;
    if (y + h > _panelH) h = _panelH - y;
    if (alignedW == 0 || h == 0) return;

    IT8951LdImgInfo imgInfo = {
        .endianType = _endianType, 
        .pixelFormat = 4,             // 4bpp
        .rotate = 0,
        .pSrcBufferAddr = nullptr, .pBufAddr = nullptr
    };
    IT8951AreaImgInfo areaInfo = {
        .areaX = alignedX, .areaY = y,
        .areaW = alignedW, .areaH = h
    };

    // Завантажуємо дані області
    loadImageStart(&imgInfo, &areaInfo);
    loadAreaPixels_4bpp(_imgBuf, alignedX, y, alignedW, h);
    loadImageEnd();

    // Оновлюємо область на екрані
    displayArea(alignedX, y, alignedW, h, mode);
}

// ── Графічні функції canvas (4bpp формат) ──────────────────────

void IT8951Class::drawPixel(uint16_t x, uint16_t y, uint8_t color) {
    if (x >= _panelW || y >= _panelH) return;
    
    uint32_t idx = ((uint32_t)y * _panelW + x) / 2;
    uint8_t c4 = color >> 4; // 8bpp -> 4bpp
    
    if (x % 2 == 0) {
        _imgBuf[idx] = (_imgBuf[idx] & 0x0F) | (c4 << 4); // Старший нібл (лівий піксель)
    } else {
        _imgBuf[idx] = (_imgBuf[idx] & 0xF0) | c4;        // Молодший нібл (правий піксель)
    }
}

void IT8951Class::drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color, bool fill) {
    if (fill) {
        uint8_t c4 = color >> 4;
        uint8_t byteColor = (c4 << 4) | c4;
        
        for (uint16_t r = 0; r < h; r++) {
            if (y + r >= _panelH) break;
            
            // Оптимізація memset для парних координат
            if (x % 2 == 0 && w % 2 == 0) {
                uint32_t idx = ((uint32_t)(y + r) * _panelW + x) / 2;
                memset(_imgBuf + idx, byteColor, w / 2);
            } else {
                for (uint16_t c = 0; c < w; c++) {
                    drawPixel(x + c, y + r, color);
                }
            }
        }
    } else {
        for (uint16_t i = 0; i < w; i++) {
            drawPixel(x + i, y, color);
            drawPixel(x + i, y + h - 1, color);
        }
        for (uint16_t i = 0; i < h; i++) {
            drawPixel(x, y + i, color);
            drawPixel(x + w - 1, y + i, color);
        }
    }
}

void IT8951Class::drawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint8_t color, bool fill) {
    if (fill) {
        for (int y = -r; y <= r; y++) {
            for (int x = -r; x <= r; x++) {
                if (x*x + y*y <= r*r) {
                    drawPixel(x0 + x, y0 + y, color);
                }
            }
        }
    } else {
        int x = r;
        int y = 0;
        int err = 0;

        while (x >= y) {
            drawPixel(x0 + x, y0 + y, color);
            drawPixel(x0 + y, y0 + x, color);
            drawPixel(x0 - y, y0 + x, color);
            drawPixel(x0 - x, y0 + y, color);
            drawPixel(x0 - x, y0 - y, color);
            drawPixel(x0 - y, y0 - x, color);
            drawPixel(x0 + y, y0 - x, color);
            drawPixel(x0 + x, y0 - y, color);

            if (err <= 0) {
                y += 1;
                err += 2*y + 1;
            }
            if (err > 0) {
                x -= 1;
                err -= 2*x + 1;
            }
        }
    }
}

void IT8951Class::drawChar(uint16_t x, uint16_t y, char c, uint8_t color, uint8_t bgColor, uint8_t scale) {
    if (c < 32 || c > 127) c = '?';
    uint16_t fontIdx = c - 32;
    
    for (uint8_t row = 0; row < 16; row++) {
        uint8_t rowBits = font8x16[fontIdx][row];
        for (uint8_t col = 0; col < 8; col++) {
            bool pixelOn = (rowBits & (1 << (7 - col))) != 0;
            uint8_t pixColor = pixelOn ? color : bgColor;
            
            if (scale == 1) {
                drawPixel(x + col, y + row, pixColor);
            } else {
                drawRect(x + col * scale, y + row * scale, scale, scale, pixColor, true);
            }
        }
    }
}

void IT8951Class::drawText(uint16_t x, uint16_t y, const char* str, uint8_t color, uint8_t bgColor, uint8_t scale) {
    uint16_t curX = x;
    uint16_t curY = y;
    while (*str) {
        if (*str == '\n') {
            curX = x;
            curY += 16 * scale;
        } else {
            drawChar(curX, curY, *str, color, bgColor, scale);
            curX += 8 * scale;
        }
        str++;
    }
}

void IT8951Class::drawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* data) {
    for (uint16_t r = 0; r < h; r++) {
        if (y + r >= _panelH) break;
        for (uint16_t c = 0; c < w; c++) {
            if (x + c >= _panelW) break;
            uint8_t color = data[r * w + c];
            drawPixel(x + c, y + r, color);
        }
    }
}

void IT8951Class::drawCyrillicText(uint16_t x, uint16_t y, const char* str, uint8_t color, uint8_t bgColor, uint8_t scale, bool transparent) {
    uint16_t curX = x;
    uint16_t curY = y;
    
    const char* p = str;
    while (*p) {
        if (*p == '\n') {
            curX = x;
            curY += irpinFont.yAdvance * scale;
            p++;
            continue;
        }
        
        uint8_t c = utf8_to_cp1251(p);
        
        // Зчитуємо інформацію про гліф з PROGMEM
        IrpinGlyph glyph;
        memcpy_P(&glyph, &irpinFont.glyphs[c], sizeof(IrpinGlyph));
        
        if (glyph.xAdvance > 0) {
            if (!transparent) {
                uint16_t ascent = irpinFont.yAdvance * 3 / 4;
                drawRect(curX, curY - ascent * scale, glyph.xAdvance * scale, irpinFont.yAdvance * scale, bgColor, true);
            }
            
            if (glyph.width > 0 && glyph.height > 0) {
                uint8_t row_bytes = (glyph.width + 7) / 8;
                
                for (uint8_t r = 0; r < glyph.height; r++) {
                    uint16_t pixelY = curY + (glyph.yOffset + r) * scale;
                    if (pixelY >= _panelH) continue;
                    
                    for (uint8_t col = 0; col < glyph.width; col++) {
                        uint16_t pixelX = curX + (glyph.xOffset + col) * scale;
                        if (pixelX >= _panelW) continue;
                        
                        uint16_t byte_idx = glyph.bitmapOffset + r * row_bytes + (col / 8);
                        uint8_t bit_pos = 7 - (col % 8);
                        
                        uint8_t bitmap_byte = pgm_read_byte(&irpinFont.bitmap[byte_idx]);
                        bool pixelOn = (bitmap_byte & (1 << bit_pos)) != 0;
                        
                        if (pixelOn) {
                            if (scale == 1) {
                                drawPixel(pixelX, pixelY, color);
                            } else {
                                drawRect(pixelX, pixelY, scale, scale, color, true);
                            }
                        }
                    }
                }
            }
            
            curX += glyph.xAdvance * scale;
        }
    }
}

size_t IT8951Class::getCyrillicTextWidth(const char* str, uint8_t scale) {
    size_t maxWidth = 0;
    size_t currentLineWidth = 0;
    const char* p = str;
    while (*p) {
        if (*p == '\n') {
            if (currentLineWidth > maxWidth) {
                maxWidth = currentLineWidth;
            }
            currentLineWidth = 0;
            p++;
            continue;
        }
        uint8_t c = utf8_to_cp1251(p);
        
        IrpinGlyph glyph;
        memcpy_P(&glyph, &irpinFont.glyphs[c], sizeof(IrpinGlyph));
        currentLineWidth += glyph.xAdvance * scale;
    }
    if (currentLineWidth > maxWidth) {
        maxWidth = currentLineWidth;
    }
    return maxWidth;
}

uint8_t IT8951Class::getPixel(uint16_t x, uint16_t y) {
    if (x >= _panelW || y >= _panelH || !_imgBuf) return 255;
    uint32_t idx = ((uint32_t)y * _panelW + x) / 2;
    if (x % 2 == 0) {
        return (_imgBuf[idx] >> 4) << 4;
    } else {
        return (_imgBuf[idx] & 0x0F) << 4;
    }
}

bool IT8951Class::loadTTF(fs::FS &fs, const char* path, uint16_t pixelHeight) {
    unloadTTF();
    
    File file = fs.open(path, "r");
    if (!file) {
        Serial.printf("[TTF] Error opening file: %s\n", path);
        return false;
    }
    
    size_t size = file.size();
    _ttfBuffer = (unsigned char*)ps_malloc(size);
    if (!_ttfBuffer) {
        Serial.printf("[TTF] Error allocating PSRAM buffer of size %d bytes\n", size);
        file.close();
        return false;
    }
    
    size_t bytesRead = file.read(_ttfBuffer, size);
    file.close();
    
    if (bytesRead != size) {
        Serial.printf("[TTF] Error reading file: read %d of %d bytes\n", bytesRead, size);
        free(_ttfBuffer);
        _ttfBuffer = nullptr;
        return false;
    }
    
    stbtt_fontinfo* info = (stbtt_fontinfo*)malloc(sizeof(stbtt_fontinfo));
    if (!info) {
        Serial.println("[TTF] Error allocating stbtt_fontinfo structure");
        free(_ttfBuffer);
        _ttfBuffer = nullptr;
        return false;
    }
    
    if (!stbtt_InitFont(info, _ttfBuffer, 0)) {
        Serial.println("[TTF] stbtt_InitFont failed");
        free(info);
        free(_ttfBuffer);
        _ttfBuffer = nullptr;
        return false;
    }
    
    _fontInfoRaw = (void*)info;
    _ttfHeight = pixelHeight;
    _ttfScale = stbtt_ScaleForPixelHeight(info, (float)pixelHeight);
    _ttfLoaded = true;
    
    Serial.printf("[TTF] Font %s successfully loaded. Height: %d px, Scale: %f\n", path, pixelHeight, _ttfScale);
    return true;
}

void IT8951Class::unloadTTF() {
    if (_ttfLoaded) {
        if (_fontInfoRaw) {
            free(_fontInfoRaw);
            _fontInfoRaw = nullptr;
        }
        if (_ttfBuffer) {
            free(_ttfBuffer);
            _ttfBuffer = nullptr;
        }
        _ttfLoaded = false;
        _ttfHeight = 0;
        _ttfScale = 0.0f;
        Serial.println("[TTF] Font unloaded.");
    }
}

static uint32_t decodeUTF8(const char* &s) {
    uint8_t c = (uint8_t)*s++;
    if (c < 0x80) {
        return c;
    } else if ((c & 0xE0) == 0xC0) {
        uint32_t cp = (c & 0x1F) << 6;
        if (*s) cp |= (*s++ & 0x3F);
        return cp;
    } else if ((c & 0xF0) == 0xE0) {
        uint32_t cp = (c & 0x0F) << 12;
        if (*s) cp |= (*s++ & 0x3F) << 6;
        if (*s) cp |= (*s++ & 0x3F);
        return cp;
    } else if ((c & 0xF8) == 0xF0) {
        uint32_t cp = (c & 0x07) << 18;
        if (*s) cp |= (*s++ & 0x3F) << 12;
        if (*s) cp |= (*s++ & 0x3F) << 6;
        if (*s) cp |= (*s++ & 0x3F);
        return cp;
    }
    return '?';
}

void IT8951Class::drawTTFText(uint16_t x, uint16_t y, const char* str, uint8_t color, uint8_t bgColor, bool transparent) {
    if (!_ttfLoaded || !_fontInfoRaw) return;
    stbtt_fontinfo* info = (stbtt_fontinfo*)_fontInfoRaw;
    
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    int fontHeight = (int)((ascent - descent + lineGap) * _ttfScale);
    
    if (!transparent) {
        size_t w = getTTFTextWidth(str);
        int lines = 1;
        const char* p = str;
        while (*p) {
            if (*p == '\n') lines++;
            p++;
        }
        drawRect(x, y - (int)(ascent * _ttfScale), w, fontHeight * lines, bgColor, true);
    }
    
    int curX = x;
    int curY = y;
    const char* p = str;
    uint32_t prev_cp = 0;
    
    while (*p) {
        if (*p == '\n') {
            curX = x;
            curY += fontHeight;
            p++;
            prev_cp = 0;
            continue;
        }
        
        uint32_t cp = decodeUTF8(p);
        
        if (cp == '\t') {
            int space_width;
            stbtt_GetCodepointHMetrics(info, ' ', &space_width, nullptr);
            curX += (int)(space_width * _ttfScale) * 4;
            prev_cp = ' ';
            continue;
        }
        if (cp < 32) {
            continue;
        }
        
        if (prev_cp != 0) {
            int kern = stbtt_GetCodepointKernAdvance(info, prev_cp, cp);
            curX += (int)(kern * _ttfScale);
        }
        
        int advanceWidth, leftSideBearing;
        stbtt_GetCodepointHMetrics(info, cp, &advanceWidth, &leftSideBearing);
        
        int w, h, xoff, yoff;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(info, _ttfScale, _ttfScale, cp, &w, &h, &xoff, &yoff);
        
        if (bitmap) {
            for (int r = 0; r < h; r++) {
                int pixelY = curY + yoff + r;
                if (pixelY < 0 || pixelY >= _panelH) continue;
                
                for (int c = 0; c < w; c++) {
                    int pixelX = curX + xoff + c;
                    if (pixelX < 0 || pixelX >= _panelW) continue;
                    
                    uint8_t alpha = bitmap[r * w + c];
                    if (alpha > 0) {
                        if (transparent) {
                            uint8_t bg = getPixel(pixelX, pixelY);
                            uint8_t blendedColor = (alpha * color + (255 - alpha) * bg) / 255;
                            drawPixel(pixelX, pixelY, blendedColor);
                        } else {
                            uint8_t blendedColor = (alpha * color + (255 - alpha) * bgColor) / 255;
                            drawPixel(pixelX, pixelY, blendedColor);
                        }
                    }
                }
            }
            stbtt_FreeBitmap(bitmap, NULL);
        }
        
        curX += (int)(advanceWidth * _ttfScale);
        prev_cp = cp;
    }
}

size_t IT8951Class::getTTFTextWidth(const char* str) {
    if (!_ttfLoaded || !_fontInfoRaw) return 0;
    stbtt_fontinfo* info = (stbtt_fontinfo*)_fontInfoRaw;
    
    size_t maxWidth = 0;
    size_t currentLineWidth = 0;
    const char* p = str;
    uint32_t prev_cp = 0;
    
    while (*p) {
        if (*p == '\n') {
            if (currentLineWidth > maxWidth) {
                maxWidth = currentLineWidth;
            }
            currentLineWidth = 0;
            p++;
            prev_cp = 0;
            continue;
        }
        
        uint32_t cp = decodeUTF8(p);
        
        if (cp == '\t') {
            int space_width;
            stbtt_GetCodepointHMetrics(info, ' ', &space_width, nullptr);
            currentLineWidth += (int)(space_width * _ttfScale) * 4;
            prev_cp = ' ';
            continue;
        }
        if (cp < 32) {
            continue;
        }
        
        if (prev_cp != 0) {
            int kern = stbtt_GetCodepointKernAdvance(info, prev_cp, cp);
            currentLineWidth += (int)(kern * _ttfScale);
        }
        
        int advanceWidth, leftSideBearing;
        stbtt_GetCodepointHMetrics(info, cp, &advanceWidth, &leftSideBearing);
        
        currentLineWidth += (int)(advanceWidth * _ttfScale);
        prev_cp = cp;
    }
    
    if (currentLineWidth > maxWidth) {
        maxWidth = currentLineWidth;
    }
    return maxWidth;
}


