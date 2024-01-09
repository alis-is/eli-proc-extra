
#include <windows.h>

int
main(int argc, char* argv[]) {
    if (argc < 3) {
        return 1;
    }
    int success = 1;

    DWORD ctrlSignal = atoi(argv[argc - 1]);
    for (int i = 2; i < argc - 1; i++) {
        int pid = atoi(argv[i]);
        FreeConsole();
        if (!AttachConsole(pid) || !GenerateConsoleCtrlEvent(ctrlSignal, 0)) {
            success = 0;
        }
    }

    return success == 1 ? 0 : 1;
}