// ============================================================
//  ESP32S3_TEST — Interactive E-Ink Graphic Console
//  Board: ESP32S3 Dev Module + DEJA-TC103 + GDEP133UT3 (1600×1200)
//  PSRAM: OPI PSRAM (MUST ENABLE IN ARDUINO IDE)
// ============================================================

#include <Arduino.h>
#include <LittleFS.h>
#include "IT8951.h"
#include "image_data.h"

// VCOM value (e.g., 2330 for -2.33V)
#define VCOM_VALUE      2330

// Create global display object
IT8951Class display;

static uint8_t  g_endianType = 1;      // 1 = Big Endian (Recommended)
static uint16_t g_defaultMode = UPDATE_MODE_GC16; // Standard update mode

// Auto-timer state
static bool autoPartialUpdate = false;
static uint32_t lastPartialTime = 0;
static uint32_t uptimeSeconds = 0;
static uint16_t timerRefreshMode = UPDATE_MODE_DU; // Timer update mode
static String g_loadedFontName = "None";

// ── Printing help in Serial ──────────────────────────────────
void printHelp() {
    Serial.println("\n=== E-INK CLASS LIBRARY GRAPHICS TERMINAL ===");
    Serial.println("Enter commands in Serial Monitor (terminate with newline):");
    Serial.println("\n--- SETTINGS ---");
    Serial.println("  help            - Show this help menu");
    Serial.println("  mode <0-2>      - Set default update mode:");
    Serial.println("                      0: INIT  (Full clearing, blinking, slow)");
    Serial.println("                      1: WDU   (Fast black/white update, 1-bit, no blinking)");
    Serial.println("                      2: GC16  (Full grayscale 16-level, 4-bit, RECOMMENDED)");
    Serial.println("                      * Modes 3-7 are not supported by this screen.");
    Serial.println("  endian <0/1>    - Change byte order (0: Little, 1: Big Endian)");
    Serial.println("  temp [C]        - Set temperature (no params - read built-in sensor)");
    Serial.println("  vcom            - Read current VCOM voltage from PMIC");
    Serial.println("  busy            - Read LUT drawing engine busy status (0x1224)");
    Serial.println("  info            - Output detailed device information (FW, LUT, size, buffer)");
    Serial.println("\n--- GENERAL OPERATIONS ---");
    Serial.println("  clear           - Fill buffer with white and fully update the screen");
    Serial.println("  init            - Hardware panel initialization cycle (INIT clear)");
    Serial.println("  dashboard       - Redraw the test dashboard to buffer and update");
    Serial.println("  sleep           - Put display to sleep mode (VCOM off)");
    Serial.println("  wakeup          - Wake up display from sleep mode (VCOM on)");
    Serial.println("  testmodes       - Run test of all update modes with timing measurement");
    Serial.println("\n--- AUTOMATIC TIMER ---");
    Serial.println("  timer <0/1> [m] - Enable(1)/Disable(0) auto-timer. Optional [m] sets mode (e.g. 'timer 1 1' for WDU)");
    Serial.println("\n--- DRAWING & LOCAL UPDATE (Coordinates and parameters) ---");
    Serial.println("  For high quality output use mode 2 (GC16), for fast text - mode 1 (WDU)");
    Serial.println("  update <x> <y> <w> <h> <mode>           - Update area from buffer (no memory allocation)");
    Serial.println("  rect <x> <y> <w> <h> <col> <fill> <m>   - Draw rectangle and update area");
    Serial.println("  circle <cx> <cy> <r> <col> <fill> <m>   - Draw circle and update area");
    Serial.println("  text <x> <y> <scale> <col> <bg> <msg>   - Draw text and update area");
    Serial.println("  cyrtext <x> <y> <scale> <col> <bg> <tr 0/1> <msg> - Draw beautiful Cyrillic text (CP1251)");
    Serial.println("  loadttf <height> <path>                            - Load new TTF font from LittleFS");
    Serial.println("  ttftext <x> <y> <color> <bg> <mode> <tr 0/1> <msg> - Draw anti-aliased TTF text");
    Serial.println("  filetext <x> <y> <color> <bg> <mode> <tr 0/1> <filePath> - Render text from file using TTF");
    Serial.println("  fileimg <x> <y> <w> <h> <bpp 4/8> <mode> <filePath>      - Render image from .bin file (4 or 8 bpp)");
    Serial.println("=============================================================================");
}

// ── Drawing the test dashboard ──────────────────────────
void drawDashboard() {
    Serial.println("[TEST] Rendering dashboard in PSRAM canvas...");
    
    // 1. Clear local canvas in PSRAM (without outputting to screen)
    display.fill(0xFF);
    
    // 2. Top header (Banner)
    display.drawRect(0, 0, display.getWidth(), 60, 0x20, true); 
    if (display.getTTFHeight() > 0) {
        display.drawTTFText(20, 42, "IT8951 DYNAMIC TTF FONT FROM LITTLEFS - 1600x1200", 0xFF, 0x20, true);
    } else {
        display.drawCyrillicText(20, 42, "IT8951 CYRILLIC TEST (Irpin, Kyiv, Ukraine) - 1600x1200", 0xFF, 0x20, 1, true);
    }
    
    // 3. Information panel
    display.drawRect(10, 70, display.getWidth() - 20, 45, 0xF0, true);
    display.drawRect(10, 70, display.getWidth() - 20, 45, 0x00, false);
    char infoText[150];
    sprintf(infoText, "Resolution: %dx%d px | VCOM: -%d mV | DefMode: %d | Endianness: %s (Type %d) | TimerMode: %d", 
            display.getWidth(), display.getHeight(), VCOM_VALUE, g_defaultMode, (g_endianType == 1) ? "Big Endian" : "Little Endian", g_endianType, timerRefreshMode);
    display.drawText(20, 85, infoText, 0x00, 0xF0, 1);
    
    // 4. Left area: Geometry and linearity test (Grid & Circles)
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
    
    // 5. Middle area: Test of fonts and contrast gradients
    uint16_t txtX = 580;
    uint16_t txtY = 140;
    display.drawText(txtX, txtY - 25, "TYPOGRAPHY & GRAYSCALE LEVELS", 0x00, 0xFF, 1);
    
    display.drawText(txtX, txtY, "Scale 1: E-Ink Carta 1000", 0x00, 0xFF, 1);
    display.drawText(txtX, txtY + 25, "Scale 2: DEJA-TC103 Driver", 0x00, 0xFF, 2);
    display.drawText(txtX, txtY + 65, "Scale 3: ESP32-S3 SPI", 0x00, 0xFF, 3);
    
    if (display.getTTFHeight() > 0) {
        display.drawTTFText(txtX, txtY + 145, "Contrast demonstration (IrpinType font):", 0x00, 0xFF, true);
        display.drawTTFText(txtX, txtY + 175, "  Color 0x00 (Black)", 0x00, 0xFF, true);
        display.drawTTFText(txtX, txtY + 205, "  Color 0x80 (Gray)", 0x80, 0xFF, true);
        display.drawTTFText(txtX, txtY + 235, "  Color 0xC0 (Light-gray)", 0xC0, 0xFF, true);
    } else {
        display.drawCyrillicText(txtX, txtY + 145, "Contrast demonstration (built-in font 32px):", 0x00, 0xFF, 1, true);
        display.drawCyrillicText(txtX, txtY + 175, "  Color 0x00 (Black)", 0x00, 0xFF, 1, true);
        display.drawCyrillicText(txtX, txtY + 205, "  Color 0x80 (Gray)", 0x80, 0xFF, 1, true);
        display.drawCyrillicText(txtX, txtY + 235, "  Color 0xC0 (Light-gray)", 0xC0, 0xFF, 1, true);
    }
    
    display.drawRect(txtX, txtY + 245, 450, 40, 0x00, false);
    for (uint16_t i = 0; i < 448; i++) {
        uint8_t color = (uint8_t)((uint32_t)i * 255 / 447);
        for (uint16_t h = 0; h < 38; h++) {
            display.drawPixel(txtX + 1 + i, txtY + 246 + h, color);
        }
    }
    
    // 6. Lower middle area: Embedded raster owl image
    uint16_t imgX = 580;
    uint16_t imgY = 460;
    display.drawText(imgX, imgY - 25, "REAL RASTER IMAGE (256x256 Grayscale)", 0x00, 0xFF, 1);
    display.drawRect(imgX - 2, imgY - 2, owl_width + 4, owl_height + 4, 0x00, false);
    display.drawImage(imgX, imgY, owl_width, owl_height, owl_image_data);
    
    // 7. Right area: Partial update
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
    
    // Serial console instructions at the bottom
    uint16_t manualY = 700;
    display.drawText(40, manualY, "SERIAL MONITOR TERMINAL ACTIVE", 0x00, 0xFF, 2);
    display.drawText(40, manualY + 40, "Enter 'help' in Serial Monitor to view the command list.", 0x00, 0xFF, 1);

    // Alphabet Rendering Test (with Font Selection)
    uint16_t alphY = 780;
    display.drawRect(30, alphY - 10, display.getWidth() - 60, 360, 0x00, false);
    display.drawRect(30, alphY - 10, display.getWidth() - 60, 45, 0xF0, true);
    display.drawRect(30, alphY - 10, display.getWidth() - 60, 45, 0x00, false);
    
    display.drawText(40, alphY + 5, "ALPHABET RENDERING TEST (WITH FONT SELECTION)", 0x00, 0xF0, 1);
    
    char fontInfo[128];
    if (display.getTTFHeight() > 0) {
        sprintf(fontInfo, "Selected Font: TTF Font [%s] (Height: %d px)", g_loadedFontName.c_str(), display.getTTFHeight());
    } else {
        sprintf(fontInfo, "Selected Font: Built-in Cyrillic Font [irpinFont] (Size: 32px)");
    }
    display.drawText(40, alphY + 50, fontInfo, 0x00, 0xFF, 1);
    
    const char* enUpper = "English (Upper): A B C D E F G H I J K L M N O P Q R S T U V W X Y Z";
    const char* enLower = "English (Lower): a b c d e f g h i j k l m n o p q r s t u v w x y z";
    const char* uaUpper = "Ukrainian (Upper): А Б В Г Ґ Д Е Є Ж З И І Ї Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ь Ю Я";
    const char* uaLower = "Ukrainian (Lower): а б в г ґ д е є ж з и і ї й к л м н о п р с т у ф х ц ч ш щ ь ю я";
    const char* specChars = "Symbols: ! @ # $ % ^ & * ( ) _ + - = { } [ ] : ; \" ' < > , . ? / \\ | ~";
    
    if (display.getTTFHeight() > 0) {
        display.drawTTFText(40, alphY + 95, enUpper, 0x00, 0xFF, true);
        display.drawTTFText(40, alphY + 135, enLower, 0x00, 0xFF, true);
        display.drawTTFText(40, alphY + 185, uaUpper, 0x00, 0xFF, true);
        display.drawTTFText(40, alphY + 225, uaLower, 0x00, 0xFF, true);
        display.drawTTFText(40, alphY + 275, specChars, 0x00, 0xFF, true);
    } else {
        display.drawCyrillicText(40, alphY + 95, enUpper, 0x00, 0xFF, 1, true);
        display.drawCyrillicText(40, alphY + 135, enLower, 0x00, 0xFF, 1, true);
        display.drawCyrillicText(40, alphY + 185, uaUpper, 0x00, 0xFF, 1, true);
        display.drawCyrillicText(40, alphY + 225, uaLower, 0x00, 0xFF, 1, true);
        display.drawCyrillicText(40, alphY + 275, specChars, 0x00, 0xFF, 1, true);
    }
}

// ── Timer Update ────────────────────────────────────────
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
    
    // Selective update (loads only the modified area directly from PSRAM and updates it)
    display.updateArea(boxX, boxY, boxW, boxH, timerRefreshMode);
}

// ── Testing all update modes (Waveform Modes 0-7) ───
void runModesTest() {
    Serial.println("\n==============================================");
    Serial.println(" Running diagnostic update modes test...");
    Serial.println("==============================================");

    // 1. Full screen clearing before the test
    display.clearScreen();
    delay(500);

    // Clear local canvas in PSRAM with white color
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

    // 4x2 grid size for 1600x1200 screen
    // Each cell has size 400x600
    uint16_t cellW = 400;
    uint16_t cellH = 600;

    for (int m = 0; m < 8; m++) {
        uint16_t row = m / 4;
        uint16_t col = m % 4;
        uint16_t cellX = col * cellW;
        uint16_t cellY = row * cellH;

        Serial.printf("[TEST] Drawing cell for %s at (%d, %d)...\n", modeNames[m], cellX, cellY);

        // Draw cell border
        display.drawRect(cellX, cellY, cellW, cellH, 0x00, false);

        // Cell header (mode title)
        display.drawRect(cellX + 2, cellY + 2, cellW - 4, 38, 0xE0, true);
        display.drawText(cellX + 10, cellY + 12, modeNames[m], 0x00, 0xE0, 1);

        // Draw owl image in the center
        uint16_t owlX = cellX + (cellW - owl_width) / 2; // 400 - 256 = 144 / 2 = 72
        uint16_t owlY = cellY + 60;
        display.drawImage(owlX, owlY, owl_width, owl_height, owl_image_data);

        // Smooth gradient
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

        // Labels under the gradient
        display.drawText(cellX + 20, cellY + 390, "0x00 (Black) -> 0xFF (White)", 0x00, 0xFF, 1);

        // Four contrast squares
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

        // Labels under the squares
        display.drawText(cellX + 45, cellY + 510, "00", 0x00, 0xFF, 1);
        display.drawText(cellX + 135, cellY + 510, "55", 0x00, 0xFF, 1);
        display.drawText(cellX + 225, cellY + 510, "AA", 0x00, 0xFF, 1);
        display.drawText(cellX + 315, cellY + 510, "FF", 0x00, 0xFF, 1);

        // Measure the update time of this area using the selected mode
        uint32_t tStart = millis();
        display.updateArea(cellX, cellY, cellW, cellH, m);
        uint32_t tEnd = millis();

        timings[m] = tEnd - tStart;
        success[m] = (timings[m] < 9900);

        Serial.printf("[TEST] Cell %s update completed in %lu ms (Status: %s)\n", 
                      modeNames[m], timings[m], success[m] ? "OK" : "TIMEOUT/WARN");

        // Short pause between updates
        delay(500);
    }

    // 2. Print results in a beautiful ASCII table
    Serial.println("\n=======================================================");
    Serial.println("  UPDATE MODES (WAVEFORM) TEST RESULTS");
    Serial.println("=======================================================");
    Serial.println(" Mode  | Mode Name      | Update Time   | Status");
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
    Serial.println("Note: For DU (1) and A2 (6), the image must be two-color (black/white).");
    Serial.println("For GC16 (2), GL16 (3), and GLD16 (5), there should be smooth gray gradients.\n");
}

// ── setup() ──────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n==============================================");
    Serial.println(" IT8951 Library Test & Verification System");
    Serial.println("==============================================");

    // Library object initialization (PSRAM allocation and SPI startup happen internally)
    if (!display.begin(VCOM_VALUE)) {
        Serial.println("[ERROR] Failed to initialize IT8951 display library!");
        while (1) { delay(1000); }
    }
    
    display.setEndianness(g_endianType);

    // LittleFS initialization for loading TTF fonts
    if (!LittleFS.begin(true)) {
        Serial.println("[LittleFS] Error mounting LittleFS! Auto-formatting enabled.");
    } else {
        Serial.println("[LittleFS] Mount successful.");
        // Try loading standard TTF
        if (display.loadTTF(LittleFS, "/IrpinType-Regular.ttf", 24)) {
            g_loadedFontName = "IrpinType-Regular.ttf";
            Serial.println("[TTF] Loaded IrpinType-Regular.ttf at height 24px successfully.");
        } else {
            g_loadedFontName = "None";
            Serial.println("[TTF] Standard font IrpinType-Regular.ttf not found in LittleFS root.");
        }
    }

    // First full screen clearing
    display.clearScreen();
    delay(500);

    // Draw and output initial dashboard
    drawDashboard();
    display.display(g_defaultMode);
    
    printHelp();
}

// ── loop() ───────────────────────────────────────────────────
void loop() {
    // Timer handling
    if (autoPartialUpdate) {
        uint32_t now = millis();
        if (now - lastPartialTime >= 1000) {
            lastPartialTime = now;
            uptimeSeconds++;
            drawAndRefreshCounter();
        }
    }

    // Interactive console
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
                int hVal = 32 * scale; // 32px font size
                int yStart = y - (24 * scale); // ascent offset
                if (yStart < 0) { hVal += yStart; yStart = 0; }
                if (hVal < 1) hVal = 1;
                display.updateArea(x, yStart, w, hVal, g_defaultMode);
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
                    int lastSlash = path.lastIndexOf('/');
                    if (lastSlash == -1) lastSlash = path.lastIndexOf('\\');
                    g_loadedFontName = (lastSlash == -1) ? path : path.substring(lastSlash + 1);
                    Serial.printf("[TTF] Loaded font %s at height %d\n", path.c_str(), height);
                } else {
                    g_loadedFontName = "None";
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
                int hVal = display.getTTFHeight();
                // Estimate baseline top yStart
                int yStart = y - (hVal * 3 / 4); // estimate ascent (75% of height)
                if (yStart < 0) { hVal += yStart; yStart = 0; }
                if (hVal < 1) hVal = 1;
                display.updateArea(x, yStart, w, hVal, mode);
                Serial.printf("[DRAW] TTF text '%s' drawn and refreshed.\n", remain.c_str());
            } else {
                Serial.println("[ERR] Usage: ttftext <x> <y> <color> <bg> <mode> <tr 0/1> <message>");
            }
        }
        // filetext <x> <y> <color> <bg> <mode> <tr 0/1> <filePath>
        else if (cmd == "filetext") {
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
            String filePath = "";
            if (sp != -1) {
                transparent = (remain.substring(0, sp).toInt() != 0);
                filePath = remain.substring(sp + 1);
                filePath.trim();
            }
            
            if (parsed == 5 && filePath.length() > 0) {
                int x = vals[0];
                int y = vals[1];
                int color = vals[2];
                int bg = vals[3];
                int mode = vals[4];
                
                File file = LittleFS.open(filePath, "r");
                if (!file) {
                    Serial.printf("[ERR] Failed to open file: %s\n", filePath.c_str());
                } else {
                    String content = file.readString();
                    file.close();
                    
                    display.drawTTFText(x, y, content.c_str(), color, bg, transparent);
                    
                    int lines = 1;
                    for (int i = 0; i < content.length(); i++) {
                        if (content[i] == '\n') lines++;
                    }
                    uint16_t w = display.getTTFTextWidth(content.c_str());
                    int hVal = display.getTTFHeight() * lines;
                    int yStart = y - (display.getTTFHeight() * 3 / 4); // estimate ascent (75% of height)
                    if (yStart < 0) { hVal += yStart; yStart = 0; }
                    if (hVal < 1) hVal = 1;
                    display.updateArea(x, yStart, w, hVal, mode);
                    Serial.printf("[DRAW] Text from file '%s' drawn and refreshed.\n", filePath.c_str());
                }
            } else {
                Serial.println("[ERR] Usage: filetext <x> <y> <color> <bg> <mode> <tr 0/1> <filePath>");
            }
        }
        // fileimg <x> <y> <w> <h> <bpp 4/8> <mode> <filePath>
        else if (cmd == "fileimg") {
            int vals[6];
            int parsed = 0;
            String remain = args;
            for (int i = 0; i < 6; i++) {
                int sp = remain.indexOf(' ');
                if (sp != -1) {
                    vals[i] = remain.substring(0, sp).toInt();
                    remain = remain.substring(sp + 1);
                    parsed++;
                }
            }
            remain.trim();
            String filePath = remain;
            
            if (parsed == 6 && filePath.length() > 0) {
                int x = vals[0];
                int y = vals[1];
                int w = vals[2];
                int h = vals[3];
                int bpp = vals[4];
                int mode = vals[5];
                
                if (bpp != 4 && bpp != 8) {
                    Serial.println("[ERR] Supported BPP values are 4 or 8.");
                } else {
                    File file = LittleFS.open(filePath, "r");
                    if (!file) {
                        Serial.printf("[ERR] Failed to open file: %s\n", filePath.c_str());
                    } else {
                        size_t expectedSize = 0;
                        if (bpp == 8) {
                            expectedSize = (size_t)w * h;
                        } else { // 4bpp
                            expectedSize = ((size_t)w * h + 1) / 2;
                        }
                        
                        size_t fileSize = file.size();
                        if (fileSize < expectedSize) {
                            Serial.printf("[WARNING] File size (%d bytes) is less than expected image size (%d bytes)\n", fileSize, expectedSize);
                        }
                        
                        Serial.printf("[DRAW] Loading image %dx%d (%d BPP) from %s...\n", w, h, bpp, filePath.c_str());
                        
                        if (bpp == 8) {
                            uint8_t* rowBuf = (uint8_t*)malloc(w);
                            if (!rowBuf) {
                                Serial.println("[ERR] Out of memory for row buffer");
                                file.close();
                            } else {
                                for (int r = 0; r < h; r++) {
                                    int readBytes = file.read(rowBuf, w);
                                    if (readBytes <= 0) break;
                                    for (int c = 0; c < readBytes; c++) {
                                        display.drawPixel(x + c, y + r, rowBuf[c]);
                                    }
                                }
                                free(rowBuf);
                            }
                        } else { // 4bpp
                            int rowBytes = (w + 1) / 2;
                            uint8_t* rowBuf = (uint8_t*)malloc(rowBytes);
                            if (!rowBuf) {
                                Serial.println("[ERR] Out of memory for row buffer");
                                file.close();
                            } else {
                                for (int r = 0; r < h; r++) {
                                    int readBytes = file.read(rowBuf, rowBytes);
                                    if (readBytes <= 0) break;
                                    for (int c = 0; c < w; c++) {
                                        int byteIdx = c / 2;
                                        if (byteIdx >= readBytes) break;
                                        uint8_t color;
                                        if (c % 2 == 0) {
                                            color = (rowBuf[byteIdx] >> 4) << 4;
                                        } else {
                                            color = (rowBuf[byteIdx] & 0x0F) << 4;
                                        }
                                        display.drawPixel(x + c, y + r, color);
                                    }
                                }
                                free(rowBuf);
                            }
                        }
                        
                        file.close();
                        display.updateArea(x, y, w, h, mode);
                        Serial.println("[DRAW] Image loaded and refreshed.");
                    }
                }
            } else {
                Serial.println("[ERR] Usage: fileimg <x> <y> <w> <h> <bpp 4/8> <mode> <filePath>");
            }
        }
        else {
            Serial.println("[ERR] Unknown command. Type 'help' to see instructions.");
        }
    }
}