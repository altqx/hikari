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

#include "check.h"
#include "AttachmentFixture.h"
#include "../HikariSub/ScriptAttachments.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

// The encoding Aegisub writes, written here from its description and not
// from the decoder: each 3 bytes as 4 characters of 6 bits plus 33, a last
// 1 or 2 bytes as 2 or 3 characters.
std::string Encode(const std::vector<unsigned char> &data)
{
	std::string out;
	for (size_t i = 0; i < data.size(); i += 3) {
		size_t n = std::min<size_t>(3, data.size() - i);
		unsigned char b[3] = { data[i], n > 1 ? data[i + 1] : (unsigned char)0, n > 2 ? data[i + 2] : (unsigned char)0 };
		unsigned char v[4] = { (unsigned char)(b[0] >> 2), (unsigned char)(((b[0] & 3) << 4) | (b[1] >> 4)),
			(unsigned char)(((b[1] & 15) << 2) | (b[2] >> 6)), (unsigned char)(b[2] & 63) };
		for (size_t k = 0; k <= n; k++)
			out += (char)(v[k] + 33);
	}
	return out;
}

std::string Utf8Lines(const std::vector<std::string> &lines)
{
	std::string out;
	for (const std::string &line : lines)
		out += line + "\r\n";
	return out;
}

} // namespace

TEST(attachments_decode_the_scripts_fonts_and_keep_its_graphics)
{
	attachment_fixture::Split split = attachment_fixture::SplitScript();
	ScriptAttachments attachments(split.embedded);
	CHECK(!attachments.Empty());
	CHECK_EQ(attachments.Fonts().size(), (size_t)1);
	if (attachments.Fonts().size() == 1) {
		CHECK(attachments.Fonts()[0].name == "HikariAttachedTest_0.ttf");
		CHECK(*attachments.Fonts()[0].data == attachment_fixture::Bytes("HikariAttachedTest.ttf"));
	}
	// the [Graphics] section as the script has it, with nothing else
	const std::string &graphics = attachments.Graphics();
	CHECK(graphics.rfind("[Graphics]\r\nfilename: red.png\r\n", 0) == 0);
	CHECK(graphics.find("[Fonts]") == std::string::npos);
	CHECK(graphics.find("fontname") == std::string::npos);
	std::string data;
	size_t at = graphics.find("red.png\r\n") + 9;
	for (size_t end; (end = graphics.find("\r\n", at)) != std::string::npos; at = end + 2)
		data += graphics.substr(at, end - at);
	CHECK(DecodeAssAttachment(data) == attachment_fixture::Bytes("red.png"));
	// GetVisible's text has neither section
	CHECK(split.visible.find("[Fonts]") == std::string::npos);
	CHECK(split.visible.find("[Graphics]") == std::string::npos);
	CHECK(split.visible.find("\\1img(red.png)") != std::string::npos);
}

TEST(attachment_decoding_reads_every_length_of_the_last_group)
{
	for (size_t size = 0; size < 12; size++) {
		std::vector<unsigned char> data;
		for (size_t i = 0; i < size; i++)
			data.push_back((unsigned char)(0xF7 - 37 * i));
		CHECK(DecodeAssAttachment(Encode(data)) == data);
	}
	// one character over is no whole byte: neither renderer decodes it
	CHECK(DecodeAssAttachment("!!!!!").empty());
}

TEST(attachments_keep_each_font_and_every_graphics_section)
{
	// c's line begins with '[' (0xE8 >> 2 is 58, '[' less 33), as one line in
	// 64 of a big attachment does: it is no section header
	std::vector<unsigned char> a = { 'a', 'b', 'c', 'd' }, b = { 1, 2, 3, 4, 5, 6, 7 }, c = { 0xE8, 0x10, 0x20, 0x30 };
	CHECK(Encode(c)[0] == '[');
	std::string sections = Utf8Lines({ "[Aegisub Project Garbage]", "Video File: x.mkv", "[Fonts]",
		"fontname: a.ttf", Encode(a).substr(0, 2), Encode(a).substr(2), "fontname: b.otf", Encode(b),
		"fontname: c.ttf", Encode(c).substr(0, 4), Encode(c).substr(4),
		"[Graphics]", "filename: one.png", "+++", "[graphics]", "filename: two.png", "---",
		"[Aegisub Extradata]", "Data: 1,x,y" });
	ScriptAttachments attachments(std::wstring(sections.begin(), sections.end()));
	CHECK_EQ(attachments.Fonts().size(), (size_t)3);
	if (attachments.Fonts().size() == 3) {
		CHECK(attachments.Fonts()[0].name == "a.ttf");
		CHECK(*attachments.Fonts()[0].data == a);
		CHECK(attachments.Fonts()[1].name == "b.otf");
		CHECK(*attachments.Fonts()[1].data == b);
		CHECK(attachments.Fonts()[2].name == "c.ttf");
		CHECK(*attachments.Fonts()[2].data == c);
	}
	CHECK(attachments.Graphics() ==
		Utf8Lines({ "[Graphics]", "filename: one.png", "+++", "[graphics]", "filename: two.png", "---" }));

	ScriptAttachments none(L"[Aegisub Project Garbage]\r\nVideo File: x.mkv\r\n");
	CHECK(none.Empty());
	CHECK(ScriptAttachments(L"").Empty());
}

TEST(attachment_names_are_utf8)
{
	// escapes: MSVC reads this file in the system code page
	std::wstring sections = L"[Graphics]\r\nfilename: \u0E20\u0E32\u0E1E.png\r\n!!!!\r\n[Fonts]\r\nfontname: \u30D5.ttf\r\n";
	sections += L"!!!!\r\n";
	ScriptAttachments attachments(sections);
	CHECK(attachments.Graphics() == "[Graphics]\r\nfilename: \xE0\xB8\xA0\xE0\xB8\xB2\xE0\xB8\x9E.png\r\n!!!!\r\n");
	CHECK_EQ(attachments.Fonts().size(), (size_t)1);
	if (!attachments.Fonts().empty())
		CHECK(attachments.Fonts()[0].name == "\xE3\x83\x95.ttf");
}

TEST(a_csri_renderer_gets_the_graphics_after_the_events)
{
	attachment_fixture::Split split = attachment_fixture::SplitScript();
	ScriptAttachments attachments(split.embedded);
	std::string text = CsriScriptText(split.visible, &attachments);
	CHECK(text == split.visible + "\r\n" + attachments.Graphics());
	size_t graphics = text.find("[Graphics]");
	CHECK(graphics != std::string::npos);
	CHECK(text.rfind("Dialogue:") < graphics);
	CHECK(text.find("[Fonts]") == std::string::npos);
	// a parse is named by the graphics too
	CHECK(CsriScriptHash(42, &attachments) != 42);
	CHECK(CsriScriptHash(42, &attachments) == CsriScriptHash(42, &attachments));
	CHECK(CsriScriptHash(42, &attachments) != CsriScriptHash(43, &attachments));

	// without graphics the text and its name are the script's own
	ScriptAttachments fontsOnly(L"[Fonts]\r\nfontname: a.ttf\r\n!!!!\r\n");
	CHECK(CsriScriptText(split.visible, &fontsOnly) == split.visible);
	CHECK(CsriScriptText(split.visible, nullptr) == split.visible);
	CHECK_EQ(CsriScriptHash(42, &fontsOnly), (size_t)42);
	CHECK_EQ(CsriScriptHash(42, nullptr), (size_t)42);
}

TEST(attached_fonts_are_added_once_for_the_run)
{
	// bytes no other test adds (the registry lasts for the run, and the
	// fixture's font may be in it already)
	std::vector<unsigned char> font;
	for (unsigned i = 0; i < 300; i++)
		font.push_back((unsigned char)(i * 7 + 13));
	size_t before = AttachedFonts::Count();
	{
		std::string first = Utf8Lines({ "[Fonts]", "fontname: Once.ttf", Encode(font) });
		ScriptAttachments attachments(std::wstring(first.begin(), first.end()));
		AttachedFonts::Add(attachments.Fonts());
	}
	CHECK_EQ(AttachedFonts::Count(), before + 1);
	// the same bytes read again from another script, under another name
	std::string renamed = Utf8Lines({ "[Fonts]", "fontname: Copy.ttf", Encode(font) });
	AttachedFonts::Add(ScriptAttachments(std::wstring(renamed.begin(), renamed.end())).Fonts());
	CHECK_EQ(AttachedFonts::Count(), before + 1);

	std::vector<std::string> handed;
	size_t count = AttachedFonts::AddSince(before, [&](const AttachedFont &added) { handed.push_back(added.name); });
	CHECK_EQ(count, before + 1);
	CHECK_EQ(handed.size(), (size_t)1);
	if (!handed.empty())
		CHECK(handed[0] == "Once.ttf");
	handed.clear();
	CHECK_EQ(AttachedFonts::AddSince(count, [&](const AttachedFont &added) { handed.push_back(added.name); }), count);
	CHECK(handed.empty());
}

// What the renderers are handed for a script whose attachments total about
// 8 MB of text (a 6 MB font, as a CJK font is, and 0.5 MB of images): the
// sections are decoded once when the script is read, and each text a CSRI
// renderer parses grows by the images alone, not by the fonts.
TEST(big_attachments_cost_once_per_script_not_per_parse)
{
	std::vector<unsigned char> font(6u << 20), image(512u << 10);
	for (size_t i = 0; i < font.size(); i++)
		font[i] = (unsigned char)(i * 2654435761u >> 13);
	for (size_t i = 0; i < image.size(); i++)
		image[i] = (unsigned char)(i * 40503u >> 7);
	std::vector<std::string> lines = { "[Fonts]", "fontname: big.ttf" };
	std::string encoded = Encode(font);
	for (size_t i = 0; i < encoded.size(); i += 80)
		lines.push_back(encoded.substr(i, 80));
	lines.push_back("[Graphics]");
	lines.push_back("filename: big.png");
	encoded = Encode(image);
	for (size_t i = 0; i < encoded.size(); i += 80)
		lines.push_back(encoded.substr(i, 80));
	std::string sections = Utf8Lines(lines);
	std::wstring wide(sections.begin(), sections.end());

	using clock = std::chrono::steady_clock;
	auto ms = [](clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
	auto start = clock::now();
	ScriptAttachments attachments(wide);
	double once = ms(clock::now() - start);
	CHECK_EQ(attachments.Fonts().size(), (size_t)1);
	if (!attachments.Fonts().empty())
		CHECK(*attachments.Fonts()[0].data == font);

	std::string script = "\xEF\xBB\xBF[Script Info]\r\n[Events]\r\n";
	for (int i = 0; i < 200; i++)
		script += "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Line\r\n";
	start = clock::now();
	std::string text;
	const int parses = 20;
	for (int i = 0; i < parses; i++)
		text = CsriScriptText(script, &attachments);
	double perParse = ms(clock::now() - start) / parses;
	CHECK_EQ(text.size(), script.size() + 2 + attachments.Graphics().size());
	CHECK(text.size() < script.size() + 800000);
	std::printf("  attachments: %zu characters of sections read in %.1f ms once; "
		"a CSRI text grows by %zu bytes in %.3f ms a parse; the font's %zu characters are in no text\n",
		wide.size(), once, text.size() - script.size(), perParse, Encode(font).size());
}
