// Runs the legacy subtitle comparison (SubsGrid::SubsComparison and
// SubsGrid::CompareTexts, SubsGridBase.cpp:1737-1884, copied unchanged by
// extract_functions.py; the GUI they touch is stood in for by
// comparison/standins.h) on the cases in inputs/comparison-cases.txt and
// prints one JSON object per case: for each run (a SUBS_COMPARISON_TYPE and a
// compareStyles list) each grid's Comparison table, row by row. A row is
// [secondComparedLine, differences (0 or 1), lineCompare...]: the other grid's
// row (-1 for none), then compareData's array as CompareTexts filled it.
//
// Every run is made twice, once per wxString width:
//   "linux":   each wchar_t a code point, as wxGTK's wxString stores text
//              (the legacy Linux build);
//   "windows": each wchar_t a UTF-16 code unit, as wxMSW's wxString stores it
//              (wchar_t is 16 bits there): a character outside the BMP is two
//              wchar_t, its surrogates. The same compiled legacy code runs on
//              both; only the text it is given differs. "same" when the
//              tables are those of "linux".
//
// Case file lines (UTF-8, fields separated by a tab):
//   case <name>                       (a space or a tab after "case")
//   first <start ms> <end ms> <style> <visibility> <selected> <text> [<translation>]
//   second ...                        (a Line of CG2, the other tab)
//                                     visibility: 0 NOT_VISIBLE, 1 VISIBLE, 2 VISIBLE_BLOCK
//   tl <CG1 hasTLMode 0|1> <CG2 hasTLMode 0|1>
//   run <SUBS_COMPARISON_TYPE> [<style>,<style>...]   (compareStyles, in order)
//   end
#include "standins.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

ProbeOptions Options;
SubsGrid *SubsGrid::CG1 = nullptr;
SubsGrid *SubsGrid::CG2 = nullptr;
wxArrayString SubsGrid::compareStyles;
// SubsDialogue.cpp:27
std::atomic<unsigned> Visibility::s_Epoch{0};

// The two legacy functions, unchanged.
#include "comparison.inc"

namespace {

struct CaseLine {
    int start = 0, end = 0, visibility = VISIBLE;
    bool selected = false;
    std::string style, text, translation;
};

std::vector<std::string> split(const std::string &line, char separator)
{
    std::vector<std::string> out;
    std::size_t from = 0;
    for (;;) {
        const auto at = line.find(separator, from);
        out.push_back(line.substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (at == std::string::npos)
            return out;
        from = at + 1;
    }
}

std::string json(const wxString &s)
{
    // Written from the code points (the "linux" form); a lone surrogate never
    // reaches here, the names and styles are ASCII.
    const std::string utf8(s.utf8_str());
    std::string out = "\"";
    for (const unsigned char c : utf8) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                out += buffer;
            } else {
                out += char(c);
            }
        }
    }
    return out + "\"";
}

// The text as the build stores it: code points (wxGTK) or UTF-16 code units
// widened one per wchar_t (wxMSW).
wxString text(const std::string &utf8, bool utf16Units)
{
    const wxString points = wxString::FromUTF8(utf8.data(), utf8.size());
    if (!utf16Units)
        return points;
    std::wstring units;
    for (const wxUniChar ch : points) {
        const unsigned long cp = ch.GetValue();
        if (cp >= 0x10000) {
            units += wchar_t(0xD800 + ((cp - 0x10000) >> 10));
            units += wchar_t(0xDC00 + ((cp - 0x10000) & 0x3FF));
        } else {
            units += wchar_t(cp);
        }
    }
    return wxString(units);
}

struct Snapshot {
    std::vector<std::wstring> texts;
    std::vector<int> numbers;
    bool operator==(const Snapshot &) const = default;
};

Snapshot snapshot(SubsFile &file)
{
    Snapshot s;
    for (Dialogue *d : file.dialogues) {
        s.texts.push_back(static_cast<const wxString &>(d->Text).ToStdWstring());
        s.texts.push_back(static_cast<const wxString &>(d->TextTl).ToStdWstring());
        s.texts.push_back(static_cast<const wxString &>(d->Style).ToStdWstring());
        s.numbers.push_back(d->Start.mstime);
        s.numbers.push_back(d->End.mstime);
        s.numbers.push_back(int(static_cast<unsigned char>(d->isVisible)));
    }
    s.numbers.push_back(int(file.Selections.size()));
    return s;
}

void fill(SubsFile &file, const std::vector<CaseLine> &lines, bool utf16Units)
{
    for (std::size_t i = 0; i < lines.size(); i++) {
        const CaseLine &l = lines[i];
        auto *d = new Dialogue;
        d->Style = text(l.style, utf16Units);
        d->Text = text(l.text, utf16Units);
        d->TextTl = text(l.translation, utf16Units);
        d->Start = SubsTime(l.start);
        d->End = SubsTime(l.end);
        d->isVisible.Init(static_cast<unsigned char>(l.visibility));
        file.dialogues.push_back(d);
        if (l.selected)
            file.Selections.insert(int(i));
    }
}

std::string table(const std::vector<compareData> &rows)
{
    std::string out = "[";
    for (std::size_t i = 0; i < rows.size(); i++) {
        const compareData &row = rows[i];
        out += i ? "," : "";
        out += "[" + std::to_string(row.secondComparedLine) + "," + (row.differences ? "1" : "0");
        for (std::size_t m = 0; m < row.size(); m++)
            out += "," + std::to_string(row[m]);
        out += "]";
    }
    return out + "]";
}

// One width: two grids, SubsComparison run twice (the second clears and
// refills the tables, as an edit's SetModified does), both results kept.
std::string run(const std::vector<CaseLine> &first, const std::vector<CaseLine> &second, bool tl1, bool tl2,
                int type, const std::vector<std::string> &styles, bool utf16Units)
{
    SubsFile file1, file2;
    fill(file1, first, utf16Units);
    fill(file2, second, utf16Units);
    SubsGrid grid1, grid2;
    grid1.file = &file1;
    grid2.file = &file2;
    grid1.hasTLMode = tl1;
    grid2.hasTLMode = tl2;
    SubsGrid::CG1 = &grid1;
    SubsGrid::CG2 = &grid2;
    SubsGrid::compareStyles.clear();
    for (const auto &s : styles)
        SubsGrid::compareStyles.Add(text(s, utf16Units));
    Options.comparisonType = type;

    const Snapshot before1 = snapshot(file1), before2 = snapshot(file2);
    SubsGrid::SubsComparison();
    const std::string t1 = table(*grid1.Comparison), t2 = table(*grid2.Comparison);
    SubsGrid::SubsComparison();
    const bool repeatable = table(*grid1.Comparison) == t1 && table(*grid2.Comparison) == t2;
    const bool unchanged = snapshot(file1) == before1 && snapshot(file2) == before2;

    delete grid1.Comparison;
    delete grid2.Comparison;
    SubsGrid::CG1 = SubsGrid::CG2 = nullptr;
    for (Dialogue *d : file1.dialogues)
        delete d;
    for (Dialogue *d : file2.dialogues)
        delete d;
    return std::string("{\"first\":") + t1 + ",\"second\":" + t2 + ",\"repeatable\":" +
           (repeatable ? "true" : "false") + ",\"unchanged\":" + (unchanged ? "true" : "false") + "}";
}

} // namespace

int main()
{
    std::string line, name, runs;
    std::vector<CaseLine> first, second;
    bool tl1 = false, tl2 = false;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        auto fields = split(line, '\t');
        if (fields.size() == 1 && line.find(' ') != std::string::npos) // "case <name>", "end"
            fields = {line.substr(0, line.find(' ')), line.substr(line.find(' ') + 1)};
        const std::string op = fields[0];
        if (op == "case") {
            name = fields.at(1);
            first.clear();
            second.clear();
            tl1 = tl2 = false;
            runs.clear();
        } else if (op == "first" || op == "second") {
            CaseLine l;
            l.start = std::stoi(fields.at(1));
            l.end = std::stoi(fields.at(2));
            l.style = fields.at(3);
            l.visibility = std::stoi(fields.at(4));
            l.selected = fields.at(5) == "1";
            l.text = fields.at(6);
            if (fields.size() > 7)
                l.translation = fields[7];
            (op == "first" ? first : second).push_back(l);
        } else if (op == "tl") {
            tl1 = fields.at(1) == "1";
            tl2 = fields.at(2) == "1";
        } else if (op == "run") {
            const int type = std::stoi(fields.at(1));
            std::vector<std::string> styles;
            if (fields.size() > 2 && !fields[2].empty())
                styles = split(fields[2], ',');
            std::string list = "[";
            for (std::size_t i = 0; i < styles.size(); i++)
                list += (i ? "," : "") + json(wxString::FromUTF8(styles[i]));
            list += "]";
            const std::string onLinux = run(first, second, tl1, tl2, type, styles, false);
            const std::string onWindows = run(first, second, tl1, tl2, type, styles, true);
            runs += std::string(runs.empty() ? "" : ",") + "{\"type\":" + std::to_string(type) +
                    ",\"styles\":" + list + ",\"linux\":" + onLinux +
                    ",\"windows\":" + (onWindows == onLinux ? std::string("\"same\"") : onWindows) + "}";
        } else if (op == "end") {
            std::printf("{\"case\":%s,\"runs\":[%s]}\n", json(wxString::FromUTF8(name)).c_str(), runs.c_str());
            std::fflush(stdout);
        } else {
            std::fprintf(stderr, "unknown op %s\n", op.c_str());
            return 2;
        }
    }
    return 0;
}
