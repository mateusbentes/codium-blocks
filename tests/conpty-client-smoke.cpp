// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks contributors

#if defined(_WIN32)
#include <windows.h>

#include <cstring>

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

} // namespace

int main()
{
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE || input == nullptr || output == nullptr) {
        return 2;
    }

    if (!WriteText(output, "ready\r\n")) return 3;

    char buffer[256]{};
    DWORD received = 0;
    char line[256]{};
    DWORD lineLength = 0;
    for (;;) {
        if (!ReadFile(input, buffer, sizeof(buffer), &received, nullptr) || received == 0) return 4;
        for (DWORD index = 0; index < received; ++index) {
            const char character = buffer[index];
            if (character == '\r' || character == '\n') {
                line[lineLength] = '\0';
                if (std::strcmp(line, "ping") == 0) {
                    if (!WriteText(output, "pong\r\n")) return 5;
                } else if (std::strcmp(line, "ansi") == 0) {
                    if (!WriteText(output, "\x1b[31mred\x1b[0m\r\n")) return 6;
                } else if (std::strcmp(line, "exit") == 0) {
                    WriteText(output, "bye\r\n");
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
