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

#include "ScriptAttachments.h"

#include <algorithm>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

void AppendUtf8(std::string &out, char32_t c)
{
	if (c < 0x80) {
		out += (char)c;
	}
	else if (c < 0x800) {
		out += (char)(0xC0 | (c >> 6));
		out += (char)(0x80 | (c & 0x3F));
	}
	else if (c < 0x10000) {
		out += (char)(0xE0 | (c >> 12));
		out += (char)(0x80 | ((c >> 6) & 0x3F));
		out += (char)(0x80 | (c & 0x3F));
	}
	else {
		out += (char)(0xF0 | (c >> 18));
		out += (char)(0x80 | ((c >> 12) & 0x3F));
		out += (char)(0x80 | ((c >> 6) & 0x3F));
		out += (char)(0x80 | (c & 0x3F));
	}
}

// wchar_t is UTF-16 on Windows and UTF-32 elsewhere
void AppendUtf8(std::string &out, std::wstring_view text)
{
	for (size_t i = 0; i < text.size(); i++) {
		char32_t c = (char32_t)text[i];
		if (sizeof(wchar_t) == 2 && c >= 0xD800 && c < 0xDC00 && i + 1 < text.size() &&
			(char32_t)text[i + 1] >= 0xDC00 && (char32_t)text[i + 1] < 0xE000) {
			c = 0x10000 + ((c - 0xD800) << 10) + ((char32_t)text[i + 1] - 0xDC00);
			i++;
		}
		AppendUtf8(out, c);
	}
}

std::wstring_view Trimmed(std::wstring_view s)
{
	auto blank = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
	while (!s.empty() && blank(s.front()))
		s.remove_prefix(1);
	while (!s.empty() && blank(s.back()))
		s.remove_suffix(1);
	return s;
}

// ASCII-only, as the section headers are
bool StartsWithNoCase(std::wstring_view s, std::wstring_view prefix)
{
	if (s.size() < prefix.size())
		return false;
	for (size_t i = 0; i < prefix.size(); i++) {
		wchar_t a = s[i], b = prefix[i];
		if (a >= L'A' && a <= L'Z')
			a += L'a' - L'A';
		if (b >= L'A' && b <= L'Z')
			b += L'a' - L'A';
		if (a != b)
			return false;
	}
	return true;
}

// A section header, not a line of an attachment: the encoding's characters
// are '!' to '`' (33 to 96), so a line of one may begin with '[' but has
// no space or lowercase letter, which every section name has
bool IsSectionHeader(std::wstring_view line)
{
	if (line.empty() || line[0] != L'[')
		return false;
	for (wchar_t c : line)
		if (c < 33 || c > 96)
			return true;
	return false;
}

} // namespace

std::vector<unsigned char> DecodeAssAttachment(std::string_view text)
{
	std::vector<unsigned char> out;
	size_t size = text.size();
	if (size % 4 == 1)
		return out;
	out.reserve(size / 4 * 3 + 2);
	auto at = [&](size_t i) { return (unsigned char)(text[i] - 33) & 63; };
	size_t whole = size & ~(size_t)3;
	for (size_t i = 0; i < whole; i += 4) {
		unsigned char a = at(i), b = at(i + 1), c = at(i + 2), d = at(i + 3);
		out.push_back((unsigned char)((a << 2) | (b >> 4)));
		out.push_back((unsigned char)(((b & 15) << 4) | (c >> 2)));
		out.push_back((unsigned char)(((c & 3) << 6) | d));
	}
	if (size % 4 >= 2) {
		unsigned char a = at(whole), b = at(whole + 1);
		out.push_back((unsigned char)((a << 2) | (b >> 4)));
		if (size % 4 == 3) {
			unsigned char c = at(whole + 2);
			out.push_back((unsigned char)(((b & 15) << 4) | (c >> 2)));
		}
	}
	return out;
}

ScriptAttachments::ScriptAttachments(std::wstring_view sections)
{
	enum { OTHER, FONTS, GRAPHICS } section = OTHER;
	std::string fontName, fontText;
	bool inFont = false;
	auto finishFont = [&]() {
		if (inFont) {
			auto data = std::make_shared<const std::vector<unsigned char>>(DecodeAssAttachment(fontText));
			if (!data->empty())
				m_Fonts.push_back({ fontName, std::move(data) });
		}
		inFont = false;
		fontName.clear();
		fontText.clear();
	};
	while (!sections.empty()) {
		size_t end = sections.find(L'\n');
		std::wstring_view line = Trimmed(sections.substr(0, end));
		sections.remove_prefix(end == std::wstring_view::npos ? sections.size() : end + 1);
		if (line.empty())
			continue;
		bool header = IsSectionHeader(line);
		if (header) {
			finishFont();
			section = StartsWithNoCase(line, L"[fonts]") ? FONTS :
				StartsWithNoCase(line, L"[graphics]") ? GRAPHICS : OTHER;
		}
		if (section == GRAPHICS) {
			AppendUtf8(m_Graphics, line);
			m_Graphics += "\r\n";
		}
		else if (section == FONTS && !header) {
			if (StartsWithNoCase(line, L"fontname:")) {
				finishFont();
				inFont = true;
				AppendUtf8(fontName, Trimmed(line.substr(9)));
			}
			else if (inFont) {
				AppendUtf8(fontText, line);
			}
		}
	}
	finishFont();
	m_GraphicsHash = std::hash<std::string_view>()(m_Graphics);
}

std::string CsriScriptText(std::string_view script, const ScriptAttachments *attachments)
{
	std::string text;
	const std::string *graphics = attachments ? &attachments->Graphics() : nullptr;
	text.reserve(script.size() + 2 + (graphics ? graphics->size() : 0));
	text.append(script);
	if (graphics && !graphics->empty()) {
		// the last line may lack its line end
		text += "\r\n";
		text += *graphics;
	}
	return text;
}

size_t CsriScriptHash(size_t scriptHash, const ScriptAttachments *attachments)
{
	if (!attachments || attachments->Graphics().empty())
		return scriptHash;
	return scriptHash ^ (attachments->GraphicsHash() + 0x9e3779b97f4a7c15ull + (scriptHash << 6) + (scriptHash >> 2));
}

namespace AttachedFonts {
namespace {

struct Registry
{
	std::mutex mutex;
	std::vector<AttachedFont> fonts;
	std::vector<size_t> hashes;
};

Registry &Fonts()
{
	static Registry registry;
	return registry;
}

size_t DataHash(const std::vector<unsigned char> &data)
{
	return std::hash<std::string_view>()(std::string_view((const char *)data.data(), data.size()));
}

} // namespace

void Add(const std::vector<AttachedFont> &fonts)
{
	Registry &registry = Fonts();
	std::lock_guard<std::mutex> lock(registry.mutex);
	for (const AttachedFont &font : fonts) {
		if (!font.data || font.data->empty())
			continue;
		size_t hash = DataHash(*font.data);
		bool known = false;
		for (size_t i = 0; i < registry.fonts.size() && !known; i++)
			known = registry.hashes[i] == hash && *registry.fonts[i].data == *font.data;
		if (known)
			continue;
#ifdef _WIN32
		// GDI copies the bytes; the fonts stay for the run, as each one may
		// be drawn by any tab's VSFilter
		DWORD count = 0;
		AddFontMemResourceEx((void *)font.data->data(), (DWORD)font.data->size(), nullptr, &count);
#endif
		registry.fonts.push_back(font);
		registry.hashes.push_back(hash);
	}
}

size_t AddSince(size_t count, const std::function<void(const AttachedFont &)> &add)
{
	Registry &registry = Fonts();
	std::lock_guard<std::mutex> lock(registry.mutex);
	for (size_t i = count; i < registry.fonts.size(); i++)
		add(registry.fonts[i]);
	return registry.fonts.size();
}

size_t Count()
{
	Registry &registry = Fonts();
	std::lock_guard<std::mutex> lock(registry.mutex);
	return registry.fonts.size();
}

} // namespace AttachedFonts
