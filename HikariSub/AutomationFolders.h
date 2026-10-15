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

// Where Automation reads and writes (installers spec, 1.7). The shipped
// Automation folder is under the resource root (Options.pathfull), which an
// installed copy cannot write: Program Files, /opt/hikarisub, an AppImage.
// What scripts write, ?user, ?local and ?temp, is under the data root
// (Options.dataPath), with the shipped folder's layout:
//
//   <data root>/Automation/{log,temp,autosave}
//   <data root>/Automation/automation/{Autoload,Include}
//
// A portable copy has one root, so everything stays beside the program as
// before. Autoload scripts come from the shipped folder and, installed,
// from the user's, where a user script replaces a shipped one of the same
// file name. DependencyControl installs into ?user/automation/autoload and
// include, in lower case, which on Linux is another folder: those are read
// too. Apart from wx so that the tests can decide it.

#pragma once

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace automation_folders
{
	struct Roots
	{
		// The resource root and the data root; equal in a portable copy.
		std::string shipped;
		std::string data;
		char separator = '/';
		// File names differ by case (Linux).
		bool caseSensitive = true;
	};

	inline std::string Join(const Roots &roots, std::string base, std::initializer_list<const char *> parts)
	{
		for (const char *part : parts) {
			if (!base.empty() && base.back() != roots.separator)
				base.push_back(roots.separator);
			base += part;
		}
		return base;
	}

	inline std::string Fold(const Roots &roots, std::string text)
	{
		if (!roots.caseSensitive) {
			std::transform(text.begin(), text.end(), text.begin(),
				[](unsigned char c) { return char(std::tolower(c)); });
		}
		return text;
	}

	inline bool Installed(const Roots &roots) { return Fold(roots, roots.shipped) != Fold(roots, roots.data); }

	inline void AddUnique(const Roots &roots, std::vector<std::string> &folders, const std::string &folder)
	{
		for (const std::string &have : folders) {
			if (Fold(roots, have) == Fold(roots, folder))
				return;
		}
		folders.push_back(folder);
	}

	// The autoload folders, the shipped one first.
	inline std::vector<std::string> AutoloadFolders(const Roots &roots)
	{
		std::vector<std::string> folders = { Join(roots, roots.shipped, { "Automation", "automation", "Autoload" }) };
		if (!Installed(roots))
			return folders;
		AddUnique(roots, folders, Join(roots, roots.data, { "Automation", "automation", "Autoload" }));
		AddUnique(roots, folders, Join(roots, roots.data, { "Automation", "automation", "autoload" }));
		return folders;
	}

	// The include folders a script searches after its own, the user's first.
	inline std::vector<std::string> IncludeFolders(const Roots &roots)
	{
		std::vector<std::string> folders;
		if (Installed(roots)) {
			AddUnique(roots, folders, Join(roots, roots.data, { "Automation", "automation", "Include" }));
			AddUnique(roots, folders, Join(roots, roots.data, { "Automation", "automation", "include" }));
		}
		AddUnique(roots, folders, Join(roots, roots.shipped, { "Automation", "automation", "Include" }));
		return folders;
	}

	// The folders an installed copy makes under its data root at start, as
	// a portable copy ships them; none for a portable copy.
	inline std::vector<std::string> UserFolders(const Roots &roots)
	{
		if (!Installed(roots))
			return {};
		return {
			Join(roots, roots.data, { "Automation", "log" }),
			Join(roots, roots.data, { "Automation", "temp" }),
			Join(roots, roots.data, { "Automation", "autosave" }),
			Join(roots, roots.data, { "Automation", "automation", "Autoload" }),
			Join(roots, roots.data, { "Automation", "automation", "Include" }),
		};
	}

	inline bool IsScriptName(const std::string &name)
	{
		// What follows the last dot, as wxString::AfterLast() takes it: the
		// whole name when there is none.
		size_t dot = name.find_last_of('.');
		std::string ext = dot == std::string::npos ? name : name.substr(dot + 1);
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		return ext == "lua" || ext == "moon";
	}

	// The scripts to load from each folder's file names, in folder order and
	// in the order the folder lists them: a later folder's script replaces an
	// earlier one of the same name, in its place. Anything but .lua and .moon
	// is left out.
	inline std::vector<std::string> ChooseScripts(const Roots &roots,
		const std::vector<std::pair<std::string, std::vector<std::string>>> &folders)
	{
		std::vector<std::pair<std::string, std::string>> chosen; // folded name, path
		for (const auto &folder : folders) {
			for (const std::string &name : folder.second) {
				if (!IsScriptName(name))
					continue;
				std::string key = Fold(roots, name);
				std::string path = Join(roots, folder.first, { name.c_str() });
				auto same = std::find_if(chosen.begin(), chosen.end(),
					[&key](const std::pair<std::string, std::string> &entry) { return entry.first == key; });
				if (same != chosen.end())
					same->second = path;
				else
					chosen.emplace_back(key, path);
			}
		}
		std::vector<std::string> paths;
		for (const auto &entry : chosen)
			paths.push_back(entry.second);
		return paths;
	}

	// The folder a path given to aegisub.decode_path starts with: ?audio,
	// ?data, ?dictionary, ?local, ?script, ?temp, ?user or ?video, told apart
	// by the second and fifth characters, so "?axxi" is ?audio as well.
	enum class PathPrefix { None, Audio, Data, Dictionary, Local, Script, Temp, User, Video };

	struct DecodedPrefix
	{
		PathPrefix prefix = PathPrefix::None;
		// The characters decode_path replaces with the folder.
		size_t length = 0;
	};

	inline DecodedPrefix DecodePrefix(const std::wstring &path)
	{
		// The fifth character is read: a shorter path has no prefix, as the
		// shortest ones, ?data, ?temp and ?user, are five characters.
		if (path.length() < 5 || path[0] != L'?')
			return {};
		wchar_t second = path[1], fifth = path[4];
		if (second == L'a' && fifth == L'i') return { PathPrefix::Audio, 6 };
		if (second == L'd' && fifth == L'a') return { PathPrefix::Data, 5 };
		if (second == L'd' && fifth == L't') return { PathPrefix::Dictionary, 11 };
		if (second == L'l' && fifth == L'a') return { PathPrefix::Local, 6 };
		if (second == L's' && fifth == L'i') return { PathPrefix::Script, 7 };
		if (second == L't' && fifth == L'p') return { PathPrefix::Temp, 5 };
		if (second == L'u' && fifth == L'r') return { PathPrefix::User, 5 };
		if (second == L'v' && fifth == L'e') return { PathPrefix::Video, 6 };
		return {};
	}
}
