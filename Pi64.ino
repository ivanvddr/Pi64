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
#include "cpu.h"
#include "configs.h"
#include "vic2.h"
#include "CIA1.h"

#include "CIA2.h"
#include "PS2Keyboard.h"
#include "VirtualKeyboard.h"
#include "FunduinoJoystick.h"
#include "joystick_config.h"
#include "SID.h"
#include "SID_shared.h"
#include "Utilities.h"
#include "Loader.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "hardware/gpio.h"
#include "LittleFS.h"
#include "char_rom.h"
#include <TFT_eSPI.h>
#include "C64Menu.h"
#include "PI64Logo.h"
#include "C64GameBrowser.h"
#include "SkipKeysUtils.h"
#include <Arduino.h>

TFT_eSPI tft;
cpu cpu;
vic2 vic2;
CIA1 cia1;
CIA2 cia2;
SID sid;
PLA pla;
PS2Keyboard keyboard;
FunduinoJoystick* joystick1 = nullptr;
FunduinoJoystick* joystick2 = nullptr;
Utilities utils(&cpu);

// Crea istanza del loader
Loader loader(&cpu);
uint8_t* bufferPrg = nullptr;
size_t sizePrg = 0;

static int currentLineInFrame = 0;
static bool frameComplete = false;
uint64_t lastFrameTime = 0;

//Ticker videoTimer;
struct repeating_timer video_timer;
struct repeating_timer sidplayer_timer;
struct repeating_timer keyboard_timer;

enum AppState {
    STATE_MENU,
    STATE_BASIC,
    STATE_GAME,
    STATE_SID
};

AppState currentState = STATE_MENU;

volatile bool changeSongRequested = false;
volatile int8_t songDirection = 0;  // +1 = next, -1 = prev
volatile uint32_t lastSongChange = 0;
const uint32_t SONG_CHANGE_DEBOUNCE_MS = 800;

C64Menu* menu = nullptr;
C64GameBrowser* gameBrowser = nullptr;

VirtualKeyboard* virtualKB = nullptr;
volatile bool kbToggleRequested = false;
volatile uint32_t lastKbToggle = 0;
const uint32_t KB_TOGGLE_DEBOUNCE_MS = 300;

volatile bool joystickInputDisabled = false;

void kbToggleISR() {
    uint32_t now = millis();
    if (now - lastKbToggle < KB_TOGGLE_DEBOUNCE_MS) return;
    
    lastKbToggle = now;
    kbToggleRequested = true;
}

void rebootPicoISR() {
    rp2040.restart();
}

// -----------------------------------------------------
//  Core1 main loop — in RAM, IRQ-free
// -----------------------------------------------------
void sid_core1_entry() {
    Serial.println("Core1: SID Audio Loop Starting...");
    
    //PRIORITA MASSIMA per interrupt audio 0 = massima
    irq_set_priority(TIMER_IRQ_1, 0);
    
    sid.reset();
    sid.begin(SID_SAMPLE_RATE);
    sid.initPWM(AUDIO_PIN);
    
    const uint32_t SAMPLE_PERIOD_US = 1000000UL / SID_SAMPLE_RATE;  // circa 45us
    
    uint32_t next_sample_time = time_us_32();
    uint32_t stats_counter = 0;
    uint32_t max_jitter_us = 0;
    
    Serial.printf("Core1: Audio loop ready (period: %lu µs)\n", SAMPLE_PERIOD_US);
    
    // ============================================
    // LOOP PRINCIPALE - TIMING CRITICO
    // ============================================
    while (true) {
        uint32_t now = time_us_32();
        
        // attesa attiva per precisione massima
        while ((int32_t)(time_us_32() - next_sample_time) < 0) {
            tight_loop_contents();  // polling
        }
        
        
        //genera sample (NON interrompibile)
        uint32_t irq_status = save_and_disable_interrupts();
        sid.audioTick();
        restore_interrupts(irq_status);
        
        // Avanza timestamp PRIMA di controllare overrun
        next_sample_time += SAMPLE_PERIOD_US;
        
        // recupero overrun (se core1 bloccato)
        uint32_t after = time_us_32();
        if ((int32_t)(after - next_sample_time) > (int32_t)SAMPLE_PERIOD_US) {
            // Se molto in ritardo, resetta timeline
            next_sample_time = after;
        }

    }
}

void sidPlayer_core1_entry() {
    Serial.println("Core1: SID Audio Loop (22050 Hz)");
    
    sid.reset();
    sid.begin(SID_SAMPLE_RATE);
    sid.initPWM(AUDIO_PIN);

    sid.setQualityMode(SID::QUALITY_FULL);
    Serial.println("SID Player: FULL QUALITY mode");
    
    const uint32_t SAMPLE_PERIOD_US = 1000000UL / SID_SAMPLE_RATE;
    uint32_t next_sample_time = time_us_32();

    while (true) {
        // Attesa precisa
        while ((int32_t)(time_us_32() - next_sample_time) < 0) {
            tight_loop_contents();
        }

        // Genera campione audio
        uint32_t irq_status = save_and_disable_interrupts();
        sid.audioTick();
        restore_interrupts(irq_status);

        // Avanza tempo
        next_sample_time += SAMPLE_PERIOD_US;
    }
}

bool video_callback(struct repeating_timer *t) {
    
    int cpuCycles = 0;
    for (int i = 0; i < LINES_PER_CALLBACK; i++) {
        cpuCycles += cpu.execCPU(CYCLES_PER_LINE);
        
        vic2.doRasterLine();

        currentLineInFrame++;
        if (currentLineInFrame >= RASTER_LINES) {
            currentLineInFrame = 0;
        }
    }
    
    cia1.update(cpuCycles);  
    cia2.update(cpuCycles);

    return true;
}

bool sidPlayer_callback(repeating_timer_t *rt) {
    uint32_t t0 = time_us_32();
    loader.tickSID();
    uint32_t dt = time_us_32() - t0;
    return true;
}


void cleanupTFT() {
    Serial.println("Cleanup TFT...");
    
    tft.endWrite();
    
    uint32_t timeout = millis();
    while (tft.dmaBusy() && (millis() - timeout < 200)) {
        delay(1);
    }
    
    if (tft.dmaBusy()) {
        Serial.println("TFT DMA force stop!");
        // TFT_eSPI usa di default channel 0 e 1
        dma_channel_abort(0);
        dma_channel_abort(1);
    }
    
    tft.fillScreen(TFT_BLACK);
    delay(50); // aspetta che fillScreen finisca
    
    tft.setViewport(0, 0, 320, 240);
    tft.setTextDatum(TL_DATUM);
    
    Serial.println("TFT clean");
}

void resetAllDMA() {
    Serial.println("Reset DMA channels...");
    
    // ferma tutte le transazioni DMA attive
    for (int ch = 0; ch < 12; ch++) {
        dma_channel_abort(ch);
        dma_channel_wait_for_finish_blocking(ch);
        
        // libera se era claimed
        if (dma_channel_is_claimed(ch)) {
            dma_channel_unclaim(ch);
        }
    }
    
    // reset hardware DMA controller
    hw_clear_bits(&dma_hw->ch[0].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    
    delay(10);
    Serial.println("DMA reset complete");
}

void C64setup(bool initAll = true) {
      cleanupTFT(); 
      pla.init(&cpu, &vic2, &cia1, &cia2, &sid);
      cpu.setPLA(&pla);
      cpu.setVic(&vic2);
      cpu.resetCPU();
      cia2.init(&cpu, nullptr);
      vic2.init(&tft);
      vic2.setCPU(&cpu);
      cia2.init(&cpu,&vic2);
      cpu.setupCIA2(&cia2);

      Serial.println("Initializing Keyboard...");
      keyboard.begin(PS2_DATA_PIN, PS2_CLOCK_PIN, KB_LAYOUT);
      cia1.init(&cpu, &keyboard);
      Serial.println();

      // setup skip button
      setupSkipButton(SKIP_BUTTON_PIN);

      // setup reset button
      pinMode(REBOOT_BUTTON_PIN, INPUT_PULLUP);
      attachInterrupt(digitalPinToInterrupt(REBOOT_BUTTON_PIN), 
                      rebootPicoISR, 
                      FALLING);  // trigger su pressione (LOW)


      Serial.println("Initializing Joysticks...");
      cia1.setJoysticks(joystick1, joystick2);
      Serial.println();

      cpu.setupCIA1(&cia1);

      Serial.println("=== MEMORY REPORT ===");
      Serial.printf("Free heap: %d bytes\n", rp2040.getFreeHeap());

      //ciclo per caricamento kernal e basic
      for (int i = 0; i < 2000000; i++) {
        cpu.execCPU();
        if (cpu.getpc() == 0xE5A0 && !vic2.rendering_started) {
          vic2.rendering_started = true;
        }
      }

      if (initAll) {
          Serial.println("Launching Core1 for SID...");
          multicore_launch_core1(sid_core1_entry);
          delay(100); // aspetta che core1 sia pronto
          delay(500); // aspetta init SID

          if (!LittleFS.begin()) {
              Serial.println("Error initializing LittleFS");
              return;
          }

          Serial.println("LittleFS initialized successfully");
          //calcola i microsecondi necessari al rendering di 8 rasterline su c64
          int timer_frequency_hz = 50 * (RASTER_LINES / LINES_PER_CALLBACK);
          int timer_period_us = 1000000 / timer_frequency_hz; // ca 513μs
            
          add_repeating_timer_us(timer_period_us, video_callback, NULL, &video_timer);
          irq_set_priority(TIMER_IRQ_0, 64);
          Serial.printf("Raster:   %dµs @ priority 64\n", timer_period_us);
            
          Serial.println("C64 Emulator started");
      }

}

void C64loop() {
    while(true) {

        if (kbToggleRequested) {
            kbToggleRequested = false;
            
            if (virtualKB->isVisible()) {
                // nascondi tastiera e riattiva joystick per gioco
                virtualKB->hide();
                joystickInputDisabled = false;  // riabilita CIA1
                Serial.println("Hidden Keyboard - Joystick > GAME");
            } else {
                // mostra tastiera e disattiva joystick per gioco
                virtualKB->show();
                joystickInputDisabled = true;   // disabilita CIA1
                Serial.println("Visible keyboard - joystick > KEYBOARD");
            }
        }
        
        // update tastiera virtuale (se visibile)
        if (virtualKB && virtualKB->isVisible()) {
            FunduinoJoystick* activeJoy = (joystick2 != nullptr) ? joystick2 : joystick1;
            if (activeJoy) {
                uint8_t joyState = activeJoy->read();
                virtualKB->update(joyState);
            }
        }

        // aggiorna stato dma se bus libero
        if (vic2.dmaInProgress && !tft.dmaBusy()) {
            vic2.dmaInProgress = false;
        }

        //aggiorna schermo
        vic2.updateDisplay();
        // check sequenza di skip
        updateSkipSequence(); 
        // libera da overrun
        yield();
    }
}





void setup() {
    //imposta clock del pico
    set_sys_clock_khz(PICO_CLOCK_KHZ, true);
  
    Serial.begin(115200);
    unsigned long t = millis();
    while (!Serial && millis() - t < 2000) delay(10);

    // Inizializza display
    tft.init();
    tft.initDMA();
    tft.setRotation(1); // 320x240

    tft.fillScreen(TFT_BLACK);

    // Inizializza joystick
    initJoysticks(&joystick1, &joystick2);
    
    // Usa il joystick attivo
    FunduinoJoystick* activeJoy = (joystick2 != nullptr) ? joystick2 : joystick1;
    
    if (activeJoy == nullptr) {
        Serial.println("Error: No joystick active!");
        tft.setTextColor(TFT_RED);
        tft.drawString("JOYSTICK ERROR!", 160, 120);
        while(1);
    }
    
    // Crea menu
    menu = new C64Menu(&tft, activeJoy);
    
    //Imposta logo
    menu->setLogo(logoPi64, 143, 70);
    
    // Inizializza menu
    menu->begin();
    
    Serial.println("Setup completed!");

} 

void handleMenu() {
    MenuAction action = menu->update(); 
    
    switch (action) {
        case ACTION_START_BASIC:
            Serial.println("BASIC start...");
            currentState = STATE_BASIC;
            break;
            
        case ACTION_START_GAME:
            Serial.println("GAME menu start...");
            currentState = STATE_GAME;
            break;

        case ACTION_START_SID:
            Serial.println("SID menu start...");
            currentState = STATE_SID;
            break;
            
        case ACTION_NONE:
        default:
            // Nessuna azione
            break;
    }
}

void handleBasic() {
  SPI1.end();
  delay(100);
  resetAllDMA();
  cleanupTFT();
  delay(100);
  sid.setQualityMode(SID::QUALITY_BALANCED);
  Serial.println("BASIC: BALANCED quality mode");
  C64setup();

  //RE-INIT PS/2 dopo Core1
  Serial.println("RE-INIT PS/2 after core1 start...");
  keyboard.begin(PS2_DATA_PIN, PS2_CLOCK_PIN, KB_LAYOUT);

  // PRIORITA INTERRUPT GPIO
  // Su RP2040, IO_IRQ_BANK0 gestisce TUTTI i GPIO interrupt
  // Priorità: 0 = massima, 255 = minima
  // Video timer = 64, quindi GPIO deve essere < 64
  irq_set_priority(IO_IRQ_BANK0, 32);  // GPIO alta priorità (può interrompere video)
  irq_set_enabled(IO_IRQ_BANK0, true);
  
  Serial.println("GPIO interrupt priority set to 32 (HIGHER than video=64)");

  add_repeating_timer_ms(10, keyboard_callback, NULL, &keyboard_timer);
  irq_set_priority(TIMER_IRQ_1, 128);
  Serial.println("Keyboard timer active at 100Hz");

  //VERIFICA
  Serial.println("Check interrupt priority:");
  Serial.printf("Video Timer (TIMER_IRQ_0):     %d\n", irq_get_priority(TIMER_IRQ_0));
  Serial.printf("Keyboard Timer (TIMER_IRQ_1):  %d\n", irq_get_priority(TIMER_IRQ_1));
  Serial.printf("GPIO Bank0 (IO_IRQ_BANK0):     %d\n\n", irq_get_priority(IO_IRQ_BANK0));

  C64loop();
}

void handleGameBrowser() {
    // Crea browser solo una volta
    if (!gameBrowser) {
        // USA LOADER per init filesystem
        // Prova prima LittleFS, poi fallback su SD
        if (!loader.initFilesystem(FS_SD)) {
            Serial.println("No filesystem available!");
            tft.fillScreen(TFT_RED);
            tft.setTextColor(TFT_WHITE);
            tft.setTextSize(2);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("NO FILESYSTEM!", 160, 120);
            delay(3000);
            currentState = STATE_MENU;
            menu->draw();
            return;
        }
        
        // Crea browser (passa il filesystem dal Loader)
        gameBrowser = new C64GameBrowser(&tft, &loader, SD_CS);
        gameBrowser->setLogo(logoPi64_small, 61, 30);
        gameBrowser->setPath("/games");
        gameBrowser->setFileExt(".prg");
        
        if (!gameBrowser->begin(loader.getCurrentFilesystem())) {
            Serial.println("Browser init failed!");
            delete gameBrowser;
            gameBrowser = nullptr;
            currentState = STATE_MENU;
            menu->draw();
            return;
        }
        
        // Browser OK, libera menu
        delete menu;
        menu = nullptr;
        Serial.printf("Browser active (%s)\n", 
                     loader.getCurrentFilesystem() == FS_SD ? "SD" : "LittleFS");
    }
    
    // Usa joystick per navigare
    FunduinoJoystick* activeJoy = (joystick2 != nullptr) ? joystick2 : joystick1;
    uint8_t joyState = activeJoy->read();
    
    // Aggiorna browser
    bool gameSelected = gameBrowser->update(joyState);
    
    if (gameSelected) {
        Serial.println("Game selected!");

        sid.setQualityMode(SID::QUALITY_BALANCED);
        Serial.println("Gaming: BALANCED quality mode");
        
        // Browser salva il path nel Loader
        gameBrowser->getPrgSelected();
        
        // ========================================
        // STEP 1: Libera browser
        // ========================================
        delete gameBrowser;
        gameBrowser = nullptr;
        Serial.println("Browser destroyed");
        
        
        // ========================================
        // STEP 3: Reset hardware
        // ========================================
        resetAllDMA();
        cleanupTFT();
        delay(100);
        
        // ========================================
        // STEP 4: Setup C64
        // ========================================
        C64setup();

        if (!virtualKB) {
            virtualKB = new VirtualKeyboard(&tft, &keyboard);
            virtualKB->begin();
        }
        
        // GPIO per toggle tastiera
        pinMode(KB_TOGGLE_PIN, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(KB_TOGGLE_PIN), 
                        kbToggleISR, 
                        FALLING);  // Trigger su pressione (LOW)
        Serial.printf("Keyboard toggle active on GPIO %d\n", KB_TOGGLE_PIN);
        
        // ========================================
        // STEP 5: Carica gioco con Loader!
        // ========================================
        if (!loader.caricaGiocoSelezionato()) {
            Serial.println("Loading error!");
            tft.fillScreen(TFT_BLUE);
            tft.setTextColor(TFT_RED);
            tft.setTextSize(2);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("LOAD ERROR!", 160, 120);
            delay(3000);
            return;
        }

        if (loader.getCurrentFilesystem() == FS_SD) {
            loader.closeFilesystem();
            delay(100);
            SPI1.end();
            delay(100);
            Serial.println("Closed filesystem and spi1");
        }
        
        // ========================================
        // STEP 6: Esegui gioco
        // ========================================
        loader.inizializzaPerPRG();
        loader.eseguiRUN();
        
        Serial.println("Game started!");
        
        C64loop();
    }
    
    delay(10);
    yield();
}

void handleSidPlayer() {
    // Crea browser solo una volta
    if (!gameBrowser) {
        // USA LOADER per init filesystem
        // Prova prima LittleFS, poi fallback su SD
        if (!loader.initFilesystem(FS_SD)) {
            Serial.println("No filesystem available!");
            tft.fillScreen(TFT_RED);
            tft.setTextColor(TFT_WHITE);
            tft.setTextSize(2);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("NO FILESYSTEM!", 160, 120);
            delay(3000);
            currentState = STATE_MENU;
            menu->draw();
            return;
        }
        
        // Crea browser (passa il filesystem dal Loader)
        gameBrowser = new C64GameBrowser(&tft, &loader, SD_CS);
        gameBrowser->setLogo(logoPi64_small, 61, 30);
        gameBrowser->setPath("/audio");
        gameBrowser->setFileExt(".sid");
        
        if (!gameBrowser->begin(loader.getCurrentFilesystem())) {
            Serial.println("Browser init failed!");
            delete gameBrowser;
            gameBrowser = nullptr;
            currentState = STATE_MENU;
            menu->draw();
            return;
        }
        
        // Browser OK, libera menu
        delete menu;
        menu = nullptr;
        Serial.printf("Browser SID active (%s)\n", 
                     loader.getCurrentFilesystem() == FS_SD ? "SD" : "LittleFS");
    }
    
    // Usa joystick per navigare
    FunduinoJoystick* activeJoy = (joystick2 != nullptr) ? joystick2 : joystick1;
    uint8_t joyState = activeJoy->read();
    
    // Aggiorna browser
    bool audioSelected = gameBrowser->update(joyState);
    
    if (audioSelected) {
        Serial.println("Piece selected!");
        
        // Browser salva il path nel Loader
        gameBrowser->getPrgSelected();
        
        // ========================================
        // STEP 1: Libera browser
        // ========================================
        delete gameBrowser;
        gameBrowser = nullptr;
        Serial.println("Browser destroyed");
               
        // ========================================
        // STEP 3: Reset hardware
        // ========================================
        resetAllDMA();
        cleanupTFT();
        delay(100);
        
        // ========================================
        // STEP 4: Setup C64
        // ========================================
        C64setup(false);

        Serial.println("\n=== ENTERING SID PLAYER MODE ===");

        memset(cpu.memory, 0, 0x10000);
        cpu.memory[0x00] = 0x2F;
        cpu.memory[0x01] = 0x35;
        cpu.prepareSidPlayer();
        if (!loader.caricaSIDSelezionato()) {
            Serial.println("Loading error!");
            tft.fillScreen(TFT_BLUE);
            tft.setTextColor(TFT_RED);
            tft.setTextSize(2);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("LOAD ERROR!", 160, 120);
            delay(3000);
            return;
         }

        if (loader.getCurrentFilesystem() == FS_SD) {
            loader.closeFilesystem();
            SPI1.end();
            delay(50);
            Serial.println("Closed filesystem and spi1");
        }

        loader.inizializzaSID(0);

        if (!loader.isSIDCaricato()) {
            Serial.println("SID not initialized!");
            delay(3000);
            rp2040.restart();
            return;
        }
    
        Serial.println("SID INIT completed");

        // Avvia core1 solo per l’audio
        multicore_launch_core1(sidPlayer_core1_entry);
        delay(200);

        // Core0: timer a 50Hz per routine play
        add_repeating_timer_ms(20, sidPlayer_callback, NULL, &sidplayer_timer);
        Serial.println("SID Player active - playback at 50Hz");

        //prepara lo schermo con le informazioni sul brano
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setTextSize(2);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("SID PLAYER", 160, 30);
        
        tft.setTextSize(1);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        
        // Nome, autore, anno
        char nameStr[40];
        snprintf(nameStr, sizeof(nameStr), "%.32s", loader.getSIDName());
        tft.drawString(nameStr, 160, 70);
        
        char authorStr[40];
        snprintf(authorStr, sizeof(authorStr), "by %.32s", loader.getSIDAuthor());
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(authorStr, 160, 90);
        
        char yearStr[40];
        snprintf(yearStr, sizeof(yearStr), "%.32s", loader.getSIDReleased());
        tft.drawString(yearStr, 160, 110);
        
        // Info song
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        char songInfo[32];
        snprintf(songInfo, sizeof(songInfo), "Song %d of %d", 
                loader.getSIDCurrentSong() + 1, 
                loader.getSIDSongCount());
        tft.drawString(songInfo, 160, 140);
        
        // Status
        tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
        tft.setTextSize(2);
        tft.drawString("PLAYING", 160, 170);
        
        // Istruzioni
        tft.setTextSize(1);
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.drawString("Press REBOOT to exit", 160, 220);
        
        Serial.println("=========================");
        Serial.println("SID PLAYER ACTIVE!");
        Serial.println("=========================\n");
    
        delay(10);

        uint32_t lastUpdate = millis();
        bool playStatus = true;
        uint16_t totalSongs = loader.getSIDSongCount();
        uint16_t currentSong = loader.getSIDCurrentSong()+1;
    
        while (true) {

          if (changeSongRequested) {
                changeSongRequested = false;
                
                // Calcola nuovo song number
                int16_t newSong = currentSong + songDirection;
                
                // Wrap around
                if (newSong < 0) {
                    newSong = totalSongs - 1;  // Vai all'ultimo
                } else if (newSong >= totalSongs) {
                    newSong = 0;  // Torna al primo
                }
                
                currentSong = newSong;
                
                Serial.printf("Change song: %d/%d\n", currentSong + 1, totalSongs);
                
                // STOP audio temporaneamente
                loader.stopSID();
                delay(50);
                
                // RE-INIT con nuovo brano
                loader.inizializzaSID(currentSong);
                
                if (!loader.isSIDPlaying()) {
                    Serial.println("Error re-init SID!");
                    // Riprova con brano 0
                    loader.inizializzaSID(0);
                    currentSong = 0;
                }
                
                // ⚡ AGGIORNA UI
                tft.fillRect(0, 130, 320, 30, TFT_BLACK);
                tft.setTextColor(TFT_GREEN, TFT_BLACK);
                tft.setTextSize(1);
                tft.setTextDatum(MC_DATUM);
                
                char songInfo[32];
                snprintf(songInfo, sizeof(songInfo), "Song %d of %d", 
                        currentSong + 1, totalSongs);
                tft.drawString(songInfo, 160, 140);
                
                Serial.println("Song changed!");
            }
            
            // JOYSTICK per cambio brano
            static uint8_t lastJoyState = 0;  // IMPORTANTE: stato precedente
            
            FunduinoJoystick* activeJoy = (joystick2 != nullptr) ? joystick2 : joystick1;
            if (activeJoy) {
                uint8_t joyState = activeJoy->read();
                
                // EDGE DETECTION: trigger solo sul NUOVO movimento
                uint8_t joyPressed = joyState & ~lastJoyState;  // Solo nuovi bit attivi
                
                // LEFT = Previous song (solo se APPENA premuto)
                if ((joyPressed & 0x04) && 
                    (millis() - lastSongChange > SONG_CHANGE_DEBOUNCE_MS)) {
                    changeSongRequested = true;
                    songDirection = -1;
                    lastSongChange = millis();
                }
                
                // RIGHT = Next song (solo se APPENA premuto)
                if ((joyPressed & 0x08) && 
                    (millis() - lastSongChange > SONG_CHANGE_DEBOUNCE_MS)) {
                    changeSongRequested = true;
                    songDirection = +1;
                    lastSongChange = millis();
                }
                
                // Aggiorna stato precedente
                lastJoyState = joyState;
            }

            // Animazione status
            if (millis() - lastUpdate > 500) {
                lastUpdate = millis();
                playStatus = !playStatus;
                
                tft.setTextColor(playStatus ? TFT_MAGENTA : TFT_BLUE, TFT_BLACK);
                tft.setTextSize(2);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("PLAYING", 160, 170);
            }
            
            // Check bottone uscita
            if (digitalRead(REBOOT_BUTTON_PIN) == LOW) {
                Serial.println("Exit required...");
                loader.stopSID();
                delay(100);
                rp2040.restart();
            }
            
            yield();
        }

    }
}

void loop() {

      switch (currentState) {
        case STATE_MENU:
            handleMenu();
            break;
            
        case STATE_BASIC:
            handleBasic();
            break;
            
        case STATE_GAME:
            handleGameBrowser();
            break;

        case STATE_SID:
            handleSidPlayer();
            break;
      } 

}

bool keyboard_callback(struct repeating_timer *t) {
    // Processa max 2 tasti per chiamata (evita lag)
    keyboard.processKeys(4);
    return true;
}
