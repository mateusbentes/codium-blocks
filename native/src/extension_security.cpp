// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/extension_security.hpp"

#include <wx/filename.h>
#include <wx/file.h>
#include <wx/wfstream.h>

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace codium {

namespace {

class Sha256 final {
public:
    Sha256()
    {
        state_ = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                  0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    }

    void Update(const unsigned char* data, size_t length)
    {
        for (size_t index = 0; index < length; ++index) {
            block_[blockLength_++] = data[index];
            if (blockLength_ == 64) {
                Transform();
                bitLength_ += 512;
                blockLength_ = 0;
            }
        }
    }

    std::array<unsigned char, 32> Final()
    {
        size_t index = blockLength_;
        block_[index++] = 0x80;
        if (index > 56) {
            while (index < 64) block_[index++] = 0;
            Transform();
            index = 0;
        }
        while (index < 56) block_[index++] = 0;
        bitLength_ += static_cast<uint64_t>(blockLength_) * 8;
        for (int shift = 7; shift >= 0; --shift) block_[56 + (7 - shift)] = static_cast<unsigned char>((bitLength_ >> (shift * 8)) & 0xff);
        Transform();

        std::array<unsigned char, 32> output{};
        for (size_t word = 0; word < state_.size(); ++word) {
            output[word * 4] = static_cast<unsigned char>((state_[word] >> 24) & 0xff);
            output[word * 4 + 1] = static_cast<unsigned char>((state_[word] >> 16) & 0xff);
            output[word * 4 + 2] = static_cast<unsigned char>((state_[word] >> 8) & 0xff);
            output[word * 4 + 3] = static_cast<unsigned char>(state_[word] & 0xff);
        }
        return output;
    }

private:
    static uint32_t RotateRight(uint32_t value, int count) { return (value >> count) | (value << (32 - count)); }
    static uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
    static uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
    static uint32_t BigSigma0(uint32_t x) { return RotateRight(x, 2) ^ RotateRight(x, 13) ^ RotateRight(x, 22); }
    static uint32_t BigSigma1(uint32_t x) { return RotateRight(x, 6) ^ RotateRight(x, 11) ^ RotateRight(x, 25); }
    static uint32_t SmallSigma0(uint32_t x) { return RotateRight(x, 7) ^ RotateRight(x, 18) ^ (x >> 3); }
    static uint32_t SmallSigma1(uint32_t x) { return RotateRight(x, 17) ^ RotateRight(x, 19) ^ (x >> 10); }

    void Transform()
    {
        static constexpr std::array<uint32_t, 64> constants = {
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
            0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
            0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
            0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
            0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
            0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};
        std::array<uint32_t, 64> schedule{};
        for (size_t index = 0; index < 16; ++index) {
            const size_t offset = index * 4;
            schedule[index] = (static_cast<uint32_t>(block_[offset]) << 24) |
                              (static_cast<uint32_t>(block_[offset + 1]) << 16) |
                              (static_cast<uint32_t>(block_[offset + 2]) << 8) |
                              static_cast<uint32_t>(block_[offset + 3]);
        }
        for (size_t index = 16; index < schedule.size(); ++index) schedule[index] = SmallSigma1(schedule[index - 2]) + schedule[index - 7] + SmallSigma0(schedule[index - 15]) + schedule[index - 16];
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (size_t index = 0; index < schedule.size(); ++index) {
            const uint32_t t1 = h + BigSigma1(e) + Ch(e, f, g) + constants[index] + schedule[index];
            const uint32_t t2 = BigSigma0(a) + Maj(a, b, c);
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    std::array<uint32_t, 8> state_{};
    std::array<unsigned char, 64> block_{};
    size_t blockLength_ = 0;
    uint64_t bitLength_ = 0;
};

wxString JsonField(const wxString& json, const wxString& field)
{
    const wxString marker = wxString::Format(wxS("\"%s\":"), field);
    const int markerStart = json.Find(marker);
    if (markerStart == wxNOT_FOUND) return wxEmptyString;
    int start = markerStart + static_cast<int>(marker.length());
    while (start < static_cast<int>(json.length()) && (json[start] == wxChar(' ') || json[start] == wxChar('\t'))) ++start;
    if (start >= static_cast<int>(json.length()) || json[start] != wxChar('"')) return wxEmptyString;
    ++start;
    wxString result;
    bool escaped = false;
    for (int index = start; index < static_cast<int>(json.length()); ++index) {
        const wxChar character = json[index];
        if (character == wxChar('"') && !escaped) break;
        if (escaped) {
            if (character == wxChar('n')) result += wxChar('\n');
            else if (character == wxChar('r')) result += wxChar('\r');
            else if (character == wxChar('t')) result += wxChar('\t');
            else result += character;
            escaped = false;
        } else if (character == wxChar('\\')) escaped = true;
        else result += character;
    }
    return result;
}

bool ValidToken(const wxString& value, bool allowDash)
{
    if (value.empty()) return false;
    for (const auto character : value) {
        const bool ok = (character >= wxChar('a') && character <= wxChar('z')) ||
                        (character >= wxChar('A') && character <= wxChar('Z')) ||
                        (character >= wxChar('0') && character <= wxChar('9')) || character == wxChar('_') ||
                        (allowDash && character == wxChar('-')) || character == wxChar('.');
        if (!ok) return false;
    }
    return true;
}

} // namespace

bool ExtensionSecurity::ComputeSha256(const wxString& path, wxString* digest, wxString* error)
{
    wxFFileInputStream input(path);
    if (!input.IsOk()) {
        if (error) *error = wxString::Format(wxS("Could not open file for SHA-256: %s."), path);
        return false;
    }
    Sha256 sha;
    std::array<unsigned char, 8192> buffer{};
    while (input.CanRead()) {
        input.Read(buffer.data(), buffer.size());
        const size_t count = input.LastRead();
        if (count == 0) break;
        sha.Update(buffer.data(), count);
    }
    if (!input.IsOk() && input.GetLastError() != wxSTREAM_EOF) {
        if (error) *error = wxS("Could not read file while computing SHA-256.");
        return false;
    }
    const auto bytes = sha.Final();
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes) output << std::setw(2) << static_cast<unsigned int>(byte);
    if (digest) *digest = wxString::FromUTF8(output.str());
    return true;
}

bool ExtensionSecurity::ValidateManifest(const wxString& json, ExtensionManifest* manifest, wxString* error)
{
    ExtensionManifest candidate;
    candidate.name = JsonField(json, wxS("name"));
    candidate.publisher = JsonField(json, wxS("publisher"));
    candidate.version = JsonField(json, wxS("version"));
    candidate.engine = JsonField(json, wxS("vscode"));
    if (!ValidToken(candidate.name, true) || !ValidToken(candidate.publisher, true) || !ValidToken(candidate.version, true)) {
        if (error) *error = wxS("Manifest requires safe name, publisher, and version fields.");
        return false;
    }
    if (candidate.name == wxS(".") || candidate.name == wxS("..") || candidate.name.Contains(wxS(".."))) {
        if (error) *error = wxS("Manifest name contains an unsafe path component.");
        return false;
    }
    if (manifest) *manifest = candidate;
    return true;
}

bool ExtensionSecurity::IsAllowedRegistryUrl(const wxString& url)
{
    return url.StartsWith(wxS("https://")) && !url.Contains(wxS("@")) && !url.Contains(wxS("\\"));
}

bool ExtensionSecurity::IsAllowedScheme(const wxString& url)
{
    return url.StartsWith(wxS("https://")) || url.StartsWith(wxS("http://"));
}

} // namespace codium
