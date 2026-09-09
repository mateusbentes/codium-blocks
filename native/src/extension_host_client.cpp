#include "codium/extension_host_client.hpp"

#include <wx/filename.h>
#include <wx/utils.h>

namespace codium {

namespace {

wxString JsonEscape(const wxString& value)
{
    wxString escaped;
    escaped.reserve(value.length() + 8);
    for (const auto ch : value) {
        switch (static_cast<wchar_t>(ch)) {
        case '\\': escaped += wxS("\\\\"); break;
        case '"': escaped += wxS("\\\""); break;
        case '\n': escaped += wxS("\\n"); break;
        case '\r': escaped += wxS("\\r"); break;
        case '\t': escaped += wxS("\\t"); break;
        default: escaped += ch; break;
        }
    }
    return escaped;
}

} // namespace

ExtensionHostClient::ExtensionHostClient(wxWindow* owner)
    : owner_(owner), process_(nullptr), pid_(0)
{
}

ExtensionHostClient::~ExtensionHostClient()
{
    Stop();
}

bool ExtensionHostClient::Start(const wxString& hostScript, const wxString& nodeExecutable)
{
    if (IsRunning()) {
        return true;
    }

    process_ = new wxProcess(owner_);
    process_->Redirect();
    const wxString command = wxString::Format(wxS("%s \"%s\""), nodeExecutable, hostScript);
    pid_ = wxExecute(command, wxEXEC_ASYNC, process_);
    if (pid_ == 0) {
        delete process_;
        process_ = nullptr;
        return false;
    }

    inputBuffer_.clear();
    return true;
}

void ExtensionHostClient::Stop()
{
    if (!process_) {
        return;
    }

    if (IsRunning()) {
        SendRaw(wxS("{\"type\":\"shutdown\"}"));
        wxKill(pid_, wxSIGTERM, nullptr, wxKILL_CHILDREN);
    }

    delete process_;
    process_ = nullptr;
    pid_ = 0;
    inputBuffer_.clear();
}

bool ExtensionHostClient::IsRunning() const
{
    return process_ != nullptr && pid_ != 0;
}

bool ExtensionHostClient::SendRaw(const wxString& jsonLine)
{
    if (!IsRunning() || !process_->GetOutputStream()) {
        return false;
    }

    const wxString line = jsonLine + wxS("\n");
    const wxScopedCharBuffer utf8 = line.utf8_str();
    wxOutputStream* output = process_->GetOutputStream();
    output->Write(utf8.data(), utf8.length());
    output->Sync();
    return output->LastWrite() == utf8.length() && output->IsOk();
}

bool ExtensionHostClient::LoadExtension(const wxString& extensionPath)
{
    return SendRaw(wxString::Format(
        wxS("{\"type\":\"load\",\"extensionPath\":\"%s\"}"),
        JsonEscape(extensionPath)));
}

bool ExtensionHostClient::ExecuteCommand(const wxString& command)
{
    return SendRaw(wxString::Format(
        wxS("{\"type\":\"executeCommand\",\"command\":\"%s\"}"),
        JsonEscape(command)));
}

wxArrayString ExtensionHostClient::Poll()
{
    wxArrayString lines;
    if (!IsRunning() || !process_->GetInputStream()) {
        return lines;
    }

    wxInputStream* stream = process_->GetInputStream();
    char byte = 0;
    while (stream->CanRead()) {
        stream->Read(&byte, 1);
        if (stream->LastRead() != 1) {
            break;
        }

        if (byte == '\n') {
            lines.Add(inputBuffer_);
            inputBuffer_.clear();
        } else if (byte != '\r') {
            inputBuffer_ += wxString::FromUTF8(&byte, 1);
        }
    }

    return lines;
}

} // namespace codium
