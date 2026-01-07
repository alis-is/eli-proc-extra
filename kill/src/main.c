#include <windows.h>

int
main(int argc, char* argv[]) {
    if (argc < 2) {
        return 1;
    }

    // protect itself
    if (!SetConsoleCtrlHandler(NULL, TRUE)) {
        return 2;
    }

    DWORD ctrlSignal = (DWORD)_atoi64(argv[argc - 1]);

    int signalSent = 0;

    for (int i = 0; i < argc - 1; i++) {
        DWORD pid = (DWORD)_atoi64(argv[i]);

        // Always detach before trying to attach (clean slate)
        FreeConsole();

        if (AttachConsole(pid)) {
            // We found a live process in the group!
            // Send the signal to the entire console group (0)
            if (GenerateConsoleCtrlEvent(ctrlSignal, 0)) {
                signalSent = 1;
                Sleep(50);

                break; // Job done, exit loop.
            }
        }
    }
    return signalSent ? 0 : 1;
}