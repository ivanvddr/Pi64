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
#ifndef LOADER_H
#define LOADER_H

#include "cpu.h"
#include "LittleFS.h"
#include <SD.h>

// Enum filesystem
enum FSType {
    FS_NONE,
    FS_SD,
    FS_LITTLEFS
};

#pragma pack(push, 1)
struct SIDHeader {
    char magicID[4];
    uint16_t version;
    uint16_t dataOffset;
    uint16_t loadAddress;
    uint16_t initAddress;
    uint16_t playAddress;
    uint16_t songs;
    uint16_t startSong;
    uint32_t speed;
    char name[32];
    char author[32];
    char released[32];
    uint16_t flags;
    uint8_t startPage;
    uint8_t pageLength;
    uint16_t reserved;
};
#pragma pack(pop)

class Loader {
private:
    cpu* _cpu;
    
    // Stato PRG
    bool _prgCaricato = false;
    uint16_t _indirizzoInizioPRG = 0;
    uint16_t _indirizzoFinePRG = 0;
    
    // Stato SID
    bool _sidCaricato = false;
    SIDHeader _sidHeader;
    uint16_t _sidCurrentSong = 0;
    uint32_t _sidPlayCounter = 0;
    bool _sidPlaying = false;
    
    // gestione filesystem
    FSType _currentFS = FS_NONE;
    bool _filesystemReady = false;
    char _selectedGamePath[128];  // Path del gioco selezionato

    bool initSD(); 
    
    // helper
    inline uint16_t swapBytes(uint16_t val) const {
        return (val << 8) | (val >> 8);
    }
    
    inline uint32_t swapBytes32(uint32_t val) const {
        return ((val << 24) & 0xFF000000) |
               ((val << 8)  & 0x00FF0000) |
               ((val >> 8)  & 0x0000FF00) |
               ((val >> 24) & 0x000000FF);
    }
    
public:
    explicit Loader(cpu* cpuRef);
    
    // Inizializza filesystem (chiamare prima di usare il browser!)
    bool initFilesystem(FSType preferredFS = FS_LITTLEFS);
    
    // Getter filesystem corrente
    FSType getCurrentFilesystem() const { return _currentFS; }
    bool isFilesystemReady() const { return _filesystemReady; }
    
    // Apri file dal filesystem corrente
    File openFile(const char* path, const char* mode = "r");
    bool fileExists(const char* path);
    
    // Path gioco selezionato (usato dal browser)
    void setSelectedGame(const char* path);
    const char* getSelectedGame() const { return _selectedGamePath; }
    
    // Chiudi filesystem (chiamare prima di C64setup se SD!)
    void closeFilesystem();
    
    // ========================================
    // Metodi PRG
    // ========================================
    
    bool caricaPRG(const char* nomeFile);
    bool caricaPRGDaBuffer(uint8_t* buffer, size_t dimensione);
    bool caricaPRGStreaming(File& file);
    
    // carica il gioco selezionato (usa _selectedGamePath)
    bool caricaGiocoSelezionato();
    
    void eseguiPRG(uint16_t indirizzoInizio = 0);
    void inizializzaPerPRG();
    void eseguiRUN();
    void eseguiPRGForzato(uint16_t indirizzo);
    
    // Getters
    bool isPrgCaricato() const { return _prgCaricato; }
    uint16_t getIndirizzoInizio() const { return _indirizzoInizioPRG; }
    uint16_t getIndirizzoFine() const { return _indirizzoFinePRG; }
    
    // Debug
    void elencaFilePRG();
    void debugMemoria(uint16_t inizio, uint16_t fine);
    
    // ========================================
    // Metodi SID
    // ========================================
    
    bool caricaSID(const char* nomeFile);
    bool caricaSIDDaBuffer(uint8_t* buffer, size_t dimensione);
    bool caricaSIDStreaming(File& file);
    bool caricaSIDSelezionato();
    void cambiaCanzone(uint8_t nuovoNumero);

    void inizializzaSID(uint8_t songNumber = 0);
    void tickSID();
    void stopSID();
    
    const char* getSIDName() const { return _sidHeader.name; }
    const char* getSIDAuthor() const { return _sidHeader.author; }
    const char* getSIDReleased() const { return _sidHeader.released; }
    uint16_t getSIDSongCount() const { return swapBytes(_sidHeader.songs); }
    uint16_t getSIDCurrentSong() const { return _sidCurrentSong; }
    bool isSIDPlaying() const { return _sidPlaying; }
    bool isSIDCaricato() const { return _sidCaricato; }
    
    void elencaFileSID();
    void debugSIDHeader();
};

#endif