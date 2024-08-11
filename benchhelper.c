#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_PROCESSORS 64
#define IDC_PROCESS_LIST 1001
#define IDC_PROCESSOR_GRID 1002
#define IDC_APPLY_BUTTON 1003
#define IDC_REVERT_BUTTON 1004

HWND hProcessList, hProcessorGrid, hApplyButton, hRevertButton;
DWORD_PTR selectedProcessors = 0;

// Function prototypes
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void PopulateProcessList(HWND hList);
void CreateProcessorGrid(HWND hwnd);
void ApplyChanges(HWND hwnd);
void RevertChanges(HWND hwnd);
void set_other_processes_affinity(DWORD selectedPID, DWORD_PTR processorMask);
void set_process_priority(DWORD pid, DWORD priority);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Register the window class
    const char CLASS_NAME[] = "Process Affinity Manager";
    
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    
    RegisterClass(&wc);

    // Create the window
    HWND hwnd = CreateWindowEx(
        0,                              // Optional window styles
        CLASS_NAME,                     // Window class
        "Process Affinity Manager",     // Window text
        WS_OVERLAPPEDWINDOW,            // Window style
        
        // Size and position
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        
        NULL,       // Parent window    
        NULL,       // Menu
        hInstance,  // Instance handle
        NULL        // Additional application data
    );

    if (hwnd == NULL) {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Run the message loop
    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            CreateControls(hwnd);
            return 0;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_APPLY_BUTTON) {
                ApplyChanges(hwnd);
            }

            if (LOWORD(wParam) == IDC_REVERT_BUTTON) {
                RevertChanges(hwnd);
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void CreateControls(HWND hwnd) {
    // Create process list dropdown with vertical scrollbar
    hProcessList = CreateWindow("COMBOBOX", NULL, 
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
        10, 10, 200, 300, hwnd, (HMENU)IDC_PROCESS_LIST, NULL, NULL);
    PopulateProcessList(hProcessList);

    // Set the height of the dropdown list
    SendMessage(hProcessList, CB_SETDROPPEDWIDTH, 200, 0);

    // Create processor grid (keep unchanged)
    hProcessorGrid = CreateWindow("LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | LBS_MULTIPLESEL,
        10, 50, 200, 200, hwnd, (HMENU)IDC_PROCESSOR_GRID, NULL, NULL);
    CreateProcessorGrid(hwnd);

    // Create Apply button (keep unchanged)
    hApplyButton = CreateWindow("BUTTON", "Apply", 
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 500, 100, 30, hwnd, (HMENU)IDC_APPLY_BUTTON, NULL, NULL);

    // Create Revert button (keep unchanged)
    hRevertButton = CreateWindow("BUTTON", "Revert", 
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        120, 500, 100, 30, hwnd, (HMENU)IDC_REVERT_BUTTON, NULL, NULL);
}

void PopulateProcessList(HWND hList) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hSnapshot, &pe32)) {
        do {
            SendMessage(hList, CB_ADDSTRING, 0, (LPARAM)pe32.szExeFile);
        } while (Process32Next(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);
}

void CreateProcessorGrid(HWND hwnd) {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    for (DWORD i = 0; i < sysInfo.dwNumberOfProcessors && i < MAX_PROCESSORS; i++) {
        char buffer[32];
        sprintf(buffer, "Processor %lu", i);
        SendMessage(hProcessorGrid, LB_ADDSTRING, 0, (LPARAM)buffer);
    }
}

void ApplyChanges(HWND hwnd) {
    // Get selected process
    int processIndex = SendMessage(hProcessList, CB_GETCURSEL, 0, 0);
    if (processIndex == CB_ERR) {
        MessageBox(hwnd, "Please select a process", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    char processName[MAX_PATH];
    SendMessage(hProcessList, CB_GETLBTEXT, processIndex, (LPARAM)processName);

    // Get selected processors
    selectedProcessors = 0;
    int count = SendMessage(hProcessorGrid, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++) {
        if (SendMessage(hProcessorGrid, LB_GETSEL, i, 0) > 0) {
            selectedProcessors |= (1ULL << i);
        }
    }

    // Find the process ID for the selected process name
    DWORD selectedPID = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hSnapshot, &pe32)) {
            do {
                if (strcmp(pe32.szExeFile, processName) == 0) {
                    selectedPID = pe32.th32ProcessID;
                    break;
                }
            } while (Process32Next(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
    }

    if (selectedPID == 0) {
        MessageBox(hwnd, "Failed to find the selected process", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Apply the changes
    set_other_processes_affinity(selectedPID, selectedProcessors);

    MessageBox(hwnd, "Changes applied successfully", "Success", MB_OK | MB_ICONINFORMATION);
}

void set_process_affinity(DWORD processID) {
    HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, processID);
    if (hProcess != NULL) {
        DWORD_PTR processAffinityMask = 0;
        DWORD_PTR systemAffinityMask = 0;

        if (GetProcessAffinityMask(hProcess, &processAffinityMask, &systemAffinityMask)) {
            // Set the process affinity to use all available processors
            SetProcessAffinityMask(hProcess, systemAffinityMask);
        }

        CloseHandle(hProcess);
    }
}

void RevertChanges(HWND hwnd) {
    HANDLE hProcessSnap;
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE) {
        MessageBox(hwnd, "Failed to create process snapshot", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    if (Process32First(hProcessSnap, &pe32)) {
        do {
            // Set process priority to NORMAL_PRIORITY_CLASS
            set_process_priority(pe32.th32ProcessID, NORMAL_PRIORITY_CLASS);
            // Set process affinity to use all available processors
            set_process_affinity(pe32.th32ProcessID);
        } while (Process32Next(hProcessSnap, &pe32));
    } else {
        MessageBox(hwnd, "Failed to enumerate processes", "Error", MB_OK | MB_ICONERROR);
    }

    CloseHandle(hProcessSnap);

    MessageBox(hwnd, "Changes reverted successfully", "Success", MB_OK | MB_ICONINFORMATION);
}



void set_other_processes_affinity(DWORD selectedPID, DWORD_PTR processorMask) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(hSnapshot, &pe32)) {
        CloseHandle(hSnapshot);
        return;
    }

    do {
        if (pe32.th32ProcessID != selectedPID) {
            HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pe32.th32ProcessID);
            if (hProcess != NULL) {
                DWORD_PTR processAffinity, systemAffinity;
                if (GetProcessAffinityMask(hProcess, &processAffinity, &systemAffinity)) {
                    // Remove selected processors from the affinity mask
                    processAffinity &= ~processorMask;

                    // Ensure at least one processor is still assigned
                    if (processAffinity == 0) {
                        processAffinity = 1;  // Assign to the first processor if all were removed
                    }

                    // Set the new affinity
                    SetProcessAffinityMask(hProcess, processAffinity);
                }

                // Set process priority to low
                set_process_priority(pe32.th32ProcessID, IDLE_PRIORITY_CLASS);

                CloseHandle(hProcess);
            }
        }
    } while (Process32Next(hSnapshot, &pe32));

    CloseHandle(hSnapshot);
}

void set_process_priority(DWORD pid, DWORD priority) {
    HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (hProcess != NULL) {
        SetPriorityClass(hProcess, priority);
        CloseHandle(hProcess);
    }
}