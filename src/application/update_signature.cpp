#include "hikari/application/update_signature.h"

namespace hikari::application {

namespace {

SignatureProblem problemOf(minisign::Refusal refusal)
{
    switch (refusal) {
    case minisign::Refusal::Malformed: return SignatureProblem::Malformed;
    case minisign::Refusal::TooLarge: return SignatureProblem::TooLarge;
    case minisign::Refusal::UnknownAlgorithm: return SignatureProblem::UnknownAlgorithm;
    case minisign::Refusal::UntrustedKey: return SignatureProblem::UntrustedKey;
    case minisign::Refusal::BadSignature: return SignatureProblem::BadSignature;
    case minisign::Refusal::BadTrustedComment: return SignatureProblem::BadTrustedComment;
    }
    return SignatureProblem::Malformed;
}

}

std::vector<minisign::PublicKey> releaseKeys()
{
    std::vector<minisign::PublicKey> keys;
    for (const std::string_view line : kReleaseKeys)
        if (const auto key = minisign::parsePublicKey(line))
            keys.push_back(*key);
    return keys;
}

std::string releaseTrustedComment(std::string_view tag, std::string_view file)
{
    std::string comment = "hikarisub ";
    comment += tag;
    comment += ' ';
    comment += file;
    return comment;
}

std::string_view signatureProblemText(SignatureProblem problem)
{
    switch (problem) {
    case SignatureProblem::Missing: return "the release has no signature";
    case SignatureProblem::OtherRelease: return "the signature is for another release or file";
    case SignatureProblem::TooLarge: return minisign::refusalText(minisign::Refusal::TooLarge);
    case SignatureProblem::Malformed: return minisign::refusalText(minisign::Refusal::Malformed);
    case SignatureProblem::UnknownAlgorithm: return minisign::refusalText(minisign::Refusal::UnknownAlgorithm);
    case SignatureProblem::UntrustedKey: return minisign::refusalText(minisign::Refusal::UntrustedKey);
    case SignatureProblem::BadSignature: return minisign::refusalText(minisign::Refusal::BadSignature);
    case SignatureProblem::BadTrustedComment: return minisign::refusalText(minisign::Refusal::BadTrustedComment);
    }
    return "the signature was refused";
}

std::optional<SignatureProblem> releaseSignatureProblem(std::string_view bytes,
                                                        std::optional<std::string_view> signature,
                                                        std::string_view tag, std::string_view file,
                                                        std::span<const minisign::PublicKey> trusted)
{
    if (!signature)
        return SignatureProblem::Missing;
    const auto verified = minisign::verify(std::as_bytes(std::span(bytes)), *signature, trusted);
    if (!verified)
        return problemOf(verified.error());
    if (verified->trustedComment != releaseTrustedComment(tag, file))
        return SignatureProblem::OtherRelease;
    return std::nullopt;
}

}
