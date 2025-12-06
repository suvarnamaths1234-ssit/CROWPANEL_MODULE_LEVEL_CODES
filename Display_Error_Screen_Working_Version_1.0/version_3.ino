
#include <lvgl.h>
#include <TFT_eSPI.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "ui.h"
#include "vars.h"
#include "screens.h"
#include "JarController.h"
#include <vector>


extern objects_t objects;

static const uint16_t screenWidth = 480;
static const uint16_t screenHeight = 320;

#define TOUCH_CS 12

// Screensaver settings
#define SCREENSAVER_TIMEOUT 60000
unsigned long lastActivityTime = 0;
bool screensaverActive = false;
lv_obj_t *screensaver_screen = nullptr;

// ===== DATA LOGGING VARIABLES (DECLARE THESE EARLY) =====
bool dataLoggingEnabled = false;
lv_timer_t *dataLogTimer = nullptr;


// Motor pins for 8 Jars
#define JAR1_ENABLE_PIN 25
#define JAR2_ENABLE_PIN 32
#define JAR3_ENABLE_PIN 22
#define JAR4_ENABLE_PIN 22
#define JAR5_ENABLE_PIN 15
#define JAR6_ENABLE_PIN 4
#define JAR7_ENABLE_PIN 17
#define JAR8_ENABLE_PIN 15


// ==================== SD Card SPI Pins (HSPI - SAME AS CODE 1) ====================
const int sck  = 18;   // Changed from 14 to 18
const int miso = 19;   // Changed from 33 to 19
const int mosi = 23;   // Changed from 13 to 23
const int cs   = 5;

const int freq = 30000;
const int pwmChannel = 0;
const int resolution = 8;
const int duty_percentage = 80;

TFT_eSPI tft(screenWidth, screenHeight);
SPIClass spiSD(HSPI);  // Use HSPI for SD card

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * 10];

// ==================== Touch Calibration (SAME AS CODE 1) ====================
uint16_t calData[5] = { 257, 3587, 292, 3442, 7 };   // Use code 1's calibration

bool sdCardReady = false;
bool ConfigurationRunning = false;


// Batch configuration variables
bool selectedJars[8] = {false, false, false, false, false, false, false, false};
int ConfigurationRPM = 0;
int ConfigurationDuration = 0;


JarController* jars[8];
JarController* currentJar = nullptr;
int currentJarIndex = -1;
lv_obj_t *lastActiveScreen = nullptr;
lv_timer_t *errorBlinkTimer = nullptr;
bool errorBlinkState = false;
ErrorManager errorManager;

// Function declarations
void listDir(fs::FS &fs, const char *dirname, uint8_t levels);
void showSdLogOnSettings(lv_event_t *e);
// ===== Forward declarations for reset callbacks =====
void confirmResetCallback(lv_event_t *e);
void confirmSingleJarResetCallback(lv_event_t *e);



// ==================== SD Card Helper Functions (FROM CODE 1) ====================
bool acquireSDCard() {
    if (!sdCardReady) {
        Serial.println("SD card not initialized / not ready");
        return false;
    }
    return true;
}

void checkSDStatus() {
    Serial.println("=== SD Card Status ===");
    Serial.printf("SD Ready Flag: %s\n", sdCardReady ? "true" : "false");
    if (!sdCardReady) return;

    uint8_t cardType = SD.cardType();
    Serial.printf("Card Type: %d\n", cardType);

    if (cardType != CARD_NONE) {
        Serial.printf("Card Size: %lluMB\n",
                      (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
        Serial.printf("Total Space: %lluMB\n",
                      (unsigned long long)(SD.totalBytes() / (1024ULL * 1024ULL)));
        Serial.printf("Used Space: %lluMB\n",
                      (unsigned long long)(SD.usedBytes() / (1024ULL * 1024ULL)));
    }
    Serial.println("===================");
}

// ==================== SD Card File Operations (FROM CODE 1) ====================
void appendFile(fs::FS &fs, const char *path, const char *message) {
    if (!acquireSDCard()) {
        Serial.println("Cannot acquire SD card for appending");
        return;
    }

    Serial.println("=== appendFile called ===");
    Serial.printf("Path: %s\n", path);
    Serial.printf("Message: %s\n", message);

    File file = fs.open(path, FILE_APPEND);
    if (!file) {
        Serial.println("ERROR: Failed to open file for appending");
       
        // Try to create the file first
        Serial.println("Attempting to create file...");
        File newFile = fs.open(path, FILE_WRITE);
        if (newFile) {
            Serial.println("File created successfully");
            newFile.close();
            delay(50);
           
            file = fs.open(path, FILE_APPEND);
            if (!file) {
                Serial.println("ERROR: Still cannot open file after creation");
                return;
            }
        } else {
            Serial.println("ERROR: Cannot create file");
            return;
        }
    }

    Serial.println("File opened successfully, writing data...");
    size_t written = file.print(message);
    Serial.printf("Bytes written: %d\n", (int)written);

    if (written > 0) {
        Serial.println("SUCCESS: Message appended successfully");
        file.flush();
    } else {
        Serial.println("ERROR: Append failed - 0 bytes written");
    }

    file.close();
    Serial.println("File closed");
    Serial.println("=== appendFile complete ===");
}









// ===== ERROR BUTTON BLINK FUNCTION =====
void errorButtonBlink_cb(lv_timer_t *timer) {
    if (errorManager.hasErrors() && !errorManager.areErrorsAcknowledged()) {
        errorBlinkState = !errorBlinkState;
        if (errorBlinkState) {
            lv_obj_set_style_bg_color(objects.errors_button,
                                     lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(objects.errors_button,
                                     lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
        }
    } else if (errorManager.hasErrors() && errorManager.areErrorsAcknowledged()) {
        lv_obj_set_style_bg_color(objects.errors_button,
                                 lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(objects.errors_button,
                                 lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
    }
}

// ===== VIEW ERRORS BUTTON CALLBACK =====
void viewErrorsButton_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        errorManager.acknowledgeErrors();
        errorManager.updateErrorDisplay();
        Serial.println("Error display refreshed - errors acknowledged");
       
        if (errorManager.hasErrors()) {
            lv_obj_set_style_bg_color(objects.errors_button,
                                     lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(objects.errors_button,
                                     lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
        }
    }
}

// ===== CLEAR ERRORS BUTTON CALLBACK =====
void clearErrorsButton_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        // Check if we're on the errors page
        lv_obj_t *current_screen = lv_scr_act();
        if (current_screen != objects.errors_page) {
            Serial.println("Clear button only works on errors page");
            return;
        }
        
        Serial.println("=== CLEARING ALL ERRORS ===");
        
        errorManager.clearErrors();
        
        // Reset error button color to default
        if (objects.errors_button) {
            lv_obj_set_style_bg_color(objects.errors_button,
                                     lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
        }
        
        // Show confirmation on the clear button
        if (objects.eror_clear_button) {
            lv_obj_set_style_bg_color(objects.eror_clear_button,
                                     lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
            
            // Reset button color after 1 second
            lv_timer_t *reset_timer = lv_timer_create([](lv_timer_t *timer) {
                if (objects.eror_clear_button) {
                    lv_obj_set_style_bg_color(objects.eror_clear_button,
                                             lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
                }
                lv_timer_del(timer);
            }, 1000, NULL);
        }
        
        Serial.println("All errors cleared successfully");
    }
}


// ===== SCREENSAVER FUNCTIONS =====
void createScreensaver() {
    screensaver_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screensaver_screen, lv_color_black(), LV_PART_MAIN);
   
    lv_obj_t *label = lv_label_create(screensaver_screen);
    lv_label_set_text(label, "Sense Semiconductor & IT Solutions");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);
   
    Serial.println("Screensaver created");
}

void activateScreensaver() {
    if (!screensaverActive) {
        screensaverActive = true;
        lastActiveScreen = lv_scr_act();
        lv_scr_load(screensaver_screen);
        Serial.println("Screensaver activated");
    }
}

void deactivateScreensaver() {
    if (screensaverActive) {
        screensaverActive = false;
        if (lastActiveScreen != nullptr) {
            lv_scr_load(lastActiveScreen);
        }
        lastActivityTime = millis();
        Serial.println("Screensaver deactivated");
    }
}

void resetActivityTimer() {
    lastActivityTime = millis();
    if (screensaverActive) {
        deactivateScreensaver();
    }
}

void checkScreensaver() {
    if (!screensaverActive && (millis() - lastActivityTime > SCREENSAVER_TIMEOUT)) {
        activateScreensaver();
    }
}

// ===== CHECKBOX CALLBACKS =====
void jar1_checkbox_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        selectedJars[0] = lv_obj_has_state(objects.jar1_checkbox, LV_STATE_CHECKED);
        Serial.printf("Jar 1 checkbox: %s\n", selectedJars[0] ? "SELECTED" : "UNSELECTED");
    }
}

void jar2_checkbox_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        selectedJars[1] = lv_obj_has_state(objects.jar2_checkbox, LV_STATE_CHECKED);
        Serial.printf("Jar 2 checkbox: %s\n", selectedJars[1] ? "SELECTED" : "UNSELECTED");
    }
}

void jar3_checkbox_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        selectedJars[2] = lv_obj_has_state(objects.jar3_checkbox, LV_STATE_CHECKED);
        Serial.printf("Jar 3 checkbox: %s\n", selectedJars[2] ? "SELECTED" : "UNSELECTED");
    }
}

void jar4_checkbox_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        selectedJars[3] = lv_obj_has_state(objects.jar4_checkbox, LV_STATE_CHECKED);
        Serial.printf("Jar 4 checkbox: %s\n", selectedJars[3] ? "SELECTED" : "UNSELECTED");
    }
}

void jar5_checkbox_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        selectedJars[4] = lv_obj_has_state(objects.jar5_checkbox, LV_STATE_CHECKED);
        Serial.printf("Jar 5 checkbox: %s\n", selectedJars[4] ? "SELECTED" : "UNSELECTED");
    }
}

void jar6_checkbox_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        selectedJars[5] = lv_obj_has_state(objects.jar6_checkbox, LV_STATE_CHECKED);
        Serial.printf("Jar 6 checkbox: %s\n", selectedJars[5] ? "SELECTED" : "UNSELECTED");
    }
}

void jar7_checkbox_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        selectedJars[6] = lv_obj_has_state(objects.jar7_checkbox, LV_STATE_CHECKED);
        Serial.printf("Jar 7 checkbox: %s\n", selectedJars[6] ? "SELECTED" : "UNSELECTED");
    }
}

void jar8_checkbox_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        selectedJars[7] = lv_obj_has_state(objects.jar8_checkbox, LV_STATE_CHECKED);
        Serial.printf("Jar 8 checkbox: %s\n", selectedJars[7] ? "SELECTED" : "UNSELECTED");
    }
}

// ===== BATCH RPM INCREMENT/DECREMENT =====
void Configuration_rpm_plus_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ConfigurationRPM = constrain(ConfigurationRPM + 5, 0, 100);
        char buf[8];
        sprintf(buf, "%d", ConfigurationRPM);
        lv_textarea_set_text(objects.rpm_textarea, buf);
        Serial.printf("Configuration RPM: %d\n", ConfigurationRPM);
    }
}

void Configuration_rpm_minus_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ConfigurationRPM = constrain(ConfigurationRPM - 5, 0, 100);
        char buf[8];
        sprintf(buf, "%d", ConfigurationRPM);
        lv_textarea_set_text(objects.rpm_textarea, buf);
        Serial.printf("Configuration RPM: %d\n", ConfigurationRPM);
    }
}

// ===== BATCH DURATION INCREMENT/DECREMENT =====
void Configuration_duration_plus_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ConfigurationDuration = constrain(ConfigurationDuration + 10, 0, 3600);
        char buf[8];
        sprintf(buf, "%d", ConfigurationDuration);
        lv_textarea_set_text(objects.duration_textarea, buf);
        Serial.printf("Configuration Duration: %d minutes\n", ConfigurationDuration);
    }
}

void Configuration_duration_minus_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ConfigurationDuration = constrain(ConfigurationDuration - 10, 0, 3600);
        char buf[8];
        sprintf(buf, "%d", ConfigurationDuration);
        lv_textarea_set_text(objects.duration_textarea, buf);
        Serial.printf("Configuration Duration: %d minutes\n", ConfigurationDuration);
    }
}


// ===== BATCH RPM VALIDATION =====
void Configuration_rpm_textarea_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        const char *txt = lv_textarea_get_text(objects.rpm_textarea);
        int newRPM = atoi(txt);
        
        if (!ErrorManager::isValidRPM(newRPM)) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Invalid Configuration RPM entered: %d", newRPM);
            errorManager.logError(ERR_INVALID_RPM, 0, msg);
            
            // Reset to last valid value
            char buf[8];
            sprintf(buf, "%d", ConfigurationRPM);
            lv_textarea_set_text(objects.rpm_textarea, buf);
        } else {
            ConfigurationRPM = constrain(newRPM, 0, 100);
            Serial.printf("Configuration RPM set to: %d\n", ConfigurationRPM);
        }
    }
}



// ===== BATCH DURATION VALIDATION =====
void Configuration_duration_textarea_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        const char *txt = lv_textarea_get_text(objects.duration_textarea);
        int newDuration = atoi(txt);
        
        if (!ErrorManager::isValidDuration(newDuration)) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Invalid Configuration duration entered: %d", newDuration);
            errorManager.logError(ERR_INVALID_DURATION, 0, msg);
            
            // Reset to last valid value
            char buf[8];
            sprintf(buf, "%d", ConfigurationDuration);
            lv_textarea_set_text(objects.duration_textarea, buf);
        } else {
            ConfigurationDuration = constrain(newDuration, 0, 3600);
            Serial.printf("Configuration Duration set to: %d minutes\n", ConfigurationDuration);
        }
    }
}



// ===== BATCH SAVE BUTTON WITH ERROR LOGGING =====
void Configuration_save_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        Serial.println("=== Configuration SAVE STARTED ===");

        // Update checkbox states
        selectedJars[0] = lv_obj_has_state(objects.jar1_checkbox, LV_STATE_CHECKED);
        selectedJars[1] = lv_obj_has_state(objects.jar2_checkbox, LV_STATE_CHECKED);
        selectedJars[2] = lv_obj_has_state(objects.jar3_checkbox, LV_STATE_CHECKED);
        selectedJars[3] = lv_obj_has_state(objects.jar4_checkbox, LV_STATE_CHECKED);
        selectedJars[4] = lv_obj_has_state(objects.jar5_checkbox, LV_STATE_CHECKED);
        selectedJars[5] = lv_obj_has_state(objects.jar6_checkbox, LV_STATE_CHECKED);
        selectedJars[6] = lv_obj_has_state(objects.jar7_checkbox, LV_STATE_CHECKED);
        selectedJars[7] = lv_obj_has_state(objects.jar8_checkbox, LV_STATE_CHECKED);
       
        Serial.println("Checkbox states:");
        for (int i = 0; i < 8; i++) {
            Serial.printf("  Jar %d: %s\n", i + 1, selectedJars[i] ? "CHECKED" : "UNCHECKED");
        }
       
        int savedCount = 0;
        bool anySelected = false;
       
        // Validate batch RPM and Duration
        if (!ErrorManager::isValidRPM(ConfigurationRPM)) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Invalid Configuration RPM: %d", ConfigurationRPM);
            errorManager.logError(ERR_INVALID_RPM, 0, msg);  // jarID = 0 for system errors
            
            if (objects.all_jar_configurations_save_button) {
                lv_obj_set_style_bg_color(objects.all_jar_configurations_save_button,
                                         lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
            }
            return;
        }
        
        if (!ErrorManager::isValidDuration(ConfigurationDuration)) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Invalid Configuration duration: %d", ConfigurationDuration);
            errorManager.logError(ERR_INVALID_DURATION, 0, msg);
            
            if (objects.all_jar_configurations_save_button) {
                lv_obj_set_style_bg_color(objects.all_jar_configurations_save_button,
                                         lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
            }
            return;
        }
       
        for (int i = 0; i < 8; i++) {
            if (selectedJars[i]) {
                anySelected = true;
                jars[i]->setRPMValue(ConfigurationRPM);
                jars[i]->setDurationValue(ConfigurationDuration);
                
                if (sdCardReady) {
                    if (jars[i]->saveSettings("/jar_settings.txt")) {
                        savedCount++;
                        Serial.printf("Jar %d: RPM=%d, Duration=%d min\n",
                                     i + 1, ConfigurationRPM, ConfigurationDuration);
                    } else {
                        // Log error if save failed
                        char msg[64];
                        snprintf(msg, sizeof(msg), "Configuration save failed for Jar %d", i + 1);
                        errorManager.logError(ERR_SD_WRITE_FAILED, 0, msg);
                    }
                } else {
                    Serial.printf("Jar %d: SD card not ready\n", i + 1);
                    errorManager.logError(ERR_SD_INIT_FAILED, 0, "SD card not ready for Configuration save");
                }
            }
        }
       
        if (!anySelected) {
            Serial.println("No jars selected!");
            errorManager.logError(ERR_INVALID_DURATION, 0, "Configuration save: No jars selected");
            
            if (objects.all_jar_configurations_save_button) {
                lv_obj_set_style_bg_color(objects.all_jar_configurations_save_button,
                                         lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
            }
        } else {
            Serial.printf("=== Configuration SAVE COMPLETE: %d/%d jars saved ===\n", savedCount, 8);
            
            if (savedCount < 8) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Configuration save partial: %d/%d jars saved", savedCount, 8);
                errorManager.logError(ERR_SD_WRITE_FAILED, 0, msg);
            }
            
            if (objects.all_jar_configurations_save_button) {
                lv_obj_set_style_bg_color(objects.all_jar_configurations_save_button,
                                         savedCount > 0 ? lv_palette_main(LV_PALETTE_GREEN) 
                                                        : lv_palette_main(LV_PALETTE_RED), 
                                         LV_PART_MAIN);
            }
        }
    }
}




// ===== BATCH START BUTTON WITH ERROR LOGGING =====
void Configuration_start_all_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        Serial.println("=== Configuration START INITIATED ===");

        int startedCount = 0;
        bool anySelected = false;

        for (int i = 0; i < 8; i++) {
            if (selectedJars[i]) {
                anySelected = true;

                if (!jars[i]->areSettingsSaved()) {
                    Serial.printf("Jar %d: Settings not saved\n", i + 1);
                    char msg[64];
                    snprintf(msg, sizeof(msg), "configuration start: Jar %d settings not saved", i + 1);
                    errorManager.logError(ERR_SETTINGS_NOT_SAVED, 0, msg);
                    continue;
                }

                if (jars[i]->getRPM() <= 0 || jars[i]->getDuration() <= 0) {
                    Serial.printf("Jar %d: Invalid RPM or Duration\n", i + 1);
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Configuration start: Jar %d invalid parameters", i + 1);
                    errorManager.logError(ERR_INVALID_RPM, 0, msg);
                    continue;
                }

                jars[i]->startMotor();
                startedCount++;
                Serial.printf("Jar %d started successfully\n", i + 1);
            }
        }

        // UI + state handling
        if (!anySelected) {
            Serial.println("No jars selected!");
            errorManager.logError(ERR_INVALID_DURATION, 0, "Configuration start: No jars selected");

            if (objects.start_all_button) {
                lv_obj_set_style_bg_color(objects.start_all_button,
                                          lv_palette_main(LV_PALETTE_RED),
                                          LV_PART_MAIN);

                lv_obj_t *label = lv_obj_get_child(objects.start_all_button, 0);
                if (label) {
                    lv_label_set_text(label, "No Selection!");
                }

                lv_timer_t *reset_timer = lv_timer_create([](lv_timer_t *timer) {
                    if (objects.start_all_button) {
                        lv_obj_set_style_bg_color(objects.start_all_button,
                                                  lv_palette_main(LV_PALETTE_BLUE),
                                                  LV_PART_MAIN);
                        lv_obj_t *label = lv_obj_get_child(objects.start_all_button, 0);
                        if (label) {
                            lv_label_set_text(label, "Start");
                        }
                    }
                    lv_timer_del(timer);
                }, 1000, NULL);
            }
        }
        else if (startedCount > 0) {
            Serial.printf("=== Configuration START COMPLETE: %d jars running ===\n", startedCount);
            
            // Log partial start if not all selected jars started
            int selectedCount = 0;
            for (int i = 0; i < 8; i++) {
                if (selectedJars[i]) selectedCount++;
            }
            
            if (startedCount < selectedCount) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Configuration start partial: %d/%d jars started", 
                        startedCount, selectedCount);
                errorManager.logError(ERR_JAR_TIMEOUT, 0, msg);
            }

            ConfigurationRunning = true;

            if (objects.start_all_button) {
                lv_obj_set_style_bg_color(objects.start_all_button,
                                          lv_palette_main(LV_PALETTE_GREEN),
                                          LV_PART_MAIN);

                lv_obj_t *label = lv_obj_get_child(objects.start_all_button, 0);
                if (label) {
                    lv_label_set_text(label, "Running");
                }
            }
        }
        else {
            Serial.println("No jars could be started (check settings)");
            errorManager.logError(ERR_SETTINGS_NOT_SAVED, 0, 
                                 "Configuration start failed: Check jar settings");

            if (objects.start_all_button) {
                lv_obj_set_style_bg_color(objects.start_all_button,
                                          lv_palette_main(LV_PALETTE_ORANGE),
                                          LV_PART_MAIN);

                lv_obj_t *label = lv_obj_get_child(objects.start_all_button, 0);
                if (label) {
                    lv_label_set_text(label, "Check Settings!");
                }

                lv_timer_t *reset_timer = lv_timer_create([](lv_timer_t *timer) {
                    if (objects.start_all_button) {
                        lv_obj_set_style_bg_color(objects.start_all_button,
                                                  lv_palette_main(LV_PALETTE_BLUE),
                                                  LV_PART_MAIN);
                        lv_obj_t *label = lv_obj_get_child(objects.start_all_button, 0);
                        if (label) {
                            lv_label_set_text(label, "Start");
                        }
                    }
                    lv_timer_del(timer);
                }, 1000, NULL);
            }
        }
    }
}



void Configuration_stop_all_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        Serial.println("=== Configuration STOP INITIATED ===");
       
        int stoppedCount = 0;
        bool anyRunning = false;
       
        for (int i = 0; i < 8; i++) {
            if (selectedJars[i] && jars[i]->isMotorRunning()) {
                anyRunning = true;
                jars[i]->stopMotor();
                stoppedCount++;
                Serial.printf(" Jar %d stopped\n", i + 1);
            }
        }
       
        if (!anyRunning) {
            Serial.println(" No jars were running");
            
            // Show visual feedback - ORANGE for warning
            if (objects.stop_all_button) {
                lv_obj_set_style_bg_color(objects.stop_all_button,
                                          lv_palette_main(LV_PALETTE_ORANGE),
                                          LV_PART_MAIN);
                
                lv_obj_t *label = lv_obj_get_child(objects.stop_all_button, 0);
                if (label) {
                    lv_label_set_text(label, "Not Running!");
                }
                
                // Reset STOP after 2 seconds
                lv_timer_t *reset_timer = lv_timer_create([](lv_timer_t *timer) {
                    if (objects.stop_all_button) {
                        lv_obj_set_style_bg_color(objects.stop_all_button,
                                                  lv_palette_main(LV_PALETTE_BLUE),
                                                  LV_PART_MAIN);
                        lv_obj_t *label = lv_obj_get_child(objects.stop_all_button, 0);
                        if (label) {
                            lv_label_set_text(label, "Stop");
                        }
                    }
                    lv_timer_del(timer);
                }, 2000, NULL);
            }
        } else {
            Serial.printf("=== Configuration STOP COMPLETE: %d jars stopped ===\n", stoppedCount);
                ConfigurationRunning = false;

            // STOP button: show completed
            if (objects.stop_all_button) {
                lv_obj_set_style_bg_color(objects.stop_all_button,
                                          lv_palette_main(LV_PALETTE_GREEN),
                                          LV_PART_MAIN);
                
                lv_obj_t *label = lv_obj_get_child(objects.stop_all_button, 0);
                if (label) {
                    lv_label_set_text(label, "Completed");
                }
            }

            // 🔹 Immediately reset START button back to idle "Start"
            if (objects.start_all_button) {
                lv_obj_set_style_bg_color(objects.start_all_button,
                                          lv_palette_main(LV_PALETTE_BLUE),
                                          LV_PART_MAIN);
                lv_obj_t *label = lv_obj_get_child(objects.start_all_button, 0);
                if (label) {
                    lv_label_set_text(label, "Start");
                }
            }

            // Only reset STOP button text/color after 1 second
            lv_timer_t *reset_timer = lv_timer_create([](lv_timer_t *timer) {
                if (objects.stop_all_button) {
                    lv_obj_set_style_bg_color(objects.stop_all_button,
                                              lv_palette_main(LV_PALETTE_BLUE),
                                              LV_PART_MAIN);
                    lv_obj_t *label = lv_obj_get_child(objects.stop_all_button, 0);
                    if (label) {
                        lv_label_set_text(label, "Stop");
                    }
                }
                lv_timer_del(timer);
            }, 1000, NULL);
        }
    }
}









void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
    Serial.printf("Listing directory: %s\n", dirname);
    File root = fs.open(dirname);
    if (!root || !root.isDirectory()) {
        Serial.println("Failed to open directory");
        return;
    }
   
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels) listDir(fs, file.path(), levels - 1);
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
    root.close();
}


void updateUIForCurrentJar() {
    if (currentJar != nullptr) {
        currentJar->updateUIFields();
        currentJar->updateButtonStates(currentJar->isMotorRunning());
       
        char buf[32];
        sprintf(buf, "RPM: %d", currentJar->getRPM());
        lv_label_set_text(objects.readings_rpm, buf);
       
        char jarTitle[32];
        sprintf(jarTitle, "JAR %d", currentJarIndex + 1);
        if (objects.jar_number_on_info_screen) {
            lv_label_set_text(objects.jar_number_on_info_screen, jarTitle);
        }
       
        Serial.printf("Switched to Jar %d\n", currentJarIndex + 1);
    }
}

// Function to configure SD data log label formatting
void configureSdDataLogUI() {
    if (objects.sd_data_logging) {
        // Align to top-left with padding from home button area
        lv_obj_align(objects.sd_data_logging, LV_ALIGN_TOP_LEFT, 10, 80);  // 80px from top, 10px from left
        
        // Set text alignment to left
        lv_obj_set_style_text_align(objects.sd_data_logging, LV_TEXT_ALIGN_LEFT, 0);

        
        // Enable text wrapping
        lv_label_set_long_mode(objects.sd_data_logging, LV_LABEL_LONG_WRAP);
        
        // Set width to prevent overflow (adjust based on your screen width)
        lv_obj_set_width(objects.sd_data_logging, 460);  // Adjust this value for your screen (480px width - 20px margins)
        
        Serial.println("SD data log UI configured - aligned to top-left");
    } else {
        Serial.println("WARNING: sd_data_logging label is NULL");
    }
}


// ===== SD DATA LOG DISPLAY FUNCTION =====
void showSdLogOnSettings(lv_event_t *e) {
    resetActivityTimer();
    Serial.println("=== Data Log Button Clicked ===");
    
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        Serial.println("Not a click event, returning");
        return;
    }
    
    if (!objects.sd_data_logging) {
        Serial.println("ERROR: sd_data_logging (display label) is NULL!");
        return;
    }
    
    configureSdDataLogUI();

    if (!sdCardReady) {
        lv_label_set_text(objects.sd_data_logging, "Error: SD card not ready");
        Serial.println("SD card not ready");
        return;
    }
    
    Serial.println("Opening /jar_settings.txt...");
    File file = SD.open("/jar_settings.txt");
    if (!file) {
        lv_label_set_text(objects.sd_data_logging, "Error: jar_settings.txt not found");
        Serial.println("Failed to open jar_settings.txt");
        
        // List files to debug
        Serial.println("Available files on SD card:");
        listDir(SD, "/", 0);
        return;
    }
    
    Serial.printf("File opened, size: %d bytes\n", file.size());
    
    // Read all lines into a vector to count them
    std::vector<String> allLines;
    String line = "";
    
    while (file.available()) {
        char c = file.read();
        if (c == '\n') {
            if (line.length() > 0) {
                allLines.push_back(line);
                line = "";
            }
        } else if (c != '\r') {
            line += c;
        }
    }
    // Add last line if it doesn't end with newline
    if (line.length() > 0) {
        allLines.push_back(line);
    }
    
    file.close();
    
    int totalLines = allLines.size();
    Serial.printf("Total entries found: %d\n", totalLines);
    
    // Get the latest 20 entries (10 for left, 10 for right)
    static char bufferLeft[2048];
    static char bufferRight[2048];
    bufferLeft[0] = '\0';
    bufferRight[0] = '\0';
    
    int startIdx = (totalLines > 20) ? (totalLines - 20) : 0;
    int midIdx = startIdx + 10;  // First 10 go to left, next 10 to right
    
    // Fill left column with entries 1-10 (OLDER entries)
    for (int i = startIdx; i < midIdx && i < totalLines; i++) {
        strcat(bufferLeft, allLines[i].c_str());
        strcat(bufferLeft, "\n");
    }
    
    // Fill right column with entries 11-20 (NEWER entries)
    for (int i = midIdx; i < totalLines && i < startIdx + 20; i++) {
        strcat(bufferRight, allLines[i].c_str());
        strcat(bufferRight, "\n");
    }
    
    // Create a container for two columns if not exists
    lv_obj_t *parent = lv_obj_get_parent(objects.sd_data_logging);
    
    // Define top offset (adjust this value based on your header height)
    int topOffset = 80;  // Change this value to move entries up or down
    
    // LEFT COLUMN - OLDER ENTRIES (1-10)
    lv_label_set_text(objects.sd_data_logging, bufferLeft);
    lv_obj_set_style_text_font(objects.sd_data_logging, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(objects.sd_data_logging, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(objects.sd_data_logging, lv_pct(48));  // 48% width for left column
    lv_obj_align(objects.sd_data_logging, LV_ALIGN_TOP_LEFT, 5, topOffset);  // Added Y offset
    
    // RIGHT COLUMN - NEWER ENTRIES (11-20)
    static lv_obj_t *rightLabel = NULL;
    if (rightLabel == NULL) {
        rightLabel = lv_label_create(parent);
    }
    
    lv_label_set_text(rightLabel, bufferRight);
    lv_obj_set_style_text_font(rightLabel, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(rightLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(rightLabel, lv_pct(48));  // 48% width for right column
    lv_obj_align(rightLabel, LV_ALIGN_TOP_RIGHT, -5, topOffset);  // Added Y offset
    
    Serial.printf("Displaying last %d entries (10 left, 10 right)\n", totalLines - startIdx);
    Serial.println("Left column content (entries 1-10):");
    Serial.println(bufferLeft);
    Serial.println("Right column content (entries 11-20):");
    Serial.println(bufferRight);
    
    Serial.println("=== SD log displayed successfully ===");
}

void updateInfoPageDisplay() {
    if (currentJar != nullptr) {
        unsigned long remainingMillis = currentJar->getRemainingTime();
        int secs = remainingMillis / 1000;
        int min = secs / 60;
        int sec = secs % 60;
       
        char buf[32];
        sprintf(buf, "Time left: %02d:%02d", min, sec);
        lv_label_set_text(objects.readings_timeleft, buf);
    }
}

void select_jar_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        int jarIndex = (int)(intptr_t)lv_event_get_user_data(e);
        currentJarIndex = jarIndex;
        currentJar = jars[jarIndex];
        Serial.printf("Selected Jar %d\n", jarIndex + 1);
        updateUIForCurrentJar();
    }
}

void rpm_textarea_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_READY && currentJar != nullptr) {
        const char *txt = lv_textarea_get_text(objects.rpm_textarea);
        currentJar->setRPMValue(atoi(txt));
        currentJar->updateUIFields();
    }
}

void duration_textarea_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_READY && currentJar != nullptr) {
        const char *txt = lv_textarea_get_text(objects.duration_textarea);
        currentJar->setDurationValue(atoi(txt));
        currentJar->updateUIFields();
    }
}

void rpm_plus_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && currentJar != nullptr) {
        currentJar->incrementRPM(5);
    }
}

void rpm_minus_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && currentJar != nullptr) {
        currentJar->decrementRPM(5);
    }
}

void time_plus_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && currentJar != nullptr) {
        currentJar->incrementDuration(10);
    }
}

void time_minus_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && currentJar != nullptr) {
        currentJar->decrementDuration(10);
    }
}



void save_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && currentJar != nullptr) {
        Serial.printf("=== Jar %d Save Button Clicked ===\n", currentJarIndex + 1);
       
        if (sdCardReady) {
            bool success = currentJar->saveSettings("/jar_settings.txt");
            if (success) {
                Serial.printf("Jar %d: Settings saved successfully\n", currentJarIndex + 1);
            } else {
                Serial.printf("Jar %d: Failed to save settings\n", currentJarIndex + 1);
            }
        } else {
            Serial.println("ERROR: SD card not ready");
        }
    }
}

void start_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && currentJar != nullptr) {
        if (!currentJar->areSettingsSaved()) {
            Serial.printf("Jar %d: Please save settings before starting\n", currentJarIndex + 1);
            if (objects.jar_info_save) {
                lv_obj_set_style_bg_color(objects.jar_info_save, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
            }
            return;
        }
        currentJar->startMotor();
    }
}

void stop_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && currentJar != nullptr) {
        currentJar->stopMotor();
    }
}

void start_all_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        Serial.println("=== START ALL JARS ===");
       
        for (int i = 0; i < 8; i++) {
            if (jars[i]->areSettingsSaved() && jars[i]->getRPM() > 0 && jars[i]->getDuration() > 0) {
                jars[i]->startMotor();
                Serial.printf("Jar %d started\n", i + 1);
            } else {
                Serial.printf("Jar %d skipped (settings not saved or invalid)\n", i + 1);
            }
        }
    }
}

void stop_all_cb(lv_event_t *e) {
    resetActivityTimer();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        Serial.println("=== STOP ALL JARS ===");
       
        for (int i = 0; i < 8; i++) {
            if (jars[i]->isMotorRunning()) {
                jars[i]->stopMotor();
                Serial.printf("Jar %d stopped\n", i + 1);
            }
        }
    }
}


void timer_cb(lv_timer_t *timer) {
    // Let each jar update its countdown / motor state
    for (int i = 0; i < 8; i++) {
        jars[i]->onTimerTick();
    }

    // Check if any SELECTED jar is still running
    bool anySelectedRunning = false;
    for (int i = 0; i < 8; i++) {
        if (selectedJars[i] && jars[i]->isMotorRunning()) {
            anySelectedRunning = true;
            break;
        }
    }

    // If batch was running and now all selected jars stopped,
    //     this means countdown reached zero for all → AUTO COMPLETE
    if (ConfigurationRunning && !anySelectedRunning) {
        Serial.println("=== Configuration AUTO-COMPLETE: all selected jars finished ===");

        ConfigurationRunning = false;  // clear flag so we don't repeat

        //  Make Start button idle (blue, 'Start')
        if (objects.start_all_button) {
            lv_obj_set_style_bg_color(objects.start_all_button,
                                      lv_palette_main(LV_PALETTE_BLUE),
                                      LV_PART_MAIN);
            lv_obj_t *label = lv_obj_get_child(objects.start_all_button, 0);
            if (label) {
                lv_label_set_text(label, "Start");
            }
        }

        // Make Stop button 'Completed' (green) like in batch_stop_all_cb
        if (objects.stop_all_button) {
            lv_obj_set_style_bg_color(objects.stop_all_button,
                                      lv_palette_main(LV_PALETTE_GREEN),
                                      LV_PART_MAIN);
            lv_obj_t *label = lv_obj_get_child(objects.stop_all_button, 0);
            if (label) {
                lv_label_set_text(label, "Completed");
            }
        }

        // After 1s, reset STOP back to normal 'Stop' (blue)
        lv_timer_t *reset_timer = lv_timer_create([](lv_timer_t *t) {
            if (objects.stop_all_button) {
                lv_obj_set_style_bg_color(objects.stop_all_button,
                                          lv_palette_main(LV_PALETTE_BLUE),
                                          LV_PART_MAIN);
                lv_obj_t *label = lv_obj_get_child(objects.stop_all_button, 0);
                if (label) {
                    lv_label_set_text(label, "Stop");
                }
            }
            lv_timer_del(t);
        }, 1000, NULL);
    }

    updateInfoPageDisplay();
    checkScreensaver();
}



// ===== DISPLAY & TOUCH FUNCTIONS (FROM CODE 1) =====
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)color_p, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    uint16_t x, y;

    bool touched = tft.getTouch(&x, &y, 600);  // timeout 600 ms

    if (!touched) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    resetActivityTimer();  // Reset screensaver timer on touch

    if (x >= screenWidth || y >= screenHeight) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    data->point.x = x;
    data->point.y = y;
    data->state   = LV_INDEV_STATE_PR;
}



// System Reset Handler Function
void systemResetHandler(lv_event_t *e) {
    resetActivityTimer();
    
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    
    Serial.println("=== System Reset Clicked ===");
    
    // Create confirmation message box
    static const char *btns[] = {"Yes", "No", ""};
    lv_obj_t *mbox = lv_msgbox_create(NULL, "Confirm Reset", 
                                       "Reset all jar values to zero?", 
                                       btns, false);
    lv_obj_add_event_cb(mbox, confirmResetCallback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

// Individual Jar Reset Callback
void jar_info_reset_cb(lv_event_t *e) {
    resetActivityTimer();
    
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    
    if (currentJar == nullptr) {
        Serial.println("No jar selected for reset");
        return;
    }
    
    Serial.printf("=== Resetting Jar %d ===\n", currentJarIndex + 1);
    
    // Create confirmation message box
    char msg[64];
    sprintf(msg, "Reset Jar %d to zero?", currentJarIndex + 1);
    
    static const char *btns[] = {"Yes", "No", ""};
    lv_obj_t *mbox = lv_msgbox_create(NULL, "Confirm Reset", msg, btns, false);
    lv_obj_add_event_cb(mbox, confirmSingleJarResetCallback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

// Confirmation callback for single jar reset
void confirmSingleJarResetCallback(lv_event_t *e) {
    lv_obj_t *mbox = lv_event_get_current_target(e);
    uint16_t btn_id = lv_msgbox_get_active_btn(mbox);
    
    if (btn_id == 0 && currentJar != nullptr) {  // Yes button clicked
        Serial.printf("Resetting Jar %d to zero\n", currentJarIndex + 1);
        
        // Reset the current jar
        currentJar->setRPMValue(0);
        currentJar->setDurationValue(0);
        
        // Save to SD card
        if (sdCardReady) {
            currentJar->saveSettings("/jar_settings.txt");
            Serial.printf("Jar %d reset saved to SD\n", currentJarIndex + 1);
        }
        
        // Update UI fields
        currentJar->updateUIFields();
        
        // Update info page display
        if (objects.rpm_textarea) {
            lv_textarea_set_text(objects.rpm_textarea, "0");
        }
        if (objects.duration_textarea) {
            lv_textarea_set_text(objects.duration_textarea, "0");
        }
        
        // Visual feedback - change button color briefly
        if (objects.jar_info_reset) {
            lv_obj_set_style_bg_color(objects.jar_info_reset,
                                     lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
            
            // Reset button color after 1 second
            lv_timer_t *reset_timer = lv_timer_create([](lv_timer_t *timer) {
                if (objects.jar_info_reset) {
                    lv_obj_set_style_bg_color(objects.jar_info_reset,
                                             lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
                }
                lv_timer_del(timer);
            }, 1000, NULL);
        }
        
        Serial.printf("Jar %d reset completed\n", currentJarIndex + 1);
    }
    
    lv_msgbox_close(mbox);
}









// Confirmation callback
void confirmResetCallback(lv_event_t *e) {
    lv_obj_t *mbox = lv_event_get_current_target(e);
    uint16_t btn_id = lv_msgbox_get_active_btn(mbox);
    
    if (btn_id == 0) {  // Yes button clicked
        Serial.println("=== Resetting All Jars to Zero ===");
        
        // Reset all 8 jars
        for (int i = 0; i < 8; i++) {
            jars[i]->setRPMValue(0);
            jars[i]->setDurationValue(0);
            
            // Save to SD card
            if (sdCardReady) {
                jars[i]->saveSettings("/jar_settings.txt");
            }
            
            // Reset selectedJars array
            selectedJars[i] = false;
            
            Serial.printf("Jar %d reset to RPM=0, Duration=0\n", i + 1);
        }
        
        // Clear all checkboxes on the UI
        lv_obj_clear_state(objects.jar1_checkbox, LV_STATE_CHECKED);
        lv_obj_clear_state(objects.jar2_checkbox, LV_STATE_CHECKED);
        lv_obj_clear_state(objects.jar3_checkbox, LV_STATE_CHECKED);
        lv_obj_clear_state(objects.jar4_checkbox, LV_STATE_CHECKED);
        lv_obj_clear_state(objects.jar5_checkbox, LV_STATE_CHECKED);
        lv_obj_clear_state(objects.jar6_checkbox, LV_STATE_CHECKED);
        lv_obj_clear_state(objects.jar7_checkbox, LV_STATE_CHECKED);
        lv_obj_clear_state(objects.jar8_checkbox, LV_STATE_CHECKED);
        
        // Clear batch configuration values
        ConfigurationRPM = 0;
        ConfigurationDuration = 0;
        lv_textarea_set_text(objects.rpm_textarea, "0");
        lv_textarea_set_text(objects.duration_textarea, "0");
        
        // Update UI if current jar is selected
        if (currentJar != nullptr) {
            currentJar->updateUIFields();
        }
        
        Serial.println("System reset completed - All jars, checkboxes, and Configuration values cleared");
        
        // Optional: Show visual feedback on the button
        if (objects.obj24) {
            lv_label_set_text(objects.obj24, "Reset!");
        }
    }
    
    lv_msgbox_close(mbox);
}


// ===== SETUP =====
void setup() {
    Serial.begin(115200);  // Changed from 9600 to 115200 for faster serial
    delay(1000);
    Serial.println("=== 8-Jar Motor Controller Starting ===");

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    // Initialize Jar Controllers
    jars[0] = new JarController(1, JAR1_ENABLE_PIN, 0);
    jars[1] = new JarController(2, JAR2_ENABLE_PIN, 1);
    jars[2] = new JarController(3, JAR3_ENABLE_PIN, 2);
    jars[3] = new JarController(4, JAR4_ENABLE_PIN, 3);
    jars[4] = new JarController(5, JAR5_ENABLE_PIN, 4);
    jars[5] = new JarController(6, JAR6_ENABLE_PIN, 5);
    jars[6] = new JarController(7, JAR7_ENABLE_PIN, 6);
    jars[7] = new JarController(8, JAR8_ENABLE_PIN, 7);

    // Set error manager for all jars
    for (int i = 0; i < 8; i++) {
        jars[i]->setErrorManager(&errorManager);
        jars[i]->begin();
    }

    // ==================== Initialize SD Card (SAME AS CODE 1) ====================
    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);
    delay(100);
   
    spiSD.begin(sck, miso, mosi, cs);

    if (!SD.begin(cs, spiSD, 4000000)) {
        Serial.println("SD Card Mount Failed");
        sdCardReady = false;
    } else {
        Serial.println("SD Card initialized successfully");
        sdCardReady = true;

        uint8_t cardType = SD.cardType();
        Serial.printf("SD Card Type: %d\n", cardType);
        Serial.printf("SD Card Size: %lluMB\n",
                      (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
    }

    // Initialize LVGL
    lv_init();

    // Initialize TFT Display
    tft.begin();
    tft.setRotation(1);
    tft.setTouch(calData);  // Use code 1's calibration data

    lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Initialize Touch Screen
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // Initialize UI
    ui_init();



    // Create screensaver
    createScreensaver();
    lastActivityTime = millis();

    // ===== VERIFY AND BIND ERROR UI =====
Serial.println("=== Verifying UI Objects ===");
Serial.printf("errors_button: %p\n", objects.errors_button);
Serial.printf("errors_page: %p\n", objects.errors_page);
Serial.printf("data_button (data log button): %p\n", objects.data_button);
Serial.printf("sd_data_logging (data display label): %p\n", objects.sd_data_logging);


lv_obj_t *errorDisplayLabel = nullptr;

// Try to find a label child on the errors_page
uint32_t child_count = lv_obj_get_child_cnt(objects.errors_page);
Serial.printf("errors_page has %d children\n", child_count);

for (uint32_t i = 0; i < child_count; i++) {
    lv_obj_t *child = lv_obj_get_child(objects.errors_page, i);
    if (lv_obj_check_type(child, &lv_label_class)) {
        errorDisplayLabel = child;
        Serial.printf("Found label child at index %d: %p\n", i, child);
        break;
    }
}

if (errorDisplayLabel) {
    errorManager.bindErrorUI(errorDisplayLabel, nullptr);
    Serial.println("Error UI bound successfully to label");
} else {
    Serial.println("WARNING: No label found on errors_page, using errors_page itself");
    errorManager.bindErrorUI(objects.errors_page, nullptr);
}

// Load previous errors from SD card
if (sdCardReady) {
    errorManager.loadErrorsFromSD();
}

// ===== REGISTER ERROR BUTTON CALLBACKS =====
if (objects.errors_button) {
    lv_obj_add_event_cb(objects.errors_button, viewErrorsButton_cb, LV_EVENT_CLICKED, NULL);
    Serial.println("Errors button callback registered");
} else {
    Serial.println("WARNING: errors_button is NULL!");
}

// Start error blink timer
errorBlinkTimer = lv_timer_create(errorButtonBlink_cb, 500, NULL);
Serial.println("Error blink timer created");

// ===== REGISTER SD DATA LOG BUTTON =====
if (objects.data_button) {
    lv_obj_add_event_cb(objects.data_button, showSdLogOnSettings, LV_EVENT_CLICKED, NULL);

    Serial.println("Data log button callback registered");
} else {
    Serial.println("WARNING: data_button (data log button) is NULL!");

}

// Register jar info reset button callback
if (objects.jar_info_reset) {
    lv_obj_add_event_cb(objects.jar_info_reset, jar_info_reset_cb, LV_EVENT_CLICKED, NULL);
    Serial.println("Jar info reset button callback registered");
} else {
    Serial.println("WARNING: jar_info_reset button is NULL!");
}

// Register jar selection buttons
lv_obj_add_event_cb(objects.jar1_button, select_jar_cb, LV_EVENT_CLICKED, (void*)0);
lv_obj_add_event_cb(objects.jar2_button, select_jar_cb, LV_EVENT_CLICKED, (void*)1);
lv_obj_add_event_cb(objects.jar3_button, select_jar_cb, LV_EVENT_CLICKED, (void*)2);
lv_obj_add_event_cb(objects.jar4_button, select_jar_cb, LV_EVENT_CLICKED, (void*)3);
lv_obj_add_event_cb(objects.jar5_button, select_jar_cb, LV_EVENT_CLICKED, (void*)4);
lv_obj_add_event_cb(objects.jar6_button, select_jar_cb, LV_EVENT_CLICKED, (void*)5);
lv_obj_add_event_cb(objects.jar7_button, select_jar_cb, LV_EVENT_CLICKED, (void*)6);
lv_obj_add_event_cb(objects.jar8_button, select_jar_cb, LV_EVENT_CLICKED, (void*)7);

// Register checkbox callbacks
lv_obj_add_event_cb(objects.jar1_checkbox, jar1_checkbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
lv_obj_add_event_cb(objects.jar2_checkbox, jar2_checkbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
lv_obj_add_event_cb(objects.jar3_checkbox, jar3_checkbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
lv_obj_add_event_cb(objects.jar4_checkbox, jar4_checkbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
lv_obj_add_event_cb(objects.jar5_checkbox, jar5_checkbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
lv_obj_add_event_cb(objects.jar6_checkbox, jar6_checkbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
lv_obj_add_event_cb(objects.jar7_checkbox, jar7_checkbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
lv_obj_add_event_cb(objects.jar8_checkbox, jar8_checkbox_cb, LV_EVENT_VALUE_CHANGED, NULL);

// Batch configuration callbacks
lv_obj_add_event_cb(objects.rpm_textarea, Configuration_rpm_textarea_cb, LV_EVENT_READY, NULL);
lv_obj_add_event_cb(objects.rpm_increase_icon, Configuration_rpm_plus_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.rpm_decrease_icon, Configuration_rpm_minus_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.duration_textarea, Configuration_duration_textarea_cb, LV_EVENT_READY, NULL);
lv_obj_add_event_cb(objects.time_increase_icon, Configuration_duration_plus_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.time_decrease_icon, Configuration_duration_minus_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.all_jar_configurations_save_button, Configuration_save_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.start_all_button, Configuration_start_all_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.stop_all_button, Configuration_stop_all_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.system_reset_btn, systemResetHandler, LV_EVENT_CLICKED, NULL);

Serial.println("=== Configuration Configuration Callbacks Registered ===");

// Register START ALL and STOP ALL buttons
lv_obj_add_event_cb(objects.start_all_button_1, start_all_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.stop_all_button_1, stop_all_cb, LV_EVENT_CLICKED, NULL);

// Register Info Page event callbacks
lv_obj_add_event_cb(objects.info_page_rpm_increase_icon, rpm_plus_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.info_page_rpm_decrease_icon, rpm_minus_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.info_page_time_increase_icon, time_plus_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.info_page_time_decrease_icon, time_minus_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.jar_info_start_button, start_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.jar_info_stop, stop_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.jar_info_save, save_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.rpm_textarea, rpm_textarea_cb, LV_EVENT_READY, NULL);
lv_obj_add_event_cb(objects.duration_textarea, duration_textarea_cb, LV_EVENT_READY, NULL);
lv_obj_add_event_cb(objects.eror_clear_button, clearErrorsButton_cb, LV_EVENT_CLICKED, NULL);
lv_obj_add_event_cb(objects.duration_textarea, duration_textarea_cb, LV_EVENT_READY, NULL);



// Bind Info Page UI
for (int i = 0; i < 8; i++) {
    jars[i]->bindInfoPageUI(
        objects.rpm_textarea,
        objects.duration_textarea,
        objects.jar_info_save,
        objects.jar_info_start_button,
        objects.jar_info_stop
    );
}

// Bind Home Page UI
jars[0]->bindHomePageUI(objects.jar1_time_countdown, objects.jar1_state);
jars[1]->bindHomePageUI(objects.jar2_time_countdown, objects.jar2_state);
jars[2]->bindHomePageUI(objects.jar3_time_countdown, objects.jar3_state);
jars[3]->bindHomePageUI(objects.jar4_time_countdown, objects.jar4_state);
jars[4]->bindHomePageUI(objects.jar5_time_countdown, objects.jar5_state);
jars[5]->bindHomePageUI(objects.jar6_time_countdown, objects.jar6_state);
jars[6]->bindHomePageUI(objects.jar7_time_countdown, objects.jar7_state);
jars[7]->bindHomePageUI(objects.jar8_time_countdown, objects.jar8_state);

// Create main timer
lv_timer_create(timer_cb, 1000, NULL);

Serial.println("=== Setup Complete ===");
}

void loop() {
    lv_tick_inc(5);
    lv_timer_handler();
    ui_tick();
    delay(5);
}