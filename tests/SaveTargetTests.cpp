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
#include "../HikariSub/SaveTarget.h"

#include <sys/stat.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;

// A new empty folder, removed with everything in it when the test ends.
struct ScratchDir {
	fs::path path;
	ScratchDir()
	{
		std::string pattern = (fs::temp_directory_path() / "hikarisub-savetarget-XXXXXX").string();
		if (mkdtemp(pattern.data()))
			path = fs::canonical(pattern);
	}
	~ScratchDir()
	{
		std::error_code ignored;
		if (!path.empty())
			fs::remove_all(path, ignored);
	}
};

void WriteText(const fs::path &file, const std::string &text)
{
	std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

std::string ReadText(const fs::path &file)
{
	std::ifstream in(file, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// What wxTempFile does on a save: write a temporary file next to the saved
// path and rename it over that path.
bool ReplaceLikeTempFile(const fs::path &saved, const std::string &text)
{
	fs::path temp = saved;
	temp += ".hikarisub-tmp";
	WriteText(temp, text);
	return std::rename(temp.c_str(), saved.c_str()) == 0;
}

} // namespace

TEST(a_save_through_a_link_replaces_the_target_and_keeps_the_link)
{
	ScratchDir dir;
	CHECK(!dir.path.empty());
	fs::path target = dir.path / "subs.ass";
	fs::path link = dir.path / "link.ass";
	WriteText(target, "old");
	fs::create_symlink("subs.ass", link);

	SaveTarget resolved;
	CHECK(ResolveSaveTarget(link.c_str(), &resolved));
	CHECK(resolved.path == target.string());

	CHECK(ReplaceLikeTempFile(resolved.path, "new"));
	CHECK(fs::is_symlink(link));
	CHECK(fs::read_symlink(link) == fs::path("subs.ass"));
	CHECK(ReadText(target) == "new");
	CHECK(ReadText(link) == "new");
}

TEST(renaming_over_the_link_itself_loses_the_link_and_the_save)
{
	// The defect the resolution prevents: the save renamed over the link.
	ScratchDir dir;
	fs::path target = dir.path / "subs.ass";
	fs::path link = dir.path / "link.ass";
	WriteText(target, "old");
	fs::create_symlink("subs.ass", link);

	CHECK(ReplaceLikeTempFile(link, "new"));
	CHECK(!fs::is_symlink(link));
	CHECK(ReadText(target) == "old");
}

TEST(a_link_in_another_folder_and_a_chain_of_links_resolve_to_the_last_target)
{
	ScratchDir dir;
	fs::create_directory(dir.path / "real");
	fs::create_directory(dir.path / "links");
	fs::path target = dir.path / "real" / "subs.ass";
	WriteText(target, "old");
	fs::create_symlink("../real/subs.ass", dir.path / "links" / "first.ass");
	fs::create_symlink("first.ass", dir.path / "links" / "second.ass");

	SaveTarget resolved;
	CHECK(ResolveSaveTarget((dir.path / "links" / "second.ass").c_str(), &resolved));
	CHECK(resolved.path == target.string());
}

TEST(a_link_that_leads_to_no_file_is_refused)
{
	ScratchDir dir;
	fs::path link = dir.path / "dangling.ass";
	fs::create_symlink("missing.ass", link);

	SaveTarget resolved;
	resolved.path = "stale";
	CHECK(!ResolveSaveTarget(link.c_str(), &resolved));
	CHECK(resolved.path.empty());
	CHECK(fs::is_symlink(link));
	CHECK(!fs::exists(dir.path / "missing.ass"));
}

TEST(a_new_file_is_saved_under_its_own_name_in_its_resolved_folder)
{
	ScratchDir dir;
	SaveTarget resolved;
	CHECK(ResolveSaveTarget((dir.path / "new.ass").c_str(), &resolved));
	CHECK(resolved.path == (dir.path / "new.ass").string());
	CHECK(!resolved.exists);
	struct stat folder {};
	CHECK(stat(dir.path.c_str(), &folder) == 0);
	CHECK(resolved.folderDevice == folder.st_dev && resolved.folderInode == folder.st_ino);

	// through a link to its folder, which the path saved to has no more
	fs::create_directory(dir.path / "real");
	fs::create_directory_symlink("real", dir.path / "linked");
	CHECK(ResolveSaveTarget((dir.path / "linked" / "new.ass").c_str(), &resolved));
	CHECK(resolved.path == (dir.path / "real" / "new.ass").string());
	CHECK(stat((dir.path / "real").c_str(), &folder) == 0);
	CHECK(resolved.folderDevice == folder.st_dev && resolved.folderInode == folder.st_ino);
}

TEST(a_resolved_file_notes_its_folder_and_itself)
{
	ScratchDir dir;
	fs::path file = dir.path / "subs.ass";
	WriteText(file, "old");
	SaveTarget resolved;
	CHECK(ResolveSaveTarget(file.c_str(), &resolved));
	struct stat folder {}, target {};
	CHECK(stat(dir.path.c_str(), &folder) == 0 && stat(file.c_str(), &target) == 0);
	CHECK(resolved.exists);
	CHECK(resolved.folderDevice == folder.st_dev && resolved.folderInode == folder.st_ino);
	CHECK(resolved.device == target.st_dev && resolved.inode == target.st_ino);
}

TEST(a_folder_link_changed_while_a_new_file_is_resolved_is_refused)
{
	ScratchDir dir, victimDir;
	fs::create_directory(dir.path / "real");
	fs::path folder = dir.path / "subs";
	fs::create_directory_symlink(dir.path / "real", folder);

	// realpath follows the folder link, then the link leads elsewhere
	SaveTarget resolved;
	CHECK(!ResolveSaveTarget((folder / "new.ass").c_str(), &resolved, [&] {
		fs::remove(folder);
		fs::create_directory_symlink(victimDir.path, folder);
	}));
	CHECK(resolved.path.empty());

	// a plain folder that becomes a link
	fs::remove(folder);
	fs::create_directory(folder);
	CHECK(!ResolveSaveTarget((folder / "new.ass").c_str(), &resolved, [&] {
		fs::rename(folder, dir.path / "moved");
		fs::create_directory_symlink(victimDir.path, folder);
	}));
	CHECK(resolved.path.empty());
	CHECK(fs::is_empty(victimDir.path));
}

TEST(a_plain_file_resolves_to_itself)
{
	ScratchDir dir;
	fs::path file = dir.path / "subs.ass";
	WriteText(file, "old");

	SaveTarget resolved;
	CHECK(ResolveSaveTarget(file.c_str(), &resolved));
	CHECK(resolved.path == file.string());
}

TEST(a_link_changed_while_it_is_resolved_is_refused)
{
	ScratchDir dir, victimDir;
	fs::path victim = victimDir.path / "victim.txt";
	WriteText(victim, "the victim's data");
	fs::path link = dir.path / "subs.ass";

	// realpath follows a link to the victim, then the link becomes a plain
	// file: the kernel's open, which makes the protected_symlinks check,
	// never sees the link the resolved path came from
	fs::create_symlink(victim, link);
	SaveTarget resolved;
	resolved.path = "stale";
	CHECK(!ResolveSaveTarget(link.c_str(), &resolved, [&] {
		fs::remove(link);
		WriteText(link, "a plain file");
	}));
	CHECK(resolved.path.empty());

	// a plain file that becomes a link to the victim
	fs::remove(link);
	WriteText(link, "a plain file");
	CHECK(!ResolveSaveTarget(link.c_str(), &resolved, [&] {
		fs::remove(link);
		fs::create_symlink(victim, link);
	}));
	CHECK(resolved.path.empty());
	CHECK(ReadText(victim) == "the victim's data");

	// a link that stays as it was resolves
	CHECK(ResolveSaveTarget(link.c_str(), &resolved, [] {}));
	CHECK(resolved.path == victim.string());
}

#endif
