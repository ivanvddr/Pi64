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
#include "Loader.h"
#include "configs.h"

Loader::Loader(cpu* cpuRef) : _cpu(cpuRef) {
    if (!cpuRef) {
        Serial.println("ERROR: Null CPU reference in Loader constructor!");
    }
}

bool Loader::caricaPRG(const char* nomeFile) {
    if (!_cpu) {
        Serial.println("ERROR: CPU not initialized!");
        return false;
    }
    
    if (!LittleFS.begin()) {
        Serial.println("Error mounting LittleFS");
        return false;
    }
    
    File file = LittleFS.open(nomeFile, "r");
    if (!file) {
        Serial.printf("The file could not be opened: %s\n", nomeFile);
        return false;
    }
    
    size_t dimensioneFile = file.size();
    if (dimensioneFile < 2) {
        Serial.println("PRG file too small (missing upload address)");
        file.close();
        return false;
    }
    
    // slloca buffer per il file
    uint8_t* buffer = (uint8_t*)malloc(dimensioneFile);
    if (!buffer) {
        Serial.println("Error allocating memory for PRG file");
        file.close();
        return false;
    }
    
    size_t byteLetti = file.read(buffer, dimensioneFile);
    file.close();
    
    if (byteLetti != dimensioneFile) {
        Serial.println("Error reading complete PRG file");
        free(buffer);
        return false;
    }
    
    bool risultato = caricaPRGDaBuffer(buffer, dimensioneFile);
    free(buffer);
    
    return risultato;
}

bool Loader::caricaPRGDaBuffer(uint8_t* buffer, size_t dimensione) {
    if (!_cpu) {
        Serial.println("ERROR: CPU not initialized!");
        return false;
    }
    
    if (dimensione < 2) {
        Serial.println("Buffer too small for PRG format");
        return false;
    }
    
    // Estrae l'indirizzo di caricamento (little-endian)
    uint16_t indirizzoCaricamento = buffer[0] | (buffer[1] << 8);
    
    Serial.printf("Uploading PRG to the address: $%04X\n", indirizzoCaricamento);
    
    size_t dimensioneProgramma = dimensione - 2;
    uint16_t indirizzoFine = indirizzoCaricamento + dimensioneProgramma - 1;
    
    Serial.printf("Program size: %d byte ($%04X - $%04X)\n", 
                  dimensioneProgramma, indirizzoCaricamento, indirizzoFine);
    
    if (indirizzoFine > 0xFFFF) {
        Serial.println("Program too large for C64 memory");
        return false;
    }
    
    // Carica il programma in memoria
    for (size_t i = 0; i < dimensioneProgramma; i++) {
        uint16_t addr = indirizzoCaricamento + i;
        _cpu->mem_write(addr, buffer[i + 2]);
    }
    
    _indirizzoInizioPRG = indirizzoCaricamento;
    _indirizzoFinePRG = indirizzoFine;
    _prgCaricato = true;
    
    Serial.printf("PRG uploaded successfully: $%04X-$%04X\n", 
                  _indirizzoInizioPRG, _indirizzoFinePRG);
    
    return true;
}

void Loader::inizializzaPerPRG() {
    if (!_cpu) {
        Serial.println("ERROR: CPU not initialized!");
        return;
    }
    
    // Imposta stato di base del sistema (più conservativo)
    _cpu->memory[0x00] = 0x2F;  // Port direction
    _cpu->memory[0x01] = 0x37;  // Port data (BASIC + KERNAL + I/O visible)
    
    // solo per programmi BASIC: imposta puntatori
    if (_indirizzoInizioPRG == 0x0801) {
        _cpu->memory[0x2B] = _indirizzoInizioPRG & 0xFF;        // start of BASIC
        _cpu->memory[0x2C] = (_indirizzoInizioPRG >> 8) & 0xFF;
        
        uint16_t endAddr = _indirizzoFinePRG + 1;
        _cpu->memory[0x2D] = endAddr & 0xFF;        // end of BASIC
        _cpu->memory[0x2E] = (endAddr >> 8) & 0xFF;
        
        // Altri puntatori BASIC
        _cpu->memory[0x2F] = _cpu->memory[0x2D];    // variables start
        _cpu->memory[0x30] = _cpu->memory[0x2E];
        _cpu->memory[0x31] = _cpu->memory[0x2D];    // arrays start  
        _cpu->memory[0x32] = _cpu->memory[0x2E];
        _cpu->memory[0x33] = _cpu->memory[0x2D];    // strings start
        _cpu->memory[0x34] = _cpu->memory[0x2E];
    }
    
    Serial.println("System initialized for PRG execution");
}

void Loader::eseguiRUN() {
    if (!_cpu) {
        Serial.println("ERROR: CPU not initialized!");
        return;
    }
    
    if (!_prgCaricato) {
        Serial.println("No BASIC program loaded!");
        return;
    }
    
    Serial.println("RUN command simulation...");
    
    // Metodo più sicuro: salta direttamente alla routine RUN del BASIC
    // Questa è la routine che viene chiamata quando si digita RUN
    
    // Imposta puntatori per BASIC
    inizializzaPerPRG();

    // Imposta registri per l'esecuzione BASIC
    _cpu->pc = 0xA7AE;  // BASIC RUN command entry point
    _cpu->a = 0;
    _cpu->x = 0; 
    _cpu->y = 0;
    _cpu->sp = 0xFF;
    _cpu->cpustatus = 0x20 | 0x04;  // FLAG_CONSTANT | FLAG_INTERRUPT disabled
    
    Serial.printf("tarting BASIC RUN at PC=$%04X\n", _cpu->pc);
}

void Loader::eseguiPRG(uint16_t indirizzoInizio) {
    if (!_cpu) {
        Serial.println("ERROR: CPU not initialized!");
        return;
    }
    
    if (!_prgCaricato && indirizzoInizio == 0) {
        Serial.println("No PRG loaded and no starting address specified");
        return;
    }
    
    uint16_t indirizzoEsecuzione = (indirizzoInizio != 0) ? indirizzoInizio : _indirizzoInizioPRG;
    
    Serial.printf("Start PRG execution at $%04X\n", indirizzoEsecuzione);
    
    if (indirizzoEsecuzione == 0x0801) {
        Serial.println("BASIC program detected - use RUN command");
        eseguiRUN();
    } else {
        Serial.printf("Machine language program - direct jump to $%04X\n", indirizzoEsecuzione);
        
        // Inizializzazione minima per ML
        inizializzaPerPRG();
        
        // Imposta registri per esecuzione ML
        _cpu->pc = indirizzoEsecuzione;
        _cpu->a = 0;
        _cpu->x = 0;
        _cpu->y = 0; 
        _cpu->sp = 0xFF;
        _cpu->cpustatus = 0x20;  // Solo FLAG_CONSTANT, interrupts abilitati
        
        Serial.printf("start ML execution at PC=$%04X\n", _cpu->pc);
    }
}

// Metodo alternativo per programmi problematici
void Loader::eseguiPRGForzato(uint16_t indirizzo) {
    if (!_cpu) {
        Serial.println("ERROR: CPU not initialized!");
        return;
    }
    
    Serial.printf("Forced execution at $%04X\n", indirizzo);
    
    // Reset minimale del sistema
    _cpu->memory[0x01] = 0x37;  // I/O visibile
    
    // Salta direttamente all'indirizzo specificato
    _cpu->pc = indirizzo;
    _cpu->a = 0;
    _cpu->x = 0;
    _cpu->y = 0;
    _cpu->sp = 0xFF;
    _cpu->cpustatus = 0x20;  // Solo constant flag
    
    Serial.printf("Forced jump to PC=$%04X\n", _cpu->pc);
}

bool Loader::caricaPRGStreaming(File& file) {
    if (!_cpu) {
        Serial.println("ERROR: CPU not initialized!");
        return false;
    }
    
    if (!file || file.available() < 2) {
        Serial.println("Invalid or too small file");
        return false;
    }
    
    // Leggi indirizzo di caricamento
    uint8_t addrLow = file.read();
    uint8_t addrHigh = file.read();
    uint16_t indirizzoCaricamento = addrLow | (addrHigh << 8);
    
    Serial.printf("Loading STREAMING to $%04X\n", indirizzoCaricamento);
    
    if (indirizzoCaricamento > 0xFFFF) {
        Serial.printf("Invalid address: $%04X\n", indirizzoCaricamento);
        return false;
    }
    
    // STREAMING DIRETTO: File > RAM C64
    uint16_t addr = indirizzoCaricamento;
    size_t bytesCaricati = 0;
    
    while (file.available() && addr <= 0xFFFF) {
        _cpu->mem_write(addr++, file.read());
        bytesCaricati++;
        
        if (bytesCaricati % 1024 == 0) {
            Serial.printf("  ...%u KB\n", bytesCaricati / 1024);
        }
    }
    
    _indirizzoInizioPRG = indirizzoCaricamento;
    _indirizzoFinePRG = addr - 1;
    _prgCaricato = true;
    
    Serial.printf("PRG loaded: %u bytes ($%04X-$%04X)\n", 
                  bytesCaricati, _indirizzoInizioPRG, _indirizzoFinePRG);
    
    return bytesCaricati > 0;
}

void Loader::elencaFilePRG() {
    if (!LittleFS.begin()) {
        Serial.println("Error mounting LittleFS");
        return;
    }
    
    Serial.println("PRG files available:");
    
    Dir dir = LittleFS.openDir("/");
    while (dir.next()) {
        String nomeFile = dir.fileName();
        if (nomeFile.endsWith(".prg") || nomeFile.endsWith(".PRG")) {
            File f = dir.openFile("r");
            if (f) {
                Serial.printf("  %s (%d byte)\n", nomeFile.c_str(), f.size());
                f.close();
            }
        }
    }
}


bool Loader::initFilesystem(FSType preferredFS) {
    if (_filesystemReady) {
        Serial.printf("Filesystem already initialized (%s)\n",
                     _currentFS == FS_SD ? "SD" : "LittleFS");
        return true;
    }
    
    Serial.println("Filesystem initialization...");
    
    // PROVA 1: Filesystem preferito
    if (preferredFS == FS_LITTLEFS) {
        Serial.println("LittleFS Attempt...");
        if (LittleFS.begin()) {
            _currentFS = FS_LITTLEFS;
            _filesystemReady = true;
            Serial.println("LittleFS initialized");
            return true;
        }
        Serial.println("LittleFS not available");
    }
    else if (preferredFS == FS_SD) {
        Serial.println("SD Attempt...");
        if (initSD()) {
            _currentFS = FS_SD;
            _filesystemReady = true;
            Serial.println("SD initialized");
            return true;
        }
        Serial.println("SD not available");
    }
    
    // PROVA 2: Fallback sull'altro filesystem
    if (preferredFS == FS_LITTLEFS) {
        Serial.println("Fallback su SD...");
        if (initSD()) {
            _currentFS = FS_SD;
            _filesystemReady = true;
            Serial.println("SD initialized (fallback)");
            return true;
        }
    }
    else {
        Serial.println("Fallback su LittleFS...");
        if (LittleFS.begin()) {
            _currentFS = FS_LITTLEFS;
            _filesystemReady = true;
            Serial.println("LittleFS initialized (fallback)");
            return true;
        }
    }
    
    Serial.println("No filesystem available!");
    return false;
}

bool Loader::initSD() {
    // Configura SPI1 per SD
    SPI1.setRX(SD_MISO);
    SPI1.setTX(SD_MOSI);
    SPI1.setSCK(SD_SCLK);
    SPI1.setCS(SD_CS);
    SPI1.begin();
    
    delay(100);
    
    if (!SD.begin(SD_CS,SPI1)) {
        Serial.println("SD init fallita!");
        return false;
    }
    return true;
}

File Loader::openFile(const char* path, const char* mode) {
    if (!_filesystemReady) {
        Serial.println("Filesystem not initialized!");
        return File();
    }
    
    if (_currentFS == FS_SD) {
        return SD.open(path, mode);
    } 
    else if (_currentFS == FS_LITTLEFS) {
        return LittleFS.open(path, mode);
    }
    
    return File();
}

bool Loader::fileExists(const char* path) {
    if (!_filesystemReady) return false;
    
    if (_currentFS == FS_SD) {
        return SD.exists(path);
    } 
    else if (_currentFS == FS_LITTLEFS) {
        return LittleFS.exists(path);
    }
    
    return false;
}

void Loader::setSelectedGame(const char* path) {
    if (path) {
        strncpy(_selectedGamePath, path, sizeof(_selectedGamePath) - 1);
        _selectedGamePath[sizeof(_selectedGamePath) - 1] = '\0';
        Serial.printf("Game selected: %s\n", _selectedGamePath);
    }
}

void Loader::closeFilesystem() {
    if (_currentFS == FS_SD) {
        Serial.println("SD closure...");
        SD.end();
        SPI1.end();
        Serial.println("SD closed");
    }
    // LittleFS non serve chiudere, non interferisce con C64    
    _filesystemReady = false;
}

// carica gioco selezionato (usa streaming diretto in memoria)
bool Loader::caricaGiocoSelezionato() {
    if (!_cpu) {
        Serial.println("ERROR: CPU not initialized!");
        return false;
    }
    
    if (_selectedGamePath[0] == '\0') {
        Serial.println("ERROR: no game selected!");
        return false;
    }
    
    Serial.printf("Loading: %s\n", _selectedGamePath);
    
    // Apri file
    File prgFile = openFile(_selectedGamePath, "r");
    if (!prgFile) {
        Serial.println("ERROR opening file!");
        return false;
    }
    
    // Usa streaming diretto
    bool loaded = caricaPRGStreaming(prgFile);
    prgFile.close();
    
    if (!loaded) {
        Serial.println("Loading ERROR!");
        return false;
    }
    
    Serial.println("Game laoded!");
    return true;
}

bool Loader::caricaSID(const char* nomeFile) {
    if (!_cpu) {
        Serial.println("ERROR: CPU not initialized!");
        return false;
    }
    
    // ========================================
    // Metodo UNIFICATO: usa filesystem corrente
    // ========================================
    if (!_filesystemReady && !initFilesystem()) {
        Serial.println("Unable to initialize filesystem!");
        return false;
    }
    
    Serial.printf("Loading SID file: %s\n", nomeFile);
    
    // Apri file dal filesystem corrente
    File file = openFile(nomeFile, "r");
    if (!file) {
        Serial.printf("Unable to open: %s\n", nomeFile);
        return false;
    }
    
    // USA STREAMING
    bool loaded = caricaSIDStreaming(file);
    file.close();
    
    if (loaded) {
        Serial.println("SID file successfully loaded!");
    } else {
        Serial.println("ERROR loading SID file");
    }
    
    return loaded;
}

bool Loader::caricaSIDStreaming(File& file) {
    if (!_cpu) {
        Serial.println("ERROR: CPU not initialized!");
        return false;
    }
    
    if (!file || file.available() < sizeof(SIDHeader)) {
        Serial.println("Invalid or too small file for SID header");
        return false;
    }
    
    Serial.println("Loading SID file STREAMING...");
    
    // STEP 1: Leggi header direttamente da file
    size_t headerRead = file.read((uint8_t*)&_sidHeader, sizeof(SIDHeader));
    
    if (headerRead != sizeof(SIDHeader)) {
        Serial.println("Error reading SID file header");
        return false;
    }
    
    // Verifica magic ID
    if (strncmp(_sidHeader.magicID, "PSID", 4) != 0 && 
        strncmp(_sidHeader.magicID, "RSID", 4) != 0) {
        Serial.println("File is not a valid SID format");
        return false;
    }
    
    // STEP 2: Converti endianness e mostra info
    uint16_t version = swapBytes(_sidHeader.version);
    uint16_t dataOffset = swapBytes(_sidHeader.dataOffset);
    uint16_t loadAddress = swapBytes(_sidHeader.loadAddress);
    uint16_t initAddress = swapBytes(_sidHeader.initAddress);
    uint16_t playAddress = swapBytes(_sidHeader.playAddress);
    uint16_t songs = swapBytes(_sidHeader.songs);
    uint16_t startSong = swapBytes(_sidHeader.startSong);
    
    Serial.println("=== SID FILE INFO ===");
    Serial.printf("Type: %c%c%c%c v%d\n", 
                  _sidHeader.magicID[0], _sidHeader.magicID[1],
                  _sidHeader.magicID[2], _sidHeader.magicID[3], version);
    Serial.printf("Name: %.32s\n", _sidHeader.name);
    Serial.printf("Author: %.32s\n", _sidHeader.author);
    Serial.printf("Released: %.32s\n", _sidHeader.released);
    Serial.printf("Songs: %d (start: %d)\n", songs, startSong);
    Serial.printf("Load: $%04X, Init: $%04X, Play: $%04X\n", 
                  loadAddress, initAddress, playAddress);
    
    // STEP 3: Posiziona file pointer al data offset
    file.seek(dataOffset);
    
    // Se loadAddress è 0, leggi i primi 2 byte come indirizzo
    if (loadAddress == 0) {
        if (file.available() < 2) {
            Serial.println("Insufficient data for load address");
            return false;
        }
        uint8_t addrLow = file.read();
        uint8_t addrHigh = file.read();
        loadAddress = addrLow | (addrHigh << 8);
        Serial.printf("Load address from data: $%04X\n", loadAddress);
    }
    
    // STEP 4: STREAMING DIRETTO File > RAM C64
    uint16_t addr = loadAddress;
    size_t bytesCaricati = 0;
    
    Serial.printf("Streaming at $%04X...\n", addr);
    
    while (file.available() && addr <= 0xFFFF) {
        _cpu->mem_write(addr++, file.read());
        bytesCaricati++;
        
        // Progress ogni 1KB
        if (bytesCaricati % 1024 == 0) {
            Serial.printf("  ...%u KB\n", bytesCaricati / 1024);
        }
    }
    
    uint16_t endAddress = addr - 1;
    
    if (endAddress > 0xFFFF) {
        Serial.println("SID file too large for C64 memory");
        return false;
    }
    
    // STEP 5: Aggiorna stato
    _sidCaricato = true;
    _sidCurrentSong = (startSong > 0) ? (startSong - 1) : 0;
    _sidPlaying = false;
    
    Serial.printf("SID file loaded: %u bytes ($%04X-$%04X)\n", 
                  bytesCaricati, loadAddress, endAddress);
    
    return bytesCaricati > 0;
}

bool Loader::caricaSIDDaBuffer(uint8_t* buffer, size_t dimensione) {
    if (!_cpu) {
        Serial.println("ERROR: CPU not initialized!");
        return false;
    }
    
    if (dimensione < sizeof(SIDHeader)) {
        Serial.println("ERROR: Buffer too small for header SID");
        return false;
    }
    
    Serial.println("Loading SID file from buffer...");
    
    // Copia header
    memcpy(&_sidHeader, buffer, sizeof(SIDHeader));
    
    // Verifica magic ID
    if (strncmp(_sidHeader.magicID, "PSID", 4) != 0 && 
        strncmp(_sidHeader.magicID, "RSID", 4) != 0) {
        Serial.println("Buffer does not contain a valid SID");
        return false;
    }
    
    // Converti endianness
    uint16_t version = swapBytes(_sidHeader.version);
    uint16_t dataOffset = swapBytes(_sidHeader.dataOffset);
    uint16_t loadAddress = swapBytes(_sidHeader.loadAddress);
    uint16_t initAddress = swapBytes(_sidHeader.initAddress);
    uint16_t playAddress = swapBytes(_sidHeader.playAddress);
    uint16_t songs = swapBytes(_sidHeader.songs);
    uint16_t startSong = swapBytes(_sidHeader.startSong);
    
    Serial.println("=== SID FILE INFO ===");
    Serial.printf("Type: %c%c%c%c v%d\n", 
                  _sidHeader.magicID[0], _sidHeader.magicID[1],
                  _sidHeader.magicID[2], _sidHeader.magicID[3], version);
    Serial.printf("Name: %.32s\n", _sidHeader.name);
    Serial.printf("Author: %.32s\n", _sidHeader.author);
    Serial.printf("Songs: %d (start: %d)\n", songs, startSong);
    Serial.printf("Load: $%04X, Init: $%04X, Play: $%04X\n", 
                  loadAddress, initAddress, playAddress);
    
    // Se loadAddress è 0, estrai dai primi 2 byte dei dati
    if (loadAddress == 0) {
        if (dimensione < dataOffset + 2) {
            Serial.println("Insufficient data for load address");
            return false;
        }
        loadAddress = buffer[dataOffset] | (buffer[dataOffset + 1] << 8);
        dataOffset += 2;
        Serial.printf("Load address from data: $%04X\n", loadAddress);
    }
    
    // Calcola dimensione dati
    size_t dataDim = dimensione - dataOffset;
    uint16_t endAddress = loadAddress + dataDim - 1;
    
    if (endAddress > 0xFFFF) {
        Serial.println("SID too large for C64 memory");
        return false;
    }
    
    // Carica dati in memoria
    Serial.printf("Loading %u bytes to $%04X-$%04X\n", 
                  dataDim, loadAddress, endAddress);
    
    for (size_t i = 0; i < dataDim; i++) {
        _cpu->mem_write(loadAddress + i, buffer[dataOffset + i]);
    }
    
    _sidCaricato = true;
    _sidCurrentSong = (startSong > 0) ? (startSong - 1) : 0;
    _sidPlaying = false;
    
    Serial.println("SID file loaded from buffer!");
    return true;
}

void Loader::inizializzaSID(uint8_t songNumber) {
    if (!_cpu || !_sidCaricato) {
        Serial.println("ERROR: CPU not initialized or SID not loaded");
        return;
    }
    
    if (songNumber >= swapBytes(_sidHeader.songs)) {
        Serial.printf("Invalid song number (%d, max %d)\n", 
                     songNumber, swapBytes(_sidHeader.songs) - 1);
        return;
    }
    
    _sidCurrentSong = songNumber;
    
    uint16_t initAddr = swapBytes(_sidHeader.initAddress);
    
    Serial.printf("SID song initialization %d at $%04X\n", songNumber, initAddr);
    
    // Setup memoria
    _cpu->memory[0x00] = 0x2F;
    _cpu->memory[0x01] = 0x37;  // KERNAL/BASIC/I/O visible
    
    // Setup registri CPU
    _cpu->a = _sidCurrentSong;
    _cpu->x = 0;
    _cpu->y = 0;
    _cpu->sp = 0xFD;
    _cpu->cpustatus = 0x24;
    
    // Push return address
    _cpu->mem_write(0x01FF, 0xEA);
    _cpu->mem_write(0x01FE, 0xEA);
    _cpu->sp = 0xFD;
    
    _cpu->pc = initAddr;
    
    // Esegui init con KERNAL stub
    uint32_t cycles = 0;
    uint16_t last_pc = 0xFFFF;
    uint32_t same_pc_count = 0;
    
    while (cycles < 100000) {
        // Stub KERNAL calls
        if (_cpu->pc >= 0xE000) {
            switch (_cpu->pc) {
                case 0xFF48:  // IOBASE - Return I/O base address
                    _cpu->x = 0x00;  // Low byte $D000
                    _cpu->y = 0xD0;  // High byte $D000
                    break;
                    
                case 0xFFBD:  // SETNAM - Set file name (NOP)
                    break;
                    
                case 0xFFBA:  // SETLFS - Set logical file params (NOP)
                    break;
                    
                case 0xFFD2:  // CHROUT - Character output (NOP)
                    break;
                    
                case 0xFFE4:  // GETIN - Get character (return 0)
                    _cpu->a = 0;
                    break;
                    
                default:
                    // Altri KERNAL: NOP
                    break;
            }
            
            // Simula RTS
            _cpu->sp++;
            uint8_t pcl = _cpu->mem_read(0x0100 | _cpu->sp);
            _cpu->sp++;
            uint8_t pch = _cpu->mem_read(0x0100 | _cpu->sp);
            _cpu->pc = (pcl | (pch << 8)) + 1;
            
            cycles++;
            continue;
        }
        
        // Check return
        if (_cpu->pc == 0xEAEA) {
            Serial.printf("Init completed in %d cycles\n", cycles);
            _sidPlaying = true;
            return;
        }
        
        // Detect loop infinito
        if (_cpu->pc == last_pc) {
            same_pc_count++;
            if (same_pc_count > 100) {
                Serial.printf("Init STUCK @ $%04X - force exit\n", _cpu->pc);
                _sidPlaying = true;
                return;
            }
        } else {
            same_pc_count = 0;
            last_pc = _cpu->pc;
        }
        
        // Esegui istruzione
        _cpu->execCPU(1);
        cycles++;
        
        // Debug periodico
        //if (cycles % 10000 == 0) {
        //    Serial.printf("Cycle %d: PC=$%04X\n", cycles, _cpu->pc);
        //}
    }
    
    Serial.printf("Init TIMEOUT after %d cycles\n", cycles);
    _sidPlaying = true;  // Prova comunque
}

void Loader::cambiaCanzone(uint8_t nuovoNumero) {
    if (!_sidCaricato) {
        Serial.println("No SID file loaded!");
        return;
    }
    
    uint16_t totalSongs = swapBytes(_sidHeader.songs);
    if (nuovoNumero >= totalSongs) {
        Serial.printf("Song %d bot valid (max %d)\n", nuovoNumero, totalSongs - 1);
        return;
    }
    
    Serial.printf("Change to song %d...\n", nuovoNumero);
    
    // Stop playback
    _sidPlaying = false;
    
    // Azzera registri SID (silenzio immediato)
    for (uint16_t i = 0xD400; i <= 0xD418; i++) {
        _cpu->mem_write(i, 0);
    }
    
    delay(20);  // Breve pausa per pulizia audio
    
    // Re-init con nuovo brano
    _sidCurrentSong = nuovoNumero;
    inizializzaSID(nuovoNumero);
    
    Serial.println("Song change completedo");
}

void Loader::tickSID() {
    if (!_cpu || !_sidCaricato || !_sidPlaying) return;
    
    uint16_t playAddr = swapBytes(_sidHeader.playAddress);
    if (playAddr == 0) return;
    
    // Salva stato
    uint16_t saved_pc = _cpu->pc;
    uint8_t saved_sp = _cpu->sp;
    
    _cpu->mem_write(0x01FF, 0xEB);
    _cpu->mem_write(0x01FE, 0xEB);
    _cpu->sp = 0xFD;
    _cpu->pc = playAddr;
    
    // Esegui play - TIMEOUT ALTO, NO LOG
    uint32_t t0 = time_us_32();
    uint32_t cycles = 0;
    while ((time_us_32() - t0) < 19990 && _cpu->pc != 0xEBEB) {
      _cpu->execCPU(1);
      cycles++;
    }
    
    // Ripristina
    _cpu->pc = saved_pc;
    _cpu->sp = saved_sp;
}

void Loader::stopSID() {
    _sidPlaying = false;
    
    // Azzera registri SID (silenzio)
    for (uint16_t i = 0xD400; i <= 0xD418; i++) {
        _cpu->mem_write(i, 0);
    }
    
    Serial.println("SID stopped");
}

void Loader::elencaFileSID() {
    if (!_filesystemReady && !initFilesystem()) {
        Serial.println("Filesystem not available");
        return;
    }
    
    Serial.println("SID files available:");
    Serial.println("========================");
    
    if (_currentFS == FS_LITTLEFS) {
        Dir dir = LittleFS.openDir("/");
        while (dir.next()) {
            String nome = dir.fileName();
            if (nome.endsWith(".sid") || nome.endsWith(".SID")) {
                File f = dir.openFile("r");
                if (f) {
                    Serial.printf("  %s (%d byte)\n", nome.c_str(), f.size());
                    f.close();
                }
            }
        }
    }
    else if (_currentFS == FS_SD) {
        File root = SD.open("/");
        if (!root) {
            Serial.println("SD root opening error");
            return;
        }
        
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                String nome = file.name();
                if (nome.endsWith(".sid") || nome.endsWith(".SID")) {
                    Serial.printf("  %s (%d byte)\n", nome.c_str(), file.size());
                }
            }
            file.close();
            file = root.openNextFile();
        }
        root.close();
    }
    
    Serial.println("========================");
}

bool Loader::caricaSIDSelezionato() {
    if (!_cpu) {
        Serial.println("CPU not initialized!");
        return false;
    }
    
    if (_selectedGamePath[0] == '\0') {
        Serial.println("No SID file selected!");
        return false;
    }
    
    Serial.printf("Loading: %s\n", _selectedGamePath);
    
    // Apri file
    File sidFile = openFile(_selectedGamePath, "r");
    if (!sidFile) {
        Serial.println("Error opening file!");
        return false;
    }
    
    // Usa streaming
    bool loaded = caricaSIDStreaming(sidFile);
    sidFile.close();
    
    if (!loaded) {
        Serial.println("Loading error!");
        return false;
    }
    
    Serial.println("SID file loaded!");
    return true;
}

void Loader::debugSIDHeader() {
    if (!_sidCaricato) {
        Serial.println("No SID file loaded");
        return;
    }
    
    Serial.println("\n=== SID HEADER DEBUG ===");
    Serial.printf("Magic: %c%c%c%c\n", _sidHeader.magicID[0], _sidHeader.magicID[1],
                  _sidHeader.magicID[2], _sidHeader.magicID[3]);
    Serial.printf("Version: %d\n", swapBytes(_sidHeader.version));
    Serial.printf("Data offset: $%04X\n", swapBytes(_sidHeader.dataOffset));
    Serial.printf("Load address: $%04X\n", swapBytes(_sidHeader.loadAddress));
    Serial.printf("Init address: $%04X\n", swapBytes(_sidHeader.initAddress));
    Serial.printf("Play address: $%04X\n", swapBytes(_sidHeader.playAddress));
    Serial.printf("Songs: %d\n", swapBytes(_sidHeader.songs));
    Serial.printf("Start song: %d\n", swapBytes(_sidHeader.startSong));
    Serial.printf("Speed: $%08X\n", swapBytes32(_sidHeader.speed));
    Serial.printf("Name: %.32s\n", _sidHeader.name);
    Serial.printf("Author: %.32s\n", _sidHeader.author);
    Serial.printf("Released: %.32s\n", _sidHeader.released);
    Serial.println("========================\n");
}

// Metodo di debug per verificare cosa c'è in memoria
void Loader::debugMemoria(uint16_t inizio, uint16_t fine) {
    if (!_cpu) return;
    
    Serial.printf("Dump memory $%04X-$%04X:\n", inizio, fine);
    for (uint16_t addr = inizio; addr <= fine; addr += 16) {
        Serial.printf("$%04X: ", addr);
        for (int i = 0; i < 16 && (addr + i) <= fine; i++) {
            Serial.printf("%02X ", _cpu->memory[addr + i]);
        }
        Serial.println();
    }
}