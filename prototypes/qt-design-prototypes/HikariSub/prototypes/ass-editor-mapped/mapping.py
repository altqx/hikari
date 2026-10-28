"""Small source-preserving ASS projection experiment; not a complete parser."""
from dataclasses import asdict, dataclass
import re
from PySide6.QtCore import QTextBoundaryFinder

OBJECT = "\u25a1"  # Visible reserved prototype marker; U+FFFC rendered invisibly in Quick.

def u16(text):
    return len(text.encode("utf-16-le", errors="surrogatepass")) // 2

def cp_index(text, offset):
    total = 0
    for index, char in enumerate(text):
        if total == offset:
            return index
        total += u16(char)
        if total > offset:
            raise ValueError("Offset splits a UTF-16 surrogate pair.")
    if total == offset:
        return len(text)
    raise ValueError("Offset is outside the text.")

def slice16(text, begin, end):
    return text[cp_index(text, begin):cp_index(text, end)]

def graphemes(text):
    finder = QTextBoundaryFinder(QTextBoundaryFinder.Grapheme, text)
    result = {0}
    while True:
        boundary = finder.toNextBoundary()
        if boundary < 0:
            return result
        result.add(boundary)

@dataclass
class Span:
    raw_start: int
    raw_end: int
    display_start: int
    display_end: int
    source: str
    value: str
    kind: str
    protected: bool = False

@dataclass
class Projection:
    text: str
    spans: list
    warnings: list

    def records(self):
        return [asdict(span) for span in self.spans]

def drawing_state(tag, current):
    """Only top-level literal pN. Nested transform pN is not interpreted."""
    depth = 0
    index = 0
    while index < len(tag):
        char = tag[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth = max(0, depth - 1)
        elif char == "\\" and depth == 0:
            match = re.match(r"\\p(\d+)(?!\d)", tag[index:])
            if match:
                current = int(match.group(1)) != 0
        index += 1
    return current

def project(raw):
    spans, output, warnings = [], [], []
    index = source = display = 0
    drawing = False
    while index < len(raw):
        begin = index
        if raw[index] == "{":
            closing = raw.find("}", index + 1)
            if closing < 0:
                index = len(raw)
                value, kind, protected = OBJECT, "malformed / unclosed override", True
                warnings.append("Unclosed override kept as a protected placeholder; repair in Raw.")
            elif "{" in raw[index + 1:closing]:
                index = closing + 1
                value, kind, protected = OBJECT, "malformed / nested brace", True
                warnings.append("Nested brace kept opaque; no grammar repair was inferred.")
            else:
                index = closing + 1
                token = raw[begin:index]
                drawing = drawing_state(token, drawing)
                value, kind, protected = "", "hidden override", False
                if re.search(r"\\t\([^}]*\\p\d", token):
                    warnings.append("Drawing mode inside a transform is not interpreted by this small scanner.")
        elif drawing:
            closing = raw.find("{", index)
            index = len(raw) if closing < 0 else closing
            value, kind, protected = OBJECT, "drawing payload", True
        elif raw[index] == "}":
            index += 1
            value, kind, protected = OBJECT, "unmatched closing brace", True
            warnings.append("Unmatched closing brace kept opaque; repair in Raw.")
        elif raw[index:index + 2] in (r"\N", r"\n", r"\h"):
            token = raw[index:index + 2]
            index += 2
            value = {r"\N": "\n", r"\n": " ", r"\h": "\u00a0"}[token]
            kind, protected = {r"\N": "hard break", r"\n": "soft break displayed as space", r"\h": "hard space"}[token], False
        else:
            value = raw[index]
            index += 1
            kind, protected = "text", False
        token = raw[begin:index]
        spans.append(Span(source, source + u16(token), display, display + u16(value), token, value, kind, protected))
        output.append(value)
        source += u16(token)
        display += u16(value)
    return Projection("".join(output), spans, list(dict.fromkeys(warnings)))

def insertion_offset(projection, offset, affinity):
    candidates = []
    for span in projection.spans:
        if span.display_start == offset:
            candidates.append(span.raw_start)
        if span.display_end == offset:
            candidates.append(span.raw_end)
    if not candidates:
        if offset == 0 and not projection.spans:
            return 0
        raise ValueError("Insertion is not at a mapped source boundary.")
    return min(candidates) if affinity == "before" else max(candidates)

def mapped_replace(raw, start, end, inserted, affinity="after", crossing="retain"):
    """Return a new raw source; never delete hidden overrides or opaque spans."""
    projection = project(raw)
    if affinity not in ("before", "after") or crossing not in ("retain", "block"):
        raise ValueError("Unknown mapping policy.")
    if start > end:
        start, end = end, start
    boundaries = graphemes(projection.text)
    if start not in boundaries or end not in boundaries:
        raise ValueError("Edit splits a grapheme / UTF-16 boundary. Select the whole character.")
    inserted = inserted.replace("\r\n", "\n").replace("\r", "\n")
    if any(char in inserted for char in ("{", "}", "\\", OBJECT, "\u2028", "\u2029")):
        raise ValueError("Paste contains ASS syntax or opaque markers. Use Raw for literal braces/backslashes; draft was retained.")
    selected = [span for span in projection.spans if span.display_end > span.display_start and span.display_start < end and span.display_end > start]
    if any(span.protected for span in selected):
        raise ValueError("Selection crosses a protected drawing / malformed span. Use Raw; source unchanged.")
    if crossing == "block" and any(span.kind == "hidden override" and start < span.display_start < end for span in projection.spans):
        raise ValueError("Strict crossing policy blocks a replacement across hidden tags. Choose Retain for the experiment or use Raw.")
    for span in selected:
        if span.display_start < start or span.display_end > end:
            raise ValueError("Selection splits an indivisible mapped token.")
    where = insertion_offset(projection, start, affinity)
    removals = [(span.raw_start, span.raw_end) for span in selected]
    encoded = inserted.replace("\u00a0", r"\h").replace("\n", r"\N")
    # Remove only the selected visible source spans, in reverse source order.
    reduced = raw
    for begin, finish in reversed(removals):
        reduced = reduced[:cp_index(reduced, begin)] + reduced[cp_index(reduced, finish):]
    shifted = where - sum(finish - begin for begin, finish in removals if finish <= where)
    point = cp_index(reduced, shifted)
    result = reduced[:point] + encoded + reduced[point:]
    desired = slice16(projection.text, 0, start) + inserted + slice16(projection.text, end, u16(projection.text))
    if project(result).text != desired:
        raise ValueError("This boundary would reinterpret text as drawing / syntax. Choose the other affinity or edit Raw.")
    before_hidden = [span.source for span in projection.spans if not span.value or span.protected]
    after_hidden = [span.source for span in project(result).spans if not span.value or span.protected]
    if before_hidden != after_hidden:
        raise ValueError("Hidden-token preservation check rejected the edit.")
    return result
