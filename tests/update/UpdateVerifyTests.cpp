//  Copyright (c) 2026, altqx

//  HikariSub is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  HikariSub is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with HikariSub.  If not, see <http://www.gnu.org/licenses/>.

// The checks the installing side of an update makes (UpdateVerify.h): the
// version it is asked for, and the file against the release's SHA256SUMS.

#include "../check.h"
#include "../../HikariSub/Sha256.h"
#include "../../HikariSub/UpdateVerify.h"

#include <string>

using namespace update_verify;

namespace {

std::string Sha(const std::string &data)
{
	Sha256 hash;
	hash.Update(data.data(), data.size());
	return hash.HexDigest();
}

const std::string kSetup = "HikariSub-0.0.3-windows-x64-setup.exe";

} // namespace

TEST(update_verify_release_versions)
{
	CHECK(IsReleaseVersion("0.0.3"));
	CHECK(IsReleaseVersion("0.1.0-beta.1"));
	CHECK(!IsReleaseVersion(""));
	CHECK(!IsReleaseVersion("v0.0.3"));
	CHECK(!IsReleaseVersion("0.0.3+build.1"));
	CHECK(!IsReleaseVersion("01.0.3"));
	CHECK(!IsReleaseVersion("0.0"));
	CHECK(!IsReleaseVersion("0.0.3/../../x"));
	CHECK(!IsReleaseVersion("0.0.3 "));
	CHECK(!IsReleaseVersion(std::string(70, '1')));

	CHECK(CheckVersion("0.0.3", "0.0.2") == Verdict::Ok);
	CHECK(CheckVersion("0.1.0-beta.1", "0.0.9") == Verdict::Ok);
	// The same version, an older one, and what is not a version are
	// refused, so an older release cannot be put back.
	CHECK(CheckVersion("0.0.2", "0.0.2") == Verdict::BadVersion);
	CHECK(CheckVersion("0.0.1", "0.0.2") == Verdict::BadVersion);
	CHECK(CheckVersion("0.0.3-rc.1", "0.0.3") == Verdict::BadVersion);
	CHECK(CheckVersion("junk", "0.0.2") == Verdict::BadVersion);
}

TEST(update_verify_sums_url_is_built_from_the_version_only)
{
	CHECK(SumsUrl("0.0.3") == "https://github.com/altqx/hikari/releases/download/v0.0.3/SHA256SUMS");
	CHECK(SumsUrl("0.1.0-rc.2") == "https://github.com/altqx/hikari/releases/download/v0.1.0-rc.2/SHA256SUMS");
	CHECK(SumsUrl("0.0.3/../../evil").empty());
	CHECK(SumsUrl("0.0.3?x=1").empty());
	CHECK(NsisAssetName("0.0.3") == kSetup);
}

TEST(update_verify_finds_the_listed_digest)
{
	std::string a(64, 'a'), b(64, 'b');
	std::string sums = a + "  " + kSetup + "\n" + b + "  hikarisub-0.0.3-linux-amd64.deb\n";
	CHECK(FindSum(sums, kSetup) == a);
	CHECK(FindSum(sums, "hikarisub-0.0.3-linux-amd64.deb") == b);
	// Binary mode's "*", and CRLF.
	CHECK(FindSum(a + " *" + kSetup + "\r\n", kSetup) == a);
	// A name that is a prefix or suffix of another is not it.
	CHECK(!FindSum(sums, "HikariSub-0.0.3-windows-x64-setup"));
	CHECK(!FindSum(a + "  x" + kSetup + "\n", kSetup));
	CHECK(!FindSum(a + "  " + kSetup + ".sig\n", kSetup));
	// Listed twice, even with one digest: refused.
	CHECK(!FindSum(sums + a + "  " + kSetup + "\n", kSetup));
	// Not lowercase hex, or one space only.
	CHECK(!FindSum(std::string(64, 'A') + "  " + kSetup + "\n", kSetup));
	CHECK(!FindSum(std::string(63, 'a') + "g  " + kSetup + "\n", kSetup));
	CHECK(!FindSum(a + " " + kSetup + "\n", kSetup));
	CHECK(!FindSum("", kSetup));
	CHECK(!FindSum(sums, ""));
	// A list longer than any release's is not read.
	CHECK(!FindSum(sums + std::string(kMaxSumsBytes, '\n'), kSetup));
}

TEST(update_verify_ignores_what_the_caller_says_the_digest_is)
{
	// The caller's forged installer, and the digest it recorded for it in
	// pending.json: the check takes neither; only the digest the release
	// lists for the release file decides.
	std::string official = "the release's installer";
	std::string forged = "someone else's installer";
	std::string sums = Sha(official) + "  " + kSetup + "\n";
	CHECK(CheckDigest(sums, kSetup, Sha(forged)) == Verdict::Mismatch);
	CHECK(CheckDigest(sums, kSetup, Sha(official)) == Verdict::Ok);
	// The forged file's digest, listed under another asset's name, is not
	// the installer's.
	std::string other = Sha(forged) + "  hikarisub-0.0.3-linux-amd64.deb\n" + sums;
	CHECK(CheckDigest(other, kSetup, Sha(forged)) == Verdict::Mismatch);
	// A release without the file lists nothing for it.
	CHECK(CheckDigest(Sha(forged) + "  hikarisub-0.0.3-linux-amd64.deb\n", kSetup, Sha(forged)) ==
		Verdict::NotListed);
}

TEST(update_verify_nsis_arguments)
{
	CHECK(NsisArguments(4242, true, false, "C:\\Users\\a b\\AppData\\Local\\Programs\\HikariSub") ==
		"/S /UPDATE /WAITPID=4242 /RELAUNCH /CURRENTUSER /D=C:\\Users\\a b\\AppData\\Local\\Programs\\HikariSub");
	CHECK(NsisArguments(7, false, true, "C:\\Program Files\\HikariSub") ==
		"/S /UPDATE /WAITPID=7 /ALLUSERS /D=C:\\Program Files\\HikariSub");
}
