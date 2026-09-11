// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

#include <map>
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
    wxString buildSessionId;
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

class BuildDiagnosticParser final {
public:
    bool ParseLine(const wxString& rawLine, const wxString& source, const wxString& workspaceRoot,
                   Problem* problem, const wxString& buildSessionId = wxEmptyString);
    void Reset();

private:
    bool hasPending_ = false;
    Problem pendingProblem_;
    wxString pendingSessionId_;
};

class ProblemStore final {
public:
    void Clear(const wxString& source = wxEmptyString);
    void Add(const Problem& problem);
    void AddCompilerLine(const wxString& line, const wxString& source, const wxString& workspaceRoot,
                         const wxString& buildSessionId = wxEmptyString);
    const std::vector<Problem>& Problems() const { return problems_; }
    wxArrayString DisplayLines(bool errorsAndWarningsOnly = false) const;
    size_t Count(ProblemSeverity severity) const;
    size_t CountForSession(const wxString& sessionId) const;
    void MarkSourceStale(const wxString& source);

private:
    std::vector<Problem> problems_;
    std::map<wxString, BuildDiagnosticParser> parsers_;
};

} // namespace codium
