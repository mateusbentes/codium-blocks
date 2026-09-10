// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/debug_model.hpp"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>

#include <cctype>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace codium {

namespace {

wxString JsonEscape(const wxString& value)
{
    wxString result;
    for (const auto character : value) {
        if (character == wxChar('\\')) result += wxS("\\\\");
        else if (character == wxChar('"')) result += wxS("\\\"");
        else result += character;
    }
    return result;
}

wxString Normalize(wxString value)
{
    value.Replace(wxS("\\"), wxS("/"));
    while (value.length() > 1 && value.EndsWith(wxS("/"))) value.RemoveLast();
    return value;
}

wxString WatchFilePath(const wxString& workspaceRoot)
{
    wxString dataRoot;
    if (!wxGetEnv(wxS("CODIUM_BLOCKS_DATA"), &dataRoot) || dataRoot.empty()) {
        dataRoot = wxStandardPaths::Get().GetUserConfigDir() + wxFILE_SEP_PATH + wxS("CodiumBlocks");
    }
    const std::string keyInput = workspaceRoot.utf8_str().data();
    const size_t key = std::hash<std::string>{}(keyInput);
    std::ostringstream suffix;
    suffix << std::hex << key;
    const wxString directory = dataRoot + wxFILE_SEP_PATH + wxS("watches");
    wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    return directory + wxFILE_SEP_PATH + wxString::FromUTF8(suffix.str()) + wxS(".txt");
}

struct JsonValue final {
    enum class Kind { Null, Boolean, Number, String, Array, Object };

    Kind kind = Kind::Null;
    long long number = 0;
    bool boolean = false;
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;
};

void AppendUtf8(std::string* output, unsigned int codepoint)
{
    if (codepoint <= 0x7f) {
        output->push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        output->push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output->push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0x10ffff) {
        output->push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output->push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

class JsonParser final {
public:
    explicit JsonParser(std::string input)
        : input_(std::move(input))
    {
    }

    bool Parse(JsonValue* value, std::string* error)
    {
        SkipWhitespace();
        if (!ParseValue(value, error)) return false;
        SkipWhitespace();
        if (position_ != input_.size()) {
            return Fail(error, "unexpected trailing JSON data");
        }
        return true;
    }

private:
    bool Fail(std::string* error, const char* message)
    {
        if (error) *error = std::string(message) + " at byte " + std::to_string(position_);
        return false;
    }

    void SkipWhitespace()
    {
        while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_;
    }

    bool Consume(char character)
    {
        SkipWhitespace();
        if (position_ >= input_.size() || input_[position_] != character) return false;
        ++position_;
        return true;
    }

    bool ParseValue(JsonValue* value, std::string* error)
    {
        SkipWhitespace();
        if (position_ >= input_.size()) return Fail(error, "expected JSON value");
        switch (input_[position_]) {
        case '{': return ParseObject(value, error);
        case '[': return ParseArray(value, error);
        case '"':
            value->kind = JsonValue::Kind::String;
            return ParseString(&value->string, error);
        case 't': return ParseLiteral("true", JsonValue::Kind::Boolean, true, value, error);
        case 'f': return ParseLiteral("false", JsonValue::Kind::Boolean, false, value, error);
        case 'n': return ParseNull(value, error);
        default: return ParseNumber(value, error);
        }
    }

    bool ParseObject(JsonValue* value, std::string* error)
    {
        if (!Consume('{')) return Fail(error, "expected object");
        value->kind = JsonValue::Kind::Object;
        value->object.clear();
        SkipWhitespace();
        if (Consume('}')) return true;
        while (position_ < input_.size()) {
            std::string key;
            if (!ParseString(&key, error)) return false;
            if (!Consume(':')) return Fail(error, "expected object colon");
            JsonValue child;
            if (!ParseValue(&child, error)) return false;
            value->object[std::move(key)] = std::move(child);
            if (Consume('}')) return true;
            if (!Consume(',')) return Fail(error, "expected object separator");
        }
        return Fail(error, "unterminated object");
    }

    bool ParseArray(JsonValue* value, std::string* error)
    {
        if (!Consume('[')) return Fail(error, "expected array");
        value->kind = JsonValue::Kind::Array;
        value->array.clear();
        SkipWhitespace();
        if (Consume(']')) return true;
        while (position_ < input_.size()) {
            JsonValue child;
            if (!ParseValue(&child, error)) return false;
            value->array.push_back(std::move(child));
            if (Consume(']')) return true;
            if (!Consume(',')) return Fail(error, "expected array separator");
        }
        return Fail(error, "unterminated array");
    }

    bool ParseString(std::string* value, std::string* error)
    {
        SkipWhitespace();
        if (position_ >= input_.size() || input_[position_] != '"') return Fail(error, "expected string");
        ++position_;
        value->clear();
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') return true;
            if (character < 0x20) return Fail(error, "control character in string");
            if (character != '\\') {
                value->push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) return Fail(error, "unfinished string escape");
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"': value->push_back('"'); break;
            case '\\': value->push_back('\\'); break;
            case '/': value->push_back('/'); break;
            case 'b': value->push_back('\b'); break;
            case 'f': value->push_back('\f'); break;
            case 'n': value->push_back('\n'); break;
            case 'r': value->push_back('\r'); break;
            case 't': value->push_back('\t'); break;
            case 'u': {
                if (position_ + 4 > input_.size()) return Fail(error, "short unicode escape");
                unsigned int codepoint = 0;
                for (int index = 0; index < 4; ++index) {
                    const char hex = input_[position_++];
                    codepoint <<= 4;
                    if (hex >= '0' && hex <= '9') codepoint |= static_cast<unsigned int>(hex - '0');
                    else if (hex >= 'a' && hex <= 'f') codepoint |= static_cast<unsigned int>(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') codepoint |= static_cast<unsigned int>(hex - 'A' + 10);
                    else return Fail(error, "invalid unicode escape");
                }
                AppendUtf8(value, codepoint);
                break;
            }
            default: return Fail(error, "unknown string escape");
            }
        }
        return Fail(error, "unterminated string");
    }

    bool ParseLiteral(const char* literal, JsonValue::Kind kind, bool boolean,
                      JsonValue* value, std::string* error)
    {
        const std::string expected(literal);
        if (input_.compare(position_, expected.size(), expected) != 0) {
            return Fail(error, "invalid JSON literal");
        }
        position_ += expected.size();
        value->kind = kind;
        value->boolean = boolean;
        return true;
    }

    bool ParseNull(JsonValue* value, std::string* error)
    {
        if (input_.compare(position_, 4, "null") != 0) return Fail(error, "invalid null literal");
        position_ += 4;
        value->kind = JsonValue::Kind::Null;
        return true;
    }

    bool ParseNumber(JsonValue* value, std::string* error)
    {
        SkipWhitespace();
        const size_t start = position_;
        if (position_ < input_.size() && (input_[position_] == '-' || input_[position_] == '+')) ++position_;
        while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '-' || input_[position_] == '+')) ++position_;
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        }
        if (start == position_) return Fail(error, "expected JSON number");
        const std::string token = input_.substr(start, position_ - start);
        char* end = nullptr;
        const long long parsed = std::strtoll(token.c_str(), &end, 10);
        value->kind = JsonValue::Kind::Number;
        value->number = end == token.c_str() ? 0 : parsed;
        return true;
    }

    std::string input_;
    size_t position_ = 0;
};

const JsonValue* Field(const JsonValue& object, const char* name)
{
    if (object.kind != JsonValue::Kind::Object) return nullptr;
    const auto found = object.object.find(name);
    return found == object.object.end() ? nullptr : &found->second;
}

wxString StringField(const JsonValue& object, const char* name)
{
    const JsonValue* value = Field(object, name);
    if (!value || value->kind != JsonValue::Kind::String) return wxEmptyString;
    return wxString::FromUTF8(value->string);
}

int IntField(const JsonValue& object, const char* name, int fallback = 0)
{
    const JsonValue* value = Field(object, name);
    if (!value || value->kind != JsonValue::Kind::Number) return fallback;
    if (value->number < std::numeric_limits<int>::min() || value->number > std::numeric_limits<int>::max()) return fallback;
    return static_cast<int>(value->number);
}

bool BoolField(const JsonValue& object, const char* name, bool fallback = false)
{
    const JsonValue* value = Field(object, name);
    return value && value->kind == JsonValue::Kind::Boolean ? value->boolean : fallback;
}

void ParseFrame(const JsonValue& value, CodeBlocksDebugFrame* frame)
{
    frame->number = IntField(value, "number");
    frame->function = StringField(value, "function");
    frame->file = StringField(value, "file");
    frame->lineText = StringField(value, "lineText");
    const JsonValue* line = Field(value, "line");
    frame->hasLine = line && line->kind == JsonValue::Kind::Number;
    frame->line = frame->hasLine ? IntField(value, "line") : 0;
    frame->valid = BoolField(value, "valid");
}

void ParseThread(const JsonValue& value, CodeBlocksDebugThread* thread)
{
    thread->active = BoolField(value, "active");
    thread->number = IntField(value, "number");
    thread->info = StringField(value, "info");
}

void ParseBreakpoint(const JsonValue& value, CodeBlocksDebugBreakpoint* breakpoint)
{
    breakpoint->location = StringField(value, "location");
    breakpoint->line = IntField(value, "line");
    breakpoint->lineText = StringField(value, "lineText");
    breakpoint->type = StringField(value, "type");
    breakpoint->info = StringField(value, "info");
    breakpoint->enabled = BoolField(value, "enabled");
    breakpoint->visible = BoolField(value, "visible");
    breakpoint->temporary = BoolField(value, "temporary");
}

void ParseValue(const JsonValue& value, CodeBlocksDebugValue* output, int depth)
{
    if (value.kind != JsonValue::Kind::Object || depth > 16) {
        output->truncated = true;
        return;
    }
    output->symbol = StringField(value, "symbol");
    output->value = StringField(value, "value");
    output->type = StringField(value, "type");
    output->full = StringField(value, "full");
    output->valueError = BoolField(value, "valueError");
    output->changed = BoolField(value, "changed");
    output->expanded = BoolField(value, "expanded");
    output->truncated = BoolField(value, "truncated");
    const JsonValue* children = Field(value, "children");
    if (!children || children->kind != JsonValue::Kind::Array) return;
    for (const auto& child : children->array) {
        CodeBlocksDebugValue parsed;
        ParseValue(child, &parsed, depth + 1);
        output->children.push_back(std::move(parsed));
    }
}

} // namespace

void SourceMapper::Add(const wxString& remoteRoot, const wxString& localRoot)
{
    const wxString remote = Normalize(remoteRoot);
    const wxString local = Normalize(localRoot);
    if (!remote.empty() && !local.empty()) mappings_[remote] = local;
}

void SourceMapper::Clear()
{
    mappings_.clear();
}

wxString SourceMapper::Map(const wxString& remotePath) const
{
    const wxString normalized = Normalize(remotePath);
    size_t bestLength = 0;
    wxString bestRemote;
    wxString bestLocal;
    for (const auto& [remote, local] : mappings_) {
        if ((normalized == remote || normalized.StartsWith(remote + wxS("/"))) && remote.length() > bestLength) {
            bestLength = remote.length();
            bestRemote = remote;
            bestLocal = local;
        }
    }
    if (bestRemote.empty()) return remotePath;
    wxString suffix = normalized.Mid(bestRemote.length());
    return bestLocal + suffix;
}

wxString SourceMapper::ToJson() const
{
    wxString result = wxS("{");
    bool first = true;
    for (const auto& [remote, local] : mappings_) {
        if (!first) result += wxS(",");
        first = false;
        result += wxS("\"") + JsonEscape(remote) + wxS("\":\"") + JsonEscape(local) + wxS("\"");
    }
    return result + wxS("}");
}

wxArrayString WatchStore::Load(const wxString& workspaceRoot)
{
    wxArrayString expressions;
    const wxString path = WatchFilePath(workspaceRoot);
    if (!wxFileExists(path)) return expressions;
    wxFile file;
    if (!file.Open(path, wxFile::read)) return expressions;
    wxString text;
    if (!file.ReadAll(&text)) return expressions;
    wxStringTokenizer tokenizer(text, wxS("\n"), wxTOKEN_STRTOK);
    while (tokenizer.HasMoreTokens()) {
        const wxString expression = tokenizer.GetNextToken().Trim(true).Trim(false);
        if (!expression.empty()) expressions.Add(expression);
    }
    return expressions;
}

bool WatchStore::Save(const wxString& workspaceRoot, const wxArrayString& expressions, wxString* error)
{
    const wxString path = WatchFilePath(workspaceRoot);
    const wxString temporary = path + wxS(".tmp");
    wxFile file;
    if (!file.Open(temporary, wxFile::write)) {
        if (error) *error = wxString::Format(wxS("Could not write watches: %s."), temporary);
        return false;
    }
    wxString text;
    for (const auto& expression : expressions) text += expression + wxS("\n");
    const wxScopedCharBuffer bytes = text.utf8_str();
    if (file.Write(bytes.data(), bytes.length()) != bytes.length() || !file.Close() || !wxRenameFile(temporary, path, true)) {
        wxRemoveFile(temporary);
        if (error) *error = wxString::Format(wxS("Could not commit watches: %s."), path);
        return false;
    }
    return true;
}

void DapDebugSessionModel::Reset()
{
    state_ = DapRunState::Disconnected;
    breakpoints_.clear();
}

void DapDebugSessionModel::MarkStarted()
{
    state_ = DapRunState::Initializing;
    for (auto& [path, items] : breakpoints_) {
        (void)path;
        for (auto& breakpoint : items) {
            breakpoint.state = DapBreakpointState::Pending;
            breakpoint.message.clear();
        }
    }
}

void DapDebugSessionModel::MarkInitializing()
{
    state_ = DapRunState::Initializing;
}

void DapDebugSessionModel::MarkDisconnected()
{
    state_ = DapRunState::Disconnected;
}

void DapDebugSessionModel::MarkStopped()
{
    state_ = DapRunState::Stopped;
}

void DapDebugSessionModel::SetState(DapRunState state, DapRefreshPlan* plan)
{
    if (state_ == state) return;
    state_ = state;
    if (plan) plan->stateChanged = true;
}

DapRefreshPlan DapDebugSessionModel::ObserveMessage(const wxString& json)
{
    DapRefreshPlan plan;
    plan.state = state_;
    const wxScopedCharBuffer utf8 = json.utf8_str();
    JsonValue root;
    std::string parseError;
    JsonParser parser(utf8 ? std::string(utf8.data()) : std::string());
    if (!parser.Parse(&root, &parseError) || root.kind != JsonValue::Kind::Object) return plan;

    const wxString type = StringField(root, "type");
    const wxString event = StringField(root, "event");
    const wxString command = StringField(root, "command");
    plan.threadId = IntField(root, "threadId");
    plan.reason = StringField(root, "reason");
    const JsonValue* body = Field(root, "body");
    if (body && body->kind == JsonValue::Kind::Object) {
        plan.threadId = IntField(*body, "threadId", plan.threadId);
        plan.reason = StringField(*body, "reason").empty() ? plan.reason : StringField(*body, "reason");
    }
    if (type == wxS("event")) {
        if (event == wxS("initialized")) {
            SetState(DapRunState::Initialized, &plan);
        } else if (event == wxS("stopped")) {
            SetState(DapRunState::Paused, &plan);
            plan.refreshThreads = true;
        } else if (event == wxS("continued")) {
            SetState(DapRunState::Running, &plan);
            plan.clearTransientViews = true;
        } else if (event == wxS("terminated") || event == wxS("exited")) {
            SetState(DapRunState::Stopped, &plan);
            plan.clearTransientViews = true;
        }
    } else if (type == wxS("response")) {
        const JsonValue* success = Field(root, "success");
        const bool succeeded = !success || success->kind != JsonValue::Kind::Boolean || success->boolean;
        if (succeeded && command == wxS("initialize")) {
            SetState(DapRunState::Initialized, &plan);
        } else if (succeeded && (command == wxS("launch") || command == wxS("attach"))) {
            SetState(DapRunState::Running, &plan);
        } else if (command == wxS("disconnect")) {
            SetState(DapRunState::Stopped, &plan);
            plan.clearTransientViews = true;
        }
    }
    plan.state = state_;
    return plan;
}

void DapDebugSessionModel::SetRequestedBreakpoints(const wxString& sourcePath, const wxArrayInt& lines)
{
    std::vector<DapBreakpoint> requested;
    for (const int line : lines) {
        if (line <= 0) continue;
        DapBreakpoint breakpoint;
        breakpoint.sourcePath = sourcePath;
        breakpoint.requestedLine = line;
        breakpoint.actualLine = line;
        breakpoint.state = DapBreakpointState::Pending;
        requested.push_back(std::move(breakpoint));
    }
    breakpoints_[sourcePath] = std::move(requested);
}

bool DapDebugSessionModel::ToggleRequestedBreakpoint(const wxString& sourcePath, int line)
{
    if (line <= 0) return false;
    auto& breakpoints = breakpoints_[sourcePath];
    const auto found = std::find_if(breakpoints.begin(), breakpoints.end(),
                                    [line](const DapBreakpoint& breakpoint) {
                                        return breakpoint.requestedLine == line;
                                    });
    if (found != breakpoints.end()) {
        breakpoints.erase(found);
        return false;
    }
    DapBreakpoint breakpoint;
    breakpoint.sourcePath = sourcePath;
    breakpoint.requestedLine = line;
    breakpoint.actualLine = line;
    breakpoint.state = DapBreakpointState::Pending;
    breakpoints.push_back(std::move(breakpoint));
    std::sort(breakpoints.begin(), breakpoints.end(), [](const DapBreakpoint& left, const DapBreakpoint& right) {
        return left.requestedLine < right.requestedLine;
    });
    return true;
}

wxArrayInt DapDebugSessionModel::RequestedBreakpointLines(const wxString& sourcePath) const
{
    wxArrayInt lines;
    const auto found = breakpoints_.find(sourcePath);
    if (found == breakpoints_.end()) return lines;
    for (const auto& breakpoint : found->second) lines.Add(breakpoint.requestedLine);
    return lines;
}

bool DapDebugSessionModel::ApplyBreakpointResponse(const wxString& sourcePath, const wxString& json,
                                                   wxString* error)
{
    const wxScopedCharBuffer utf8 = json.utf8_str();
    JsonValue root;
    std::string parseError;
    JsonParser parser(utf8 ? std::string(utf8.data()) : std::string());
    if (!parser.Parse(&root, &parseError) || root.kind != JsonValue::Kind::Object) {
        if (error) *error = wxString::FromUTF8(parseError.empty() ? "invalid DAP breakpoint response" : parseError);
        return false;
    }
    const JsonValue* success = Field(root, "success");
    const bool succeeded = !success || success->kind != JsonValue::Kind::Boolean || success->boolean;
    auto& requested = breakpoints_[sourcePath];
    if (!succeeded) {
        const wxString message = StringField(root, "message");
        for (auto& breakpoint : requested) {
            breakpoint.state = DapBreakpointState::Rejected;
            breakpoint.message = message.empty() ? wxS("The debug adapter rejected this breakpoint.") : message;
        }
        if (error) *error = message;
        return true;
    }

    const JsonValue* body = Field(root, "body");
    const JsonValue* values = body ? Field(*body, "breakpoints") : nullptr;
    if (!values || values->kind != JsonValue::Kind::Array) {
        if (error) *error = wxS("DAP breakpoint response has no breakpoints array.");
        return false;
    }
    for (auto& breakpoint : requested) {
        breakpoint.state = DapBreakpointState::Rejected;
        breakpoint.message = wxS("The debug adapter did not confirm this breakpoint.");
    }
    for (size_t index = 0; index < values->array.size(); ++index) {
        const JsonValue& value = values->array[index];
        if (value.kind != JsonValue::Kind::Object) continue;
        const int actualLine = IntField(value, "line");
        const int requestedLine = index < requested.size() ? requested[index].requestedLine : actualLine;
        auto found = std::find_if(requested.begin(), requested.end(),
                                  [requestedLine](const DapBreakpoint& breakpoint) {
                                      return breakpoint.requestedLine == requestedLine;
                                  });
        if (found == requested.end()) {
            DapBreakpoint extra;
            extra.sourcePath = sourcePath;
            extra.requestedLine = requestedLine;
            extra.actualLine = actualLine > 0 ? actualLine : requestedLine;
            extra.id = IntField(value, "id");
            extra.message = StringField(value, "message");
            extra.state = BoolField(value, "verified") ? DapBreakpointState::Verified : DapBreakpointState::Rejected;
            requested.push_back(std::move(extra));
            continue;
        }
        found->actualLine = actualLine > 0 ? actualLine : found->requestedLine;
        found->id = IntField(value, "id");
        found->message = StringField(value, "message");
        found->state = BoolField(value, "verified") ? DapBreakpointState::Verified : DapBreakpointState::Rejected;
    }
    return true;
}

const std::vector<DapBreakpoint>& DapDebugSessionModel::Breakpoints(const wxString& sourcePath) const
{
    static const std::vector<DapBreakpoint> empty;
    const auto found = breakpoints_.find(sourcePath);
    return found == breakpoints_.end() ? empty : found->second;
}

wxString DapDebugSessionModel::StateName(DapRunState state)
{
    switch (state) {
    case DapRunState::Disconnected: return wxS("disconnected");
    case DapRunState::Initializing: return wxS("initializing");
    case DapRunState::Initialized: return wxS("initialized");
    case DapRunState::Running: return wxS("running");
    case DapRunState::Paused: return wxS("paused");
    case DapRunState::Stopped: return wxS("stopped");
    }
    return wxS("unknown");
}

wxString DapDebugSessionModel::BreakpointStateName(DapBreakpointState state)
{
    switch (state) {
    case DapBreakpointState::Pending: return wxS("pending");
    case DapBreakpointState::Verified: return wxS("verified");
    case DapBreakpointState::Rejected: return wxS("rejected");
    case DapBreakpointState::Disabled: return wxS("disabled");
    }
    return wxS("unknown");
}

bool CodeBlocksDebugSnapshot::Parse(const wxString& json, CodeBlocksDebugSnapshot* snapshot, wxString* error)
{
    if (!snapshot) {
        if (error) *error = wxS("The debugger snapshot destination is null.");
        return false;
    }
    *snapshot = CodeBlocksDebugSnapshot();
    const wxScopedCharBuffer utf8 = json.utf8_str();
    JsonValue root;
    std::string parseError;
    JsonParser parser(utf8 ? std::string(utf8.data()) : std::string());
    if (!parser.Parse(&root, &parseError) || root.kind != JsonValue::Kind::Object) {
        if (error) *error = wxString::FromUTF8(parseError.empty() ? "debugger snapshot is not a JSON object" : parseError);
        return false;
    }

    snapshot->dataKind = StringField(root, "dataKind");
    snapshot->expression = StringField(root, "expression");
    snapshot->activeFrame = IntField(root, "activeFrame");
    const JsonValue* items = Field(root, "items");
    if (items && items->kind == JsonValue::Kind::Array) {
        if (snapshot->dataKind == wxS("frames")) {
            for (const auto& item : items->array) {
                if (item.kind != JsonValue::Kind::Object || BoolField(item, "truncated")) continue;
                CodeBlocksDebugFrame frame;
                ParseFrame(item, &frame);
                snapshot->frames.push_back(std::move(frame));
            }
        } else if (snapshot->dataKind == wxS("threads")) {
            for (const auto& item : items->array) {
                if (item.kind != JsonValue::Kind::Object || BoolField(item, "truncated")) continue;
                CodeBlocksDebugThread thread;
                ParseThread(item, &thread);
                snapshot->threads.push_back(std::move(thread));
            }
        } else if (snapshot->dataKind == wxS("breakpoints")) {
            for (const auto& item : items->array) {
                if (item.kind != JsonValue::Kind::Object || BoolField(item, "truncated")) continue;
                CodeBlocksDebugBreakpoint breakpoint;
                ParseBreakpoint(item, &breakpoint);
                snapshot->breakpoints.push_back(std::move(breakpoint));
            }
        }
    }
    const JsonValue* value = Field(root, "item");
    if (value) {
        ParseValue(*value, &snapshot->value, 0);
        snapshot->hasValue = true;
    }
    return true;
}

} // namespace codium
