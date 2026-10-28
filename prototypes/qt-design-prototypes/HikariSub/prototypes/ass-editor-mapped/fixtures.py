"""Requested mapping boundary fixtures, not a production parity suite."""
import json
from mapping import mapped_replace, project, u16

def run_fixtures():
    results = []
    def check(name, raw, start, end, insert, expected=None, reject=False, **policy):
        original = raw
        try:
            changed = mapped_replace(raw, start, end, insert, **policy)
            assert not reject, "Expected rejection"
            assert changed == expected, (changed, expected)
            before = [s.source for s in project(raw).spans if not s.value or s.protected]
            after = [s.source for s in project(changed).spans if not s.value or s.protected]
            assert before == after, "Original hidden/opaque tokens changed"
            assert raw == original, "Input mutated"
            results.append({"name": name, "outcome": "passed", "raw": changed})
        except ValueError as error:
            assert reject, str(error)
            assert raw == original
            results.append({"name": name, "outcome": "expected rejection", "reason": str(error)})
    check("insertion after boundary tags", r"A{\i1}B{\i0}C",1,1,"X",r"A{\i1}XB{\i0}C")
    check("insertion before boundary tags", r"A{\i1}B{\i0}C",1,1,"X",r"AX{\i1}B{\i0}C",affinity="before")
    check("replace across tags retains exact tokens", r"A{\i1}B{\i0}C",0,3,"X",r"X{\i1}{\i0}")
    check("strict crossing refusal", r"A{\i1}B{\i0}C",0,3,"X",reject=True,crossing="block")
    check("deletion across tags", r"A{\b1}B{\b0}C",0,2,"",r"{\b1}{\b0}C")
    check("leading tag affinity",r"{\an8\bord2}AB",0,0,"X",r"{\an8\bord2}XAB")
    check("literal hard break is atomic",r"A\NB\nC\hD",1,2,"-",r"A-B\nC\hD")
    check("untouched soft/hard space preserved",r"A\NB\nC\hD",0,1,"X",r"X\NB\nC\hD")
    check("pasted newline encoded",r"{\i1}AB{\i0}",1,1,"X\nY",r"{\i1}AX\NYB{\i0}")
    check("pasted hard space encoded","AB",1,1,"\u00a0",r"A\hB")
    check("ASS paste rejected",r"{\i1}AB{\i0}",0,2,r"{\b1}X",reject=True)
    check("emoji UTF16 exact span",r"{\b1}A😀B{\b0}",1,3,"🎬",r"{\b1}A🎬B{\b0}")
    check("surrogate split rejected","A😀B",2,3,"X",reject=True)
    check("combining split rejected","Ae\u0301B",1,2,"X",reject=True)
    check("combining whole replacement","Ae\u0301B",1,3,"é","AéB")
    check("ZWJ cluster retained","A👩🏽‍💻B",1,8,"X","AXB")
    check("ZWJ internal insertion rejected","A👩🏽‍💻B",3,3,"X",reject=True)
    check("karaoke duration tokens unchanged",r"{\k20}Ka{\kf30}ra{\ko15}oke",0,4,"New",r"{\k20}New{\kf30}{\ko15}oke")
    drawing=r"{\p1}m 0 0 l 20 30{\p0}Text"
    check("drawing bytes retained",drawing,1,5,"字幕",r"{\p1}m 0 0 l 20 30{\p0}字幕")
    check("drawing selection rejected",drawing,0,1,"X",reject=True)
    check("drawing-side insertion rejected",drawing,0,0,"X",reject=True)
    check("malformed protected",r"Text {\i1",0,4,"Word",r"Word {\i1")
    check("malformed selection rejected",r"Text {\i1",5,6,"X",reject=True)
    check("nested brace retained",r"A{\i1{bad}B",0,1,"X",r"X{\i1{bad}B")
    check("RTL logical source untouched",r"{\an7}مرحبا {\i1}שלום{\i0}",0,5,"أهلا",r"{\an7}أهلا {\i1}שלום{\i0}")
    check("all hidden empty projection",r"{\i1}{\i0}",0,0,"X",r"{\i1}{\i0}X")
    return results

if __name__ == "__main__":
    records = run_fixtures()
    print(json.dumps({"fixtures": len(records), "results": records}, ensure_ascii=True, indent=2))
