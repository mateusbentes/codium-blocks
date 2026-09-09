#include "codium/problem_model.hpp"

#include <wx/filename.h>
#include <wx/regex.h>

#include <algorithm>

namespace codium {

namespace {

ProblemSeverity ParseSeverity(const wxString& value)
{
    const wxString lower = value.Lower();
    if (lower.Contains(wxS("error"))) return ProblemSeverity::Error;
    if (lower.Contains(wxS("warning"))) return ProblemSeverity::Warning;
    if (lower.Contains(wxS("hint"))) return ProblemSeverity::Hint;
    return ProblemSeverity::Information;
}

wxString AbsolutePath(const wxString& path, const wxString& workspaceRoot)
{
    wxString combined = path;
    wxFileName filename(path);
    if (!filename.IsAbsolute() && !workspaceRoot.empty()) {
        combined = workspaceRoot;
        if (!combined.EndsWith(wxFILE_SEP_PATH)) combined += wxFILE_SEP_PATH;
        combined += path;
        filename.Assign(combined);
    }
    filename.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    return filename.GetFullPath();
}

wxString StripAnsi(const wxString& input)
{
    wxString output;
    for (size_t index = 0; index < input.length(); ++index) {
        if (input[index] == wxChar(0x1b) && index + 1 < input.length() && input[index + 1] == wxChar('[')) {
            index += 2;
            while (index < input.length() && !(input[index] >= wxChar('@') && input[index] <= wxChar('~'))) ++index;
            continue;
        }
        output += input[index];
    }
    return output;
}

bool ParseRegex(const wxRegEx& expression, const wxString& line, const wxString& source,
                const wxString& workspaceRoot, Problem* problem)
{
    if (!expression.Matches(line)) return false;
    const wxString path = expression.GetMatch(line, 1);
    const wxString lineNumber = expression.GetMatch(line, 2);
    const wxString column = expression.GetMatch(line, 3);
    wxString severity;
    wxString code;
    wxString message;
    if (expression.GetMatchCount() >= 7) {
        severity = expression.GetMatch(line, 4);
        code = expression.GetMatch(line, 5);
        message = expression.GetMatch(line, 6);
    } else {
        severity = expression.GetMatch(line, 4);
        message = expression.GetMatch(line, 5);
    }
    problem->severity = ParseSeverity(severity);
    problem->source = source;
    problem->message = message;
    problem->path = AbsolutePath(path, workspaceRoot);
    problem->line = std::max(0, wxAtoi(lineNumber) - 1);
    problem->column = std::max(0, wxAtoi(column) - 1);
    problem->endLine = problem->line;
    problem->endColumn = problem->column + 1;
    problem->code = code;
    problem->raw = line;
    return true;
}

} // namespace

bool ProblemParser::ParseCompilerLine(const wxString& line, const wxString& source,
                                      const wxString& workspaceRoot, Problem* problem)
{
    if (!problem || line.empty()) return false;
    wxRegEx gcc(wxS("^(.+):([0-9]+):([0-9]+):[[:space:]]*(fatal error|error|warning|note|information|hint)[[:space:]]*:[[:space:]]*(.*)$"));
    if (gcc.IsValid() && ParseRegex(gcc, line, source, workspaceRoot, problem)) return true;

    wxRegEx msvc(wxS("^(.+)\\(([0-9]+),([0-9]+)\\):[[:space:]]*(warning|error)[[:space:]]+([^:[:space:]]+):[[:space:]]*(.*)$"));
    if (msvc.IsValid() && ParseRegex(msvc, line, source, workspaceRoot, problem)) return true;

    wxRegEx msvcSimple(wxS("^(.+)\\(([0-9]+),([0-9]+)\\):[[:space:]]*(warning|error)[[:space:]]*:[[:space:]]*(.*)$"));
    if (msvcSimple.IsValid() && ParseRegex(msvcSimple, line, source, workspaceRoot, problem)) return true;
    return false;
}

wxString ProblemParser::SeverityName(ProblemSeverity severity)
{
    switch (severity) {
    case ProblemSeverity::Error: return wxS("Error");
    case ProblemSeverity::Warning: return wxS("Warning");
    case ProblemSeverity::Information: return wxS("Info");
    case ProblemSeverity::Hint: return wxS("Hint");
    }
    return wxS("Info");
}

void ProblemStore::Clear(const wxString& source)
{
    if (source.empty()) {
        problems_.clear();
        return;
    }
    problems_.erase(std::remove_if(problems_.begin(), problems_.end(), [&](const Problem& problem) {
        return problem.source == source;
    }), problems_.end());
}

void ProblemStore::Add(const Problem& problem)
{
    problems_.push_back(problem);
}

void ProblemStore::AddCompilerLine(const wxString& line, const wxString& source, const wxString& workspaceRoot)
{
    wxString candidate = StripAnsi(line);
    if (candidate.StartsWith(wxS("[stderr] "))) candidate = candidate.Mid(9);
    Problem problem;
    if (ProblemParser::ParseCompilerLine(candidate, source, workspaceRoot, &problem)) Add(problem);
}

wxArrayString ProblemStore::DisplayLines(bool errorsAndWarningsOnly) const
{
    wxArrayString result;
    for (const auto& problem : problems_) {
        if (errorsAndWarningsOnly && problem.severity != ProblemSeverity::Error && problem.severity != ProblemSeverity::Warning) continue;
        const wxString stale = problem.stale ? wxS(" [stale]") : wxEmptyString;
        result.Add(wxString::Format(wxS("%s%s %s:%d:%d %s"), ProblemParser::SeverityName(problem.severity), stale,
                                    problem.path, problem.line + 1, problem.column + 1, problem.message));
    }
    return result;
}

size_t ProblemStore::Count(ProblemSeverity severity) const
{
    return static_cast<size_t>(std::count_if(problems_.begin(), problems_.end(), [&](const Problem& problem) {
        return problem.severity == severity;
    }));
}

void ProblemStore::MarkSourceStale(const wxString& source)
{
    for (auto& problem : problems_) {
        if (problem.source == source) problem.stale = true;
    }
}

} // namespace codium
