// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks contributors

#if defined(_WIN32)
#include <windows.h>

#include <cstring>
#include <cstdio>
#include <iterator>

namespace {

bool WriteBytes(HANDLE output, const char* text, DWORD length)
{
    DWORD written = 0;
    return WriteFile(output, text, length, &written, nullptr) && written == length;
}

bool WriteText(HANDLE output, const char* text)
{
    return WriteBytes(output, text, static_cast<DWORD>(std::strlen(text)));
}

bool ReportError(HANDLE output, const char* operation, DWORD error)
{
    char message[128]{};
    const int length = std::snprintf(message, sizeof(message), "%s:%lu\r\n", operation,
                                     static_cast<unsigned long>(error));
    return length > 0 && static_cast<size_t>(length) < sizeof(message) &&
           WriteBytes(output, message, static_cast<DWORD>(length));
}

bool WriteOutput(HANDLE output, const char* text)
{
    return WriteText(output, text);
}

void EnableVirtualTerminalOutput(HANDLE output)
{
    DWORD mode = 0;
    if (GetConsoleMode(output, &mode)) {
        SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}

bool ReadInput(HANDLE input, char* buffer, DWORD capacity, DWORD* received, DWORD* errorCode)
{
    DWORD mode = 0;
    if (GetConsoleMode(input, &mode)) {
        // ConPTY injects terminal input as console key events. Reading the
        // input-record buffer avoids the line-editor semantics of ReadConsoleA
        // and works for both cooked and raw console input modes.
        if (!SetConsoleMode(input, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT))) {
            if (errorCode) *errorCode = GetLastError();
            return false;
        }
        INPUT_RECORD records[64]{};
        while (*received == 0) {
            DWORD recordCount = 0;
            if (!ReadConsoleInputA(input, records, static_cast<DWORD>(std::size(records)), &recordCount)) {
                if (errorCode) *errorCode = GetLastError();
                return false;
            }
            for (DWORD index = 0; index < recordCount && *received < capacity; ++index) {
                const KEY_EVENT_RECORD& key = records[index].Event.KeyEvent;
                if (records[index].EventType == KEY_EVENT && key.bKeyDown && key.uChar.AsciiChar != '\0') {
                    buffer[(*received)++] = key.uChar.AsciiChar;
                }
            }
        }
        return true;
    }
    const BOOL read = ReadFile(input, buffer, capacity, received, nullptr);
    if (!read && errorCode) *errorCode = GetLastError();
    return read != FALSE;
}

} // namespace

int main()
{
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE || input == nullptr || output == nullptr) {
        return 2;
    }

    DWORD inputMode = 0;
    DWORD outputMode = 0;
    const bool inputIsConsole = GetConsoleMode(input, &inputMode) != FALSE;
    const bool outputIsConsole = GetConsoleMode(output, &outputMode) != FALSE;
    EnableVirtualTerminalOutput(output);
    const char* handleDescription = inputIsConsole
        ? (outputIsConsole ? "ready handles=console,console\r\n" : "ready handles=console,redirected\r\n")
        : (outputIsConsole ? "ready handles=redirected,console\r\n" : "ready handles=redirected,redirected\r\n");
    if (!WriteOutput(output, handleDescription)) return 3;

    char buffer[256]{};
    DWORD received = 0;
    DWORD inputError = ERROR_SUCCESS;
    char line[256]{};
    DWORD lineLength = 0;
    for (;;) {
        if (!ReadInput(input, buffer, sizeof(buffer), &received, &inputError)) {
            ReportError(output, "input-error", inputError);
            return 4;
        }
        if (received == 0) {
            ReportError(output, "input-empty", ERROR_SUCCESS);
            return 4;
        }
        for (DWORD index = 0; index < received; ++index) {
            const char character = buffer[index];
            if (character == '\r' || character == '\n') {
                line[lineLength] = '\0';
                if (std::strcmp(line, "ping") == 0) {
                    if (!WriteOutput(output, "pong\r\n")) return 5;
                } else if (std::strcmp(line, "ansi") == 0) {
                    if (!WriteOutput(output, "\x1b[31mred\x1b[0m\r\n")) return 6;
                } else if (std::strcmp(line, "exit") == 0) {
                    WriteOutput(output, "bye\r\n");
                    return 0;
                }
                lineLength = 0;
            } else if (lineLength + 1 < sizeof(line)) {
                line[lineLength++] = character;
            } else {
                lineLength = 0;
            }
        }
    }
}
#else
int main()
{
    return 0;
}
#endif
