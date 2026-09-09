#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

#include <vector>

namespace codium {

enum class ProblemSeverity {
    Error,
    Warning,
    Information,
    Hint
};

struct Problem final {
    ProblemSeverity severity = ProblemSeverity::Information;
    wxString source;
    wxString message;
    wxString path;
    int line = 0;
    int column = 0;
    int endLine = 0;
    int endColumn = 0;
    wxString code;
    wxString raw;
    bool stale = false;
};

class ProblemParser final {
public:
    static bool ParseCompilerLine(const wxString& line, const wxString& source,
                                  const wxString& workspaceRoot, Problem* problem);
    static wxString SeverityName(ProblemSeverity severity);
};

class ProblemStore final {
public:
    void Clear(const wxString& source = wxEmptyString);
    void Add(const Problem& problem);
    void AddCompilerLine(const wxString& line, const wxString& source, const wxString& workspaceRoot);
    const std::vector<Problem>& Problems() const { return problems_; }
    wxArrayString DisplayLines(bool errorsAndWarningsOnly = false) const;
    size_t Count(ProblemSeverity severity) const;
    void MarkSourceStale(const wxString& source);

private:
    std::vector<Problem> problems_;
};

} // namespace codium
