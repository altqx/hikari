#pragma once

// Release signatures (ADR 0027). Qt-free: the release job compiles this check alone
// (tools/release/verify_release_signature.cpp).

#include "hikari/minisign/minisign.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

inline constexpr std::string_view kManifestSignatureAsset = "hikarisub-update.json.minisig";

// The base64 lines of the owner's minisign public keys. A rotation ships the next key here one release before
// signing switches to it.
inline constexpr std::array<std::string_view, 1> kReleaseKeys{
    "RWSZidnvIHp8qW7ynpUV8iDc/FH3riGpLuA+tz6zSeqwE3Q9Y+jmY5TH", // key id A97C7A20EFD98999
};
std::vector<minisign::PublicKey> releaseKeys();

// "hikarisub <tag> <file>": the trusted comment the release job signs `file` with, so that a signature can't be
// moved to another release or file.
std::string releaseTrustedComment(std::string_view tag, std::string_view file);

enum class SignatureProblem {
    Missing,
    TooLarge,
    Malformed,
    UnknownAlgorithm,
    UntrustedKey,
    BadSignature,
    BadTrustedComment,
    OtherRelease, // a good signature whose trusted comment names another release or file
};
std::string_view signatureProblemText(SignatureProblem problem);

// nullopt when `signature`, the text of `file`'s .minisig (nullopt: the release has none), verifies `bytes` with a
// trusted key and its trusted comment is releaseTrustedComment(tag, file).
std::optional<SignatureProblem> releaseSignatureProblem(std::string_view bytes,
                                                        std::optional<std::string_view> signature,
                                                        std::string_view tag, std::string_view file,
                                                        std::span<const minisign::PublicKey> trusted);

}
