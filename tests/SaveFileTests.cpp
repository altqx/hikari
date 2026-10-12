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
#include "../HikariSub/SaveTarget.h"

#include <errno.h>
#include <signal.h>
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
#include <sys/file.h>
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

// Opens a save of target as the application does, after ResolveSaveTarget.
bool OpenAt(SaveFile &file, const fs::path &target)
{
	SaveTarget resolved;
	return ResolveSaveTarget(target.c_str(), &resolved) && file.Open(resolved);
}

bool Save(SaveFile &file, const fs::path &target, const std::string &text)
{
	return OpenAt(file, target) && file.Write(text.data(), text.size()) && file.Commit();
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

	facts = PlainFile();
	facts.lockedByOthers = true;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Rewrite);
}

TEST(a_file_that_cannot_be_written_is_refused)
{
	SaveTargetFacts facts = PlainFile();
	facts.writable = false;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Refuse);
	facts.type = S_IFIFO;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Refuse);
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

TEST(a_target_whose_name_leads_to_another_file_is_refused)
{
	for (mode_t type : { S_IFREG, S_IFIFO, S_IFCHR }) {
		SaveTargetFacts facts = PlainFile();
		facts.type = type;
		facts.changed = true;
		CHECK(ChooseSaveMethod(facts) == SaveMethod::Refuse);
	}
	SaveTargetFacts taken;
	taken.tempMade = true;
	taken.changed = true;
	CHECK(ChooseSaveMethod(taken) == SaveMethod::Refuse);
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

TEST(a_long_file_name_still_gets_its_temporary_files_next_to_it)
{
	SaveScratchDir dir;
	long nameMax = pathconf(dir.path.c_str(), _PC_NAME_MAX);
	if (nameMax <= 0 || nameMax > 1024)
		return; // no fixed limit
	SaveScratchDir temp;
	TempFolderOverride tempFolder(temp.path);
	// The name plus ".XXXXXX" fits, the name plus ".old.XXXXXX" does not: the
	// backup of a Rewrite takes a short name.
	fs::path file = dir.path / (std::string(static_cast<size_t>(nameMax - 7), 'a'));
	fs::path other = dir.path / "other.ass";
	WriteText(file, "old");
	fs::create_hard_link(file, other);
	ino_t before = StatOf(file).st_ino;
	SaveFile rewrite;
	CHECK(Save(rewrite, file, "new"));
	CHECK(rewrite.Method() == SaveMethod::Rewrite);
	CHECK(ReadText(file) == "new");
	CHECK(ReadText(other) == "new");
	CHECK(StatOf(file).st_ino == before);
	CHECK(Names(dir.path) == (std::set<std::string>{ file.filename().string(), "other.ass" }));
	CHECK(Names(temp.path).empty());

	// The name plus ".XXXXXX" does not fit either: the temporary file takes a
	// short name in the same folder and is still renamed over the file.
	fs::path longer = dir.path / (std::string(static_cast<size_t>(nameMax - 3), 'b'));
	WriteText(longer, "old");
	chmod(longer.c_str(), 0640);
	SaveFile replace;
	CHECK(Save(replace, longer, "new"));
	CHECK(replace.Method() == SaveMethod::Replace);
	CHECK(ReadText(longer) == "new");
	CHECK((StatOf(longer).st_mode & 07777) == 0640);
	CHECK(Names(dir.path) ==
		(std::set<std::string>{ file.filename().string(), longer.filename().string(), "other.ass" }));
	CHECK(Names(temp.path).empty());

	// the same through a Rewrite, with a link
	fs::create_hard_link(longer, dir.path / "third.ass");
	SaveFile rewriteLonger;
	CHECK(Save(rewriteLonger, longer, "newer"));
	CHECK(rewriteLonger.Method() == SaveMethod::Rewrite);
	CHECK(ReadText(dir.path / "third.ass") == "newer");
	CHECK(Names(dir.path).size() == 4);
	CHECK(Names(temp.path).empty());
}

TEST(the_method_is_picked_again_for_the_file_that_is_there_at_commit)
{
	SaveScratchDir dir;
	fs::path file = dir.path / "subs.ass";
	fs::path other = dir.path / "other.ass";

	// a link made while the contents were written
	WriteText(file, "old");
	ino_t before = StatOf(file).st_ino;
	SaveFile linked;
	CHECK(OpenAt(linked, file));
	CHECK(linked.Method() == SaveMethod::Replace);
	CHECK(linked.Write("new", 3));
	fs::create_hard_link(file, other);
	CHECK(linked.Commit());
	CHECK(linked.Method() == SaveMethod::Rewrite);
	CHECK(ReadText(file) == "new");
	CHECK(ReadText(other) == "new");
	CHECK(StatOf(file).st_ino == before);
	CHECK(Names(dir.path) == (std::set<std::string>{ "subs.ass", "other.ass" }));

	// a link removed while the contents were written
	SaveFile unlinked;
	CHECK(OpenAt(unlinked, file));
	CHECK(unlinked.Method() == SaveMethod::Rewrite);
	CHECK(unlinked.Write("newer", 5));
	fs::remove(other);
	CHECK(unlinked.Commit());
	CHECK(unlinked.Method() == SaveMethod::Replace);
	CHECK(ReadText(file) == "newer");
	CHECK(Names(dir.path) == std::set<std::string>{ "subs.ass" });

	// a file another program put in place of the target, with a second link,
	// is not the file the save started with and is not written
	SaveFile replaced;
	CHECK(OpenAt(replaced, file));
	CHECK(replaced.Method() == SaveMethod::Replace);
	CHECK(replaced.Write("ours", 4));
	fs::path theirs = dir.path / "theirs.tmp";
	WriteText(theirs, "theirs");
	fs::create_hard_link(theirs, other);
	CHECK(std::rename(theirs.c_str(), file.c_str()) == 0);
	ino_t theirInode = StatOf(file).st_ino;
	CHECK(!replaced.Commit());
	CHECK(replaced.Method() == SaveMethod::Refuse);
	CHECK(ReadText(file) == "theirs");
	CHECK(ReadText(other) == "theirs");
	CHECK(StatOf(file).st_ino == theirInode);
	CHECK(Names(dir.path) == (std::set<std::string>{ "subs.ass", "other.ass" }));

	// a file made read-only while the contents were written
	if (geteuid() != 0) {
		SaveFile readOnly;
		CHECK(OpenAt(readOnly, file));
		CHECK(readOnly.Write("never", 5));
		chmod(file.c_str(), 0444);
		CHECK(!readOnly.Commit());
		CHECK(readOnly.Method() == SaveMethod::Refuse);
		CHECK(ReadText(file) == "theirs");
		CHECK(Names(dir.path) == (std::set<std::string>{ "subs.ass", "other.ass" }));
		chmod(file.c_str(), 0644);
	}

	// a mode changed while the contents were written is kept by the rename
	fs::remove(other);
	chmod(file.c_str(), 0644);
	SaveFile chmodded;
	CHECK(OpenAt(chmodded, file));
	CHECK(chmodded.Write("last", 4));
	chmod(file.c_str(), 0600);
	CHECK(chmodded.Commit());
	CHECK(chmodded.Method() == SaveMethod::Replace);
	CHECK(ReadText(file) == "last");
	CHECK((StatOf(file).st_mode & 07777) == 0600);
	CHECK(Names(dir.path) == std::set<std::string>{ "subs.ass" });
}

#ifdef __linux__
TEST(a_locked_file_is_rewritten_so_the_lock_still_covers_it)
{
	SaveScratchDir dir;
	fs::path file = dir.path / "subs.ass";
	for (int kind = 0; kind < 2; ++kind) {
		WriteText(file, "old");
		ino_t before = StatOf(file).st_ino;
		// another program's lock, here held by this process through its own
		// descriptor; fcntl locks of this process would go with any close
		// of the file, so the fcntl lock is an open file description lock
		int holder = open(file.c_str(), O_RDWR | O_CLOEXEC);
		CHECK(holder >= 0);
		if (kind == 0) {
			CHECK(flock(holder, LOCK_SH) == 0);
		}
		else {
			struct flock lock {};
			lock.l_type = F_RDLCK;
			lock.l_whence = SEEK_SET;
			CHECK(fcntl(holder, F_OFD_SETLK, &lock) == 0);
		}

		SaveFile save;
		CHECK(Save(save, file, "new"));
		CHECK(save.Method() == SaveMethod::Rewrite);
		CHECK(ReadText(file) == "new");
		CHECK(StatOf(file).st_ino == before);
		struct stat held {};
		CHECK(fstat(holder, &held) == 0 && held.st_nlink == 1);
		CHECK(Names(dir.path) == std::set<std::string>{ "subs.ass" });
		close(holder);

		// without the lock the file is renamed over
		SaveFile unlocked;
		CHECK(Save(unlocked, file, "newer"));
		CHECK(unlocked.Method() == SaveMethod::Replace);
		CHECK(ReadText(file) == "newer");
	}
}
#endif

TEST(a_save_that_is_not_committed_leaves_the_file_and_no_temporary_file)
{
	SaveScratchDir dir;
	fs::path file = dir.path / "subs.ass";
	WriteText(file, "old");
	{
		SaveFile save;
		CHECK(OpenAt(save, file));
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
	CHECK(!OpenAt(refused, dir.path / "new.ass"));
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
	CHECK(!OpenAt(save, file));
	CHECK(ReadText(file) == "old");
	CHECK(Names(dir.path) == std::set<std::string>{ "subs.ass" });
}

TEST(a_folder_is_refused)
{
	SaveScratchDir dir;
	fs::create_directory(dir.path / "folder.ass");
	SaveFile save;
	CHECK(!OpenAt(save, dir.path / "folder.ass"));
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

#ifdef __linux__
namespace {

// What another user who can change the entries of the subtitle folder puts
// at the target's name while a save runs; the attack is made in this
// process, which holds the lock the other user would hold.
enum class Swap { LinkToVictim, HardLinkOfVictim, Folder };

// A file the user can write but that is not the subtitle file: what an
// attacker wants the save to write into. A shared flock on it, as another
// user would hold, made the save rewrite whatever file the name led to.
struct Victim {
	fs::path path;
	int holder = -1;
	ino_t inode = 0;
	explicit Victim(const fs::path &folder)
	{
		path = folder / "victim.txt";
		WriteText(path, "the victim's data");
		inode = StatOf(path).st_ino;
		holder = open(path.c_str(), O_RDONLY | O_CLOEXEC);
		if (holder >= 0)
			flock(holder, LOCK_SH);
	}
	~Victim()
	{
		if (holder >= 0)
			close(holder);
	}
	bool Untouched() const
	{
		return ReadText(path) == "the victim's data" && StatOf(path).st_ino == inode;
	}
};

// Moves the target aside to moved.ass and puts swap at its name.
void SwapTarget(const fs::path &file, const Victim &victim, Swap swap)
{
	if (fs::exists(fs::symlink_status(file)))
		fs::rename(file, file.parent_path() / "moved.ass");
	switch (swap) {
	case Swap::LinkToVictim: fs::create_symlink(victim.path, file); break;
	case Swap::HardLinkOfVictim: fs::create_hard_link(victim.path, file); break;
	case Swap::Folder: fs::create_directory(file); break;
	}
}

bool StillSwapped(const fs::path &file, const Victim &victim, Swap swap)
{
	switch (swap) {
	case Swap::LinkToVictim: return fs::is_symlink(file) && fs::read_symlink(file) == victim.path;
	case Swap::HardLinkOfVictim: return StatOf(file).st_ino == victim.inode;
	case Swap::Folder: return fs::is_directory(file) && fs::is_empty(file);
	}
	return false;
}

} // namespace

TEST(a_target_swapped_before_commit_is_refused_and_nothing_is_written)
{
	for (Swap swap : { Swap::LinkToVictim, Swap::HardLinkOfVictim, Swap::Folder }) {
		// a plain file, a hard linked file, which is rewritten, and a locked
		// file, which is rewritten too
		for (int original = 0; original < 3; ++original) {
			SaveScratchDir dir, victimDir;
			Victim victim(victimDir.path);
			CHECK(victim.holder >= 0);
			fs::path file = dir.path / "subs.ass";
			WriteText(file, "old");
			if (original == 1)
				fs::create_hard_link(file, dir.path / "other.ass");
			int lock = -1;
			if (original == 2) {
				lock = open(file.c_str(), O_RDONLY | O_CLOEXEC);
				CHECK(flock(lock, LOCK_SH) == 0);
			}

			SaveFile save;
			CHECK(OpenAt(save, file));
			CHECK(save.Method() == (original == 0 ? SaveMethod::Replace : SaveMethod::Rewrite));
			CHECK(save.Write("ours", 4));
			SwapTarget(file, victim, swap);
			CHECK(!save.Commit());
			CHECK(save.Method() == SaveMethod::Refuse);
			CHECK(victim.Untouched());
			CHECK(StillSwapped(file, victim, swap));
			CHECK(ReadText(dir.path / "moved.ass") == "old");
			std::set<std::string> names{ "subs.ass", "moved.ass" };
			if (original == 1) {
				names.insert("other.ass");
				CHECK(ReadText(dir.path / "other.ass") == "old");
			}
			CHECK(Names(dir.path) == names);
			if (lock >= 0)
				close(lock);
		}
	}
}

TEST(a_new_file_whose_name_is_taken_before_commit_is_refused)
{
	for (Swap swap : { Swap::LinkToVictim, Swap::HardLinkOfVictim, Swap::Folder }) {
		SaveScratchDir dir, victimDir;
		Victim victim(victimDir.path);
		fs::path file = dir.path / "new.ass";
		SaveFile save;
		CHECK(OpenAt(save, file));
		CHECK(save.Method() == SaveMethod::Replace);
		CHECK(save.Write("ours", 4));
		SwapTarget(file, victim, swap);
		CHECK(!save.Commit());
		CHECK(save.Method() == SaveMethod::Refuse);
		CHECK(victim.Untouched());
		CHECK(StillSwapped(file, victim, swap));
		CHECK(Names(dir.path) == std::set<std::string>{ "new.ass" });
	}
}

TEST(a_fifo_swapped_before_commit_is_refused)
{
	SaveScratchDir dir, victimDir, temp;
	TempFolderOverride tempFolder(temp.path);
	Victim victim(victimDir.path);
	fs::path fifo = dir.path / "pipe.ass";
	CHECK(mkfifo(fifo.c_str(), 0600) == 0);
	SaveFile save;
	CHECK(OpenAt(save, fifo));
	CHECK(save.Method() == SaveMethod::WriteThrough);
	CHECK(save.Write("ours", 4));
	fs::remove(fifo);
	fs::create_symlink(victim.path, fifo);
	CHECK(!save.Commit());
	CHECK(save.Method() == SaveMethod::Refuse);
	CHECK(victim.Untouched());
	CHECK(fs::is_symlink(fifo));
	CHECK(Names(temp.path).empty());
}

TEST(a_link_at_the_resolved_name_is_refused_when_the_save_opens)
{
	for (Swap swap : { Swap::LinkToVictim, Swap::HardLinkOfVictim, Swap::Folder }) {
		SaveScratchDir dir, victimDir;
		Victim victim(victimDir.path);
		fs::path file = dir.path / "subs.ass";
		WriteText(file, "old");
		SaveTarget resolved;
		CHECK(ResolveSaveTarget(file.c_str(), &resolved));
		// the plain file ResolveSaveTarget found is swapped before Open
		SwapTarget(file, victim, swap);
		SaveFile save;
		CHECK(!save.Open(resolved));
		CHECK(victim.Untouched());
		CHECK(StillSwapped(file, victim, swap));
		CHECK(Names(dir.path) == (std::set<std::string>{ "subs.ass", "moved.ass" }));
	}
}

TEST(a_folder_swapped_after_the_target_was_resolved_is_refused)
{
	for (bool exists : { true, false }) {
		SaveScratchDir dir, victimDir;
		Victim victim(victimDir.path);
		fs::path folder = dir.path / "subs";
		fs::create_directory(folder);
		// the victim's folder holds a file of the same name
		fs::path file = folder / "victim.txt";
		if (exists)
			WriteText(file, "old");
		else
			fs::remove(victim.path);
		SaveTarget resolved;
		CHECK(ResolveSaveTarget(file.c_str(), &resolved));
		CHECK(resolved.exists == exists);

		// the folder, a part of the resolved path, becomes a link elsewhere
		fs::rename(folder, dir.path / "moved");
		fs::create_directory_symlink(victimDir.path, folder);
		SaveFile save;
		CHECK(!save.Open(resolved));
		if (exists)
			CHECK(victim.Untouched());
		else
			CHECK(fs::is_empty(victimDir.path));

		// a plain folder put in its place
		fs::remove(folder);
		fs::create_directory(folder);
		CHECK(!save.Open(resolved));
		CHECK(fs::is_empty(folder));
	}
}

TEST(a_file_made_at_a_new_file_s_name_after_it_was_resolved_is_refused)
{
	SaveScratchDir dir;
	fs::path file = dir.path / "new.ass";
	SaveTarget resolved;
	CHECK(ResolveSaveTarget(file.c_str(), &resolved));
	WriteText(file, "theirs");
	SaveFile save;
	CHECK(!save.Open(resolved));
	CHECK(ReadText(file) == "theirs");
	CHECK(Names(dir.path) == std::set<std::string>{ "new.ass" });
}

// A swap in the moment between Commit's check and its write. A rename puts
// the new file at the name and writes into nothing that was there; a rewrite
// or a write-through writes into the file the save started with, wherever it
// was moved: a swap there is the same as one right after the save.
TEST(a_target_swapped_after_the_check_is_never_written_through_its_new_name)
{
	for (Swap swap : { Swap::LinkToVictim, Swap::HardLinkOfVictim, Swap::Folder }) {
		for (bool locked : { false, true }) {
			SaveScratchDir dir, victimDir;
			Victim victim(victimDir.path);
			fs::path file = dir.path / "subs.ass";
			WriteText(file, "old");
			int lock = -1;
			if (locked) {
				lock = open(file.c_str(), O_RDONLY | O_CLOEXEC);
				CHECK(flock(lock, LOCK_SH) == 0);
			}
			SaveFile save;
			CHECK(OpenAt(save, file));
			CHECK(save.Write("ours", 4));
			save.SetBeforeWrite([&] { SwapTarget(file, victim, swap); });
			bool saved = save.Commit();
			CHECK(save.Method() == (locked ? SaveMethod::Rewrite : SaveMethod::Replace));
			CHECK(victim.Untouched());
			if (locked) {
				// into the locked file, now at moved.ass
				CHECK(saved);
				CHECK(ReadText(dir.path / "moved.ass") == "ours");
				CHECK(StillSwapped(file, victim, swap));
			}
			else {
				// the new file swaps names with what is there, which is not
				// the file the save started with and gets its name back
				CHECK(!saved);
				CHECK(StillSwapped(file, victim, swap));
				CHECK(ReadText(dir.path / "moved.ass") == "old");
			}
			CHECK(Names(dir.path) == (std::set<std::string>{ "subs.ass", "moved.ass" }));
			if (lock >= 0)
				close(lock);
		}
	}

	// a link made in that moment to the file the save started with: the
	// rename is taken back and the file rewritten, so the link sees the save
	{
		SaveScratchDir dir;
		fs::path file = dir.path / "subs.ass";
		WriteText(file, "old");
		ino_t before = StatOf(file).st_ino;
		SaveFile save;
		CHECK(OpenAt(save, file));
		CHECK(save.Write("ours", 4));
		save.SetBeforeWrite([&] { fs::create_hard_link(file, dir.path / "other.ass"); });
		CHECK(save.Commit());
		CHECK(save.Method() == SaveMethod::Rewrite);
		CHECK(ReadText(file) == "ours");
		CHECK(ReadText(dir.path / "other.ass") == "ours");
		CHECK(StatOf(file).st_ino == before);
		CHECK(Names(dir.path) == (std::set<std::string>{ "subs.ass", "other.ass" }));
	}

	// a new file's name taken in that moment is not renamed over
	SaveScratchDir dir, victimDir;
	Victim victim(victimDir.path);
	fs::path file = dir.path / "new.ass";
	SaveFile save;
	CHECK(OpenAt(save, file));
	CHECK(save.Write("ours", 4));
	save.SetBeforeWrite([&] { SwapTarget(file, victim, Swap::LinkToVictim); });
	CHECK(!save.Commit());
	CHECK(victim.Untouched());
	CHECK(fs::is_symlink(file));
	CHECK(Names(dir.path) == std::set<std::string>{ "new.ass" });
}
#endif

TEST(another_user_s_file_in_a_shared_sticky_folder_is_refused)
{
	SaveTargetFacts facts = PlainFile();
	facts.othersFileInSharedFolder = true;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Refuse);
	facts.type = S_IFIFO;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Refuse);
	facts.type = S_IFCHR;
	CHECK(ChooseSaveMethod(facts) == SaveMethod::Refuse);

	// the user's own file there is saved; another user's file cannot be made
	// here without root
	SaveScratchDir dir;
	chmod(dir.path.c_str(), 01777);
	fs::path file = dir.path / "subs.ass";
	WriteText(file, "old");
	SaveFile save;
	CHECK(Save(save, file, "new"));
	CHECK(ReadText(file) == "new");
	chmod(dir.path.c_str(), 0700);
}

TEST(the_temporary_file_takes_the_target_s_mode_only_when_it_can_be_renamed_over_it)
{
	SaveScratchDir dir;
	fs::path file = dir.path / "subs.ass";
	WriteText(file, "old");
	CHECK(chmod(file.c_str(), 04755) == 0);
	fs::create_hard_link(file, dir.path / "other.ass");
	mode_t mask = umask(022);
	SaveFile save;
	CHECK(OpenAt(save, file));
	CHECK(save.Method() == SaveMethod::Rewrite);
	bool seen = false;
	for (const std::string &name : Names(dir.path)) {
		if (name == "subs.ass" || name == "other.ass")
			continue;
		seen = true;
		CHECK((StatOf(dir.path / name).st_mode & 07777) == 0644);
	}
	CHECK(seen);
	CHECK(save.Write("new", 3));
	CHECK(save.Commit());
	CHECK(ReadText(dir.path / "other.ass") == "new");
	umask(mask);
}

#ifdef __linux__
namespace {
int leavingReader = -1;
// closes the FIFO's only reader before the first write into it
ssize_t ReaderLeavesWriter(int fd, const void *data, size_t size)
{
	if (leavingReader >= 0) {
		close(leavingReader);
		leavingReader = -1;
	}
	return write(fd, data, size);
}
} // namespace

TEST(a_fifo_whose_reader_goes_away_fails_the_save_without_ending_the_program)
{
	SaveScratchDir dir, temp;
	TempFolderOverride tempFolder(temp.path);
	fs::path fifo = dir.path / "pipe.ass";
	CHECK(mkfifo(fifo.c_str(), 0600) == 0);
	leavingReader = open(fifo.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	CHECK(leavingReader >= 0);
	SaveFile save;
	save.SetTargetWriter(ReaderLeavesWriter);
	CHECK(!Save(save, fifo, "nobody is left to read this"));
	CHECK(save.Method() == SaveMethod::WriteThrough);
	// no SIGPIPE is left for the thread either
	sigset_t pending;
	CHECK(sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 0);
	CHECK(Names(temp.path).empty());
}

TEST(a_fifo_whose_reader_stops_reading_fails_the_save_instead_of_waiting)
{
	SaveScratchDir dir, temp;
	TempFolderOverride tempFolder(temp.path);
	fs::path fifo = dir.path / "pipe.ass";
	CHECK(mkfifo(fifo.c_str(), 0600) == 0);
	int reader = open(fifo.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	CHECK(reader >= 0);
	// more than the pipe holds, and a reader that reads none of it
	std::string text(4 << 20, 'x');
	SaveFile save;
	save.SetStallTimeout(200);
	auto start = std::chrono::steady_clock::now();
	CHECK(!Save(save, fifo, text));
	CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
	CHECK(save.Method() == SaveMethod::WriteThrough);
	close(reader);
	CHECK(Names(temp.path).empty());
}
#endif

TEST(a_relative_temporary_folder_stays_where_it_was_when_the_current_folder_changes)
{
	SaveScratchDir base, elsewhere;
	fs::create_directory(base.path / "tmp");
	fs::path fifo = base.path / "pipe.ass";
	CHECK(mkfifo(fifo.c_str(), 0600) == 0);
	fs::path old = fs::current_path();
	fs::current_path(base.path);
	TempFolderOverride tempFolder("tmp");
	{
		SaveFile save;
		CHECK(OpenAt(save, fifo));
		CHECK(save.Write("never", 5));
		CHECK(Names(base.path / "tmp").size() == 1);
		fs::current_path(elsewhere.path);
		fs::create_directory(elsewhere.path / "tmp");
	}
	fs::current_path(old);
	CHECK(Names(base.path / "tmp").empty());
	CHECK(Names(elsewhere.path / "tmp").empty());
}

#endif
