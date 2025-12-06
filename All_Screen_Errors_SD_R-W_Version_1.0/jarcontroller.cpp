#include "JarController.h"
#include "FS.h"
#include "SD.h"

const int PWM_FREQ = 30000;
const int PWM_RESOLUTION = 8;

// ===== ADD THIS HELPER FUNCTION AT THE TOP =====
extern bool sdCardReady;  // Declared in main .ino file

bool acquireSDCardForJar() {
    if (!sdCardReady) {
        Serial.println("SD card not initialized / not ready");
        return false;
    }
    return true;
}

JarController::JarController(int id, int enablePin, int channel)
    : jarID(id), motorEnablePin(enablePin), 
      pwmChannel(channel), setRPM(0), setDuration(0),
      motorOn(false), settingsSaved(false), motorStartMillis(0),
      lastMotorCheckMillis(0), consecutiveFailures(0), errorManager(nullptr),
      ui_rpm_textarea(nullptr), ui_duration_textarea(nullptr),
      ui_save_button(nullptr), ui_start_button(nullptr), ui_stop_button(nullptr),
      ui_home_countdown_label(nullptr), 
      ui_home_state_label(nullptr), ui_rpm_reading_label(nullptr), 
      ui_timeleft_label(nullptr) {
}

void JarController::begin() {
    
    
    ledcAttach(motorEnablePin, PWM_FREQ, PWM_RESOLUTION);
    ledcWrite(pwmChannel, 0);
    
    delay(10);
    
    // Verify PWM initialization
    if (ledcRead(pwmChannel) != 0) {
        if (errorManager) {
            errorManager->logError(ERR_PWM_INIT_FAILED, jarID, "PWM channel failed to initialize");
        }
        Serial.printf("Jar %d: PWM initialization error\n", jarID);
    } else {
        Serial.printf("Jar %d: Initialized successfully (Pin: %d, Channel: %d)\n", 
                     jarID, motorEnablePin, pwmChannel);
    }
}

bool JarController::validateSettings() {
    if (!ErrorManager::isValidRPM(setRPM)) {
        if (errorManager) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Invalid RPM: %d (Range: 0-100)", setRPM);
            errorManager->logError(ERR_INVALID_RPM, jarID, msg);
        }
        return false;
    }
    
    if (!ErrorManager::isValidDuration(setDuration)) {
        if (errorManager) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Invalid Duration: %d (Range: 0-3600)", setDuration);
            errorManager->logError(ERR_INVALID_DURATION, jarID, msg);
        }
        return false;
    }
    
    return true;
}

bool JarController::checkMotorHealth() {
    unsigned long elapsed = getElapsedTime();
    unsigned long expectedDuration = (unsigned long)setDuration * 1000UL;
    
    // Check if motor has been running longer than expected (with 5 second tolerance)
    if (motorOn && elapsed > expectedDuration + 5000) {
        if (errorManager) {
            errorManager->logError(ERR_JAR_TIMEOUT, jarID, "Motor operation timeout");
        }
        return false;
    }
    
    return true;
}

void JarController::applyMotorSettings() {
    // Calculate duty cycle from RPM (0-100 maps to 0-255)
    int dutyCycle = map(setRPM, 0, 100, 0, 255);
    
    // Set PWM for speed control
    ledcWrite(pwmChannel, dutyCycle);
}

bool JarController::startMotor() {
    Serial.printf("Jar %d: START requested\n", jarID);
    
    // Validate settings first
    if (!validateSettings()) {
        Serial.printf("Jar %d: Settings validation FAILED\n", jarID);
        if (errorManager) {
            errorManager->logError(ERR_INVALID_RPM, jarID, "Settings validation failed");
        }
        return false;
    }
    
    // Check if RPM or Duration is zero
    if (setRPM <= 0 || setDuration <= 0) {
        Serial.printf("Jar %d: Invalid parameters - RPM=%d, Duration=%d\n", jarID, setRPM, setDuration);
        if (errorManager) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Invalid parameters: RPM=%d, Duration=%d", setRPM, setDuration);
            errorManager->logError(ERR_INVALID_RPM, jarID, msg);
        }
        return false;
    }
    
    // Check if settings are saved
    if (!settingsSaved) {
        Serial.printf("Jar %d: Settings NOT SAVED - cannot start\n", jarID);
        if (errorManager) {
            errorManager->logError(ERR_SETTINGS_NOT_SAVED, jarID, "Settings must be saved before starting");
        }
        if (ui_save_button) {
            lv_obj_set_style_bg_color(ui_save_button, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
        }
        return false;
    }
    
    // If already running, don't restart
    if (motorOn) {
        Serial.printf("Jar %d: Already running\n", jarID);
        return true;
    }
    
    // Start the motor
    motorOn = true;
    motorStartMillis = millis();
    lastMotorCheckMillis = millis();
    consecutiveFailures = 0;
    
    // Apply motor settings (PWM and direction)
    applyMotorSettings();
    
    // Update UI
    updateButtonStates(true);
    updateHomePageUI();
    
    Serial.printf("Jar %d: Motor STARTED - RPM: %d, Duration: %d sec\n", 
                  jarID, setRPM, setDuration);
    
    return true;
}

void JarController::stopMotor(bool isError) {
    if (!motorOn) {
        Serial.printf("Jar %d: Motor already stopped\n", jarID);
        return; // Already stopped
    }
    
    // Stop motor immediately
    motorOn = false;
    motorStartMillis = 0;
    ledcWrite(pwmChannel, 0);

    
    updateButtonStates(false);
    updateHomePageUI();
    
    Serial.printf("Jar %d: Motor STOPPED\n", jarID);

    if (isError && errorManager) {
        errorManager->logError(ERR_MOTOR_STALLED, jarID, "Motor stopped due to error");
    }
}

void JarController::updateMotor() {
    if (!motorOn) return;

    // If duration is zero, motor runs until manually stopped
    if (setDuration == 0) {
        updateHomePageUI();
        return;
    }

    // For non-zero duration, check if time expired
    unsigned long elapsed = getElapsedTime();
    unsigned long totalDuration = (unsigned long)setDuration * 1000UL;
    
    if (elapsed >= totalDuration) {
        Serial.printf("Jar %d: Timer EXPIRED - Stopping motor\n", jarID);
        stopMotor(false);
        return;
    }

    // Health check every 5 seconds
    if (millis() - lastMotorCheckMillis >= 5000) {
        if (!checkMotorHealth()) {
            consecutiveFailures++;
            Serial.printf("Jar %d: Health check FAILED (Failures: %d)\n", jarID, consecutiveFailures);
            if (consecutiveFailures >= 3) {
                stopMotor(true);
                return;
            }
        } else {
            consecutiveFailures = 0;
        }
        lastMotorCheckMillis = millis();
    }

    updateHomePageUI();
}

void JarController::setRPMValue(int rpm) {
    if (!ErrorManager::isValidRPM(rpm)) {
        if (errorManager) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Attempted invalid RPM: %d", rpm);
            errorManager->logError(ERR_INVALID_RPM, jarID, msg);
        }
        return;
    }
    
    setRPM = constrain(rpm, 0, 100);
    markUnsaved();
    
    // If motor is running, apply new RPM immediately
    if (motorOn) {
        int dutyCycle = map(setRPM, 0, 100, 0, 255);
        ledcWrite(pwmChannel, dutyCycle);
        Serial.printf("Jar %d: RPM changed to %d while running\n", jarID, setRPM);
    }
}

void JarController::setDurationValue(int duration) {
    if (!ErrorManager::isValidDuration(duration)) {
        if (errorManager) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Attempted invalid duration: %d", duration);
            errorManager->logError(ERR_INVALID_DURATION, jarID, msg);
        }
        return;
    }
    
    setDuration = constrain(duration, 0, 3600);
    markUnsaved();
}



void JarController::incrementRPM(int step) {
    setRPMValue(setRPM + step);
    updateUIFields();
}

void JarController::decrementRPM(int step) {
    setRPMValue(setRPM - step);
    
    // If RPM becomes zero while running, stop motor
    if (motorOn && setRPM == 0) {
        Serial.printf("Jar %d: RPM set to 0, stopping motor\n", jarID);
        stopMotor(false);
    }
    
    updateUIFields();
}

void JarController::incrementDuration(int step) {
    setDurationValue(setDuration + step);
    updateUIFields();
}

void JarController::decrementDuration(int step) {
    setDurationValue(setDuration - step);
    updateUIFields();
}

// ===== FIXED saveSettings() FUNCTION =====
bool JarController::saveSettings(const char *filepath) {
    Serial.printf("Jar %d: Saving settings...\n", jarID);
    
    // Check SD card availability FIRST
    if (!acquireSDCardForJar()) {
        Serial.printf("Jar %d: SD card not available\n", jarID);
        if (errorManager) {
            errorManager->logError(ERR_SD_WRITE_FAILED, jarID, "SD card not ready");
        }
        return false;
    }
    
    // Validate settings
    if (!validateSettings()) {
        Serial.printf("Jar %d: Settings validation FAILED\n", jarID);
        return false;
    }
    
    // Prepare data string
    char data[128];
    sprintf(data, "Jar%d,RPM:%d,Duration:%d\n", 
            jarID, setRPM, setDuration);
    
    Serial.printf("Jar %d: Attempting to write: %s", jarID, data);
    
    // Try to open file for appending
    File file = SD.open(filepath, FILE_APPEND);
    if (!file) {
        Serial.printf("Jar %d: Failed to open file, attempting to create...\n", jarID);
        
        // Try to create the file first
        File newFile = SD.open(filepath, FILE_WRITE);
        if (newFile) {
            Serial.printf("Jar %d: File created successfully\n", jarID);
            newFile.close();
            delay(50);  // Small delay for file system
            
            // Try opening again
            file = SD.open(filepath, FILE_APPEND);
            if (!file) {
                Serial.printf("Jar %d: Still cannot open file after creation\n", jarID);
                if (errorManager) {
                    errorManager->logError(ERR_SD_WRITE_FAILED, jarID, "Cannot open file");
                }
                return false;
            }
        } else {
            Serial.printf("Jar %d: Cannot create file\n", jarID);
            if (errorManager) {
                errorManager->logError(ERR_SD_WRITE_FAILED, jarID, "Cannot create file");
            }
            return false;
        }
    }
    
    // Write data
    size_t written = file.print(data);
    Serial.printf("Jar %d: Bytes written: %d\n", jarID, (int)written);
    
    if (written > 0) {
        file.flush();  // Ensure data is written to SD card
        file.close();
        
        settingsSaved = true;
        Serial.printf("Jar %d: Settings SAVED successfully\n", jarID);
        
        if (ui_save_button) {
            lv_obj_set_style_bg_color(ui_save_button, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
        }
        return true;
    } else {
        file.close();
        Serial.printf("Jar %d: Failed to write to file (0 bytes)\n", jarID);
        if (errorManager) {
            errorManager->logError(ERR_SD_WRITE_FAILED, jarID, "Zero bytes written");
        }
        return false;
    }
}

void JarController::markUnsaved() {
    settingsSaved = false;
    if (ui_save_button) {
        lv_obj_set_style_bg_color(ui_save_button, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    }
}

void JarController::bindInfoPageUI(lv_obj_t *rpm_ta, lv_obj_t *dur_ta, lv_obj_t *save_btn, lv_obj_t *start_btn, lv_obj_t *stop_btn) {
    ui_rpm_textarea = rpm_ta;
    ui_duration_textarea = dur_ta;
    ui_save_button = save_btn;
    ui_start_button = start_btn;
    ui_stop_button = stop_btn;
}

void JarController::bindHomePageUI(lv_obj_t *home_countdown, lv_obj_t *home_state) {
    ui_home_countdown_label = home_countdown;
    ui_home_state_label = home_state;
}

unsigned long JarController::getElapsedTime() const {
    if (!motorOn || motorStartMillis == 0) return 0;
    return millis() - motorStartMillis;
}

unsigned long JarController::getRemainingTime() const {
    if (!motorOn || setDuration == 0) return 0;
    
    unsigned long elapsed = getElapsedTime();
    unsigned long totalMillis = (unsigned long)setDuration * 1000UL;
    
    if (elapsed >= totalMillis) return 0;
    return totalMillis - elapsed;
}

void JarController::updateUIFields() {
    if (ui_rpm_textarea) {
        char buf[8];
        sprintf(buf, "%d", setRPM);
        lv_textarea_set_text(ui_rpm_textarea, buf);
    }
    
    if (ui_duration_textarea) {
        char buf[8];
        sprintf(buf, "%d", setDuration);
        lv_textarea_set_text(ui_duration_textarea, buf);
    }
}
    
void JarController::updateHomePageUI() {
    unsigned long remainingMillis = getRemainingTime();
    
    int secs = remainingMillis / 1000;
    int min = secs / 60;
    int sec = secs % 60;
    
    if (ui_home_countdown_label) {
        char buf[16];
        sprintf(buf, "%02d:%02d", min, sec);
        lv_label_set_text(ui_home_countdown_label, buf);
    }
    
    if (ui_home_state_label) {
        if (motorOn) {
            lv_label_set_text(ui_home_state_label, hasError() ? "ERROR" : "ON");
            lv_obj_set_style_text_color(ui_home_state_label, 
                hasError() ? lv_palette_main(LV_PALETTE_RED) : lv_palette_main(LV_PALETTE_GREEN), 
                LV_PART_MAIN);
        } else {
            lv_label_set_text(ui_home_state_label, "OFF");
            lv_obj_set_style_text_color(ui_home_state_label, 
                lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
        }
    }
}

void JarController::updateButtonStates(bool running) {
    if (running) {
        if (ui_start_button) {
            lv_obj_t *label = lv_obj_get_child(ui_start_button, 0);
            if (label) lv_label_set_text(label, "Running");
            lv_obj_set_style_bg_color(ui_start_button, 
                lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
        }
        if (ui_stop_button) {
            lv_obj_t *label = lv_obj_get_child(ui_stop_button, 0);
            if (label) lv_label_set_text(label, "Stop");
            lv_obj_set_style_bg_color(ui_stop_button, 
                lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
        }
    } else {
        if (ui_start_button) {
            lv_obj_t *label = lv_obj_get_child(ui_start_button, 0);
            if (label) lv_label_set_text(label, "Start");
            lv_obj_set_style_bg_color(ui_start_button, 
                lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
        }
        if (ui_stop_button) {
            lv_obj_t *label = lv_obj_get_child(ui_stop_button, 0);
            if (label) lv_label_set_text(label, "Completed");
            lv_obj_set_style_bg_color(ui_stop_button, 
                lv_palette_main(LV_PALETTE_YELLOW), LV_PART_MAIN);
        }
    }
}

void JarController::onTimerTick() {
    updateMotor();
    updateHomePageUI();
}

