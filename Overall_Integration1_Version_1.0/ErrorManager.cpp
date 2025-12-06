
#include "ErrorManager.h"
#include "FS.h"
#include "SD.h"


#include "RTCManager.h"
extern RTCManager rtcManager;


ErrorManager::ErrorManager() 
    : errorListLabel(nullptr), errorCountLabel(nullptr), errorsAcknowledged(true) {
    errorLog.reserve(MAX_ERRORS);
}

const char* ErrorManager::getErrorDescription(ErrorCode code) {
    switch(code) {
        case ERR_SD_INIT_FAILED: return "SD Card Init Failed";
        case ERR_SD_READ_FAILED: return "SD Read Error";
        case ERR_SD_WRITE_FAILED: return "SD Write Error";
        case ERR_MOTOR_OVERCURRENT: return "Motor Overcurrent";
        case ERR_MOTOR_STALLED: return "Motor Stalled";
        case ERR_INVALID_RPM: return "Invalid RPM Value";
        case ERR_INVALID_DURATION: return "Invalid Duration";
        case ERR_SETTINGS_NOT_SAVED: return "Settings Not Saved";
        case ERR_JAR_TIMEOUT: return "Operation Timeout";
        case ERR_TOUCH_SCREEN_FAILED: return "Touch Screen Failed";
        case ERR_DISPLAY_FAILED: return "Display Init Failed";
        case ERR_PWM_INIT_FAILED: return "PWM Setup Failed";
        case ERR_LOW_VOLTAGE: return "Low Voltage Detected";
        case ERR_HIGH_TEMPERATURE: return "High Temperature";
        case ERR_EMERGENCY_STOP: return "Emergency Stop";
        default: return "Unknown Error";
    }
}

/*void ErrorManager::logError(ErrorCode code, int jarID, const char* customMsg) {
    if (errorLog.size() >= MAX_ERRORS) {
        errorLog.erase(errorLog.begin());
    }
    
    char finalMsg[64];
    if (customMsg) {
        snprintf(finalMsg, sizeof(finalMsg), "%s", customMsg);
    } else {
        snprintf(finalMsg, sizeof(finalMsg), "%s", getErrorDescription(code));
    }
    
    ErrorInfo err(code, jarID, finalMsg);
    errorLog.push_back(err);
    
    // Reset acknowledgment when new error occurs
    errorsAcknowledged = false;
    
    // Log to serial
    Serial.printf("[ERROR %03d] Jar %d: %s\n", 
                  code, jarID, finalMsg);
                  
    
    // Update UI
    updateErrorDisplay();
    
    // Save to SD card
    saveErrorsToSD();
}*/ //changed

void ErrorManager::logError(ErrorCode code, int jarID, const char* customMsg) {
    if (errorLog.size() >= MAX_ERRORS) {
        errorLog.erase(errorLog.begin());
    }
    
    char finalMsg[64];
    if (customMsg) {
        snprintf(finalMsg, sizeof(finalMsg), "%s", customMsg);
    } else {
        snprintf(finalMsg, sizeof(finalMsg), "%s", getErrorDescription(code));
    }
    
    ErrorInfo err(code, jarID, finalMsg);
    
    // NEW: Add timestamp
    DateTime now = rtcManager.getCurrentTime();
    snprintf(err.timestamp, sizeof(err.timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());
    
    errorLog.push_back(err);
    
    errorsAcknowledged = false;
    
    // Log to serial with timestamp
    Serial.printf("[ERROR %03d] %s Jar %d: %s\n", 
                  code, err.timestamp, jarID, finalMsg);
    
    updateErrorDisplay();
    saveErrorsToSD();
}

void ErrorManager::clearErrors() {
    errorLog.clear();
    errorsAcknowledged = true;
    updateErrorDisplay();
    
    // Clear error log file
   // SD.remove(errorFilePath);
    Serial.println("All errors cleared");
}

ErrorInfo ErrorManager::getError(int index) const {
    if (index >= 0 && index < errorLog.size()) {
        return errorLog[index];
    }
    return ErrorInfo();
}

bool ErrorManager::hasJarErrors(int jarID) const {
    for (const auto& err : errorLog) {
        if (err.jarID == jarID) return true;
    }
    return false;
}

void ErrorManager::bindErrorUI(lv_obj_t *errorList, lv_obj_t *errorCount) {
    errorListLabel = errorList;
    errorCountLabel = errorCount;
    
    // Configure error list label alignment and style
    if (errorListLabel) {
        // Align to top-left with padding from home button area
        lv_obj_align(errorListLabel, LV_ALIGN_TOP_LEFT, 10, 80);  // 80px from top, 10px from left
        
        // Set text alignment to left
        lv_obj_set_style_text_align(errorListLabel, LV_TEXT_ALIGN_LEFT, 0);
        
        // Enable text wrapping
        lv_label_set_long_mode(errorListLabel, LV_LABEL_LONG_WRAP);
        
        // Set width to prevent overflow (adjust based on your screen width)
        lv_obj_set_width(errorListLabel, 460);  // Adjust this value for your screen
    }
    
    Serial.printf("Error UI bound - List: %p, Count: %p\n", errorList, errorCount);
}

/*void ErrorManager::updateErrorDisplay() {
    Serial.println("=== Updating Error Display ===");
    Serial.printf("Total errors: %d\n", errorLog.size());
    
    if (!errorListLabel) {
        Serial.println("ERROR: errorListLabel is NULL!");
        return;
    }
    
    // Update error count if label exists
    if (errorCountLabel) {
        char countBuf[32];
        snprintf(countBuf, sizeof(countBuf), "Total Errors: %d", errorLog.size());
        lv_label_set_text(errorCountLabel, countBuf);
        Serial.printf("Error count updated: %s\n", countBuf);
    }
    
    // Build error list string
    String errorText = "";
    
    if (errorLog.empty()) {
        errorText = "No errors logged";
        Serial.println("No errors to display");
    } else {
        // Show last 15 errors (most recent first)
        int startIdx = errorLog.size() > 15 ? errorLog.size() - 15 : 0;
        
        Serial.printf("Displaying %d errors (from index %d to %d)\n", 
                     errorLog.size() - startIdx, startIdx, errorLog.size() - 1);
        
        for (int i = errorLog.size() - 1; i >= startIdx; i--) {
            ErrorInfo err = errorLog[i];
            
            char line[150];
            
            if (err.jarID == 0) {
                snprintf(line, sizeof(line), "Err %03d: SYSTEM\n%s\n\n",
                        err.code, err.message);
            } else {
                snprintf(line, sizeof(line), "Err %03d: Jar %d\n%s\n\n",
                        err.code, err.jarID, err.message);
            }
            
            errorText += line;
            Serial.printf("Error %d: %s", errorLog.size() - i, line);
        }
    }
    
    // Update the label
    lv_label_set_text(errorListLabel, errorText.c_str());
    Serial.println("=== Error Display Updated ===");
}*/ //changed


void ErrorManager::updateErrorDisplay() {
    //Serial.println("=== Updating Error Display ===");
    //Serial.printf("Total errors: %d\n", errorLog.size());
    
    if (!errorListLabel) {
        Serial.println("ERROR: errorListLabel is NULL!");
        return;
    }
    
    if (errorCountLabel) {
        char countBuf[32];
       // snprintf(countBuf, sizeof(countBuf), "Total Errors: %d", errorLog.size());
        lv_label_set_text(errorCountLabel, countBuf);
    }
    
    String errorText = "";
    
    if (errorLog.empty()) {
        errorText = "No errors logged";
    } else {
        int startIdx = errorLog.size() > 15 ? errorLog.size() - 15 : 0;
        
        for (int i = errorLog.size() - 1; i >= startIdx; i--) {
            ErrorInfo err = errorLog[i];
            
            char line[200];  // Increased size for timestamp
            
            if (err.jarID == 0) {
                snprintf(line, sizeof(line), "%s\nErr %03d: SYSTEM\n%s\n\n",
                        err.timestamp, err.code, err.message);
            } else {
                snprintf(line, sizeof(line), "%s\nErr %03d: Jar %d\n%s\n\n",
                        err.timestamp, err.code, err.jarID, err.message);
            }
            
            errorText += line;
        }
    }
    
    lv_label_set_text(errorListLabel, errorText.c_str());
    //Serial.println("=== Error Display Updated ===");
}








/*bool ErrorManager::saveErrorsToSD() {
    File file = SD.open(errorFilePath, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open error log file for writing");
        return false;
    }
    
    file.println("=== ERROR LOG ===");
    file.printf("Total Errors: %d\n\n", errorLog.size());
    
    for (const auto& err : errorLog) {
        file.printf("Error Code: %03d\n", err.code);
        file.printf("Jar ID: %d\n", err.jarID == 0 ? 0 : err.jarID);
        file.printf("Message: %s\n", err.message);
        file.println("---");
    }
    
    file.close();
    return true;
}*/ //changed


bool ErrorManager::saveErrorsToSD() {
    File file = SD.open(errorFilePath, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open error log file for writing");
        return false;
    }
    
    file.println("=== ERROR LOG ===");
    file.printf("Total Errors: %d\n\n", errorLog.size());
    
    for (const auto& err : errorLog) {
        file.printf("Timestamp: %s\n", err.timestamp);
        file.printf("Error Code: %03d\n", err.code);
        file.printf("Jar ID: %d\n", err.jarID == 0 ? 0 : err.jarID);
        file.printf("Message: %s\n", err.message);
        file.println("---");
    }
    
    file.close();
    return true;
}


bool ErrorManager::loadErrorsFromSD() {
    File file = SD.open(errorFilePath);
    if (!file) {
        Serial.println("No error log file found");
        return false;
    }
    
    errorLog.clear();
    
    // Skip header lines
    file.readStringUntil('\n');
    file.readStringUntil('\n');
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        
        if (line.startsWith("Error Code:")) {
            int code = line.substring(12).toInt();
            
            line = file.readStringUntil('\n');
            int jarID = line.substring(8).toInt();
            
            line = file.readStringUntil('\n');
            String msg = line.substring(9);
            
            file.readStringUntil('\n'); // Skip separator
            
            ErrorInfo err((ErrorCode)code, jarID, msg.c_str());
            errorLog.push_back(err);
        }
    }
    
    file.close();
    updateErrorDisplay();
    
    Serial.printf("Loaded %d errors from SD card\n", errorLog.size());
    return true;
}

String ErrorManager::getErrorLogAsString() {
    String log = "=== ERROR LOG ===\n";
    log += "Total Errors: " + String(errorLog.size()) + "\n\n";
    
    for (const auto& err : errorLog) {
        log += "Error " + String(err.code) + ": ";
        log += err.jarID == 0 ? "SYSTEM\n" : "Jar " + String(err.jarID) + "\n";
        log += String(err.message) + "\n\n";
    }
    
    return log;
}












