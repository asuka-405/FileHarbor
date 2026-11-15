# Run: scripts/test.sh   (or python3 fileharbor/tools/tests/test_devmode.py)   prints PASS/FAIL per check
import os
import sys
import tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from fhtools import devmode_logic as d


def check(name, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else "  " + str(detail)))


r = d.parse_log_line("12:00:01.123\tWARNING\tNemo\tCSS error\x1fline 2")
check("parse a log line", r == ("12:00:01.123", "WARNING", "Nemo", "CSS error\nline 2"), r)
check("parse a foreign line", d.parse_log_line("plain text")[1:] == ("MESSAGE", "-", "plain text"))
recs = [("t", "DEBUG", "A", "x"), ("t", "ERROR", "B", "boom"), ("t", "WARNING", "A", "careful")]
check("filter by level", [r[3] for r in d.filter_records(recs, {"ERROR", "WARNING"})] == ["boom", "careful"])
check("filter by text", [r[3] for r in d.filter_records(recs, set(d.LEVELS), "care")] == ["careful"])
check("contrast black on white is 21", abs(d.contrast("#000000", "#ffffff") - 21) < 0.01)
check("contrast of equal colours is 1", abs(d.contrast("#777777", "#777777") - 1) < 0.001)
rep = d.contrast_report({"text": "#ffffff", "surface": "#ffffff", "accent_text": "#000000", "accent": "#ffffff"})
check("contrast report flags a bad pair", any(not x[4] for x in rep) and any(x[4] for x in rep), rep)

with tempfile.TemporaryDirectory() as t:
    p = os.path.join(t, "debug.log")
    open(p, "w").write("1\tINFO\tNemo\tone\n2\tERROR\tNemo\ttwo\n3\tINFO\tNemo\thal")      # last line is half written
    recs, off = d.read_new(p, 0)
    check("read new lines, hold back the half line", [r[3] for r in recs] == ["one", "two"], recs)
    open(p, "a").write("f\n")
    recs, off2 = d.read_new(p, off)
    check("the held line arrives once it is finished", [r[3] for r in recs] == ["half"], recs)
    open(p, "w").write("9\tINFO\tNemo\tnew\n")                                       # cleared and rewritten
    recs, off3 = d.read_new(p, off2)
    check("a shorter file starts again from the top", [r[3] for r in recs] == ["new"], recs)
    check("nothing new reads nothing", d.read_new(p, off3)[0] == [])

os.environ["XDG_CACHE_HOME"] = "/tmp/fh-test-cache"
check("log lives in the cache folder", d.log_path() == "/tmp/fh-test-cache/fileharbor/debug/debug.log", d.log_path())
check("report is Markdown with the log section", "## Last" in d.report_text(tail=5))
