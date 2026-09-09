#include "codium/signature_verifier.hpp"

#include <wx/file.h>
#include <wx/wfstream.h>

#include <vector>

#if defined(CODIUM_BLOCKS_HAVE_OPENSSL)
#include <openssl/evp.h>
#endif

namespace codium {

namespace {

bool HexDecode(const wxString& input, std::vector<unsigned char>* output)
{
    if (input.length() % 2 != 0) return false;
    auto value = [](wxChar character) -> int {
        if (character >= wxChar('0') && character <= wxChar('9')) return character - wxChar('0');
        if (character >= wxChar('a') && character <= wxChar('f')) return character - wxChar('a') + 10;
        if (character >= wxChar('A') && character <= wxChar('F')) return character - wxChar('A') + 10;
        return -1;
    };
    output->clear();
    for (size_t index = 0; index < input.length(); index += 2) {
        const int high = value(input[index]);
        const int low = value(input[index + 1]);
        if (high < 0 || low < 0) return false;
        output->push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return true;
}

bool ReadFile(const wxString& path, std::vector<unsigned char>* data)
{
    wxFile file;
    if (!file.Open(path, wxFile::read)) return false;
    const wxFileOffset length = file.Length();
    if (length < 0) return false;
    data->resize(static_cast<size_t>(length));
    return length == 0 || file.Read(data->data(), data->size()) == length;
}

} // namespace

bool SignatureVerifier::VerifyEd25519File(const wxString& path, const wxString& publicKeyHex,
                                          const wxString& signatureHex, wxString* error)
{
#if !defined(CODIUM_BLOCKS_HAVE_OPENSSL)
    if (error) *error = wxS("Ed25519 verification is unavailable because OpenSSL was not found at build time.");
    return false;
#else
    std::vector<unsigned char> publicKey;
    std::vector<unsigned char> signature;
    std::vector<unsigned char> data;
    if (!HexDecode(publicKeyHex, &publicKey) || publicKey.size() != 32 ||
        !HexDecode(signatureHex, &signature) || signature.size() != 64 || !ReadFile(path, &data)) {
        if (error) *error = wxS("Invalid Ed25519 key/signature encoding or unreadable artifact.");
        return false;
    }

    EVP_PKEY* key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, publicKey.data(), publicKey.size());
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    const bool initialized = key != nullptr && context != nullptr &&
        EVP_DigestVerifyInit(context, nullptr, nullptr, nullptr, key) == 1;
    const bool verified = initialized && EVP_DigestVerify(context, signature.data(), signature.size(), data.data(), data.size()) == 1;
    if (context) EVP_MD_CTX_free(context);
    if (key) EVP_PKEY_free(key);
    if (!verified && error) *error = wxS("Ed25519 signature verification failed.");
    return verified;
#endif
}

} // namespace codium
