"""Quick minidump triage: print exception record + modules near the
crashing thread's RIP. Run as:

    python parse_dump.py "<path-to-dmp>"

Intentionally throwaway — committed to scripts/ for the rebuild path
but you can delete after the crash is rooted.
"""
import sys
from minidump.minidumpfile import MinidumpFile

if len(sys.argv) < 2:
    print("usage: parse_dump.py <dmp>")
    sys.exit(1)

mf = MinidumpFile.parse(sys.argv[1])

print("=== EXCEPTION ===")
ex = mf.exception
addr = 0
if ex is None or not ex.exception_records:
    print("no exception record")
else:
    r = ex.exception_records[0].ExceptionRecord
    print(f"ExceptionCode    : {r.ExceptionCode}")
    print(f"ExceptionFlags   : {hex(r.ExceptionFlags)}")
    print(f"ExceptionAddress : {hex(r.ExceptionAddress)}")
    print(f"NumberParameters : {r.NumberParameters}")
    for i, p in enumerate(r.ExceptionInformation):
        print(f"  param[{i}]      : {hex(p)}")
    print(f"ThreadId         : {ex.exception_records[0].ThreadId}")
    addr = r.ExceptionAddress

print()
print("=== MODULES (crash mod + plugin/host) ===")
for m in mf.modules.modules:
    lo = m.baseaddress
    hi = lo + m.size
    contains = " <-- crash" if lo <= addr < hi else ""
    if "TONE3000" in m.name or "Ableton" in m.name or "NeuralAmp" in m.name or contains:
        print(f"  {hex(lo)}-{hex(hi)}  {m.name}{contains}")

print()
print("=== THREADS (rip) ===")
for t in mf.threads.threads[:8]:
    ctx = t.ContextObject
    rip = getattr(ctx, "Rip", None) or getattr(ctx, "Eip", None)
    rsp = getattr(ctx, "Rsp", None) or getattr(ctx, "Esp", None)
    print(f"  thread {t.ThreadId}  rip={hex(rip) if rip else '?'}  rsp={hex(rsp) if rsp else '?'}")
