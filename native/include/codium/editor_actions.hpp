#pragma once

#include <wx/string.h>

namespace codium {

struct EditorMatch final {
    long start = -1;
    long length = 0;

    bool Found() const { return start >= 0; }
};

struct EditorLineColumn final {
    int line = 0;
    int column = 0;
};

class EditorActions final {
public:
    static EditorMatch Find(const wxString& text, const wxString& query,
                            long start = 0, bool backwards = false,
                            bool matchCase = true);
    static wxString ReplaceAll(const wxString& text, const wxString& query,
                               const wxString& replacement, bool matchCase = true,
                               int* replacementCount = nullptr);
    static long PositionForLineColumn(const wxString& text, int line, int column);
    static EditorLineColumn LineColumnForPosition(const wxString& text, long position);
};

} // namespace codium
