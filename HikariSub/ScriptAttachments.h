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

#pragma once

// The fonts and images a script carries in its [Fonts] and [Graphics]
// sections, made ready for the subtitle renderers once when the script is
// read, not again with every text a renderer parses:
// - each font is decoded once and added to the renderers once for the run
//   (AttachedFonts): to libass's library, and on Windows to the process for
//   the GDI that VSFilter draws with. Parsed from the text instead, VSFilter
//   would add every font to the process again with each parse, and libass
//   reads no font from the text at all (no ass_set_extract_fonts);
// - the [Graphics] sections, which only VSFilterMod reads (\1img and the
//   other image tags), are kept as UTF-8 and go after the events of each
//   text a CSRI renderer parses (CsriScriptText).

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct AttachedFont
{
	std::string name; // after "fontname:", as written (UTF-8)
	std::shared_ptr<const std::vector<unsigned char>> data;
};

// The uuencoding of ASS attachments, as Aegisub writes it and VSFilter and
// libass read it: each character is 6 bits plus 33, four characters make
// three bytes, and a last two or three characters one or two bytes. Empty
// when the length leaves a single character over, which neither renderer
// decodes.
std::vector<unsigned char> DecodeAssAttachment(std::string_view text);

class ScriptAttachments
{
public:
	// The sections SubsFile keeps aside (GetEmbeddedSections): each line
	// with its header lines, every section the loader did not read itself.
	explicit ScriptAttachments(std::wstring_view sections);
	const std::vector<AttachedFont> &Fonts() const { return m_Fonts; }
	// every [Graphics] section, header included, as UTF-8 lines ending in
	// CRLF; empty without one
	const std::string &Graphics() const { return m_Graphics; }
	size_t GraphicsHash() const { return m_GraphicsHash; }
	bool Empty() const { return m_Fonts.empty() && m_Graphics.empty(); }

private:
	std::vector<AttachedFont> m_Fonts;
	std::string m_Graphics;
	size_t m_GraphicsHash = 0;
};

// The text a CSRI renderer opens: the script, whose events come last, then
// the script's [Graphics] sections.
std::string CsriScriptText(std::string_view script, const ScriptAttachments *attachments);
// What names a CSRI renderer's parse of that text, from the hash of the script.
size_t CsriScriptHash(size_t scriptHash, const ScriptAttachments *attachments);

// Every font the scripts read in this run attached, each once (by content).
namespace AttachedFonts
{
	// On Windows each new font is also added to the process
	// (AddFontMemResourceEx, private to it) for VSFilter's GDI.
	void Add(const std::vector<AttachedFont> &fonts);
	// Hands `add` each font past the first `count`, in the order they were
	// added, and returns how many there are now.
	size_t AddSince(size_t count, const std::function<void(const AttachedFont &)> &add);
	size_t Count();
}
