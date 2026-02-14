/*
 * Questo file fa parte del progetto Pi64.
 *
 * Copyright (C) 2025 Ivan Vettori
 *
 * Questo file è opera originale dell'autore e viene rilasciato
 * sotto licenza GNU General Public License v3.0.
 * Puoi ridistribuirlo e/o modificarlo secondo i termini della GPLv3.
 *
 * Questo programma è distribuito nella speranza che sia utile,
 * ma SENZA ALCUNA GARANZIA; senza neppure la garanzia implicita
 * di COMMERCIABILITÀ o IDONEITÀ PER UN PARTICOLARE SCOPO.
 * Vedi la licenza GPLv3 per maggiori dettagli.
 */
#include "C64GameBrowser.h"

TFT_eSPI* C64GameBrowser::staticTFT = nullptr;

C64GameBrowser::C64GameBrowser(TFT_eSPI* display, Loader* loaderRef, uint8_t sdCS) {
    tft = display;
    loader = loaderRef;
    sdCSPin = sdCS;
    staticTFT = tft;
    
    totalGames = 0;
    currentIndex = 0;
    currentFile[0] = '\0';
    lastJoyTime = 0;
    needsRedraw = true;
    
    logo = nullptr;
    logoWidth = 0;
    logoHeight = 0;
    hasLogo = false;
    
    // array ordinato nomi file
    sortedFileNames = nullptr;
}


// Inizializzazione
bool C64GameBrowser::begin(FSType fsType) {
    Serial.println("C64GameBrowser: Init...");
    
    if (browserPath[0] == '\0') { 
        sprintf(browserPath, "%s", GAME_DIR);
    } 

    if (browserExt[0] == '\0') { 
        sprintf(browserExt, "%s", ".prg");
    } 

    // usa Loader per init filesystem
    if (!loader->initFilesystem(fsType)) {
        Serial.println("Filesystem unavailable!");
        showError("NO FILESYSTEM!");
        return false;
    }
    
    currentFS = loader->getCurrentFilesystem();
    Serial.printf("Filesystem: %s\n", 
                 currentFS == FS_SD ? "SD" : "LittleFS");
    
    // verifica directory
    Serial.printf("Path: %s\n", browserPath);

    File dir = loader->openFile(browserPath, "r");
    if (!dir || !dir.isDirectory()) {
        Serial.println("Directory /Pi64 not found!");
        showError("NO /Pi64 FOLDER!");
        return false;
    }
    dir.close();
    
    // conta giochi (scan veloce, solo nomi)
    totalGames = countPRGFiles();
    if (totalGames == 0) {
        Serial.println("no games found!");
        showError("NO GAMES FOUND!");
        return false;
    }
    
    Serial.printf("found %d games\n", totalGames);
    
    // costruisco array ordinato
    if (!buildSortedFileList()) {
        Serial.println("Unable to sort games list!");
        showError("SORT ERROR!");
        return false;
    }
    
    // init decoder JPG
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tftOutput);
    
    // carica primo gioco
    currentIndex = 0;
    if (!getFileAtIndex(0, currentFile)) {
        Serial.println("Unable to load first game!");
        showError("LOAD ERROR!");
        return false;
    }
    
    Serial.printf("First Game: %s (index %d/%d)\n", 
                 currentFile, currentIndex + 1, totalGames);
    
    draw();
    
    return true;
}

// popola array ordinato alfabeticamente
bool C64GameBrowser::buildSortedFileList() {
    if (totalGames == 0 || totalGames > MAX_SORTED_GAMES) {
        Serial.printf("Too many games for sorting (%d > %d)\n", 
                     totalGames, MAX_SORTED_GAMES);
        return false;
    }
    
    Serial.printf("Building sorted array for %d games...\n", totalGames);
    
    // alloca array di puntatori
    sortedFileNames = (char**)malloc(totalGames * sizeof(char*));
    if (!sortedFileNames) {
        Serial.println("Malloc failed!");
        return false;
    }
    
    // inizializza a NULL
    for (int i = 0; i < totalGames; i++) {
        sortedFileNames[i] = nullptr;
    }
    
    // scansiona e carica tutti i nomi
    File dir = openFile(browserPath, "r");
    if (!dir || !dir.isDirectory()) {
        free(sortedFileNames);
        sortedFileNames = nullptr;
        return false;
    }
    
    int count = 0;
    File entry;
    // popola array
    while ((entry = dir.openNextFile()) && count < totalGames) {
        if (!entry.isDirectory() && isPRG(entry.name())) {
            sortedFileNames[count] = (char*)malloc(32);
            if (sortedFileNames[count]) {
                strncpy(sortedFileNames[count], entry.name(), 31);
                sortedFileNames[count][31] = '\0';
                count++;
            }
        }
        entry.close();
    }
    dir.close();
    
    if (count != totalGames) {
        Serial.printf("Count mismatch: expected %d, found %d\n", totalGames, count);
        totalGames = count;  // aggiusta
    }
    
    // ordina alfabeticamente (bubble sort semplice)
    Serial.println("Sorting...");
    
    for (int i = 0; i < totalGames - 1; i++) {
        for (int j = 0; j < totalGames - i - 1; j++) {
            if (strcasecmp(sortedFileNames[j], sortedFileNames[j + 1]) > 0) {
                // swap
                char* temp = sortedFileNames[j];
                sortedFileNames[j] = sortedFileNames[j + 1];
                sortedFileNames[j + 1] = temp;
            }
        }
    }
    
    // debug: mostra primi 5
    Serial.println("Array sorted:");
    for (int i = 0; i < min(5, totalGames); i++) {
        Serial.printf("  [%d] %s\n", i, sortedFileNames[i]);
    }
    if (totalGames > 5) {
        Serial.println("  ...");
    }
    
    return true;
}

// libera array dei giochi
void C64GameBrowser::freeSortedFileList() {
    if (sortedFileNames) {
        for (int i = 0; i < totalGames; i++) {
            if (sortedFileNames[i]) {
                free(sortedFileNames[i]);
            }
        }
        free(sortedFileNames);
        sortedFileNames = nullptr;
    }
}


// CORE: accesso file per indice (usa array ordinato)
bool C64GameBrowser::getFileAtIndex(int targetIndex, char* outName) {
    if (targetIndex < 0 || targetIndex >= totalGames) {
        Serial.printf("Index %d out of range (0-%d)\n", targetIndex, totalGames-1);
        return false;
    }
    
    // ACCESSO DIRETTO all'array ordinato (O(1) invece di O(n))
    if (!sortedFileNames || !sortedFileNames[targetIndex]) {
        Serial.println("Sorted array not available!");
        return false;
    }
    
    strncpy(outName, sortedFileNames[targetIndex], 31);
    outName[31] = '\0';
    
    Serial.printf("File[%d]: %s\n", targetIndex, outName);
    
    return true;
}


// navigazione bidirezionale con Wrap-Around
void C64GameBrowser::nextGame() {
    if (totalGames == 0) return;
    
    int oldIndex = currentIndex;
    
    // forward con wrap
    currentIndex = (currentIndex + 1) % totalGames;
    
    Serial.printf("Navigation: %d → %d (total: %d)\n", 
                 oldIndex, currentIndex, totalGames);
    
    if (getFileAtIndex(currentIndex, currentFile)) {
        Serial.printf("New game: %s (%d/%d)\n", 
                     currentFile, currentIndex + 1, totalGames);
        
        // navigazione istantanea (no scan, usa array!)
        needsRedraw = true;
        drawGameImage();
        drawGameName();
        needsRedraw = false;
    } else {
        // rollback se fallisce
        Serial.println("Loading failed, rollback");
        currentIndex = oldIndex;
    }
}

void C64GameBrowser::prevGame() {
    if (totalGames == 0) return;
    
    int oldIndex = currentIndex;
    
    // backward con wrap (aggiunge totalGames per evitare negativi)
    currentIndex = (currentIndex - 1 + totalGames) % totalGames;
    
    Serial.printf("Navigation: %d → %d (total: %d)\n", 
                 oldIndex, currentIndex, totalGames);
    
    if (getFileAtIndex(currentIndex, currentFile)) {
        Serial.printf("New game: %s (%d/%d)\n", 
                     currentFile, currentIndex + 1, totalGames);
        
        // Navigazione istantanea (no scan, usa array!)
        needsRedraw = true;
        drawGameImage();
        drawGameName();
        needsRedraw = false;
    } else {
        // Rollback se fallisce
        Serial.println("Loading failed, rollback");
        currentIndex = oldIndex;
    }
}

// update con joystick
bool C64GameBrowser::update(uint8_t joyState) {
    if (totalGames == 0) return false;
    
    uint32_t now = millis();
    if (now - lastJoyTime < JOY_DEBOUNCE_MS) return false;
    
    if (!(joyState & 0x08)) { // LEFT
        lastJoyTime = now;
        prevGame();
        return false;
    }
    if (!(joyState & 0x04)) { // RIGHT
        lastJoyTime = now;
        nextGame();
        return false;
    }
    if (!(joyState & 0x10)) { // FIRE
        lastJoyTime = now;
        return true;  // Gioco selezionato!
    }
    
    return false;
}

void C64GameBrowser::setPath(const char* cpath) {
  sprintf(browserPath, "%s%s", GAME_DIR, cpath);
}

void C64GameBrowser::setFileExt(const char* fext) {
  sprintf(browserExt, "%s", fext);
}

// Selezione gioco
String C64GameBrowser::getPrgSelected() {
    char fullPath[128];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", browserPath, currentFile);
    
    // Salva nel Loader
    loader->setSelectedGame(fullPath);
    
    Serial.printf("File selected: %s\n", fullPath);
    
    return String(fullPath);
}


// utility
int C64GameBrowser::countPRGFiles() {
    int count = 0;
    
    File dir = openFile(browserPath, "r");
    if (!dir || !dir.isDirectory()) return 0;
    
    File entry;
    while ((entry = dir.openNextFile())) {
        if (!entry.isDirectory() && isPRG(entry.name())) {
            count++;
        }
        entry.close();
    }
    dir.close();
    
    Serial.printf("Scan completed: %d file %s\n", count, browserExt);
    
    return count;
}

File C64GameBrowser::openFile(const char* path, const char* mode) {
    return loader->openFile(path, mode);
}

bool C64GameBrowser::fileExists(const char* path) {
    return loader->fileExists(path);
}

bool C64GameBrowser::isPRG(const char* name) {
    int len = strlen(name);
    if (len <= 4) return false;
    return (strcasecmp(name + len - 4, browserExt) == 0);
}

String C64GameBrowser::getDisplayName(const char* filename) {
    String name = String(filename);
    int dotPos = name.lastIndexOf('.');
    return (dotPos > 0) ? name.substring(0, dotPos) : name;
}

bool C64GameBrowser::hasImageFile(const char* prgName) {
    String baseName = getDisplayName(prgName);
    char jpgPath[64];
    snprintf(jpgPath, sizeof(jpgPath), "%s/%s.jpg", browserPath, baseName.c_str());
    return fileExists(jpgPath);
}

// ============================================================
// Disegno
// ============================================================

void C64GameBrowser::draw() {
    if (!needsRedraw) return;
    
    drawBackground();
    drawLogo();
    drawGameImage();
    drawGameName();
    drawArrows();
    drawCredits();
    
    needsRedraw = false;
}

void C64GameBrowser::drawBackground() {
    tft->fillScreen(TFT_BLUE);
}

void C64GameBrowser::drawLogo() {
    if (!hasLogo) {
        tft->setTextColor(TFT_CYAN);
        tft->setTextSize(2);
        tft->setTextDatum(TL_DATUM);
        tft->drawString("Pico64", 5, 5);
        return;
    }
    tft->setSwapBytes(true);
    tft->pushImage(5, 5, logoWidth, logoHeight, logo, 0x0000);
    tft->setSwapBytes(false);
}

void C64GameBrowser::drawGameImage() {
    const int imgWidth = 200;
    const int imgHeight = 150;
    const int imgX = (320 - imgWidth) / 2;
    const int imgY = 40;
    
    tft->fillRect(imgX, imgY, imgWidth, imgHeight, TFT_BLUE);
    
    if (totalGames == 0 || currentFile[0] == '\0') return;
    
    String displayName = getDisplayName(currentFile);
    
    char imagePath[64];
    if (hasImageFile(currentFile)) {
        snprintf(imagePath, sizeof(imagePath), "%s/%s.jpg", browserPath, displayName.c_str());
    } else {
        snprintf(imagePath, sizeof(imagePath), "%s/noImage.jpg", browserPath);
    }
    
    if (!fileExists(imagePath)) {
        tft->drawRect(imgX, imgY, imgWidth, imgHeight, TFT_CYAN);
        tft->setTextColor(TFT_CYAN);
        tft->setTextSize(2);
        tft->setTextDatum(MC_DATUM);
        tft->drawString("NO IMAGE", 160, imgY + imgHeight/2);
        return;
    }
    
    if (currentFS == FS_SD) {
        TJpgDec.drawSdJpg(imgX, imgY, imagePath);
    } else if (currentFS == FS_LITTLEFS) {
        TJpgDec.drawFsJpg(imgX, imgY, imagePath);
    }
}

void C64GameBrowser::drawGameName() {
    if (totalGames == 0 || currentFile[0] == '\0') return;
    
    String displayName = getDisplayName(currentFile);
    
    tft->setTextColor(TFT_CYAN);
    tft->setTextSize(2);
    tft->setTextDatum(TC_DATUM);
    tft->fillRect(0, 200, 320, 20, TFT_BLUE);
    tft->drawString(displayName, 160, 200);
}

void C64GameBrowser::drawArrows() {
    if (totalGames <= 1) return;
    
    tft->setTextColor(TFT_WHITE);
    tft->setTextSize(4);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("<", 20, 120);
    tft->drawString(">", 300, 120);
}

void C64GameBrowser::drawCredits() {
    tft->setTextSize(1);
    tft->setTextColor(TFT_CYAN);
    tft->setTextDatum(BR_DATUM);
    tft->drawString("@IV COMMODORE 64 emulator", 315, 235);
}

void C64GameBrowser::showError(const char* msg) {
    tft->fillScreen(TFT_BLUE);
    tft->setTextColor(TFT_RED);
    tft->setTextSize(2);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(msg, 160, 120);
}

void C64GameBrowser::setLogo(const unsigned short* logoData, int width, int height) {
    logo = logoData;
    logoWidth = width;
    logoHeight = height;
    hasLogo = (logo != nullptr);
}

bool C64GameBrowser::tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (bitmap && staticTFT) {
        staticTFT->setSwapBytes(true);
        staticTFT->pushImage(x, y, w, h, bitmap);
        staticTFT->setSwapBytes(false);
    }
    return true;
}