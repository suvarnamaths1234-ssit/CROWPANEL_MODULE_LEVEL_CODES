
#ifndef ERROR_MANAGER_H
#define ERROR_MANAGER_H

#include <Arduino.h>
#include <lvgl.h>
#include <vector>

// Error Codes Definition
enum ErrorCode {
    ERR_NONE = 0,
    ERR_SD_INIT_FAILED = 1,
    ERR_SD_READ_FAILED = 2,
    ERR_SD_WRITE_FAILED = 3,
    ERR_MOTOR_OVERCURRENT = 4,
    ERR_MOTOR_STALLED = 5,
    ERR_INVALID_RPM = 6,
    ERR_INVALID_DURATION = 7,
    ERR_SETTINGS_NOT_SAVED = 8,
    ERR_JAR_TIMEOUT = 9,
    ERR_TOUCH_SCREEN_FAILED = 10,
    ERR_DISPLAY_FAILED = 11,
    ERR_PWM_INIT_FAILED = 12,
    ERR_LOW_VOLTAGE = 13,
    ERR_HIGH_TEMPERATURE = 14,
    ERR_EMERGENCY_STOP = 15
};

/* Error Structure
struct ErrorInfo {
    ErrorCode code;
    int jarID;
    char message[64];
    
    ErrorInfo() : code(ERR_NONE), jarID(0) {
        message[0] = '\0';
    }
    
    ErrorInfo(ErrorCode c, int jar, const char* msg) 
        : code(c), jarID(jar) {
        strncpy(message, msg, sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
    }
};*/


struct ErrorInfo {
    ErrorCode code;
    int jarID;
    char message[64];
    char timestamp[20];  // NEW: Add timestamp field
    
    ErrorInfo() : code(ERR_SD_INIT_FAILED), jarID(0) {
        message[0] = '\0';
        timestamp[0] = '\0';  // NEW
    }
    
    ErrorInfo(ErrorCode c, int id, const char* msg) 
        : code(c), jarID(id) {
        strncpy(message, msg, sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
        timestamp[0] = '\0';  // NEW
    }
};// changed




class ErrorManager {
private:
    static const int MAX_ERRORS = 50;
    std::vector<ErrorInfo> errorLog;
    
    // UI Objects
    lv_obj_t *errorListLabel;
    lv_obj_t *errorCountLabel;
    
    // Error file path
    const char *errorFilePath = "/error_log.txt";
    
    // Acknowledgment tracking
    bool errorsAcknowledged;
    
    // Get error description
    const char* getErrorDescription(ErrorCode code);
    
public:
    ErrorManager();
    
    // Log errors
    void logError(ErrorCode code, int jarID, const char* customMsg = nullptr);
    void clearErrors();
    
    // Error retrieval
    int getErrorCount() const { return errorLog.size(); }
    ErrorInfo getError(int index) const;
    bool hasErrors() const { return !errorLog.empty(); }
    bool hasJarErrors(int jarID) const;
    
    // Acknowledgment management
    bool areErrorsAcknowledged() const { return errorsAcknowledged; }
    void acknowledgeErrors() { errorsAcknowledged = true; }
    void resetAcknowledgment() { errorsAcknowledged = false; }
    
    // UI binding
    void bindErrorUI(lv_obj_t *errorList, lv_obj_t *errorCount);
    void updateErrorDisplay();
    
    // File operations
    bool saveErrorsToSD();
    bool loadErrorsFromSD();
    String getErrorLogAsString();
    
    // Check specific error conditions
    static bool isValidRPM(int rpm) { return rpm >= 0 && rpm <= 100; }
    static bool isValidDuration(int duration) { return duration >= 0 && duration <= 3600; }
};

#endif










