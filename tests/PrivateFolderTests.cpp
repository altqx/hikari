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
#include "../HikariSub/AudioCacheName.h"
#include "../HikariSub/PrivateFolder.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;

// A new empty folder, removed with everything in it when the test ends, in
// the temporary folder, or in base.
struct PrivateScratchDir {
	fs::path path;
	explicit PrivateScratchDir(const fs::path &base = fs::temp_directory_path())
	{
		std::string pattern = (base / "hikarisub-private-XXXXXX").string();
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

// Sets an environment variable, or unsets it for nullptr, for the length of
// a test.
struct EnvOverride {
	std::string name, old;
	bool had;
	EnvOverride(const char *variable, const char *value) : name(variable)
	{
		const char *now = getenv(variable);
		had = now != nullptr;
		if (had)
			old = now;
		if (value)
			setenv(variable, value, 1);
		else
			unsetenv(variable);
	}
	~EnvOverride()
	{
		if (had)
			setenv(name.c_str(), old.c_str(), 1);
		else
			unsetenv(name.c_str());
	}
};

mode_t ModeOf(const fs::path &path)
{
	struct stat st {};
	lstat(path.c_str(), &st);
	return st.st_mode & 07777;
}

// /tmp, which is sticky, for the runtime folders' tests: the folders above
// theirs must be ones no other user can rename folders in, which $TMPDIR, as
// a build sets it, need not be
fs::path SteadyBase()
{
	return "/tmp";
}

std::string OwnFolderName()
{
	return "hikarisub-" + std::to_string(static_cast<unsigned long>(geteuid()));
}

// Binds a socket at path, as wxServer does, under umask, and returns it, or
// -1.
int BindSocketAt(const fs::path &path, mode_t mask)
{
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	sockaddr_un address {};
	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
	mode_t old = umask(mask);
	int bound = bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
	umask(old);
	if (bound != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

// The runtime folders tried, all in a scratch folder: $XDG_RUNTIME_DIR unset,
// the temporary folder and the cache folder as given.
struct RuntimeFolders {
	EnvOverride xdg { "XDG_RUNTIME_DIR", nullptr };
	EnvOverride tmpdir;
	EnvOverride cache;
	RuntimeFolders(const fs::path &temp, const fs::path &cacheFolder)
		: tmpdir("TMPDIR", temp.c_str()), cache("XDG_CACHE_HOME", cacheFolder.c_str())
	{
	}
};

// UserRuntimeSocketPath for hikarisub.ipc with no /run/user/<uid> and no
// /tmp to fall back to, so that only the folders a test gives are tried
std::string SocketPath(const fs::path &loginRuntime = fs::path(), const fs::path &temp = fs::path())
{
	return UserRuntimeSocketPathIn("hikarisub.ipc", loginRuntime.string(), temp.string());
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

TEST(the_runtime_socket_is_in_xdg_runtime_dir_only_when_it_is_private)
{
	PrivateScratchDir dir(SteadyBase());
	fs::path runtime = dir.path / "run";
	fs::create_directory(runtime);
	CHECK(chmod(runtime.c_str(), 0700) == 0);
	fs::path temp = dir.path / "tmp";
	fs::create_directory(temp);
	CHECK(chmod(temp.c_str(), 0755) == 0);
	fs::path cache = dir.path / "cache";
	fs::create_directory(cache);
	CHECK(chmod(cache.c_str(), 0755) == 0);
	RuntimeFolders folders(temp, cache);
	fs::path own = temp / OwnFolderName();
	{
		EnvOverride xdg("XDG_RUNTIME_DIR", runtime.c_str());
		CHECK(SocketPath() == (runtime / "hikarisub.ipc").string());

		// a runtime folder others may open is not used, nor narrowed
		CHECK(chmod(runtime.c_str(), 0755) == 0);
		CHECK(SocketPath() == (own / "hikarisub.ipc").string());
		CHECK(ModeOf(runtime) == 0755);
		CHECK(ModeOf(own) == 0700);
	}
	// one made with another mode before is narrowed
	CHECK(chmod(own.c_str(), 0777) == 0);
	CHECK(SocketPath() == (own / "hikarisub.ipc").string());
	CHECK(ModeOf(own) == 0700);

	// a link at its name, as another user could put there, is not used: the
	// cache folder is
	fs::remove(own);
	fs::create_directory_symlink(runtime, own);
	std::string socket = SocketPath();
	CHECK(socket.rfind((cache / "hikarisub-").string(), 0) == 0);
	CHECK(fs::is_directory(fs::path(socket).parent_path()));
	CHECK(ModeOf(fs::path(socket).parent_path()) == 0700);
	CHECK(fs::is_symlink(own));
	CHECK(ModeOf(runtime) == 0755);
}

TEST(no_runtime_folder_is_used_where_another_user_could_rename_it)
{
	PrivateScratchDir dir(SteadyBase());
	fs::path cache = dir.path / "cache";
	fs::create_directory(cache);
	CHECK(chmod(cache.c_str(), 0700) == 0);

	// a temporary folder anyone may write in and that is not sticky: another
	// user could rename hikarisub-<uid> once it is checked and put a folder
	// of theirs at its name before the socket is made
	fs::path shared = dir.path / "shared";
	fs::create_directory(shared);
	CHECK(chmod(shared.c_str(), 0777) == 0);
	{
		RuntimeFolders folders(shared, cache);
		std::string socket = SocketPath();
		CHECK(socket.rfind(cache.string() + "/", 0) == 0);
		CHECK(!fs::exists(shared / OwnFolderName()));
	}
	// nor where only the group may write
	CHECK(chmod(shared.c_str(), 0770) == 0);
	{
		RuntimeFolders folders(shared, cache);
		CHECK(SocketPath().rfind(cache.string() + "/", 0) == 0);
		CHECK(!fs::exists(shared / OwnFolderName()));
	}
	// nor below such a folder, which lets the folders in it be renamed
	fs::path inner = shared / "inner";
	fs::create_directory(inner);
	CHECK(chmod(inner.c_str(), 0755) == 0);
	CHECK(chmod(shared.c_str(), 0777) == 0);
	{
		RuntimeFolders folders(inner, cache);
		CHECK(SocketPath().rfind(cache.string() + "/", 0) == 0);
		CHECK(!fs::exists(inner / OwnFolderName()));
	}
	{
		// $XDG_RUNTIME_DIR too
		fs::path runtime = inner / "run";
		fs::create_directory(runtime);
		CHECK(chmod(runtime.c_str(), 0700) == 0);
		RuntimeFolders folders(inner, cache);
		EnvOverride xdg("XDG_RUNTIME_DIR", runtime.c_str());
		CHECK(SocketPath().rfind(cache.string() + "/", 0) == 0);
	}
	// a sticky folder lets only the owner of hikarisub-<uid> rename it, as /tmp
	CHECK(chmod(shared.c_str(), 01777) == 0);
	{
		RuntimeFolders folders(inner, cache);
		CHECK(SocketPath() == (inner / OwnFolderName() / "hikarisub.ipc").string());
	}
	// when no folder is safe, there is no socket
	CHECK(chmod(shared.c_str(), 0777) == 0);
	CHECK(chmod(cache.c_str(), 0777) == 0);
	{
		RuntimeFolders folders(inner, cache);
		CHECK(SocketPath().empty());
	}
	CHECK(chmod(cache.c_str(), 0700) == 0);
	CHECK(chmod(shared.c_str(), 0755) == 0);

	// a path through a link is given without the link, and walked only as
	// the folder it leads to
	fs::path real = dir.path / "real";
	fs::create_directory(real);
	CHECK(chmod(real.c_str(), 0755) == 0);
	fs::path link = dir.path / "link";
	fs::create_directory_symlink(real, link);
	{
		RuntimeFolders folders(link, cache);
		CHECK(SocketPath() == (real / OwnFolderName() / "hikarisub.ipc").string());
	}

	// a socket path too long for a socket address, which would be cut short
	// and could then name the folder above, is not given
	fs::path deep = dir.path;
	while (deep.string().size() < 120) {
		deep /= "deeper";
		fs::create_directory(deep);
		CHECK(chmod(deep.c_str(), 0755) == 0);
	}
	{
		RuntimeFolders folders(deep, cache);
		CHECK(SocketPath().rfind(cache.string() + "/", 0) == 0);
		// and the folder is not made there to be left unused
		CHECK(!fs::exists(deep / OwnFolderName()));
	}
}

TEST(the_login_s_runtime_folder_is_used_when_xdg_runtime_dir_is_not_set)
{
	PrivateScratchDir dir(SteadyBase());
	fs::path login = dir.path / "login";
	fs::create_directory(login);
	CHECK(chmod(login.c_str(), 0700) == 0);
	fs::path temp = dir.path / "tmp";
	fs::create_directory(temp);
	CHECK(chmod(temp.c_str(), 0755) == 0);
	RuntimeFolders folders(temp, dir.path / "cache");
	// as from su or cron, which do not set it: the same folder as the login's
	CHECK(SocketPath(login) == (login / "hikarisub.ipc").string());
	{
		EnvOverride xdg("XDG_RUNTIME_DIR", login.c_str());
		CHECK(SocketPath(login) == (login / "hikarisub.ipc").string());
	}
	// one others may open is not used, nor narrowed
	CHECK(chmod(login.c_str(), 0755) == 0);
	CHECK(SocketPath(login) == (temp / OwnFolderName() / "hikarisub.ipc").string());
	CHECK(ModeOf(login) == 0755);
	// nor one that is not there
	CHECK(SocketPath(dir.path / "none") == (temp / OwnFolderName() / "hikarisub.ipc").string());
}

TEST(the_temporary_folder_is_tried_when_tmpdir_cannot_be_used)
{
	PrivateScratchDir dir(SteadyBase());
	fs::path cache = dir.path / "cache";
	fs::create_directory(cache);
	CHECK(chmod(cache.c_str(), 0700) == 0);
	fs::path temp = dir.path / "tmp";
	fs::create_directory(temp);
	CHECK(chmod(temp.c_str(), 01777) == 0);
	fs::path expected = temp / OwnFolderName() / "hikarisub.ipc";

	// a $TMPDIR too long for the socket's path
	fs::path deep = dir.path;
	while (deep.string().size() < 120) {
		deep /= "deeper";
		fs::create_directory(deep);
		CHECK(chmod(deep.c_str(), 0755) == 0);
	}
	{
		RuntimeFolders folders(deep, cache);
		CHECK(SocketPath(fs::path(), temp) == expected.string());
		CHECK(!fs::exists(deep / OwnFolderName()));
	}
	// one others may write in
	fs::path shared = dir.path / "shared";
	fs::create_directory(shared);
	CHECK(chmod(shared.c_str(), 0777) == 0);
	{
		RuntimeFolders folders(shared, cache);
		CHECK(SocketPath(fs::path(), temp) == expected.string());
		CHECK(!fs::exists(shared / OwnFolderName()));
	}
	// and with no $TMPDIR
	{
		RuntimeFolders folders(temp, cache);
		EnvOverride tmpdir("TMPDIR", nullptr);
		CHECK(SocketPath(fs::path(), temp) == expected.string());
	}
}

TEST(a_cache_folder_that_is_not_there_is_made_for_the_socket)
{
	PrivateScratchDir dir(SteadyBase());
	fs::path temp = dir.path / "shared";
	fs::create_directory(temp);
	CHECK(chmod(temp.c_str(), 0777) == 0);
	fs::path home = dir.path / "home";
	fs::create_directory(home);
	CHECK(chmod(home.c_str(), 0755) == 0);
	fs::path cache = home / ".cache";
	{
		RuntimeFolders folders(temp, cache);
		std::string socket = SocketPath();
		CHECK(socket.rfind((cache / "hikarisub-").string(), 0) == 0);
		CHECK(fs::is_directory(cache));
		CHECK(ModeOf(cache) == 0700);
		CHECK(ModeOf(fs::path(socket).parent_path()) == 0700);
	}
	// from $HOME too, and not in a folder others may write in
	fs::remove_all(cache);
	{
		RuntimeFolders folders(temp, fs::path());
		EnvOverride cacheHome("XDG_CACHE_HOME", nullptr);
		EnvOverride homeEnv("HOME", home.c_str());
		CHECK(SocketPath().rfind((cache / "hikarisub-").string(), 0) == 0);
	}
	fs::remove_all(cache);
	CHECK(chmod(home.c_str(), 0777) == 0);
	{
		RuntimeFolders folders(temp, cache);
		CHECK(SocketPath().empty());
		CHECK(!fs::exists(cache));
	}
	CHECK(chmod(home.c_str(), 0755) == 0);
}

TEST(the_socket_made_is_narrowed_in_the_folder_that_was_checked)
{
	PrivateScratchDir dir(SteadyBase());
	fs::path temp = dir.path / "tmp";
	fs::create_directory(temp);
	CHECK(chmod(temp.c_str(), 0755) == 0);
	RuntimeFolders folders(temp, dir.path / "cache");
	std::string path = SocketPath();
	CHECK(path == (temp / OwnFolderName() / "hikarisub.ipc").string());

	// made as a careless umask would make it
	int server = BindSocketAt(path, 0);
	CHECK(server >= 0);
	CHECK(ModeOf(path) == 0777);
	CHECK(!IsUserRuntimeSocket(path));
	CHECK(NarrowUserRuntimeSocket(path));
	CHECK(ModeOf(path) == 0600);
	CHECK(ModeOf(temp / OwnFolderName()) == 0700);
	CHECK(IsUserRuntimeSocket(path));
	if (server >= 0)
		close(server);
}

TEST(a_socket_in_a_folder_that_is_not_private_is_not_used)
{
	PrivateScratchDir dir(SteadyBase());
	fs::path temp = dir.path / "tmp";
	fs::create_directory(temp);
	CHECK(chmod(temp.c_str(), 0755) == 0);
	RuntimeFolders folders(temp, dir.path / "cache");
	std::string path = SocketPath();
	fs::path own = temp / OwnFolderName();
	CHECK(path == (own / "hikarisub.ipc").string());

	// what another user could do were the temporary folder theirs to write
	// in: rename the checked folder away once it is checked, and put one
	// that others may open at its name, with a socket of theirs in it
	fs::path away = temp / "away";
	fs::rename(own, away);
	fs::create_directory(own);
	CHECK(chmod(own.c_str(), 0755) == 0);
	int other = BindSocketAt(path, 077);
	CHECK(other >= 0);
	CHECK(!IsUserRuntimeSocket(path));
	CHECK(!NarrowUserRuntimeSocket(path));
	CHECK(ModeOf(path) == 0700);
	if (other >= 0)
		close(other);
	fs::remove(path);

	// a link to a socket, or another file at the name, is no socket
	CHECK(chmod(own.c_str(), 0700) == 0);
	int server = BindSocketAt(away / "hikarisub.ipc", 077);
	CHECK(server >= 0);
	CHECK(chmod((away / "hikarisub.ipc").c_str(), 0600) == 0);
	fs::create_symlink(away / "hikarisub.ipc", path);
	CHECK(!IsUserRuntimeSocket(path));
	CHECK(!NarrowUserRuntimeSocket(path));
	fs::remove(path);
	std::ofstream(path) << "";
	CHECK(chmod(path.c_str(), 0600) == 0);
	CHECK(!IsUserRuntimeSocket(path));
	CHECK(!NarrowUserRuntimeSocket(path));
	fs::remove(path);
	if (server >= 0)
		close(server);

	// a folder above that another user could rename
	fs::rename(away / "hikarisub.ipc", path);
	CHECK(IsUserRuntimeSocket(path));
	CHECK(chmod(temp.c_str(), 0777) == 0);
	CHECK(!IsUserRuntimeSocket(path));
	CHECK(!NarrowUserRuntimeSocket(path));
	CHECK(chmod(temp.c_str(), 0755) == 0);
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

	// nor a hard link to another of the user's files, which would be
	// narrowed and emptied with it
	fs::path hard = dir.path / "hard.part";
	fs::create_hard_link(elsewhere, hard);
	CHECK(OpenPrivateFileAt(hard.string()) == nullptr);
	CHECK(ReadAll(elsewhere) == "kept");
	CHECK(ModeOf(elsewhere) == 0644);
}

// Two tabs open at once, as ProviderFFMS2::DiskCache makes their audio
// caches: each opens its .part with OpenPrivateFile, writes it, and reads its
// audio from it while the other tab makes its own.
namespace {

// the cache a tab makes for video, with the provider's number n in this process
FILE *MakeAudioCacheOfTab(const fs::path &cacheFolder, const fs::path &video, unsigned long n,
	const std::string &audio)
{
	std::wstring name = AudioCachePartName(AudioCacheName(video.wstring(), 1, 2, 0),
		static_cast<unsigned long>(getpid()), n);
	FILE *fp = OpenPrivateFileAt((cacheFolder / name).string());
	if (!fp)
		return nullptr;
	fwrite(audio.data(), 1, audio.size(), fp);
	fflush(fp);
	rewind(fp);
	return fp;
}

// what the tab plays: ReadCache plays what it cannot read as silence
std::string ReadAudioCache(FILE *fp, size_t bytes)
{
	std::string audio(bytes, '\0');
	size_t read = 0;
	if (fseek(fp, 0, SEEK_SET) == 0)
		read = fread(&audio[0], 1, bytes, fp);
	audio.resize(read);
	audio.resize(bytes, '\0');
	return audio;
}

}

TEST(two_tabs_on_one_video_each_keep_their_own_audio_cache)
{
	PrivateScratchDir dir;
	fs::path cacheFolder = dir.path / "AudioCache";
	CHECK(MakePrivateFolderAt(cacheFolder.string()));
	fs::path video = dir.path / "Kage 07.mkv";
	const std::string audio(4096, 'A');

	FILE *first = MakeAudioCacheOfTab(cacheFolder, video, 1, audio);
	CHECK(first != nullptr);
	// the second tab finds no finished cache, as the first has not closed
	FILE *second = MakeAudioCacheOfTab(cacheFolder, video, 2, std::string());
	CHECK(second != nullptr);
	if (first)
		CHECK(ReadAudioCache(first, audio.size()) == audio);

	if (second)
		fclose(second);
	if (first)
		fclose(first);
}

TEST(two_tabs_on_two_videos_of_one_name_each_keep_their_own_audio)
{
	PrivateScratchDir dir;
	fs::path cacheFolder = dir.path / "AudioCache";
	CHECK(MakePrivateFolderAt(cacheFolder.string()));
	const std::string firstAudio(4096, 'A');
	const std::string secondAudio(2048, 'B');

	FILE *first = MakeAudioCacheOfTab(cacheFolder, dir.path / "a" / "Kage 07.mkv", 1, firstAudio);
	CHECK(first != nullptr);
	FILE *second = MakeAudioCacheOfTab(cacheFolder, dir.path / "b" / "Kage 07.mkv", 2, secondAudio);
	CHECK(second != nullptr);
	if (first)
		CHECK(ReadAudioCache(first, firstAudio.size()) == firstAudio);
	if (second)
		CHECK(ReadAudioCache(second, secondAudio.size()) == secondAudio);

	if (second)
		fclose(second);
	if (first)
		fclose(first);
}

#endif
