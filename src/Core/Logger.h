#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <mutex>
#include <ctime>
#include <iomanip>

// Simple macro for logging
#define LOG_INFO(...) Logger::Log(Logger::Level::Info, __VA_ARGS__)
#define LOG_WARNING(...) Logger::Log(Logger::Level::Warning, __VA_ARGS__)
#define LOG_ERROR(...) Logger::Log(Logger::Level::Error, __VA_ARGS__)

class Logger {
public:
    enum class Level { Info, Warning, Error };

    static void Init() {
        AllocConsole();
        FILE* f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        
        m_logFile.open("WideCapture.log", std::ios::out | std::ios::trunc);
    }

    static void Shutdown() {
        if (m_logFile.is_open()) {
            m_logFile.close();
        }
        FreeConsole();
    }

    template<typename... Args>
    static void Log(Level level, Args&&... args) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        std::stringstream ss;
        (ss << ... << args);
        
        std::string prefix;
        switch (level) {
            case Level::Info: prefix = "[INFO] "; break;
            case Level::Warning: prefix = "[WARN] "; break;
            case Level::Error: prefix = "[ERR] "; break;
        }

        std::time_t t = std::time(nullptr);
        std::tm tm;
        localtime_s(&tm, &t);

        std::stringstream timestamp;
        timestamp << std::put_time(&tm, "%H:%M:%S");

        std::string msg = "[" + timestamp.str() + "] " + prefix + ss.str();

        // Console
        std::cout << msg << std::endl;

        // File
        if (m_logFile.is_open()) {
            m_logFile << msg << std::endl;
            m_logFile.flush();
        }
    }

private:
    static inline std::mutex m_mutex;
    static inline std::ofstream m_logFile;
};
