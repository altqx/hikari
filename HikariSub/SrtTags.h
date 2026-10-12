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

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

struct SrtTagRemoval
{
	std::wstring text;
	// The selection mapped onto the new text.
	size_t from = 0;
	size_t to = 0;
	bool changed = false;
};

// Turns the SRT tag <name> off over the selection [from, to] (a caret when
// from == to), for unticking bold, italic, underline or strikeout. Tag
// names match case-insensitively and exactly, so removing "b" keeps <br>
// and <font ...>.
//
// The tags of the name are read as spans in text order, matched by depth:
// a span starts at an opening tag and ends after the closing tag that
// brings the depth back to zero, or at the end of the text when none does,
// so nested same-name tags belong to the outer span. A closing tag with no
// opening before it is a span of its own.
//
// With a selection, every span that overlaps it loses its tags, and the
// formatting stays on the parts of the span outside the selection: the
// span is closed before the selection and opened again after it when text
// (not tags) lies there. "<b>aaa bbb</b>" with "bbb" selected becomes
// "<b>aaa </b>bbb". Pass from = 0 and to = text.size() to clear a whole
// line.
//
// With a caret, one span loses all its tags: the one around the caret,
// else the one that ends at it (where PutinNonass leaves the caret), else
// the one that starts at it. Spans elsewhere and all other tags stay.
inline SrtTagRemoval RemoveSrtTagPairs(const std::wstring &text, const std::wstring &name, size_t from, size_t to)
{
	SrtTagRemoval result;
	result.text = text;
	if (from > to)
		std::swap(from, to);
	if (to > text.size())
		to = text.size();
	if (from > to)
		from = to;
	result.from = from;
	result.to = to;
	if (name.empty())
		return result;

	struct Range
	{
		size_t start;
		size_t end;
	};
	// Every <...> in the text, to tell text from tags.
	std::vector<Range> anyTags;
	for (size_t pos = text.find(L'<'); pos != std::wstring::npos;) {
		size_t close = text.find(L'>', pos + 1);
		if (close == std::wstring::npos)
			break;
		size_t next = text.find(L'<', pos + 1);
		if (next < close) {
			pos = next;
			continue;
		}
		anyTags.push_back({ pos, close + 1 });
		pos = text.find(L'<', close + 1);
	}
	auto hasText = [&text, &anyTags](size_t a, size_t b) {
		for (size_t pos = a; pos < b && pos < text.size(); pos++) {
			bool inTag = false;
			for (const auto &tag : anyTags) {
				if (tag.start <= pos && pos < tag.end) {
					inTag = true;
					break;
				}
			}
			if (!inTag)
				return true;
		}
		return false;
	};

	struct Tag
	{
		size_t start;
		size_t end;
		bool closing;
	};
	std::vector<Tag> tags;
	for (size_t pos = text.find(L'<'); pos != std::wstring::npos; pos = text.find(L'<', pos + 1)) {
		size_t at = pos + 1;
		bool closing = at < text.size() && text[at] == L'/';
		if (closing)
			++at;
		if (at + name.size() >= text.size())
			continue;
		bool same = true;
		for (size_t i = 0; i < name.size() && same; i++)
			same = std::towlower(text[at + i]) == std::towlower(name[i]);
		if (!same || text[at + name.size()] != L'>')
			continue;
		tags.push_back({ pos, at + name.size() + 1, closing });
	}

	struct Span
	{
		size_t first;
		size_t last;
		size_t start;
		size_t end;
		bool opened;
		bool closed;
	};
	std::vector<Span> spans;
	for (size_t i = 0; i < tags.size();) {
		Span span{ i, i, tags[i].start, tags[i].end, !tags[i].closing, tags[i].closing };
		if (span.opened) {
			int depth = 1;
			size_t t = i + 1;
			for (; t < tags.size() && depth > 0; t++)
				depth += tags[t].closing ? -1 : 1;
			span.last = t - 1;
			span.closed = depth == 0;
			span.end = span.closed ? tags[span.last].end : text.size();
		}
		spans.push_back(span);
		i = span.last + 1;
	}

	// Each change removes [pos, pos + length) and writes insert there.
	struct Change
	{
		size_t pos;
		size_t length;
		std::wstring insert;
	};
	std::vector<Change> edits;
	auto removeTags = [&edits, &tags](const Span &span, bool keepOpening, bool keepClosing) {
		for (size_t t = span.first; t <= span.last; t++) {
			if ((t == span.first && keepOpening) || (t == span.last && keepClosing))
				continue;
			edits.push_back({ tags[t].start, tags[t].end - tags[t].start, std::wstring() });
		}
	};

	if (from == to) {
		const Span *chosen = nullptr;
		for (const auto &span : spans)
			if (span.start < from && from < span.end)
				chosen = &span;
		for (size_t s = 0; s < spans.size() && !chosen; s++)
			if (spans[s].end == from)
				chosen = &spans[s];
		for (size_t s = 0; s < spans.size() && !chosen; s++)
			if (spans[s].start == from)
				chosen = &spans[s];
		if (chosen)
			removeTags(*chosen, false, false);
	}
	else {
		for (const auto &span : spans) {
			if (!(from < span.end && span.start < to))
				continue;
			if (!span.opened) {
				removeTags(span, false, false);
				continue;
			}
			// A bound inside one of the span's tags moves out of it.
			size_t a = from;
			size_t b = to;
			for (size_t t = span.first; t <= span.last; t++) {
				if (tags[t].start < a && a < tags[t].end)
					a = tags[t].start;
				if (tags[t].start < b && b < tags[t].end)
					b = tags[t].end;
			}
			size_t contentStart = tags[span.first].end;
			size_t contentEnd = span.closed ? tags[span.last].start : text.size();
			bool keepBefore = a > contentStart && hasText(contentStart, a);
			bool keepAfter = b < contentEnd && hasText(b, contentEnd);
			removeTags(span, keepBefore, keepAfter && span.closed);
			if (keepBefore)
				edits.push_back({ a, 0, L"</" + name + L">" });
			if (keepAfter)
				edits.push_back({ b, 0, L"<" + name + L">" });
		}
	}
	if (edits.empty())
		return result;

	// A position moves by the edits before it; an insertion at the position
	// itself counts only for the start of a selection, so the selection
	// keeps covering the same text.
	auto mapPosition = [&edits](size_t pos, bool start) {
		long long shift = 0;
		for (const auto &edit : edits) {
			if (edit.pos < pos) {
				size_t removedEnd = (edit.pos + edit.length < pos) ? edit.pos + edit.length : pos;
				shift += (long long)edit.insert.size() - (long long)(removedEnd - edit.pos);
			}
			else if (edit.pos == pos && edit.length == 0 && start)
				shift += (long long)edit.insert.size();
		}
		return (size_t)((long long)pos + shift);
	};
	result.from = mapPosition(from, from != to);
	result.to = (from == to) ? result.from : mapPosition(to, false);
	// Apply from the end; at one position the removal goes before the
	// insertion so it cannot remove the inserted tag.
	std::sort(edits.begin(), edits.end(), [](const Change &x, const Change &y) {
		if (x.pos != y.pos)
			return x.pos > y.pos;
		return x.length > y.length;
	});
	for (const auto &edit : edits) {
		result.text.erase(edit.pos, edit.length);
		result.text.insert(edit.pos, edit.insert);
	}
	result.changed = result.text != text;
	return result;
}
