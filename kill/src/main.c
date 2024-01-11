#include <windows.h>

int
main(int argc, char* argv[]) {
    if (argc < 2) {
        return 1;
    }
    int success = 1;

    DWORD ctrlSignal = (DWORD)_atoi64(argv[argc - 1]);
    for (int i = 0; i < argc - 1; i++) {
        DWORD pid = (DWORD)_atoi64(argv[i]);
        FreeConsole();
        if (!AttachConsole(pid) || !GenerateConsoleCtrlEvent(ctrlSignal, 0)) {
            success = 0;
        }
    }
    return success == 1 ? 0 : 1;
}