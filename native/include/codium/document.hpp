// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include <wx/string.h>

namespace codium {

class Document final {
public:
    Document() = default;
    explicit Document(wxString path);

    bool Load(const wxString& path, wxString* error = nullptr);
    bool Save(wxString* error = nullptr) const;

    void SetText(wxString text);
    void MarkClean();

    const wxString& Path() const { return path_; }
    const wxString& Text() const { return text_; }
    bool IsDirty() const { return dirty_; }
    bool IsUntitled() const { return path_.empty(); }

private:
    wxString path_;
    wxString text_;
    bool dirty_ = false;
};

} // namespace codium
