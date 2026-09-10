// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
#include "codium/build_session.hpp"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>
#include <wx/time.h>
#include <wx/utils.h>

#include <algorithm>

namespace codium {
namespace {

std::int64_t ParseMillis(const wxString& value)
{
    wxLongLong_t parsed = 0;
    return value.ToLongLong(&parsed) ? static_cast<std::int64_t>(parsed) : 0;
}

wxString Escape(const wxString& value)
{
    wxString escaped;
    for (const wxChar character : value) {
        if (character == wxChar('\\')) escaped += wxS("\\\\");
        else if (character == wxChar('\t')) escaped += wxS("\\t");
        else if (character == wxChar('\n')) escaped += wxS("\\n");
        else if (character == wxChar('\r')) escaped += wxS("\\r");
        else escaped += character;
    }
    return escaped;
}

wxString Unescape(const wxString& value)
{
    wxString result;
    bool escaped = false;
    for (const wxChar character : value) {
        if (!escaped && character == wxChar('\\')) {
            escaped = true;
            continue;
        }
        if (escaped) {
            if (character == wxChar('t')) result += wxChar('\t');
            else if (character == wxChar('n')) result += wxChar('\n');
            else if (character == wxChar('r')) result += wxChar('\r');
            else result += character;
            escaped = false;
        } else {
            result += character;
        }
    }
    if (escaped) result += wxChar('\\');
    return result;
}

wxArrayString Fields(const wxString& line)
{
    wxArrayString fields;
    wxString current;
    for (const wxChar character : line) {
        if (character == wxChar('\t')) {
            fields.Add(Unescape(current));
            current.clear();
        } else {
            current += character;
        }
    }
    fields.Add(Unescape(current));
    return fields;
}

} // namespace

std::int64_t BuildSessionStore::NowMillis()
{
    return wxGetUTCTimeMillis().GetValue();
}

wxString BuildSessionStore::StoragePath(const wxString& workspaceRoot)
{
    const wxString directory = workspaceRoot + wxFILE_SEP_PATH + wxS(".codium-blocks");
    wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    return directory + wxFILE_SEP_PATH + wxS("build-sessions.tsv");
}

wxString BuildSessionStore::StatusName(BuildSessionStatus status)
{
    switch (status) {
    case BuildSessionStatus::Running: return wxS("running");
    case BuildSessionStatus::Succeeded: return wxS("succeeded");
    case BuildSessionStatus::Failed: return wxS("failed");
    case BuildSessionStatus::Cancelled: return wxS("cancelled");
    }
    return wxS("failed");
}

BuildSessionStatus BuildSessionStore::ParseStatus(const wxString& value)
{
    if (value == wxS("running")) return BuildSessionStatus::Running;
    if (value == wxS("succeeded")) return BuildSessionStatus::Succeeded;
    if (value == wxS("cancelled")) return BuildSessionStatus::Cancelled;
    return BuildSessionStatus::Failed;
}

bool BuildSessionStore::Load(const wxString& workspaceRoot, wxString* error)
{
    workspaceRoot_ = workspaceRoot;
    sessions_.clear();
    currentId_.clear();
    const wxString path = StoragePath(workspaceRoot);
    if (!wxFileExists(path)) return true;

    wxFile file;
    if (!file.Open(path, wxFile::read)) {
        if (error) *error = wxString::Format(wxS("Could not read build session history: %s."), path);
        return false;
    }
    wxString content;
    if (!file.ReadAll(&content)) {
        if (error) *error = wxString::Format(wxS("Could not read build session history: %s."), path);
        return false;
    }
    const wxArrayString lines = wxSplit(content, wxChar('\n'));
    for (wxString line : lines) {
        if (line.EndsWith(wxS("\r"))) line.RemoveLast();
        if (line.empty() || line == wxS("version=1")) continue;
        if (line.StartsWith(wxS("current\t"))) {
            const wxArrayString fields = Fields(line.Mid(8));
            if (!fields.empty()) currentId_ = fields[0];
            continue;
        }
        if (line.StartsWith(wxS("session\t"))) {
            const wxArrayString fields = Fields(line.Mid(8));
            if (fields.size() < 13) continue;
            BuildSession session;
            session.id = fields[0];
            session.taskName = fields[1];
            session.target = fields[2];
            session.configuration = fields[3];
            session.toolchain = fields[4];
            session.projectFile = fields[5];
            session.workingDirectory = fields[6];
            session.status = ParseStatus(fields[7]);
            session.exitCode = wxAtoi(fields[8]);
            session.startedAtMillis = ParseMillis(fields[9]);
            session.elapsedMillis = ParseMillis(fields[10]);
            session.statusMessage = fields[11];
            sessions_.push_back(session);
            continue;
        }
        if (line.StartsWith(wxS("output\t"))) {
            const wxArrayString fields = Fields(line.Mid(7));
            if (fields.size() < 2) continue;
            for (auto& session : sessions_) {
                if (session.id == fields[0]) {
                    session.output.Add(fields[1]);
                    break;
                }
            }
        }
    }
    if (currentId_.empty() && !sessions_.empty()) currentId_ = sessions_.back().id;
    return true;
}

bool BuildSessionStore::Save(wxString* error) const
{
    if (workspaceRoot_.empty()) return true;
    const wxString path = StoragePath(workspaceRoot_);
    const wxString temporary = path + wxS(".tmp");
    wxFile file;
    if (!file.Open(temporary, wxFile::write)) {
        if (error) *error = wxString::Format(wxS("Could not write build session history: %s."), temporary);
        return false;
    }
    wxString content = wxS("version=1\n");
    if (!currentId_.empty()) content += wxS("current\t") + Escape(currentId_) + wxS("\n");
    for (const auto& session : sessions_) {
        content += wxS("session\t") + Escape(session.id) + wxS("\t") + Escape(session.taskName) + wxS("\t") +
                   Escape(session.target) + wxS("\t") + Escape(session.configuration) + wxS("\t") +
                   Escape(session.toolchain) + wxS("\t") + Escape(session.projectFile) + wxS("\t") +
                   Escape(session.workingDirectory) + wxS("\t") + Escape(StatusName(session.status)) + wxS("\t") +
                   wxString::Format(wxS("%d\t%lld\t%lld\t"), session.exitCode,
                                    static_cast<long long>(session.startedAtMillis),
                                    static_cast<long long>(session.elapsedMillis)) +
                   Escape(session.statusMessage) + wxS("\treserved\n");
        for (const auto& line : session.output) {
            content += wxS("output\t") + Escape(session.id) + wxS("\t") + Escape(line) + wxS("\n");
        }
    }
    const wxScopedCharBuffer bytes = content.utf8_str();
    if (file.Write(bytes.data(), bytes.length()) != bytes.length() || !file.Close() || !wxRenameFile(temporary, path, true)) {
        wxRemoveFile(temporary);
        if (error) *error = wxString::Format(wxS("Could not commit build session history: %s."), path);
        return false;
    }
    return true;
}

wxString BuildSessionStore::Begin(const BuildSessionSpec& specification, wxString* error)
{
    if (workspaceRoot_.empty()) {
        if (error) *error = wxS("Open a workspace before starting a build session.");
        return wxEmptyString;
    }
    if (!currentId_.empty()) {
        if (BuildSession* previous = const_cast<BuildSession*>(Find(currentId_)); previous && previous->IsRunning()) {
            previous->status = BuildSessionStatus::Cancelled;
            previous->statusMessage = wxS("Superseded by a newer build session.");
            previous->elapsedMillis = std::max<std::int64_t>(0, NowMillis() - previous->startedAtMillis);
        }
    }
    BuildSession session;
    session.id = wxString::Format(wxS("build-%lld"), static_cast<long long>(NowMillis()));
    if (Find(session.id)) session.id += wxString::Format(wxS("-%zu"), sessions_.size());
    session.taskName = specification.taskName;
    session.target = specification.target;
    session.configuration = specification.configuration;
    session.toolchain = specification.toolchain;
    session.projectFile = specification.projectFile;
    session.workingDirectory = specification.workingDirectory;
    session.startedAtMillis = NowMillis();
    session.statusMessage = wxS("Build started");
    sessions_.push_back(session);
    currentId_ = session.id;
    while (sessions_.size() > 20) sessions_.erase(sessions_.begin());
    if (!Save(error)) return wxEmptyString;
    return currentId_;
}

const BuildSession* BuildSessionStore::Find(const wxString& id) const
{
    for (const auto& session : sessions_) if (session.id == id) return &session;
    return nullptr;
}

const BuildSession* BuildSessionStore::Current() const
{
    return currentId_.empty() ? nullptr : Find(currentId_);
}

bool BuildSessionStore::AppendOutput(const wxString& id, const wxString& line, wxString* error)
{
    for (auto& session : sessions_) {
        if (session.id != id) continue;
        session.output.Add(line);
        if (session.output.size() > 2000) session.output.RemoveAt(0);
        return Save(error);
    }
    if (error) *error = wxString::Format(wxS("Unknown build session: %s."), id);
    return false;
}

bool BuildSessionStore::Finish(const wxString& id, int exitCode, bool cancelled, wxString* error)
{
    for (auto& session : sessions_) {
        if (session.id != id) continue;
        session.exitCode = exitCode;
        session.status = cancelled ? BuildSessionStatus::Cancelled :
                         exitCode == 0 ? BuildSessionStatus::Succeeded : BuildSessionStatus::Failed;
        session.statusMessage = cancelled ? wxS("Build cancelled") :
                                exitCode == 0 ? wxS("Build succeeded") : wxS("Build failed");
        session.elapsedMillis = std::max<std::int64_t>(0, NowMillis() - session.startedAtMillis);
        if (currentId_ == id) currentId_.clear();
        return Save(error);
    }
    if (error) *error = wxString::Format(wxS("Unknown build session: %s."), id);
    return false;
}

wxString BuildSessionStore::DisplayLabel(const BuildSession& session)
{
    const wxString target = session.target.empty() ? wxS("default") : session.target;
    const wxString configuration = session.configuration.empty() ? wxS("default") : session.configuration;
    return wxString::Format(wxS("%s · %s · %s · %s · %lld ms"),
                            session.statusMessage, configuration, target,
                            session.toolchain.empty() ? wxS("unknown toolchain") : session.toolchain,
                            static_cast<long long>(session.elapsedMillis));
}

void BuildSessionStore::Clear()
{
    sessions_.clear();
    currentId_.clear();
}

} // namespace codium
