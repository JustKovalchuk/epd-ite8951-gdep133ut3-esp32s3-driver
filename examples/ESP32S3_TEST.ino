// ============================================================
//  ESP32S3_TEST — Інтерактивна графічна консоль E-Ink
//  Плата: ESP32S3 Dev Module + DEJA-TC103 + GDEP133UT3 (1600×1200)
//  PSRAM: OPI PSRAM (ОБОВ'ЯЗКОВО УВІМКНУТИ В ARDUINO IDE)
// ============================================================

#include <Arduino.h>
#include <LittleFS.h>
#include "IT8951.h"
#include "image_data.h"

// Значення VCOM (наприклад, 2330 для -2.33V)
#define VCOM_VALUE      2330

// Створюємо глобальний об'єкт дисплея
IT8951Class display;

static uint8_t  g_endianType = 1;      // 1 = Big Endian (Рекомендовано)
static uint16_t g_defaultMode = UPDATE_MODE_GC16; // Стандартний режим оновлення

// Стан автотаймера
static bool autoPartialUpdate = false;
static uint32_t lastPartialTime = 0;
static uint32_t uptimeSeconds = 0;
static uint16_t timerRefreshMode = UPDATE_MODE_DU; // Режим оновлення таймера

// ── Друк довідки в Serial ──────────────────────────────────
void printHelp() {
    Serial.println("\n=== E-INK CLASS LIBRARY GRAPHICS TERMINAL ===");
    Serial.println("Вводьте команди в Serial Monitor (завершуйте символом нового рядка):");
    Serial.println("\n--- НАЛАШТУВАННЯ ---");
    Serial.println("  help            - Показати це меню довідки");
    Serial.println("  mode <0-2>      - Встановити стандартний режим оновлення:");
    Serial.println("                      0: INIT  (Повне очищення, блимає, повільно)");
    Serial.println("                      1: WDU   (Швидке ч/б оновлення, 1-bit, без блимання)");
    Serial.println("                      2: GC16  (Повноцінне сіре 16-рівнів, 4-bit, РЕКОМЕНДОВАНО)");
    Serial.println("                      * Режими 3-7 не підтримуються даним екраном.");
    Serial.println("  endian <0/1>    - Зміна порядку байт (0: Little, 1: Big Endian)");
    Serial.println("  temp [C]        - Задати температуру (без параметрів - зчитати вбудований термодатчик)");
    Serial.println("  vcom            - Зчитати поточне значення напруги VCOM з PMIC");
    Serial.println("  busy            - Зчитати статус активності рушія малювання LUT (0x1224)");
    Serial.println("  info            - Вивести детальну інформацію про пристрій (FW, LUT, розміри, буфер)");
    Serial.println("\n--- ЗАГАЛЬНІ ОПЕРАЦІЇ ---");
    Serial.println("  clear           - Залити буфер білим та оновити екран повністю");
    Serial.println("  init            - Апаратний цикл ініціалізації панелі (INIT clear)");
    Serial.println("  dashboard       - Перемалювати тестовий дашборд у буфер та оновити");
    Serial.println("  sleep           - Перевести дисплей у режим сну (з вимкненням VCOM)");
    Serial.println("  wakeup          - Пробудити дисплей зі сну (з увімкненням VCOM)");
    Serial.println("  testmodes       - Запустити тест всіх режимів оновлення з виміром часу");
    Serial.println("\n--- АВТОМАТИЧНИЙ ТАЙМЕР ---");
    Serial.println("  timer <0/1> [m] - Увімкнути(1)/Вимкнути(0) автотаймер. Опціонально [m] задає режим (наприклад: 'timer 1 1' для WDU)");
    Serial.println("\n--- МАЛЮВАННЯ ТА ЛОКАЛЬНЕ ОНОВЛЕННЯ (Координати та параметри) ---");
    Serial.println("  Для якісного виводу використовуйте режим 2 (GC16), для швидкого тексту - 1 (WDU)");
    Serial.println("  update <x> <y> <w> <h> <mode>           - Оновити область з буфера (без виділення пам'яті)");
    Serial.println("  rect <x> <y> <w> <h> <col> <fill> <m>   - Намалювати прямокутник та оновити область");
    Serial.println("  circle <cx> <cy> <r> <col> <fill> <m>   - Намалювати коло та оновити область");
    Serial.println("  text <x> <y> <scale> <col> <bg> <msg>   - Намалювати текст та оновити область");
    Serial.println("  cyrtext <x> <y> <scale> <col> <bg> <tr 0/1> <msg> - Намалювати красиву кирилицю (CP1251)");
    Serial.println("  loadttf <height> <path>                            - Завантажити новий TTF шрифт з LittleFS");
    Serial.println("  ttftext <x> <y> <color> <bg> <mode> <tr 0/1> <msg> - Намалювати текст шрифтом TTF з згладжуванням");
    Serial.println("=============================================================================");
}

// ── Малювання тестового дашборду ──────────────────────────
void drawDashboard() {
    Serial.println("[TEST] Rendering dashboard in PSRAM canvas...");
    
    // 1. Очистити локальний canvas у PSRAM (без виводу на екран)
    display.fill(0xFF);
    
    // 2. Верхній заголовок (Banner)
    display.drawRect(0, 0, display.getWidth(), 60, 0x20, true); 
    if (display.getTTFHeight() > 0) {
        display.drawTTFText(20, 42, "IT8951 ДИНАМІЧНИЙ TTF ШРИФТ З LITTLEFS - 1600x1200", 0xFF, 0x20, true);
    } else {
        display.drawCyrillicText(20, 42, "IT8951 ТЕСТ КИРИЛИЦІ (Ірпінь, Київ, Україна) - 1600x1200", 0xFF, 0x20, 1, true);
    }
    
    // 3. Інформаційна панель
    display.drawRect(10, 70, display.getWidth() - 20, 45, 0xF0, true);
    display.drawRect(10, 70, display.getWidth() - 20, 45, 0x00, false);
    char infoText[150];
    sprintf(infoText, "Resolution: %dx%d px | VCOM: -%d mV | DefMode: %d | Endianness: %s (Type %d) | TimerMode: %d", 
            display.getWidth(), display.getHeight(), VCOM_VALUE, g_defaultMode, (g_endianType == 1) ? "Big Endian" : "Little Endian", g_endianType, timerRefreshMode);
    display.drawText(20, 85, infoText, 0x00, 0xF0, 1);
    
    // 4. Ліва область: Тест геометрії та лінійності (Grid & Circles)
    uint16_t gridX = 40;
    uint16_t gridY = 140;
    uint16_t gridW = 500;
    uint16_t gridH = 500;
    
    display.drawText(gridX, gridY - 25, "GEOMETRY & LINEARITY TEST (50px GRID)", 0x00, 0xFF, 1);
    display.drawRect(gridX, gridY, gridW, gridH, 0x00, false);
    
    for (uint16_t offset = 50; offset < gridW; offset += 50) {
        for (uint16_t py = 0; py < gridH; py++) {
            display.drawPixel(gridX + offset, gridY + py, 0xCC); 
        }
        for (uint16_t px = 0; px < gridW; px++) {
            display.drawPixel(gridX + px, gridY + offset, 0xCC);
        }
    }
    
    uint16_t cx = gridX + gridW / 2;
    uint16_t cy = gridY + gridH / 2;
    display.drawCircle(cx, cy, 220, 0x00, false);
    display.drawCircle(cx, cy, 180, 0x40, false);
    display.drawCircle(cx, cy, 120, 0x80, false);
    display.drawCircle(cx, cy, 60, 0xC0, false);
    display.drawCircle(cx, cy, 10, 0x00, true);
    
    // 5. Середня область: Тест шрифтів та градієнтів контрасту
    uint16_t txtX = 580;
    uint16_t txtY = 140;
    display.drawText(txtX, txtY - 25, "TYPOGRAPHY & GRAYSCALE LEVELS", 0x00, 0xFF, 1);
    
    display.drawText(txtX, txtY, "Scale 1: E-Ink Carta 1000", 0x00, 0xFF, 1);
    display.drawText(txtX, txtY + 25, "Scale 2: DEJA-TC103 Driver", 0x00, 0xFF, 2);
    display.drawText(txtX, txtY + 65, "Scale 3: ESP32-S3 SPI", 0x00, 0xFF, 3);
    
    display.drawCyrillicText(txtX, txtY + 145, "Демонстрація контрасту (шрифт IrpinType 32px):", 0x00, 0xFF, 1, true);
    display.drawCyrillicText(txtX, txtY + 175, "  Колір 0x00 (Чорний)", 0x00, 0xFF, 1, true);
    display.drawCyrillicText(txtX, txtY + 205, "  Колір 0x80 (Сірий)", 0x80, 0xFF, 1, true);
    display.drawCyrillicText(txtX, txtY + 235, "  Колір 0xC0 (Світло-сірий)", 0xC0, 0xFF, 1, true);
    
    display.drawRect(txtX, txtY + 245, 450, 40, 0x00, false);
    for (uint16_t i = 0; i < 448; i++) {
        uint8_t color = (uint8_t)((uint32_t)i * 255 / 447);
        for (uint16_t h = 0; h < 38; h++) {
            display.drawPixel(txtX + 1 + i, txtY + 246 + h, color);
        }
    }
    
    // 6. Нижня середня область: Вбудоване растрове зображення сови
    uint16_t imgX = 580;
    uint16_t imgY = 460;
    display.drawText(imgX, imgY - 25, "REAL RASTER IMAGE (256x256 Grayscale)", 0x00, 0xFF, 1);
    display.drawRect(imgX - 2, imgY - 2, owl_width + 4, owl_height + 4, 0x00, false);
    display.drawImage(imgX, imgY, owl_width, owl_height, owl_image_data);
    
    // 7. Права область: Часткове оновлення
    uint16_t partX = 1100;
    uint16_t partY = 140;
    uint16_t partW = 460;
    uint16_t partH = 500;
    
    display.drawText(partX, partY - 25, "FAST PARTIAL UPDATE DEMO ZONE", 0x00, 0xFF, 1);
    display.drawRect(partX, partY, partW, partH, 0x00, false);
    display.drawRect(partX + 3, partY + 3, partW - 6, partH - 6, 0x80, false);
    
    display.drawText(partX + 20, partY + 30, "This zone is updated separately", 0x00, 0xFF, 1);
    display.drawText(partX + 20, partY + 50, "Send 'timer 1' to start counting.", 0x00, 0xFF, 1);
    display.drawText(partX + 20, partY + 70, "To fully clear ghosting in updates,", 0x00, 0xFF, 1);
    display.drawText(partX + 20, partY + 90, "use GLD16 (mode 5) or GC16 (mode 2).", 0x00, 0xFF, 1);
    
    display.drawRect(partX + 20, partY + 150, partW - 40, 100, 0x00, false);
    display.drawRect(partX + 21, partY + 151, partW - 42, 98, 0xEE, true); 
    display.drawText(partX + 40, partY + 165, "SYSTEM UPTIME COUNTER", 0x40, 0xEE, 1);
    
    char timeStr[20];
    sprintf(timeStr, "%05d SEC", uptimeSeconds);
    display.drawText(partX + 40, partY + 195, timeStr, 0x00, 0xEE, 3);
    
    // Інструкція для Serial консолі внизу
    uint16_t manualY = 700;
    display.drawText(40, manualY, "SERIAL MONITOR TERMINAL ACTIVE", 0x00, 0xFF, 2);
    display.drawText(40, manualY + 40, "Введіть 'help' у Serial Monitor для перегляду списку команд.", 0x00, 0xFF, 1);
}

// ── Оновлення таймера ────────────────────────────────────────
void drawAndRefreshCounter() {
    uint16_t boxX = 1100 + 40;
    uint16_t boxY = 140 + 195;
    uint16_t boxW = 8 * 3 * 10; 
    uint16_t boxH = 16 * 3;
    
    // Повністю перезаписуємо область на canvas кольором підкладки 0xEE
    display.drawRect(boxX, boxY, boxW, boxH, 0xEE, true);
    
    char timeStr[20];
    sprintf(timeStr, "%05d SEC", uptimeSeconds);
    display.drawText(boxX, boxY, timeStr, 0x00, 0xEE, 3);
    
    // Вибіркове оновлення (завантажує тільки змінену область прямо з PSRAM та оновлює її)
    display.updateArea(boxX, boxY, boxW, boxH, timerRefreshMode);
}

// ── Тестування всіх режимів оновлення (Waveform Modes 0-7) ───
void runModesTest() {
    Serial.println("\n==============================================");
    Serial.println(" Запуск діагностичного тесту режимів оновлення...");
    Serial.println("==============================================");

    // 1. Повне очищення екрана перед тестом
    display.clearScreen();
    delay(500);

    // Очищаємо локальний canvas у PSRAM білим кольором
    display.fill(0xFF);

    const char* modeNames[8] = {
        "INIT (Mode 0)",
        "DU (Mode 1)",
        "GC16 (Mode 2)",
        "GL16 (Mode 3)",
        "GLR16 (Mode 4)",
        "GLD16 (Mode 5)",
        "A2 (Mode 6)",
        "DU4 (Mode 7)"
    };

    uint32_t timings[8] = {0};
    bool success[8] = {false};

    // Розміри сітки 4x2 для 1600x1200 екрану
    // Кожен осередк (cell) має розмір 400x600
    uint16_t cellW = 400;
    uint16_t cellH = 600;

    for (int m = 0; m < 8; m++) {
        uint16_t row = m / 4;
        uint16_t col = m % 4;
        uint16_t cellX = col * cellW;
        uint16_t cellY = row * cellH;

        Serial.printf("[TEST] Малювання осередку для %s на (%d, %d)...\n", modeNames[m], cellX, cellY);

        // Малюємо рамку осередку
        display.drawRect(cellX, cellY, cellW, cellH, 0x00, false);

        // Шапка осередку (заголовок режиму)
        display.drawRect(cellX + 2, cellY + 2, cellW - 4, 38, 0xE0, true);
        display.drawText(cellX + 10, cellY + 12, modeNames[m], 0x00, 0xE0, 1);

        // Малюємо зображення сови по центру
        uint16_t owlX = cellX + (cellW - owl_width) / 2; // 400 - 256 = 144 / 2 = 72
        uint16_t owlY = cellY + 60;
        display.drawImage(owlX, owlY, owl_width, owl_height, owl_image_data);

        // Плавний градієнт
        uint16_t gradX = cellX + 20;
        uint16_t gradY = cellY + 345;
        uint16_t gradW = cellW - 40; // 360
        uint16_t gradH = 30;
        display.drawRect(gradX, gradY, gradW, gradH, 0x00, false);
        for (uint16_t i = 0; i < gradW - 2; i++) {
            uint8_t color = (uint8_t)(i * 255 / (gradW - 3));
            for (uint16_t py = 0; py < gradH - 2; py++) {
                display.drawPixel(gradX + 1 + i, gradY + 1 + py, color);
            }
        }

        // Мітки під градієнтом
        display.drawText(cellX + 20, cellY + 390, "0x00 (Black) -> 0xFF (White)", 0x00, 0xFF, 1);

        // Чотири квадрати контрасту
        uint16_t sqY = cellY + 430;
        display.drawText(cellX + 20, sqY, "Contrast Levels:", 0x00, 0xFF, 1);

        uint16_t boxY = cellY + 460;
        // Square 1: 0x00 (pure black)
        display.drawRect(cellX + 30, boxY, 60, 40, 0x00, true);
        // Square 2: 0x55 (dark gray)
        display.drawRect(cellX + 120, boxY, 60, 40, 0x55, true);
        // Square 3: 0xAA (light gray)
        display.drawRect(cellX + 210, boxY, 60, 40, 0xAA, true);
        // Square 4: 0xFF (white with black border)
        display.drawRect(cellX + 300, boxY, 60, 40, 0x00, false);
        display.drawRect(cellX + 301, boxY + 1, 58, 38, 0xFF, true);

        // Написи під квадратами
        display.drawText(cellX + 45, cellY + 510, "00", 0x00, 0xFF, 1);
        display.drawText(cellX + 135, cellY + 510, "55", 0x00, 0xFF, 1);
        display.drawText(cellX + 225, cellY + 510, "AA", 0x00, 0xFF, 1);
        display.drawText(cellX + 315, cellY + 510, "FF", 0x00, 0xFF, 1);

        // Вимірюємо час оновлення цієї області за допомогою обраного режиму
        uint32_t tStart = millis();
        display.updateArea(cellX, cellY, cellW, cellH, m);
        uint32_t tEnd = millis();

        timings[m] = tEnd - tStart;
        success[m] = (timings[m] < 9900);

        Serial.printf("[TEST] Оновлення осередку %s завершено за %lu ms (Статус: %s)\n", 
                      modeNames[m], timings[m], success[m] ? "OK" : "TIMEOUT/WARN");

        // Коротка пауза між оновленнями
        delay(500);
    }

    // 2. Друк результатів у вигляді красивої ASCII таблиці
    Serial.println("\n=======================================================");
    Serial.println("  РЕЗУЛЬТАТИ ТЕСТУВАННЯ РЕЖИМІВ ОНОВЛЕННЯ (WAVEFORM)");
    Serial.println("=======================================================");
    Serial.println(" Режим | Назва Режиму   | Час оновлення | Статус");
    Serial.println("-------|----------------|---------------|--------------");
    for (int m = 0; m < 8; m++) {
        char timeBuf[16];
        if (success[m]) {
            sprintf(timeBuf, "%lu ms", timings[m]);
        } else {
            sprintf(timeBuf, ">%lu ms", timings[m]);
        }
        Serial.printf("   %d   | %-14s | %-13s | %s\n", 
                      m, 
                      (m == 0) ? "INIT" : 
                      (m == 1) ? "DU" : 
                      (m == 2) ? "GC16" : 
                      (m == 3) ? "GL16" : 
                      (m == 4) ? "GLR16" : 
                      (m == 5) ? "GLD16" : 
                      (m == 6) ? "A2" : "DU4",
                      timeBuf,
                      success[m] ? "OK" : "TIMEOUT / NOT SUPPORTED");
    }
    Serial.println("=======================================================");
    Serial.println("Примітка: Для DU (1) та A2 (6) зображення має бути двоколірним.");
    Serial.println("Для GC16 (2), GL16 (3), GLD16 (5) мають бути плавні сірі градієнти.\n");
}

// ── setup() ──────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n==============================================");
    Serial.println(" IT8951 Library Test & Verification System");
    Serial.println("==============================================");

    // Ініціалізація об'єкта бібліотеки (виділення PSRAM та запуск SPI відбувається всередині)
    if (!display.begin(VCOM_VALUE)) {
        Serial.println("[ERROR] Failed to initialize IT8951 display library!");
        while (1) { delay(1000); }
    }
    
    display.setEndianness(g_endianType);

    // Ініціалізація LittleFS для завантаження TTF шрифтів
    if (!LittleFS.begin(true)) {
        Serial.println("[LittleFS] Error mounting LittleFS! Auto-formatting enabled.");
    } else {
        Serial.println("[LittleFS] Mount successful.");
        // Пробуємо завантажити стандартний TTF
        if (display.loadTTF(LittleFS, "/IrpinType-Regular.ttf", 10)) {
            Serial.println("[TTF] Loaded IrpinType-Regular.ttf at height 10px successfully.");
        } else {
            Serial.println("[TTF] Standard font IrpinType-Regular.ttf not found in LittleFS root.");
        }
    }

    // Перше повне очищення екрана
    display.clearScreen();
    delay(500);

    // Малюємо та виводимо початковий дашборд
    drawDashboard();
    display.display(g_defaultMode);
    
    printHelp();
}

// ── loop() ───────────────────────────────────────────────────
void loop() {
    // Обробка таймера
    if (autoPartialUpdate) {
        uint32_t now = millis();
        if (now - lastPartialTime >= 1000) {
            lastPartialTime = now;
            uptimeSeconds++;
            drawAndRefreshCounter();
        }
    }

    // Інтерактивна консоль
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) return;

        Serial.printf("\n> %s\n", line.c_str());

        int firstSpace = line.indexOf(' ');
        String cmd = (firstSpace == -1) ? line : line.substring(0, firstSpace);
        String args = (firstSpace == -1) ? "" : line.substring(firstSpace + 1);
        cmd.toLowerCase();

        if (cmd == "help" || cmd == "h") {
            printHelp();
        }
        else if (cmd == "mode") {
            int mode = args.toInt();
            if (mode >= 0 && mode <= 7) {
                g_defaultMode = mode;
                Serial.printf("[SET] Default refresh mode set to: %d\n", g_defaultMode);
            } else {
                Serial.println("[ERR] Invalid mode. Choose 0-7.");
            }
        }
        else if (cmd == "endian") {
            int endVal = args.toInt();
            if (endVal == 0 || endVal == 1) {
                g_endianType = endVal;
                display.setEndianness(g_endianType);
                Serial.printf("[SET] Endianness set to: %d (%s)\n", g_endianType, (g_endianType == 1) ? "Big Endian" : "Little Endian");
            } else {
                Serial.println("[ERR] Choose 0 or 1.");
            }
        }
        else if (cmd == "temp") {
            if (args.length() > 0) {
                int tempVal = args.toInt();
                display.setTemperature(tempVal);
            } else {
                int8_t sensorTemp = display.readTemperature();
                Serial.printf("[INFO] Sensor temperature from DEJA sensor: %d C\n", sensorTemp);
            }
        }
        else if (cmd == "vcom") {
            uint16_t vcomVal = display.getVCOM();
            Serial.printf("[INFO] Current VCOM voltage: -%d mV\n", vcomVal);
        }
        else if (cmd == "busy") {
            bool busy = display.isEngineBusy();
            Serial.printf("[INFO] LUT Engine status: %s\n", busy ? "BUSY (Refreshing)" : "FREE (Idle)");
        }
        else if (cmd == "info") {
            IT8951DevInfo info;
            display.getDeviceInfo(&info);
            Serial.println("\n=== IT8951 DEVICE INFO ===");
            Serial.printf("  Resolution  : %d x %d px\n", info.panelW, info.panelH);
            Serial.printf("  Buffer Addr : 0x%08X\n", display.getImgBufAddr());
            
            Serial.print("  FW Version  : ");
            for (int i = 0; i < 8; i++) {
                Serial.printf("%04X ", info.fwVersion[i]);
            }
            Serial.print(" (ASCII: ");
            for (int i = 0; i < 8; i++) {
                char c1 = info.fwVersion[i] & 0xFF;
                char c2 = info.fwVersion[i] >> 8;
                Serial.print(isprint((unsigned char)c1) ? c1 : '.');
                Serial.print(isprint((unsigned char)c2) ? c2 : '.');
            }
            Serial.println(")");

            Serial.print("  LUT Version : ");
            for (int i = 0; i < 8; i++) {
                Serial.printf("%04X ", info.lutVersion[i]);
            }
            Serial.print(" (ASCII: ");
            for (int i = 0; i < 8; i++) {
                char c1 = info.lutVersion[i] & 0xFF;
                char c2 = info.lutVersion[i] >> 8;
                Serial.print(isprint((unsigned char)c1) ? c1 : '.');
                Serial.print(isprint((unsigned char)c2) ? c2 : '.');
            }
            Serial.println(")");
            Serial.println("==========================");
        }
        else if (cmd == "clear") {
            Serial.println("Clearing screen...");
            display.clear(0xFF, g_defaultMode);
            Serial.println("Cleared.");
        }
        else if (cmd == "init") {
            Serial.println("Triggering hardware INIT sequence...");
            display.clearScreen();
        }
        else if (cmd == "dashboard") {
            drawDashboard();
            display.display(g_defaultMode);
        }
        else if (cmd == "sleep") {
            display.sleep();
        }
        else if (cmd == "wakeup") {
            display.wakeup();
        }
        else if (cmd == "testmodes") {
            runModesTest();
        }
        else if (cmd == "timer") {
            int spaceIdx = args.indexOf(' ');
            String arg1 = (spaceIdx == -1) ? args : args.substring(0, spaceIdx);
            String arg2 = (spaceIdx == -1) ? "" : args.substring(spaceIdx + 1);
            
            int enable = arg1.toInt();
            autoPartialUpdate = (enable == 1);
            if (arg2.length() > 0) {
                timerRefreshMode = arg2.toInt();
            }
            lastPartialTime = millis();
            Serial.printf("[SET] Timer active: %s, using mode: %d\n", autoPartialUpdate ? "YES" : "NO", timerRefreshMode);
        }
        // update <x> <y> <w> <h> <mode>
        else if (cmd == "update") {
            int vals[5];
            int parsed = 0;
            String remain = args;
            for (int i = 0; i < 5; i++) {
                int sp = remain.indexOf(' ');
                if (sp == -1 && remain.length() > 0) {
                    vals[i] = remain.toInt();
                    parsed++;
                    break;
                } else if (sp != -1) {
                    vals[i] = remain.substring(0, sp).toInt();
                    remain = remain.substring(sp + 1);
                    parsed++;
                }
            }
            if (parsed == 5) {
                Serial.printf("Triggering partial update at (%d,%d), size %dx%d with mode %d\n", vals[0], vals[1], vals[2], vals[3], vals[4]);
                display.updateArea(vals[0], vals[1], vals[2], vals[3], vals[4]);
            } else {
                Serial.println("[ERR] Usage: update <x> <y> <w> <h> <mode>");
            }
        }
        // rect <x> <y> <w> <h> <color> <fill> <mode>
        else if (cmd == "rect") {
            int vals[7];
            int parsed = 0;
            String remain = args;
            for (int i = 0; i < 7; i++) {
                int sp = remain.indexOf(' ');
                if (sp == -1 && remain.length() > 0) {
                    vals[i] = remain.toInt();
                    parsed++;
                    break;
                } else if (sp != -1) {
                    vals[i] = remain.substring(0, sp).toInt();
                    remain = remain.substring(sp + 1);
                    parsed++;
                }
            }
            if (parsed == 7) {
                display.drawRect(vals[0], vals[1], vals[2], vals[3], vals[4], vals[5] == 1);
                display.updateArea(vals[0], vals[1], vals[2], vals[3], vals[6]);
                Serial.println("[DRAW] Rectangle drawn and refreshed.");
            } else {
                Serial.println("[ERR] Usage: rect <x> <y> <w> <h> <color> <fill> <mode>");
            }
        }
        // circle <cx> <cy> <r> <color> <fill> <mode>
        else if (cmd == "circle") {
            int vals[6];
            int parsed = 0;
            String remain = args;
            for (int i = 0; i < 6; i++) {
                int sp = remain.indexOf(' ');
                if (sp == -1 && remain.length() > 0) {
                    vals[i] = remain.toInt();
                    parsed++;
                    break;
                } else if (sp != -1) {
                    vals[i] = remain.substring(0, sp).toInt();
                    remain = remain.substring(sp + 1);
                    parsed++;
                }
            }
            if (parsed == 6) {
                display.drawCircle(vals[0], vals[1], vals[2], vals[3], vals[4] == 1);
                uint16_t x = vals[0] - vals[2];
                uint16_t y = vals[1] - vals[2];
                uint16_t size = vals[2] * 2 + 1;
                display.updateArea(x, y, size, size, vals[5]);
                Serial.println("[DRAW] Circle drawn and refreshed.");
            } else {
                Serial.println("[ERR] Usage: circle <cx> <cy> <r> <color> <fill> <mode>");
            }
        }
        // image <x> <y> <mode>
        else if (cmd == "image") {
            int vals[3];
            int parsed = 0;
            String remain = args;
            for (int i = 0; i < 3; i++) {
                int sp = remain.indexOf(' ');
                if (sp == -1 && remain.length() > 0) {
                    vals[i] = remain.toInt();
                    parsed++;
                    break;
                } else if (sp != -1) {
                    vals[i] = remain.substring(0, sp).toInt();
                    remain = remain.substring(sp + 1);
                    parsed++;
                }
            }
            if (parsed == 3) {
                display.drawImage(vals[0], vals[1], owl_width, owl_height, owl_image_data);
                display.updateArea(vals[0], vals[1], owl_width, owl_height, vals[2]);
                Serial.println("[DRAW] Owl image loaded and refreshed.");
            } else {
                Serial.println("[ERR] Usage: image <x> <y> <mode>");
            }
        }
        // text <x> <y> <scale> <color> <bg_color> <message>
        else if (cmd == "text") {
            int vals[5];
            int parsed = 0;
            String remain = args;
            for (int i = 0; i < 5; i++) {
                int sp = remain.indexOf(' ');
                if (sp != -1) {
                    vals[i] = remain.substring(0, sp).toInt();
                    remain = remain.substring(sp + 1);
                    parsed++;
                }
            }
            if (parsed == 5 && remain.length() > 0) {
                int x = vals[0];
                int y = vals[1];
                int scale = vals[2];
                int color = vals[3];
                int bg = vals[4];
                
                display.drawText(x, y, remain.c_str(), color, bg, scale);
                
                uint16_t w = remain.length() * 8 * scale;
                uint16_t h = 16 * scale;
                
                display.updateArea(x, y, w, h, g_defaultMode);
                Serial.printf("[DRAW] Text '%s' drawn and refreshed.\n", remain.c_str());
            } else {
                Serial.println("[ERR] Usage: text <x> <y> <scale> <color> <bg> <message>");
            }
        }
        // cyrtext <x> <y> <scale> <color> <bg_color> <transparent 0/1> <message>
        else if (cmd == "cyrtext") {
            int vals[5];
            int parsed = 0;
            String remain = args;
            for (int i = 0; i < 5; i++) {
                int sp = remain.indexOf(' ');
                if (sp != -1) {
                    vals[i] = remain.substring(0, sp).toInt();
                    remain = remain.substring(sp + 1);
                    parsed++;
                }
            }
            
            // Get transparent parameter
            int sp = remain.indexOf(' ');
            bool transparent = true;
            if (sp != -1) {
                transparent = (remain.substring(0, sp).toInt() != 0);
                remain = remain.substring(sp + 1);
            }
            
            if (parsed == 5 && remain.length() > 0) {
                int x = vals[0];
                int y = vals[1];
                int scale = vals[2];
                int color = vals[3];
                int bg = vals[4];
                
                display.drawCyrillicText(x, y, remain.c_str(), color, bg, scale, transparent);
                
                uint16_t w = display.getCyrillicTextWidth(remain.c_str(), scale);
                uint16_t h = 32 * scale; // 32px font size
                uint16_t yStart = y - (24 * scale); // ascent offset
                
                display.updateArea(x, yStart, w, h, g_defaultMode);
                Serial.printf("[DRAW] Cyrillic text '%s' drawn and refreshed.\n", remain.c_str());
            } else {
                Serial.println("[ERR] Usage: cyrtext <x> <y> <scale> <color> <bg> <transparent 0/1> <message>");
            }
        }
        // loadttf <height> <path>
        else if (cmd == "loadttf") {
            int firstSpace = args.indexOf(' ');
            if (firstSpace != -1) {
                int height = args.substring(0, firstSpace).toInt();
                String path = args.substring(firstSpace + 1);
                path.trim();
                
                if (display.loadTTF(LittleFS, path.c_str(), height)) {
                    Serial.printf("[TTF] Loaded font %s at height %d\n", path.c_str(), height);
                } else {
                    Serial.printf("[TTF] Failed to load font %s\n", path.c_str());
                }
            } else {
                Serial.println("[ERR] Usage: loadttf <height> <path>");
            }
        }
        // ttftext <x> <y> <color> <bg> <mode> <tr 0/1> <msg>
        else if (cmd == "ttftext") {
            int vals[5];
            int parsed = 0;
            String remain = args;
            for (int i = 0; i < 5; i++) {
                int sp = remain.indexOf(' ');
                if (sp != -1) {
                    vals[i] = remain.substring(0, sp).toInt();
                    remain = remain.substring(sp + 1);
                    parsed++;
                }
            }
            
            // Get transparent parameter
            int sp = remain.indexOf(' ');
            bool transparent = true;
            if (sp != -1) {
                transparent = (remain.substring(0, sp).toInt() != 0);
                remain = remain.substring(sp + 1);
            }
            
            if (parsed == 5 && remain.length() > 0) {
                int x = vals[0];
                int y = vals[1];
                int color = vals[2];
                int bg = vals[3];
                int mode = vals[4];
                
                display.drawTTFText(x, y, remain.c_str(), color, bg, transparent);
                
                uint16_t w = display.getTTFTextWidth(remain.c_str());
                uint16_t h = display.getTTFHeight();
                // Estimate baseline top yStart
                uint16_t yStart = y - (h * 3 / 4); // estimate ascent (75% of height)
                
                display.updateArea(x, yStart, w, h, mode);
                Serial.printf("[DRAW] TTF text '%s' drawn and refreshed.\n", remain.c_str());
            } else {
                Serial.println("[ERR] Usage: ttftext <x> <y> <color> <bg> <mode> <tr 0/1> <message>");
            }
        }
        else {
            Serial.println("[ERR] Unknown command. Type 'help' to see instructions.");
        }
    }
}