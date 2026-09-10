#include "codium/problem_model.hpp"

#include <wx/init.h>

#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) return 1;
    codium::ProblemStore store;
    store.AddCompilerLine(wxS("src/main.cpp:42:7: error: use of undeclared identifier 'value'"), wxS("clang"), wxS("/workspace"), wxS("build-1"));
    store.AddCompilerLine(wxS("src/main.cpp:18:3: warning: unused variable 'x'"), wxS("gcc"), wxS("/workspace"));
    store.AddCompilerLine(wxS("main.cpp(12,5): warning C4100: unreferenced parameter"), wxS("msvc"), wxS("C:/workspace"));
    store.AddCompilerLine(wxS("error[E0382]: borrow of moved value: `value`"), wxS("cargo"), wxS("/workspace"));
    store.AddCompilerLine(wxS("  --> src/lib.rs:9:13"), wxS("cargo"), wxS("/workspace"));
    store.AddCompilerLine(wxS("CMake Error at CMakeLists.txt:3 (project):"), wxS("cmake"), wxS("/workspace"), wxS("build-1"));
    store.AddCompilerLine(wxS("ninja: error: loading 'build.ninja': No such file or directory"), wxS("ninja"), wxS("/workspace"), wxS("build-1"));
    store.AddCompilerLine(wxS("collect2: error: ld returned 1 exit status"), wxS("linker"), wxS("/workspace"), wxS("build-2"));
    codium::BuildDiagnosticParser ansiParser;
    codium::Problem ansiProblem;
    if (!ansiParser.ParseLine(wxS("\x1b[31m[stderr] src/main.cpp:3:2: warning: terminal diagnostic\x1b[0m"),
                              wxS("Terminal"), wxS("/workspace"), &ansiProblem) ||
        ansiProblem.path != wxS("/workspace/src/main.cpp") || ansiProblem.line != 2) {
        std::cerr << "problem-model-smoke: ANSI parser failed\n";
        return 2;
    }
    if (store.Problems().size() != 7 || store.Count(codium::ProblemSeverity::Error) != 5 ||
        store.Count(codium::ProblemSeverity::Warning) != 2 ||
        store.Problems()[0].line != 41 || store.Problems()[0].column != 6 ||
        store.Problems()[2].code != wxS("C4100") || store.Problems()[2].path != wxS("C:/workspace/main.cpp") ||
        store.Problems()[3].path != wxS("/workspace/src/lib.rs") ||
        store.Problems()[3].line != 8 || store.Problems()[3].column != 12 ||
        store.Problems()[4].path != wxS("/workspace/CMakeLists.txt") ||
        store.Problems()[4].line != 2 || store.Problems()[5].path != wxEmptyString ||
        store.Problems()[6].message.Find(wxS("collect2")) == wxNOT_FOUND ||
        store.CountForSession(wxS("build-1")) != 3 || store.CountForSession(wxS("build-2")) != 1) {
        std::cerr << "problem-model-smoke: parser failed\n";
        return 2;
    }
    const wxArrayString visible = store.DisplayLines(true);
    if (visible.GetCount() != 7 || visible[0].Find(wxS("Error")) == wxNOT_FOUND) {
        std::cerr << "problem-model-smoke: display model failed\n";
        return 3;
    }
    store.MarkSourceStale(wxS("clang"));
    if (!store.Problems()[0].stale || store.Problems()[1].stale) {
        std::cerr << "problem-model-smoke: stale state failed\n";
        return 4;
    }
    std::cout << "problem-model-smoke: ok — compiler diagnostics normalized\n";
    return 0;
}
