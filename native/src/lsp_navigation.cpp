// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/lsp_navigation.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <map>
#include <string>
#include <utility>

namespace codium {
namespace {

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
    explicit JsonParser(std::string input) : input_(std::move(input)) {}

    bool Parse(JsonValue* value)
    {
        SkipWhitespace();
        if (!ParseValue(value)) return false;
        SkipWhitespace();
        return position_ == input_.size();
    }

private:
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

    bool ParseValue(JsonValue* value)
    {
        SkipWhitespace();
        if (position_ >= input_.size()) return false;
        switch (input_[position_]) {
        case '{': return ParseObject(value);
        case '[': return ParseArray(value);
        case '"':
            value->kind = JsonValue::Kind::String;
            return ParseString(&value->string);
        case 't': return ParseLiteral("true", JsonValue::Kind::Boolean, true, value);
        case 'f': return ParseLiteral("false", JsonValue::Kind::Boolean, false, value);
        case 'n':
            if (input_.compare(position_, 4, "null") != 0) return false;
            position_ += 4;
            value->kind = JsonValue::Kind::Null;
            return true;
        default: return ParseNumber(value);
        }
    }

    bool ParseObject(JsonValue* value)
    {
        if (!Consume('{')) return false;
        value->kind = JsonValue::Kind::Object;
        value->object.clear();
        SkipWhitespace();
        if (Consume('}')) return true;
        while (position_ < input_.size()) {
            std::string key;
            if (!ParseString(&key) || !Consume(':')) return false;
            JsonValue child;
            if (!ParseValue(&child)) return false;
            value->object[std::move(key)] = std::move(child);
            if (Consume('}')) return true;
            if (!Consume(',')) return false;
        }
        return false;
    }

    bool ParseArray(JsonValue* value)
    {
        if (!Consume('[')) return false;
        value->kind = JsonValue::Kind::Array;
        value->array.clear();
        SkipWhitespace();
        if (Consume(']')) return true;
        while (position_ < input_.size()) {
            JsonValue child;
            if (!ParseValue(&child)) return false;
            value->array.push_back(std::move(child));
            if (Consume(']')) return true;
            if (!Consume(',')) return false;
        }
        return false;
    }

    bool ParseString(std::string* value)
    {
        SkipWhitespace();
        if (position_ >= input_.size() || input_[position_] != '"') return false;
        ++position_;
        value->clear();
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') return true;
            if (character < 0x20) return false;
            if (character != '\\') {
                value->push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) return false;
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
                if (position_ + 4 > input_.size()) return false;
                unsigned int codepoint = 0;
                for (int index = 0; index < 4; ++index) {
                    const char hex = input_[position_++];
                    codepoint <<= 4;
                    if (hex >= '0' && hex <= '9') codepoint |= static_cast<unsigned int>(hex - '0');
                    else if (hex >= 'a' && hex <= 'f') codepoint |= static_cast<unsigned int>(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') codepoint |= static_cast<unsigned int>(hex - 'A' + 10);
                    else return false;
                }
                AppendUtf8(value, codepoint);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool ParseLiteral(const char* literal, JsonValue::Kind kind, bool boolean, JsonValue* value)
    {
        const std::string expected(literal);
        if (input_.compare(position_, expected.size(), expected) != 0) return false;
        position_ += expected.size();
        value->kind = kind;
        value->boolean = boolean;
        return true;
    }

    bool ParseNumber(JsonValue* value)
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
        if (start == position_) return false;
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
    return value && value->kind == JsonValue::Kind::String ? wxString::FromUTF8(value->string) : wxString();
}

int IntField(const JsonValue& object, const char* name, int fallback = 0)
{
    const JsonValue* value = Field(object, name);
    if (!value || value->kind != JsonValue::Kind::Number ||
        value->number < std::numeric_limits<int>::min() || value->number > std::numeric_limits<int>::max()) return fallback;
    return static_cast<int>(value->number);
}

const JsonValue* ResultValue(const JsonValue& root)
{
    return Field(root, "result");
}

bool ParsePosition(const JsonValue* value, LspPosition* position)
{
    if (!value || value->kind != JsonValue::Kind::Object || !position) return false;
    position->line = std::max(0, IntField(*value, "line"));
    position->character = std::max(0, IntField(*value, "character"));
    return Field(*value, "line") != nullptr || Field(*value, "character") != nullptr;
}

bool ParseRange(const JsonValue* value, LspRange* range)
{
    if (!value || value->kind != JsonValue::Kind::Object || !range) return false;
    return ParsePosition(Field(*value, "start"), &range->start) &&
           ParsePosition(Field(*value, "end"), &range->end);
}

bool ParseLocation(const JsonValue& value, LspLocation* location, bool symbol)
{
    if (value.kind != JsonValue::Kind::Object || !location) return false;
    location->uri = StringField(value, "uri");
    if (location->uri.empty()) location->uri = StringField(value, "targetUri");
    location->name = StringField(value, "name");
    location->detail = StringField(value, "detail");
    location->kind = IntField(value, "kind");
    const JsonValue* range = Field(value, "range");
    if (!range) range = Field(value, "selectionRange");
    if (!range) range = Field(value, "targetSelectionRange");
    if (!range) range = Field(value, "targetRange");
    if (!ParseRange(range, &location->range)) return false;
    if (location->uri.empty() && !symbol) return false;
    return true;
}

void AppendLocations(const JsonValue& value, std::vector<LspLocation>* locations, bool symbols)
{
    if (value.kind == JsonValue::Kind::Array) {
        for (const auto& child : value.array) AppendLocations(child, locations, symbols);
        return;
    }
    LspLocation location;
    if (ParseLocation(value, &location, symbols)) locations->push_back(location);
    if (symbols) {
        const JsonValue* children = Field(value, "children");
        if (children) AppendLocations(*children, locations, true);
    }
}

bool ParseTextEdit(const JsonValue& value, const wxString& uri, LspTextEdit* edit)
{
    if (value.kind != JsonValue::Kind::Object || !edit) return false;
    edit->uri = uri.empty() ? StringField(value, "uri") : uri;
    edit->newText = StringField(value, "newText");
    return ParseRange(Field(value, "range"), &edit->range);
}

void AppendChanges(const JsonValue& changes, std::vector<LspTextEdit>* edits)
{
    if (changes.kind != JsonValue::Kind::Object) return;
    for (const auto& [uri, value] : changes.object) {
        if (value.kind != JsonValue::Kind::Array) continue;
        for (const auto& rawEdit : value.array) {
            LspTextEdit edit;
            if (ParseTextEdit(rawEdit, wxString::FromUTF8(uri), &edit)) edits->push_back(edit);
        }
    }
}

void AppendDocumentChanges(const JsonValue& documentChanges, std::vector<LspTextEdit>* edits)
{
    if (documentChanges.kind != JsonValue::Kind::Array) return;
    for (const auto& documentChange : documentChanges.array) {
        if (documentChange.kind != JsonValue::Kind::Object) continue;
        wxString uri = StringField(documentChange, "uri");
        if (uri.empty()) {
            const JsonValue* textDocument = Field(documentChange, "textDocument");
            if (textDocument) uri = StringField(*textDocument, "uri");
        }
        const JsonValue* rawEdits = Field(documentChange, "edits");
        if (!rawEdits || rawEdits->kind != JsonValue::Kind::Array) continue;
        for (const auto& rawEdit : rawEdits->array) {
            LspTextEdit edit;
            if (ParseTextEdit(rawEdit, uri, &edit)) edits->push_back(edit);
        }
    }
}

wxString DisplayValue(const JsonValue& value)
{
    if (value.kind == JsonValue::Kind::String) return wxString::FromUTF8(value.string);
    if (value.kind != JsonValue::Kind::Object) return wxString();
    const wxString language = StringField(value, "language");
    const wxString text = StringField(value, "value");
    if (text.empty()) return StringField(value, "kind");
    return language.empty() ? text : wxString::Format(wxS("```%s\n%s\n```"), language, text);
}

bool ParseRoot(const wxString& responseLine, JsonValue* root)
{
    if (!root) return false;
    const wxScopedCharBuffer utf8 = responseLine.utf8_str();
    JsonParser parser(std::string(utf8.data() ? utf8.data() : ""));
    return parser.Parse(root);
}

} // namespace

std::vector<LspLocation> LspNavigation::LocationsFromResult(const wxString& responseLine, bool includeSymbols)
{
    JsonValue root;
    if (!ParseRoot(responseLine, &root)) return {};
    const JsonValue* result = ResultValue(root);
    if (!result) return {};
    std::vector<LspLocation> locations;
    AppendLocations(*result, &locations, includeSymbols);
    return locations;
}

std::vector<LspCompletionItem> LspNavigation::CompletionItemsFromResult(const wxString& responseLine)
{
    JsonValue root;
    if (!ParseRoot(responseLine, &root)) return {};
    const JsonValue* result = ResultValue(root);
    if (!result) return {};
    const JsonValue* items = result->kind == JsonValue::Kind::Array ? result : Field(*result, "items");
    if (!items || items->kind != JsonValue::Kind::Array) return {};
    std::vector<LspCompletionItem> output;
    for (const auto& value : items->array) {
        if (value.kind != JsonValue::Kind::Object) continue;
        LspCompletionItem item;
        item.label = StringField(value, "label");
        item.detail = StringField(value, "detail");
        item.insertText = StringField(value, "insertText");
        const JsonValue* documentation = Field(value, "documentation");
        if (documentation) item.documentation = DisplayValue(*documentation);
        const JsonValue* textEdit = Field(value, "textEdit");
        if (textEdit && ParseTextEdit(*textEdit, wxString(), &item.textEdit)) item.hasTextEdit = true;
        if (!item.label.empty()) output.push_back(item);
    }
    return output;
}

std::vector<LspCodeAction> LspNavigation::CodeActionsFromResult(const wxString& responseLine)
{
    JsonValue root;
    if (!ParseRoot(responseLine, &root)) return {};
    const JsonValue* result = ResultValue(root);
    if (!result || result->kind != JsonValue::Kind::Array) return {};
    std::vector<LspCodeAction> output;
    for (const auto& value : result->array) {
        if (value.kind != JsonValue::Kind::Object) continue;
        LspCodeAction action;
        action.title = StringField(value, "title");
        action.kind = StringField(value, "kind");
        const JsonValue* edit = Field(value, "edit");
        const JsonValue* changes = edit && edit->kind == JsonValue::Kind::Object
            ? Field(*edit, "changes") : nullptr;
        if (changes) AppendChanges(*changes, &action.edits);
        const JsonValue* documentChanges = edit && edit->kind == JsonValue::Kind::Object
            ? Field(*edit, "documentChanges") : nullptr;
        if (documentChanges) AppendDocumentChanges(*documentChanges, &action.edits);
        if (!action.title.empty()) output.push_back(action);
    }
    return output;
}

std::vector<LspTextEdit> LspNavigation::WorkspaceEditsFromResult(const wxString& responseLine)
{
    JsonValue root;
    if (!ParseRoot(responseLine, &root)) return {};
    const JsonValue* result = ResultValue(root);
    if (!result || result->kind != JsonValue::Kind::Object) return {};
    std::vector<LspTextEdit> edits;
    const JsonValue* changes = Field(*result, "changes");
    if (changes) AppendChanges(*changes, &edits);
    const JsonValue* documentChanges = Field(*result, "documentChanges");
    if (documentChanges) AppendDocumentChanges(*documentChanges, &edits);
    return edits;
}

wxString LspNavigation::HoverTextFromResult(const wxString& responseLine)
{
    JsonValue root;
    if (!ParseRoot(responseLine, &root)) return wxEmptyString;
    const JsonValue* result = ResultValue(root);
    if (!result || result->kind != JsonValue::Kind::Object) return wxEmptyString;
    const JsonValue* contents = Field(*result, "contents");
    wxString output;
    if (contents && contents->kind == JsonValue::Kind::Array) {
        for (const auto& item : contents->array) {
            const wxString part = DisplayValue(item);
            if (part.empty()) continue;
            if (!output.empty()) output += wxS("\n\n");
            output += part;
        }
    } else if (contents) {
        output = DisplayValue(*contents);
    }
    const wxString rangeText = Field(*result, "range") ? wxS("\n\n[range available]") : wxString();
    return output + rangeText;
}

wxString LspNavigation::UriToPath(const wxString& uri)
{
    if (!uri.StartsWith(wxS("file://"))) return uri;
    wxString path = uri.Mid(7);
    path.Replace(wxS("%20"), wxS(" "));
    path.Replace(wxS("%23"), wxS("#"));
    path.Replace(wxS("%25"), wxS("%"));
#if defined(_WIN32)
    if (path.StartsWith(wxS("/")) && path.length() > 2 && path[2] == wxChar(':')) path = path.Mid(1);
#endif
    return path;
}

long LspNavigation::OffsetForPosition(const wxString& text, const LspPosition& position)
{
    if (position.line < 0 || position.character < 0) return -1;
    long offset = 0;
    int line = 0;
    while (line < position.line) {
        const int breakAt = text.Mid(offset).Find(wxChar('\n'));
        if (breakAt == wxNOT_FOUND) return -1;
        offset += breakAt + 1;
        ++line;
    }
    return std::min<long>(static_cast<long>(text.length()), offset + position.character);
}

wxString LspNavigation::ApplyTextEdits(const wxString& text, const std::vector<LspTextEdit>& edits)
{
    struct ResolvedEdit final { long start; long end; wxString replacement; };
    std::vector<ResolvedEdit> resolved;
    for (const auto& edit : edits) {
        const long start = OffsetForPosition(text, edit.range.start);
        const long end = OffsetForPosition(text, edit.range.end);
        if (start >= 0 && end >= start) resolved.push_back({start, end, edit.newText});
    }
    std::sort(resolved.begin(), resolved.end(), [](const auto& left, const auto& right) {
        return left.start > right.start;
    });
    wxString result = text;
    for (const auto& edit : resolved) result.replace(edit.start, edit.end - edit.start, edit.replacement);
    return result;
}

} // namespace codium
