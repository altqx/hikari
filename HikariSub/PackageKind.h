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

// How this copy was installed, from the package-kind.txt marker beside the
// executable, and what that means for updates and for where the settings
// live. Apart from wx and the file system so that the tests can decide it.
// The tokens are a contract with the installers and packages (installers
// spec, section 3).

#pragma once

#include <optional>
#include <string_view>

namespace package_kind
{
	enum class Kind
	{
		NsisUser,
		NsisMachine,
		AppImage,
		Deb,
		Rpm,
		// The AUR package, hikarisub-bin.
		Arch,
		// No marker: the zip or the tar.gz.
		PortableWindows,
		PortableLinux,
	};

	// The marker's name, beside the main executable.
	constexpr std::string_view kMarkerFile = "package-kind.txt";

	// The tokens a marker may hold, in Kind's order up to Arch.
	constexpr std::string_view Token(Kind kind)
	{
		switch (kind) {
		case Kind::NsisUser: return "nsis-user";
		case Kind::NsisMachine: return "nsis-machine";
		case Kind::AppImage: return "appimage";
		case Kind::Deb: return "deb";
		case Kind::Rpm: return "rpm";
		case Kind::Arch: return "arch";
		case Kind::PortableWindows: return "portable-windows";
		case Kind::PortableLinux: return "portable-linux";
		}
		return "";
	}

	// A token of an installed kind, as a marker or HIKARISUB_PACKAGE_KIND
	// holds it, or nothing. The portable kinds have no marker, so their
	// tokens are not accepted here.
	constexpr std::optional<Kind> ParseToken(std::string_view token)
	{
		constexpr Kind kInstalled[] = { Kind::NsisUser, Kind::NsisMachine, Kind::AppImage, Kind::Deb,
			Kind::Rpm, Kind::Arch };
		for (Kind kind : kInstalled) {
			if (token == Token(kind))
				return kind;
		}
		return std::nullopt;
	}

	// The marker's content: one token and, at most, one line ending after
	// it. Anything else is no marker.
	constexpr std::optional<Kind> ParseMarker(std::string_view content)
	{
		if (content.size() >= 2 && content.substr(content.size() - 2) == "\r\n")
			content.remove_suffix(2);
		else if (!content.empty() && content.back() == '\n')
			content.remove_suffix(1);
		return ParseToken(content);
	}

	// The kind this copy runs as. A marker of "appimage" counts only when
	// $APPIMAGE names an existing regular file; otherwise, as in an
	// extracted AppDir, the copy is portable. So is one with no marker, or
	// one that says nothing this build knows.
	constexpr Kind Resolve(std::optional<Kind> marker, bool windows, bool appImageFileExists)
	{
		Kind portable = windows ? Kind::PortableWindows : Kind::PortableLinux;
		if (!marker)
			return portable;
		if (*marker == Kind::AppImage && !appImageFileExists)
			return portable;
		// A marker of the other platform's kinds is a copied tree.
		bool windowsKind = *marker == Kind::NsisUser || *marker == Kind::NsisMachine;
		if (windowsKind != windows)
			return portable;
		return *marker;
	}

	// Whether the marker names an installed kind, whose program folder is
	// read-only, so that the settings live in the user's data folder
	// (installers spec, 1.7). Decided by the marker alone: an extracted
	// AppDir with its marker keeps its settings out of the tree too.
	constexpr bool IsInstalled(std::optional<Kind> marker)
	{
		return marker.has_value();
	}

	// How a newer release reaches this kind.
	enum class Flow
	{
		// Downloaded, verified and installed by the app (4.2 to 4.4).
		Install,
		// The AUR notice: update with the package manager (4.5).
		AurNotice,
		// The notice and the release page (4.6).
		Notify,
	};

	constexpr Flow FlowOf(Kind kind)
	{
		switch (kind) {
		case Kind::NsisUser:
		case Kind::NsisMachine:
		case Kind::AppImage:
		case Kind::Deb:
		case Kind::Rpm:
			return Flow::Install;
		case Kind::Arch:
			return Flow::AurNotice;
		case Kind::PortableWindows:
		case Kind::PortableLinux:
			return Flow::Notify;
		}
		return Flow::Notify;
	}

	// The key of the update manifest's package for this kind, or "" when it
	// takes none.
	constexpr std::string_view PackageKey(Kind kind)
	{
		switch (kind) {
		case Kind::NsisUser:
		case Kind::NsisMachine:
			return "nsis";
		case Kind::AppImage: return "appimage";
		case Kind::Deb: return "deb";
		case Kind::Rpm: return "rpm";
		default: return "";
		}
	}

	// Whether "Install updates automatically" means anything for this kind:
	// for the AUR and portable copies nothing is ever installed, so the
	// option is hidden.
	constexpr bool HasAutoInstallOption(Kind kind)
	{
		return FlowOf(kind) == Flow::Install;
	}

	// How About names the kind, after the version: "0.0.2 (AppImage)".
	// Not translated, so that bug reports carry the same words.
	constexpr std::string_view AboutName(Kind kind)
	{
		switch (kind) {
		case Kind::NsisUser: return "installer, just me";
		case Kind::NsisMachine: return "installer, all users";
		case Kind::AppImage: return "AppImage";
		case Kind::Deb: return "deb";
		case Kind::Rpm: return "rpm";
		case Kind::Arch: return "AUR";
		case Kind::PortableWindows:
		case Kind::PortableLinux:
			return "portable";
		}
		return "";
	}
}
