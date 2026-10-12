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
#include "../HikariSub/SaveFile.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <thread>
#ifdef __linux__
#include <sys/xattr.h>
#endif

namespace {

namespace fs = std::filesystem;

// A new empty folder, removed with everything in it when the test ends.
struct SaveScratchDir {
	fs::path path;
	SaveScratchDir()
	{
		std::string pattern = (fs::temp_directory_path() / "hikarisub-savefile-XXXXXX").string();
		if (mkdtemp(pattern.data()))
			path = fs::canonical(pattern);
	}
	~SaveScratchDir()
	{
		std::error_code ignored;
		if (path.empty())
			return;
		for (auto &entry : fs::recursive_directory_iterator(path, ignored))
			if (entry.is_directory(ignored))
				fs::permissions(entry.path(), fs::perms::owner_all, fs::perm_options::add, ignored);
		fs::permissions(path, fs::perms::owner_all, fs::perm_options::add, ignored);
		fs::remove_all(path, ignored);
	}
};

// Points TMPDIR at a folder for the length of a test.
struct TempFolderOverride {
	std::string old;
	bool had;
	explicit TempFolderOverride(const fs::path &folder)
	{
		const char *value = getenv("TMPDIR");
		had = value != nullptr;
		if (had)
			old = value;
		setenv("TMPDIR", folder.c_str(), 1);
	}
	~TempFolderOverride()
	{
		if (had)
			setenv("TMPDIR", old.c_str(), 1);
		else
			unsetenv("TMPDIR");
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

struct stat StatOf(const fs::path &file)
{
	struct stat st {};
	stat(file.c_str(), &st);
	return st;
}

std::set<std::string> Names(const fs::path &folder)
{
	std::set<std::string> names;
	for (auto &entry : fs::directory_iterator(folder))
		names.insert(entry.path().filename().string());
	return names;
}

bool Save(SaveFile &file, const fs::path &target, const std::string &text)
{
	return file.Open(target.string()) && file.Write(text.data(), text.size()) && file.Commit();
}

// A target writer that writes a few bytes and then fails as a full disk does.
int bytesBeforeFailure = 0;
ssize_t FailingWriter(int fd, const void *data, size_t size)
{
	if (bytesBeforeFailure <= 0) {
		errno = ENOSPC;
		return -1;
	}
	size_t now = size < static_cast<size_t>(bytesBeforeFailure) ? size : static_cast<size_t>(bytesBeforeFailure);
	ssize_t written = write(fd, data, now);
	if (written > 0)
		bytesBeforeFailure -= static_cast<int>(written);
	return written;
}

SaveTargetFacts PlainFile()
{
	SaveTargetFacts facts;
	facts.exists = true;
	facts.type = S_IFREG;
	facts.links = 1;
	facts.uid = facts.tempUid = 1000;
	facts.gid = facts.tempGid = 1000;
	facts.mode = facts.tempMode = 0644;
	facts.device = facts.tempDevice = 42;
	facts.tempMade = true;
	return facts;
}

} // namespace

TEST(a_plain_file_is_replaced_by_a_rename)
{
	CHECK(ChooseSaveMethod(PlainFile()) == SaveMethod::Replace);
}

TEST(a_file_a_rename_would_change_is_rewritten_in_place)
{
	SaveTargetFacts facts = PlainFile();
	facts.links = 2;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Rewrite);

	facts = PlainFile();
	facts.uid = 0;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Rewrite);

	facts = PlainFile();
	facts.gid = 100;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Rewrite);

	facts = PlainFile();
	facts.mode = 02644;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Rewrite);

	facts = PlainFile();
	facts.device = 43;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Rewrite);

	facts = PlainFile();
	facts.sameExtendedAttributes = false;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Rewrite);

	facts = PlainFile();
	facts.tempMade = false;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Rewrite);
}

TEST(a_new_file_is_made_by_a_rename_or_refused_without_a_temporary_file)
{
	SaveTargetFacts facts;
	facts.tempMade = true;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Replace);
	facts.tempMade = false;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Refuse);
}

TEST(fifos_and_character_devices_are_written_through_and_other_files_refused)
{
	SaveTargetFacts facts = PlainFile();
	facts.type = S_IFIFO;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::WriteThrough);
	facts.type = S_IFCHR;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::WriteThrough);
	facts.type = S_IFBLK;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Refuse);
	facts.type = S_IFDIR;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Refuse);
	facts.type = S_IFSOCK;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Refuse);
}

TEST(saving_a_plain_file_renames_over_it_and_keeps_its_mode)
{
	SaveScratchDir dir;
	CHECK(!dir.path.empty());
	fs::path file = dir.path / "subs.ass";
	WriteText(file, "old");
	chmod(file.c_str(), 0640);
	ino_t before = StatOf(file).st_ino;

	SaveFile save;
	CHECK(Save(save, file, "new"));
	CHECK(save.Method() == SaveMethod::Replace);
	CHECK(ReadText(file) == "new");
	CHECK((StatOf(file).st_mode & 07777) == 0640);
	CHECK(StatOf(file).st_ino != before);
	CHECK(Names(dir.path) == std::set<std::string>{ "subs.ass" });
}

TEST(saving_a_new_file_makes_it_with_the_umask)
{
	SaveScratchDir dir;
	fs::path file = dir.path / "new.ass";
	mode_t mask = umask(022);

	SaveFile save;
	CHECK(Save(save, file, "fresh"));
	CHECK(save.Method() == SaveMethod::Replace);
	CHECK(ReadText(file) == "fresh");
	CHECK((StatOf(file).st_mode & 07777) == 0644);
	CHECK(Names(dir.path) == std::set<std::string>{ "new.ass" });
	umask(mask);
}

TEST(saving_a_hard_linked_file_keeps_its_inode_so_every_link_sees_the_save)
{
	SaveScratchDir dir;
	fs::path file = dir.path / "subs.ass";
	fs::path other = dir.path / "other.ass";
	for (const std::string &text : { std::string("a longer new text than the old one"), std::string("short") }) {
		WriteText(file, "the old contents");
		std::error_code ignored;
		fs::remove(other, ignored);
		fs::create_hard_link(file, other);
		ino_t before = StatOf(file).st_ino;

		SaveFile save;
		CHECK(Save(save, file, text));
		CHECK(save.Method() == SaveMethod::Rewrite);
		CHECK(ReadText(file) == text);
		CHECK(ReadText(other) == text);
		CHECK(StatOf(file).st_ino == before);
		CHECK(StatOf(other).st_ino == before);
		CHECK(StatOf(file).st_nlink == 2);
		CHECK(Names(dir.path) == (std::set<std::string>{ "subs.ass", "other.ass" }));
	}
}

TEST(the_renaming_save_would_have_left_the_other_link_with_the_old_contents)
{
	// The defect the in-place rewrite prevents.
	SaveScratchDir dir;
	fs::path file = dir.path / "subs.ass";
	fs::path other = dir.path / "other.ass";
	WriteText(file, "old");
	fs::create_hard_link(file, other);
	fs::path temp = dir.path / "subs.ass.tmp";
	WriteText(temp, "new");
	CHECK(std::rename(temp.c_str(), file.c_str()) == 0);
	CHECK(ReadText(file) == "new");
	CHECK(ReadText(other) == "old");
}

TEST(a_failed_rewrite_puts_the_old_contents_back_into_the_same_inode)
{
	SaveScratchDir dir;
	fs::path file = dir.path / "subs.ass";
	fs::path other = dir.path / "other.ass";
	const std::string old = "the old contents of the file";
	for (const std::string &text : { std::string("new contents that are longer than the old contents"), std::string("new") }) {
		for (int failAfter : { 0, 2 }) {
			WriteText(file, old);
			std::error_code ignored;
			fs::remove(other, ignored);
			fs::create_hard_link(file, other);
			ino_t before = StatOf(file).st_ino;

			SaveFile save;
			bytesBeforeFailure = failAfter;
			save.SetTargetWriter(FailingWriter);
			CHECK(!Save(save, file, text));
			CHECK(save.Method() == SaveMethod::Rewrite);
			CHECK(save.KeptBackup().empty());
			CHECK(ReadText(file) == old);
			CHECK(ReadText(other) == old);
			CHECK(StatOf(file).st_ino == before);
			CHECK(Names(dir.path) == (std::set<std::string>{ "subs.ass", "other.ass" }));
		}
	}
}

TEST(a_save_that_is_not_committed_leaves_the_file_and_no_temporary_file)
{
	SaveScratchDir dir;
	fs::path file = dir.path / "subs.ass";
	WriteText(file, "old");
	{
		SaveFile save;
		CHECK(save.Open(file.string()));
		CHECK(save.Write("new", 3));
	}
	CHECK(ReadText(file) == "old");
	CHECK(Names(dir.path) == std::set<std::string>{ "subs.ass" });
}

TEST(a_writable_file_in_a_read_only_folder_is_rewritten_with_its_temporary_files_elsewhere)
{
	if (geteuid() == 0)
		return; // root writes into any folder
	SaveScratchDir dir;
	SaveScratchDir temp;
	TempFolderOverride tempFolder(temp.path);
	fs::path file = dir.path / "subs.ass";
	WriteText(file, "old");
	ino_t before = StatOf(file).st_ino;
	chmod(dir.path.c_str(), 0555);

	SaveFile save;
	CHECK(Save(save, file, "new"));
	CHECK(save.Method() == SaveMethod::Rewrite);
	CHECK(ReadText(file) == "new");
	CHECK(StatOf(file).st_ino == before);
	CHECK(Names(dir.path) == std::set<std::string>{ "subs.ass" });
	CHECK(Names(temp.path).empty());

	// a new file cannot be made there
	SaveFile refused;
	CHECK(!refused.Open((dir.path / "new.ass").string()));
	CHECK(!fs::exists(dir.path / "new.ass"));
	CHECK(Names(temp.path).empty());
	chmod(dir.path.c_str(), 0755);
}

TEST(a_read_only_file_is_refused)
{
	if (geteuid() == 0)
		return;
	SaveScratchDir dir;
	fs::path file = dir.path / "subs.ass";
	WriteText(file, "old");
	chmod(file.c_str(), 0444);

	SaveFile save;
	CHECK(!save.Open(file.string()));
	CHECK(ReadText(file) == "old");
	CHECK(Names(dir.path) == std::set<std::string>{ "subs.ass" });
}

TEST(a_folder_is_refused)
{
	SaveScratchDir dir;
	fs::create_directory(dir.path / "folder.ass");
	SaveFile save;
	CHECK(!save.Open((dir.path / "folder.ass").string()));
	CHECK(fs::is_directory(dir.path / "folder.ass"));
	CHECK(Names(dir.path) == std::set<std::string>{ "folder.ass" });
}

#ifdef __linux__
TEST(a_file_with_extended_attributes_is_rewritten_and_keeps_them)
{
	SaveScratchDir dir;
	fs::path file = dir.path / "subs.ass";
	WriteText(file, "old");
	if (setxattr(file.c_str(), "user.hikarisub.test", "kept", 4, 0) != 0)
		return; // the file system takes no user attributes
	ino_t before = StatOf(file).st_ino;

	SaveFile save;
	CHECK(Save(save, file, "new"));
	CHECK(save.Method() == SaveMethod::Rewrite);
	CHECK(ReadText(file) == "new");
	CHECK(StatOf(file).st_ino == before);
	char value[16] = {};
	CHECK(getxattr(file.c_str(), "user.hikarisub.test", value, sizeof(value)) == 4);
	CHECK(std::string(value) == "kept");
	CHECK(Names(dir.path) == std::set<std::string>{ "subs.ass" });
}
#endif

TEST(a_character_device_is_written_through)
{
	SaveScratchDir temp;
	TempFolderOverride tempFolder(temp.path);
	SaveFile save;
	CHECK(Save(save, "/dev/null", "new"));
	CHECK(save.Method() == SaveMethod::WriteThrough);
	CHECK(fs::is_character_file("/dev/null"));
	CHECK(Names(temp.path).empty());
}

TEST(a_fifo_is_written_through_and_stays_a_fifo)
{
	SaveScratchDir dir;
	SaveScratchDir temp;
	TempFolderOverride tempFolder(temp.path);
	fs::path fifo = dir.path / "pipe.ass";
	CHECK(mkfifo(fifo.c_str(), 0600) == 0);

	// The reader is there before the save; the contents fit in the pipe's
	// buffer, so the save does not wait for them to be read.
	int reader = open(fifo.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	CHECK(reader >= 0);
	SaveFile save;
	bool saved = Save(save, fifo, "through the pipe");
	std::string received;
	char buffer[64];
	for (ssize_t got; (got = read(reader, buffer, sizeof(buffer))) > 0;)
		received.append(buffer, static_cast<size_t>(got));
	close(reader);
	CHECK(saved);
	CHECK(save.Method() == SaveMethod::WriteThrough);
	CHECK(received == "through the pipe");
	CHECK(fs::is_fifo(fifo));
	CHECK(Names(dir.path) == std::set<std::string>{ "pipe.ass" });
	CHECK(Names(temp.path).empty());
}

TEST(a_fifo_that_no_program_reads_fails_the_save_instead_of_waiting)
{
	SaveScratchDir dir;
	SaveScratchDir temp;
	TempFolderOverride tempFolder(temp.path);
	fs::path fifo = dir.path / "pipe.ass";
	CHECK(mkfifo(fifo.c_str(), 0600) == 0);

	std::atomic<bool> done{ false };
	bool saved = true;
	SaveFile save;
	std::thread saver([&] {
		saved = Save(save, fifo, "nobody reads this");
		done = true;
	});
	for (int waited = 0; !done && waited < 500; ++waited)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	bool finishedAlone = done;
	if (!finishedAlone) {
		// a save that waits for a reader: give it one so the test can end
		int fd = open(fifo.c_str(), O_RDONLY | O_NONBLOCK);
		while (!done)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		if (fd >= 0)
			close(fd);
	}
	saver.join();
	CHECK(finishedAlone);
	CHECK(!saved);
	CHECK(save.Method() == SaveMethod::WriteThrough);
	CHECK(fs::is_fifo(fifo));
	CHECK(Names(dir.path) == std::set<std::string>{ "pipe.ass" });
	CHECK(Names(temp.path).empty());
}

#endif
