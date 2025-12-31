#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <vector>
#include <string>
#include <atomic>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

class ProcessManager {
private:
    std::vector<std::wstring> targetProcesses = { L"TiWorker.exe", L"TrustedInstaller.exe" };
    std::vector<std::wstring> targetServices = { L"wuauserv", L"bits", L"UsoSvc" };
    
    std::atomic<bool> shouldStop{false};
    std::atomic<bool> isPaused{false};
    
    enum Hotkeys { PAUSE_KEY = 1 };
    
    int totalKilled = 0;
    int totalStopped = 0;
    int checksCount = 0;
    
    bool hasAdminRights() {
        HANDLE token;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            TOKEN_ELEVATION elevation;
            DWORD size;
            if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
                CloseHandle(token);
                return elevation.TokenIsElevated;
            }
            CloseHandle(token);
        }
        return false;
    }
    
    bool registerHotkeys() {
        if (!RegisterHotKey(NULL, PAUSE_KEY, MOD_CONTROL | MOD_SHIFT, 'P')) {
            return false;
        }
        return true;
    }
    
    void unregisterHotkeys() {
        UnregisterHotKey(NULL, PAUSE_KEY);
    }
    
    bool checkStopHotkey() {
        bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        bool wPressed = (GetAsyncKeyState('W') & 0x8000) != 0;
        bool mPressed = (GetAsyncKeyState('M') & 0x8000) != 0;
        
        return ctrlPressed && altPressed && wPressed && mPressed;
    }
    
    void checkHotkeys() {
        MSG msg;
        while (PeekMessage(&msg, NULL, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
            if (msg.wParam == PAUSE_KEY) {
                isPaused.store(!isPaused.load()); // Используем store/load для atomic
            }
        }
        
        static bool stopKeyWasPressed = false;
        if (checkStopHotkey()) {
            if (!stopKeyWasPressed) {
                shouldStop.store(true);
                stopKeyWasPressed = true;
            }
        } else {
            stopKeyWasPressed = false;
        }
    }
    
    bool killProcess(const std::wstring& name) {
        bool killed = false;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(PROCESSENTRY32W);
            
            if (Process32FirstW(snapshot, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, name.c_str()) == 0) {
                        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                        if (hProcess) {
                            if (TerminateProcess(hProcess, 0)) {
                                killed = true;
                                totalKilled++;
                            }
                            CloseHandle(hProcess);
                        }
                    }
                } while (Process32NextW(snapshot, &pe));
            }
            CloseHandle(snapshot);
        }
        return killed;
    }
    
    bool stopService(const std::wstring& name) {
        SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
        if (!scm) return false;
        
        SC_HANDLE service = OpenServiceW(scm, name.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (!service) {
            CloseServiceHandle(scm);
            return false;
        }
        
        SERVICE_STATUS status;
        bool stopped = false;
        
        if (QueryServiceStatus(service, &status)) {
            if (status.dwCurrentState == SERVICE_RUNNING) {
                if (ControlService(service, SERVICE_CONTROL_STOP, &status)) {
                    stopped = true;
                    totalStopped++;
                }
            }
        }
        
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return stopped;
    }

public:
    void run() {
        if (!hasAdminRights()) {
            std::cout << "========================================\n";
            std::cout << "   Windows Modules Installer Killer\n";
            std::cout << "========================================\n\n";
            std::cout << "ERROR: Administrator rights required!\n\n";
            std::cout << "Please run the program as administrator.\n";
            system("pause");
            return;
        }
        
        if (!registerHotkeys()) {
            // Silent fail
        }
        
        // Initial screen
        system("cls");
        std::cout << "========================================\n";
        std::cout << "   Windows Modules Installer Killer\n";
        std::cout << "========================================\n\n";
        
        std::cout << "Program started with administrator rights\n\n";
        
        std::cout << "Monitoring processes:\n";
        std::cout << "  * TiWorker.exe\n";
        std::cout << "  * TrustedInstaller.exe\n";
        
        std::cout << "\nMonitoring services:\n";
        std::cout << "  * wuauserv\n";
        std::cout << "  * bits\n";
        std::cout << "  * UsoSvc\n";
        
        std::cout << "\nControls:\n";
        std::cout << "* Ctrl+Alt+W+M - Exit\n";
        std::cout << "* Ctrl+Shift+P - Pause/Resume\n";
        std::cout << "========================================\n\n";
        
        std::cout << "Starting background monitoring...\n";
        Sleep(3000);
        
        DWORD lastCheck = GetTickCount();
        bool wasPaused = false;
        bool showBackground = true;
        
        while (!shouldStop.load()) {
            checkHotkeys();
            
            bool currentPaused = isPaused.load();
            
            // Обновляем экран при изменении состояния паузы
            if (currentPaused && !wasPaused) {
                system("cls");
                std::cout << "========================================\n";
                std::cout << "   Windows Modules Installer Killer\n";
                std::cout << "========================================\n\n";
                std::cout << "=== WORK PAUSED ===\n\n";
                std::cout << "Statistics:\n";
                std::cout << "* Checks performed: " << checksCount << "\n";
                std::cout << "* Processes killed: " << totalKilled << "\n";
                std::cout << "* Services stopped: " << totalStopped << "\n\n";
                std::cout << "Press Ctrl+Shift+P to resume\n";
                showBackground = false;
                wasPaused = true;
            } else if (!currentPaused && wasPaused) {
                system("cls");
                std::cout << "WMI Killer running in background\n";
                std::cout << "Ctrl+Alt+W+M to exit | Ctrl+Shift+P to pause\n";
                showBackground = true;
                wasPaused = false;
            } else if (showBackground && !wasPaused) {
                // Показываем фоновый экран если не на паузе
                system("cls");
                std::cout << "WMI Killer running in background\n";
                std::cout << "Ctrl+Alt+W+M to exit | Ctrl+Shift+P to pause\n";
                showBackground = false; // Чтобы не очищать экран каждый цикл
            }
            
            if (!currentPaused) {
                DWORD now = GetTickCount();
                
                if (now - lastCheck >= 10000) {
                    lastCheck = now;
                    checksCount++;
                    
                    // Background checks
                    for (const auto& service : targetServices) {
                        stopService(service);
                    }
                    
                    for (const auto& process : targetProcesses) {
                        killProcess(process);
                    }
                }
            }
            
            Sleep(100);
        }
        
        unregisterHotkeys();
        
        // Exit screen
        system("cls");
        std::cout << "========================================\n";
        std::cout << "   Windows Modules Installer Killer\n";
        std::cout << "========================================\n\n";
        
        std::cout << "=== SHUTTING DOWN ===\n\n";
        
        std::cout << "Final statistics:\n";
        std::cout << "* Checks performed: " << checksCount << "\n";
        std::cout << "* Processes killed: " << totalKilled << "\n";
        std::cout << "* Services stopped: " << totalStopped << "\n";
        
        std::cout << "\nPress any key to exit...\n";
        std::cout << "========================================\n";
        
        std::cin.ignore();
        std::cin.get();
    }
};

int main() {
    SetConsoleTitleW(L"WMI Killer (Ctrl+Alt+W+M - exit, Ctrl+Shift+P - pause)");
    
    ProcessManager manager;
    manager.run();
    
    return 0;
}