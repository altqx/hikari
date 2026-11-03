#include "hikari/core/editor_syntax.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>

namespace hikari::core::syntax {

TextState textState(bool comment, std::u16string_view effect)
{
    if (!comment)
        return TextState::Dialogue;
    if (effect.starts_with(u"template") || effect.starts_with(u"mixin"))
        return TextState::Template;
    if (effect.starts_with(u"code"))
        return TextState::Code;
    return TextState::Comment;
}

namespace {

// Digits and dots only; true for "".
bool isNumberFloat(std::u16string_view s)
{
    for (const char16_t c : s)
        if (!((c >= u'0' && c <= u'9') || c == u'.'))
            return false;
    return true;
}

bool isKeyword(std::u16string_view s)
{
    static constexpr std::array<std::u16string_view, 13> keywords{
        u"function", u"for", u"if", u"while", u"do", u"then", u"end", u"or", u"and", u"repeat", u"until", u"math", u"local"};
    for (const auto k : keywords)
        if (s == k)
            return true;
    return false;
}

// Leading blanks, a sign, then digits; 0 for none.
long atoi16(std::u16string_view s)
{
    std::size_t i = 0;
    while (i < s.size() && (s[i] == u' ' || s[i] == u'\t'))
        ++i;
    bool negative = false;
    if (i < s.size() && (s[i] == u'-' || s[i] == u'+'))
        negative = s[i++] == u'-';
    long value = 0;
    for (; i < s.size() && s[i] >= u'0' && s[i] <= u'9'; ++i)
        value = value * 10 + (s[i] - u'0');
    return negative ? -value : value;
}

// Characters read since the last draw, with their positions; tabs left out.
struct Part {
    std::u16string text;
    std::vector<std::size_t> at;
    void add(char16_t c, std::size_t i)
    {
        text.push_back(c);
        at.push_back(i);
    }
    void clear()
    {
        text.clear();
        at.clear();
    }
    void removeLast()
    {
        if (!text.empty()) {
            text.pop_back();
            at.pop_back();
        }
    }
};

class Walker {
public:
    Walker(std::u16string_view text, TextState state)
        : m_text(text), m_state(state), m_roles(text.size()), m_templateCode(state == TextState::Code),
          m_templateLine(state == TextState::Template || state == TextState::Code)
    {
    }

    std::vector<Run> run()
    {
        for (std::size_t i = 0; i < m_text.size(); ++i)
            step(m_text[i], i);
        // The row's end colours the pending token as a trailing space would.
        const char16_t ch = u' ';
        const Role role = (m_val || (m_templateLine && isNumberFloat(m_part.text) && (m_tags || m_templateCode)))
                              ? Role::Values
                          : m_slash          ? Role::Names
                          : m_templateString ? Role::TemplateStrings
                          : (m_templateLine && ch == u'(') ? Role::TemplateFunctions
                          : (m_templateLine && isKeyword(m_part.text)) ? Role::TemplateKeywords
                          : m_templateCode   ? Role::TemplateVariables
                          : m_hasDrawing     ? Role::SplitAndDrawings
                                             : Role::Text;
        draw(m_part, role);
        markDrawings();
        std::vector<Run> runs;
        for (std::size_t i = 0; i < m_roles.size(); ++i) {
            if (!m_roles[i])
                continue;
            if (!runs.empty() && runs.back().end == i && runs.back().role == *m_roles[i])
                runs.back().end = i + 1;
            else
                runs.push_back({i, i + 1, *m_roles[i]});
        }
        return runs;
    }

private:
    static bool drawingCommand(char16_t c)
    {
        return c == u'm' || c == u'n' || c == u'l' || c == u'b' || c == u's' || c == u'p' || c == u'c';
    }
    static bool digit(char16_t c) { return c >= u'0' && c <= u'9'; }

    void markCoordinates(std::size_t from, std::size_t to)
    {
        bool y = false;
        for (std::size_t k = from; k < to;) {
            const char16_t c = m_text[k];
            if (drawingCommand(c)) {
                y = false;
                ++k;
            } else if (digit(c) || c == u'-' || c == u'.') {
                std::size_t end = k + 1;
                while (end < to && (digit(m_text[end]) || m_text[end] == u'.'))
                    ++end;
                for (; k < end; ++k)
                    m_roles[k] = y ? Role::DrawingY : Role::DrawingX;
                y = !y;
            } else {
                ++k;
            }
        }
    }

    void markClip(std::size_t open, std::size_t blockEnd)
    {
        const std::size_t close = std::min(m_text.find(u')', open), blockEnd);
        const std::u16string_view args = m_text.substr(open + 1, close - open - 1);
        if (std::ranges::none_of(args, drawingCommand))
            return;
        const std::size_t scale = args.find(u',');
        markCoordinates(scale == std::u16string_view::npos ? open + 1 : open + 2 + scale, close);
    }

    void markDrawings()
    {
        long level = 0;
        for (std::size_t i = 0; i < m_text.size();) {
            if (m_text[i] != u'{') {
                const std::size_t next = std::min(m_text.find(u'{', i), m_text.size());
                if (level > 0)
                    markCoordinates(i, next);
                i = next;
                continue;
            }
            const std::size_t end = std::min(m_text.find(u'}', i), m_text.size());
            for (std::size_t k = i; k < end; ++k) {
                if (m_text[k] != u'\\')
                    continue;
                const std::u16string_view tag = m_text.substr(k + 1, end - k - 1);
                if (tag.size() > 1 && tag[0] == u'p' && digit(tag[1]))
                    level = atoi16(tag.substr(1));
                else if (tag.starts_with(u"clip(") || tag.starts_with(u"iclip("))
                    markClip(k + 1 + tag.find(u'('), end);
            }
            i = end + 1;
        }
    }

    void draw(const Part &part, Role role)
    {
        // A code Line's plain text is a template variable.
        if (role == Role::Text && m_state == TextState::Code)
            role = Role::TemplateVariables;
        for (const std::size_t i : part.at)
            m_roles[i] = role;
    }
    void draw(char16_t, std::size_t i, Role role) { m_roles[i] = role; }

    void step(char16_t ch, std::size_t i)
    {
        if (m_templateLine) {
            const bool numberOrValue = isNumberFloat(m_part.text) || m_val;
            if (!m_templateString &&
                (ch == u'!' || (ch == u'.' && !numberOrValue) || ch == u',' || ch == u'+' || ch == u'-' || ch == u'=' ||
                 ch == u'(' || ch == u')' || ch == u'>' || ch == u'<' || ch == u'[' || ch == u']' || ch == u'*' ||
                 ch == u'/' || ch == u':' || ch == u';' || ch == u'~')) {
                draw(m_part, numberOrValue                ? Role::Values
                             : m_slash                    ? Role::Names
                             : (ch == u'(' && !m_slash)   ? Role::TemplateFunctions
                             : isKeyword(m_part.text)     ? Role::TemplateKeywords
                                                          : Role::TemplateVariables);
                m_part.clear();
                draw(ch, i, ch == u'!' ? Role::TemplateCodeMarks : Role::Operators);
                if (m_state == TextState::Template && ch == u'!')
                    m_templateCode = !m_templateCode;
                m_slash = m_val = false;
                return;
            }
            if (ch == u'"') {
                if (m_templateString) {
                    m_part.add(ch, i);
                    draw(m_part, Role::TemplateStrings);
                    m_part.clear();
                    m_templateString = false;
                    return;
                }
                m_templateString = true;
            }
            if (!m_templateString && ch == u' ') {
                draw(m_part, (!m_templateCode && !m_val && !m_slash) ? Role::Text
                             : numberOrValue                         ? Role::Values
                             : m_slash                               ? Role::Names
                             : isKeyword(m_part.text)                ? Role::TemplateKeywords
                                                                     : Role::TemplateVariables);
                m_part.clear();
                m_slash = m_val = false;
                return;
            }
        }
        if (ch != u'\t')
            m_part.add(ch, i);

        if (m_templateString)
            return;
        if (m_hasSplit) {
            if (ch == u'N' || ch == u'n' || ch == u'h') {
                draw(m_part, Role::SplitAndDrawings);
                m_part.clear();
            }
            m_hasSplit = false;
        }

        if (ch == u'{' || ch == u'}' || (ch == u'\\' && !m_tags)) {
            if (ch == u'{' || ch == u'\\') {
                m_tags = ch == u'{';
                m_hasSplit = ch == u'\\';
                Part before = m_part;
                before.removeLast();
                draw(before, m_hasDrawing ? Role::SplitAndDrawings : Role::Text);
                m_part.clear();
                m_part.add(ch, i);
            } else {
                Part before = m_part;
                before.removeLast();
                draw(before, m_val ? Role::Values : m_slash ? Role::Names : Role::Text);
                m_part.clear();
                m_part.add(ch, i);
                m_tags = m_slash = m_val = false;
            }
            if (ch != u'\\') {
                draw(m_part, Role::CurlyBraces);
                m_part.clear();
                m_val = false;
            }
        }

        if (m_slash) {
            m_tagTest.push_back(ch);
            static constexpr std::u16string_view digits = u"(0123456789-&+";
            if ((digits.find(ch) != std::u16string_view::npos && m_tagTest != u"1" && m_tagTest != u"2" &&
                 m_tagTest != u"3" && m_tagTest != u"4") ||
                m_tagTest == u"fn" || ch == u'(') {
                m_slash = false;
                if (m_tagTest.starts_with(u"p") && ch != u'(')
                    m_hasDrawing = atoi16(std::u16string_view(m_tagTest).substr(1)) > 0;
                Part name = m_part;
                if (m_tagTest != u"fn")
                    name.removeLast();
                draw(name, Role::Names);
                m_part.clear();
                if (m_tagTest != u"fn" && ch != u'\t')
                    m_part.add(ch, i);
                m_val = true;
                m_tagTest.clear();
            }
        }

        if ((ch == u'\\' || ch == u'(' || ch == u')' || ch == u',') && m_tags) {
            Part before = m_part;
            before.removeLast();
            draw(before, (m_val && (ch == u'\\' || ch == u')' || ch == u',')) ? Role::Values
                         : m_slash                                            ? Role::Names
                                                                              : Role::Text);
            m_part.clear();
            draw(ch, i, Role::Operators);
            if (ch == u'\\')
                m_slash = true;
            if (ch == u'(') {
                m_val = true;
                m_slash = false;
            } else if (ch != u',') {
                m_val = false;
            }
        }
    }

    std::u16string_view m_text;
    TextState m_state;
    std::vector<std::optional<Role>> m_roles;
    Part m_part;
    std::u16string m_tagTest;
    bool m_tags = false, m_slash = false, m_val = false, m_templateString = false;
    bool m_templateCode, m_templateLine;
    bool m_hasDrawing = false, m_hasSplit = false;
};

}

std::vector<Run> colourRuns(std::u16string_view text, TextState state)
{
    return Walker(text, state).run();
}

}
