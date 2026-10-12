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

#ifndef _WIN32

#include "check.h"
#include "../HikariSub/PrivateFolder.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;

// A new empty folder, removed with everything in it when the test ends.
struct PrivateScratchDir {
	fs::path path;
	PrivateScratchDir()
	{
		std::string pattern = (fs::temp_directory_path() / "hikarisub-private-XXXXXX").string();
		if (mkdtemp(pattern.data()))
			path = fs::canonical(pattern);
	}
	~PrivateScratchDir()
	{
		std::error_code ignored;
		if (!path.empty())
			fs::remove_all(path, ignored);
	}
};

mode_t ModeOf(const fs::path &path)
{
	struct stat st {};
	lstat(path.c_str(), &st);
	return st.st_mode & 07777;
}

std::string ReadAll(const fs::path &file)
{
	std::ifstream in(file, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST(a_private_folder_is_made_0700_and_one_there_is_narrowed)
{
	PrivateScratchDir dir;
	CHECK(!dir.path.empty());
	mode_t mask = umask(022);
	fs::path made = dir.path / "Subs";
	CHECK(MakePrivateFolderAt(made.string() + "/"));
	CHECK(fs::is_directory(made));
	CHECK(ModeOf(made) == 0700);

	fs::path older = dir.path / "Recovery";
	fs::create_directory(older);
	CHECK(chmod(older.c_str(), 0755) == 0);
	CHECK(MakePrivateFolderAt(older.string()));
	CHECK(ModeOf(older) == 0700);
	umask(mask);
}

TEST(a_link_at_a_private_folder_s_name_is_left_as_it_is)
{
	PrivateScratchDir dir;
	fs::path shared = dir.path / "shared";
	fs::create_directory(shared);
	CHECK(chmod(shared.c_str(), 0755) == 0);
	fs::path link = dir.path / "AudioCache";
	fs::create_directory_symlink(shared, link);
	CHECK(!MakePrivateFolderAt(link.string()));
	CHECK(ModeOf(shared) == 0755);
	CHECK(fs::is_symlink(link));
}

TEST(a_private_file_is_made_0600_and_one_there_is_narrowed_before_it_is_emptied)
{
	PrivateScratchDir dir;
	mode_t mask = umask(022);
	fs::path cache = dir.path / "media.w64.part";
	FILE *made = OpenPrivateFileAt(cache.string());
	CHECK(made != nullptr);
	if (made) {
		CHECK(fwrite("audio", 1, 5, made) == 5);
		fclose(made);
	}
	CHECK(ModeOf(cache) == 0600);
	CHECK(ReadAll(cache) == "audio");

	CHECK(chmod(cache.c_str(), 0644) == 0);
	FILE *again = OpenPrivateFileAt(cache.string());
	CHECK(again != nullptr);
	if (again)
		fclose(again);
	CHECK(ModeOf(cache) == 0600);
	CHECK(ReadAll(cache).empty());
	umask(mask);
}

TEST(a_link_at_a_private_file_s_name_is_not_followed)
{
	PrivateScratchDir dir;
	fs::path elsewhere = dir.path / "elsewhere";
	std::ofstream(elsewhere) << "kept";
	CHECK(chmod(elsewhere.c_str(), 0644) == 0);
	fs::path link = dir.path / "media.w64.part";
	fs::create_symlink(elsewhere, link);
	CHECK(OpenPrivateFileAt(link.string()) == nullptr);
	CHECK(ReadAll(elsewhere) == "kept");
	CHECK(ModeOf(elsewhere) == 0644);

	fs::path folder = dir.path / "folder.part";
	fs::create_directory(folder);
	CHECK(OpenPrivateFileAt(folder.string()) == nullptr);
}

#endif
