#include "codium/problem_model.hpp"

#include <wx/init.h>

#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) return 1;
    codium::ProblemStore store;
    store.AddCompilerLine(wxS("src/main.cpp:42:7: error: use of undeclared identifier 'value'"), wxS("clang"), wxS("/workspace"));
    store.AddCompilerLine(wxS("src/main.cpp:18:3: warning: unused variable 'x'"), wxS("gcc"), wxS("/workspace"));
    store.AddCompilerLine(wxS("main.cpp(12,5): warning C4100: unreferenced parameter"), wxS("msvc"), wxS("C:/workspace"));
    if (store.Problems().size() != 3 || store.Count(codium::ProblemSeverity::Error) != 1 ||
        store.Count(codium::ProblemSeverity::Warning) != 2 ||
        store.Problems()[0].line != 41 || store.Problems()[0].column != 6 ||
        store.Problems()[2].code != wxS("C4100")) {
        std::cerr << "problem-model-smoke: parser failed\n";
        return 2;
    }
    const wxArrayString visible = store.DisplayLines(true);
    if (visible.GetCount() != 3 || visible[0].Find(wxS("Error")) == wxNOT_FOUND) {
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
