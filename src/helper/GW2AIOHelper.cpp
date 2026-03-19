// GW2AIOHelper.cpp - Helper DLL v9 (GPU + Exit Detection)
// Uses GPU usage detection to know when game is rendering (not splash)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#include <thread>
#include <atomic>
#include <string>
#include <fstream>

#pragma comment(lib, "pdh.lib")

class Logger {
public:
    static void Log(const std::wstring& msg) {
        wchar_t path[MAX_PATH];
        GetTempPathW(MAX_PATH, path);
        std::wstring logPath = std::wstring(path) + L"GW2AIOHelper.log";
        
        std::wofstream file(logPath, std::ios::app);
        if (file.is_open()) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t time[64];
            swprintf_s(time, L"[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            file << time << msg << std::endl;
            file.close();
        }
    }
};

HMODULE g_hModule = nullptr;
DWORD g_dwPid = 0;
std::atomic<bool> g_bRunning{true};

// Signal launcher with message (LOADED or EXITING)
bool SignalLauncher(DWORD pid, const char* message, int maxRetries = 150, int retryDelayMs = 100) {
    wchar_t pipeName[256];
    swprintf_s(pipeName, L"\\\\.\\pipe\\GW2AIO_%lu", pid);
    
    Logger::Log(std::wstring(L"Connecting to pipe: ") + pipeName + L" (msg: " + 
                std::wstring(message, message + strlen(message)) + L")");
    
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    for (int i = 0; i < maxRetries && hPipe == INVALID_HANDLE_VALUE; i++) {
        hPipe = CreateFileW(pipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hPipe == INVALID_HANDLE_VALUE) Sleep(retryDelayMs);
    }
    
    if (hPipe == INVALID_HANDLE_VALUE) {
        Logger::Log(L"Failed to connect to pipe, error: " + std::to_wstring(GetLastError()));
        return false;
    }
    
    DWORD written;
    WriteFile(hPipe, message, (DWORD)strlen(message), &written, nullptr);
    Logger::Log(std::wstring(L"Sent signal: ") + std::wstring(message, message + strlen(message)));
    CloseHandle(hPipe);
    return true;
}

// Quick signal attempt for DLL_PROCESS_DETACH - very short timeout
// This MUST be fast and safe - game stability is #1 priority
bool QuickSignalExit(DWORD pid) {
    __try {
        // Only 1 retry, 100ms timeout - if AIO is gone, we give up immediately
        return SignalLauncher(pid, "EXITING", 1, 100);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // Swallow ANY exception - never crash the game
        return false;
    }
}

// Get GPU usage using Performance Data Helper
double GetGPUUsage(DWORD pid) {
    // Try to get GPU engine usage for this specific process
    PDH_HQUERY hQuery = nullptr;
    PDH_HCOUNTER hCounter = nullptr;
    
    if (PdhOpenQuery(nullptr, 0, &hQuery) != ERROR_SUCCESS) {
        return -1;
    }
    
    // Build counter path for GPU Engine - 3D usage
    // Format: \GPU Engine(pid_XXXX_*engtype_3D)\Utilization Percentage
    wchar_t counterPath[512];
    swprintf_s(counterPath, L"\\GPU Engine(pid_%lu_*engtype_3D)\\Utilization Percentage", pid);
    
    PDH_STATUS status = PdhAddCounterW(hQuery, counterPath, 0, &hCounter);
    if (status != ERROR_SUCCESS) {
        // Try alternative path without engtype filter
        swprintf_s(counterPath, L"\\GPU Engine(pid_%lu_*)\\Utilization Percentage", pid);
        status = PdhAddCounterW(hQuery, counterPath, 0, &hCounter);
    }
    
    if (status != ERROR_SUCCESS) {
        PdhCloseQuery(hQuery);
        return -1;
    }
    
    // First collect to initialize
    PdhCollectQueryData(hQuery);
    Sleep(100);
    
    // Second collect to get actual value
    if (PdhCollectQueryData(hQuery) != ERROR_SUCCESS) {
        PdhCloseQuery(hQuery);
        return -1;
    }
    
    PDH_FMT_COUNTERVALUE value;
    if (PdhGetFormattedCounterValue(hCounter, PDH_FMT_DOUBLE, nullptr, &value) != ERROR_SUCCESS) {
        PdhCloseQuery(hQuery);
        return -1;
    }
    
    PdhCloseQuery(hQuery);
    return value.doubleValue;
}

void MonitorThread() {
    DWORD pid = GetCurrentProcessId();
    Logger::Log(L"MonitorThread started, PID: " + std::to_wstring(pid));
    Logger::Log(L"Using GPU USAGE DETECTION (triggers when GPU > 5%)");
    
    bool signalSent = false;
    int highGpuCount = 0;
    
    while (g_bRunning && !signalSent) {
        double gpuUsage = GetGPUUsage(pid);
        
        if (gpuUsage > 5.0) {
            highGpuCount++;
            Logger::Log(L"GPU usage: " + std::to_wstring(gpuUsage) + L"% (high count: " + std::to_wstring(highGpuCount) + L")");
            
            // Require 3 consecutive high readings to avoid false triggers
            if (highGpuCount >= 3) {
                Logger::Log(L"*** GPU ACTIVE! Game is rendering! ***");
                if (SignalLauncher(pid, "LOADED")) {
                    Logger::Log(L"SUCCESS! Window positioning triggered.");
                    signalSent = true;
                }
            }
        } else {
            // Reset counter if GPU drops
            if (highGpuCount > 0) {
                highGpuCount = 0;
            }
        }
        
        Sleep(1000);  // Check every second
    }
    
    Logger::Log(L"MonitorThread stopped");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        g_dwPid = GetCurrentProcessId();  // Store for exit signal
        DisableThreadLibraryCalls(hModule);
        Logger::Log(L"=== GW2AIOHelper DLL v9 (GPU + Exit Detection) ===");
        std::thread(MonitorThread).detach();
    } else if (reason == DLL_PROCESS_DETACH) {
        g_bRunning = false;
        Logger::Log(L"DLL_PROCESS_DETACH - Game is closing");
        
        // Send EXITING signal - quick and safe (max 100ms)
        // If AIO is closed, this silently fails - no crash
        QuickSignalExit(g_dwPid);
        
        Logger::Log(L"Exit signal sent (or AIO not available)");
    }
    return TRUE;
}
