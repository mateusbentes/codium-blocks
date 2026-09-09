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
    const bool windowsDrivePath = path.length() >= 3 &&
        ((path[0] >= wxChar('A') && path[0] <= wxChar('Z')) || (path[0] >= wxChar('a') && path[0] <= wxChar('z'))) &&
        path[1] == wxChar(':') && (path[2] == wxChar('/') || path[2] == wxChar('\\'));
    if (windowsDrivePath) return path;
    const bool windowsDriveRoot = workspaceRoot.length() >= 3 &&
        ((workspaceRoot[0] >= wxChar('A') && workspaceRoot[0] <= wxChar('Z')) ||
         (workspaceRoot[0] >= wxChar('a') && workspaceRoot[0] <= wxChar('z'))) &&
        workspaceRoot[1] == wxChar(':') &&
        (workspaceRoot[2] == wxChar('/') || workspaceRoot[2] == wxChar('\\'));
    if (windowsDriveRoot && !path.empty()) {
        wxString joined = workspaceRoot;
        if (!joined.EndsWith(wxS("/")) && !joined.EndsWith(wxS("\\"))) joined += wxS("/");
        joined += path;
        return joined;
    }
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
    wxRegEx gcc(wxS("^(.+):([0-9]+):([0-9]+):[[:space:]]*(fatal error|error|warning|note|information|info|hint)[[:space:]]*:[[:space:]]*(.*)$"));
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

bool BuildDiagnosticParser::ParseLine(const wxString& rawLine, const wxString& source,
                                      const wxString& workspaceRoot, Problem* problem)
{
    if (!problem) return false;
    const wxString line = StripAnsi(rawLine);
    wxString candidate = line;
    if (candidate.StartsWith(wxS("[stderr] "))) candidate = candidate.Mid(9);

    Problem direct;
    if (ProblemParser::ParseCompilerLine(candidate, source, workspaceRoot, &direct)) {
        hasPending_ = false;
        *problem = direct;
        return true;
    }

    wxRegEx rustHeader(wxS("^[[:space:]]*(error|warning)(\\[([^]]+)\\])?:[[:space:]]*(.*)$"));
    if (rustHeader.IsValid() && rustHeader.Matches(candidate)) {
        hasPending_ = true;
        pendingProblem_.source = source;
        pendingProblem_.severity = ParseSeverity(rustHeader.GetMatch(candidate, 1));
        pendingProblem_.code = rustHeader.GetMatch(candidate, 3);
        pendingProblem_.message = rustHeader.GetMatch(candidate, 4);
        pendingProblem_.raw = candidate;
        return false;
    }

    wxRegEx rustLocation(wxS("^[[:space:]]*-->[[:space:]]+(.+):([0-9]+):([0-9]+)[[:space:]]*$"));
    if (hasPending_ && rustLocation.IsValid() && rustLocation.Matches(candidate)) {
        pendingProblem_.path = AbsolutePath(rustLocation.GetMatch(candidate, 1), workspaceRoot);
        pendingProblem_.line = std::max(0, wxAtoi(rustLocation.GetMatch(candidate, 2)) - 1);
        pendingProblem_.column = std::max(0, wxAtoi(rustLocation.GetMatch(candidate, 3)) - 1);
        pendingProblem_.endLine = pendingProblem_.line;
        pendingProblem_.endColumn = pendingProblem_.column + 1;
        pendingProblem_.raw += wxS("\n") + candidate;
        *problem = pendingProblem_;
        hasPending_ = false;
        pendingProblem_ = Problem();
        return true;
    }
    return false;
}

void BuildDiagnosticParser::Reset()
{
    hasPending_ = false;
    pendingProblem_ = Problem();
}

void ProblemStore::Clear(const wxString& source)
{
    if (source.empty()) {
        problems_.clear();
        parsers_.clear();
        return;
    }
    problems_.erase(std::remove_if(problems_.begin(), problems_.end(), [&](const Problem& problem) {
        return problem.source == source;
    }), problems_.end());
    parsers_.erase(source);
}

void ProblemStore::Add(const Problem& problem)
{
    problems_.push_back(problem);
}

void ProblemStore::AddCompilerLine(const wxString& line, const wxString& source, const wxString& workspaceRoot)
{
    Problem problem;
    if (parsers_[source].ParseLine(line, source, workspaceRoot, &problem)) Add(problem);
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
