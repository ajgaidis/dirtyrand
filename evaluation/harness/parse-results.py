#!/usr/bin/env python3
"""Parse benchmark results and report the overhead our defense adds over a
baseline kernel, for BOTH suites the evaluation uses: the Phoronix Test Suite
and lmbench.

The two suites record results in completely different ways -- PTS in a single
XML file with per-iteration samples inline, lmbench as one plain-text file per
iteration -- but both reduce to the same record: a metric, its direction, a
value per kernel, and the scatter of the samples behind each value.  Collection
is therefore per-suite and everything after it is shared, so the two halves of
the evaluation are read, weighted and reported identically.

run-pts.sh sets TEST_RESULTS_NAME (the sweep tag) and TEST_RESULTS_IDENTIFIER
(vanilla / rnguard), so an entire two-kernel sweep lands in ONE result directory
as one composite.xml holding two identifiers:

    test-results/<tag>/composite.xml
      <Result> nettle / sha256
        <Entry> vanilla 1234
        <Entry> rnguard   1180

We therefore bucket by the <Entry> identifier, and cross-check each identifier
against the kernel recorded in its own <System> block -- the two halves are run
across a reboot, so a mislabeled column is the one error that would silently
invert every number below.

NOTE: this reads the new single-directory layout only.  Result directories from
before that change (the ~98 dated per-profile dirs) are not parsed.

For each metric measured in *both* groups we compute the overhead -- the
performance penalty the defense pays -- honouring the metric's direction:

    HIB (higher-is-better, e.g. throughput):  overhead% = (base - def) / base * 100
    LIB (lower-is-better,  e.g. latency/time): overhead% = (def - base) / base * 100

A positive overhead therefore always means "the defense is slower / worse".

The <Proportion> tag is what tells us which direction is "better", so it is
never guessed: a result whose Proportion is missing or unrecognised aborts the
run rather than being assumed HIB, because assuming HIB for a LIB metric
REVERSES its sign (a 2x slowdown is reported as a 2x speedup).  Proportion
ABSTRACT is a legitimate PTS value for non-directional profiles (pass/fail,
image-quality); those are dropped with a note instead.

A NEGATIVE overhead means the defense measured *faster* than the baseline.
On this testbed that is thermal/clock drift, not a real speedup -- a defense
cannot make the kernel faster -- yet those values pull every average down and
credit the defense with a free lunch.  --clamp-negative floors them at 0 for
the aggregates only (per-family means, overall mean/median, the
excluding-noise mean/median); the per-metric table always shows what was
actually measured.

Usage:
    ./parse-results.py [--suite pts|lmbench|both] [--baseline ID] [--defense ID]
                       [--results-dir DIR] [--lmbench-dir DIR] [--sweep TAG]
                       [--reference] [--csv] [--clamp-negative]

    ./parse-results.py --reference       # the numbers behind the paper,
                                         # without running anything

Defaults: --suite both  --baseline vanilla  --defense rnguard
          --results-dir ~/.phoronix-test-suite/test-results
          --lmbench-dir <lmbench tree>/results/<arch>-linux-gnu

In 'both' mode a suite with no results present is skipped with a note rather
than being fatal, because the two suites are installed and run independently.
"""

import argparse
import glob
import os
import re
import sys
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict


def parse_args():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--baseline", default="vanilla",
                   help="result identifier of the baseline run "
                        "(default: vanilla)")
    p.add_argument("--defense", default="rnguard",
                   help="result identifier of the defense run "
                        "(default: rnguard)")
    p.add_argument("--sweep", default=None,
                   help="sweep tag (or path) to read; defaults to the tag in "
                        "bmk/.sweep-tag written by run-pts.sh")
    p.add_argument("--results-dir",
                   default=os.path.expanduser(
                       "~/.phoronix-test-suite/test-results"),
                   help="Phoronix test-results directory")
    p.add_argument("--csv", action="store_true",
                   help="emit CSV instead of a formatted table")
    p.add_argument("--suite", choices=("pts", "lmbench", "both"),
                   default="both",
                   help="which suite to report; 'both' skips whichever has no "
                        "results present (default: both)")
    p.add_argument("--lmbench-dir", default=None,
                   help="directory of lmbench result files named "
                        "<identifier>.<N> (default: the lmbench tree's own "
                        "results/<arch>-linux-gnu)")
    p.add_argument("--reference", action="store_true",
                   help="read the reference results shipped with the artifact "
                        "(../results/) instead of this machine's own output")
    p.add_argument("--clamp-negative", action="store_true",
                   help="treat negative overhead (defense measured faster -- "
                        "drift, not a real speedup) as 0 when averaging; the "
                        "per-metric table still shows the measured value")
    return p.parse_args()


def samples_of(entry):
    """Per-iteration samples from <RawString> (colon-separated)."""
    raw = (entry.findtext("RawString") or "").strip()
    out = []
    for tok in raw.split(":"):
        tok = tok.strip()
        if not tok:
            continue
        try:
            out.append(float(tok))
        except ValueError:
            pass
    return out


def stdev_pct(values):
    """Coefficient of variation, as a percentage.

    Matches pts_math::percent_standard_deviation so these numbers line up
    with `phoronix-test-suite result-file-confidence`: SAMPLE standard
    deviation (n-1 denominator), expressed as a percentage of the mean.
    Returns None when there are too few samples to say anything.
    """
    n = len(values)
    if n < 2:
        return None
    mean = sum(values) / n
    if mean == 0:
        return None
    var = sum((v - mean) ** 2 for v in values) / (n - 1)
    return (var ** 0.5) / mean * 100.0


def combined_noise(cv_a, cv_b):
    """Rough noise floor for a comparison of two independently-measured
    means, added in quadrature. This is a rule of thumb for 'is this delta
    even worth reading', NOT a significance test -- with n=3 iterations on a
    thermally-drifting laptop, no honest p-value is available here."""
    a = cv_a or 0.0
    b = cv_b or 0.0
    if a == 0.0 and b == 0.0:
        return None
    return (a * a + b * b) ** 0.5


def sweep_path(results_dir, sweep):
    """Resolve which sweep directory to read."""
    if sweep:
        cand = sweep if os.path.isdir(sweep) else os.path.join(results_dir, sweep)
        if not os.path.isfile(os.path.join(cand, "composite.xml")):
            sys.exit(f"error: no composite.xml under {cand}")
        return cand

    # Default to the tag run-pts.sh persisted, so parsing needs no arguments.
    tag_file = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            ".sweep-tag")
    if os.path.isfile(tag_file):
        tag = open(tag_file).read().strip()
        cand = os.path.join(results_dir, tag)
        if os.path.isfile(os.path.join(cand, "composite.xml")):
            return cand
        sys.exit(f"error: .sweep-tag names '{tag}' but {cand} has no "
                 "composite.xml -- has that half of the sweep run yet?")
    sys.exit("error: no --sweep given and no .sweep-tag found; "
             "pass --sweep TAG")


def kernels_by_identifier(root):
    """identifier -> kernel string, from the per-identifier <System> blocks.

    A merged sweep carries one <System> per identifier, so we can check that
    the column labelled 'rnguard' really was produced by a rnguard kernel -- the
    two halves are run across a reboot and mislabeling is the failure mode
    that would silently invert the whole comparison.
    """
    out = {}
    for sysnode in root.findall("./System"):
        ident = (sysnode.findtext("Identifier") or "").strip()
        sw = sysnode.findtext("Software", default="")
        kern = ""
        for part in sw.split(","):
            part = part.strip()
            if part.startswith("Kernel:"):
                kern = part[len("Kernel:"):].strip().split(" ")[0]
                break
        if ident:
            out[ident] = kern
    return out


def collect(sweep_dir, baseline_id, defense_id):
    """Bucket every <Entry> of the sweep's composite.xml by its identifier.

    Returns: (groups, kernels, seen, empty, abstract, undirected, dup)
       groups[group][key] = record
       key = (test identifier, arguments, description, scale)

    <Arguments> is part of the key because it is part of what makes a PTS
    result unique.  Without it two distinct sub-tests can collapse onto one
    key and the second silently overwrites the first, dropping a real
    measurement with no warning.  Note the key only has to be unique ACROSS
    results -- both identifiers live inside the same <Result> and so share
    it by construction, which is what makes baseline/defense matching work.
    """
    comp = os.path.join(sweep_dir, "composite.xml")
    try:
        root = ET.parse(comp).getroot()
    except ET.ParseError as e:
        sys.exit(f"error: unparseable {comp}: {e}")

    groups = {"baseline": {}, "defense": {}}
    kernels = kernels_by_identifier(root)
    seen = set()
    empty = []          # results where NO identifier recorded a value
    abstract = []       # Proportion=ABSTRACT -- not a directional metric
    undirected = []     # Proportion missing/unrecognised -- fatal, see below
    dup = []            # two results collapsing onto one key -- fatal
    keys_seen = {}

    for result in root.findall("./Result"):
        entries = result.findall("./Data/Entry")
        ident = result.findtext("Identifier", "").strip()
        desc = (result.findtext("Description", "") or "").strip()
        scale = (result.findtext("Scale", "") or "").strip()
        title = (result.findtext("Title", "") or "").strip()
        targs = (result.findtext("Arguments", "") or "").strip()
        prop = (result.findtext("Proportion", "") or "").strip().upper()
        key = (ident, targs, desc, scale)

        # Record which columns exist before any skipping, so "identifier not
        # present" cannot be reported for a sweep whose results are merely
        # empty or non-directional.
        for e in entries:
            eid = (e.findtext("Identifier") or "").strip()
            if eid:
                seen.add(eid)

        if entries and all(not (e.findtext("Value") or "").strip()
                           for e in entries):
            # The test ran but stored nothing -- pts/redis and pts/nginx do
            # this silently. Surfaced below so it cannot hide across sweeps.
            empty.append((title, desc))
            continue

        # Direction is never guessed.  Defaulting an unknown Proportion to
        # HIB would invert the sign of every LIB metric it hit, which is the
        # one error that turns a slowdown into a reported speedup.  This is
        # not hypothetical: pts/redis and pts/stress-ng ship NO <Proportion>
        # in their test-definition.xml (67 of the 120 metrics in the
        # 2026-08-22 sweep) and are correct only because PTS resolved the
        # direction when it wrote composite.xml.
        if prop not in ("HIB", "LIB"):
            (abstract if prop == "ABSTRACT" else undirected).append(
                (title, desc, scale, prop or "(empty)"))
            continue

        if key in keys_seen:
            dup.append((title, desc, scale, targs))
        keys_seen[key] = True

        # One <Entry> per identifier (per kernel) inside each <Result>.
        for entry in entries:
            eid = (entry.findtext("Identifier") or "").strip()
            if not eid:
                continue
            raw = (entry.findtext("Value") or "").strip()
            if not raw:
                continue      # failed / empty sub-test
            try:
                val = float(raw)
            except ValueError:
                continue

            if eid == baseline_id:
                group = "baseline"
            elif eid == defense_id:
                group = "defense"
            else:
                continue      # some other column in the same file

            samples = samples_of(entry)
            groups[group][key] = {
                "value": val, "proportion": prop,
                "title": title, "dir": os.path.basename(sweep_dir),
                "n": len(samples), "cv": stdev_pct(samples),
            }
    return groups, kernels, seen, empty, abstract, undirected, dup


def overhead_pct(base, dfn, proportion):
    """Percentage performance penalty of defense vs baseline.
    Positive => defense is worse/slower."""
    if base == 0:
        return None
    if proportion == "LIB":          # lower is better (latency, seconds)
        return (dfn - base) / base * 100.0
    return (base - dfn) / base * 100.0   # HIB: higher is better (throughput)


# ---------------------------------------------------------------------------
# lmbench
# ---------------------------------------------------------------------------
#
# lmbench writes one result file per iteration, named <identifier>.<N> in
# results/<arch>-linux-gnu (run-lmbench.sh passes the identifier through from
# run-bmks.sh, so it is the same vanilla/rnguard label PTS uses).  Unlike PTS
# there is no machine-readable schema: the file is the concatenated stderr of
# every benchmark, in three shapes.
#
#   1. scalar lines      "Simple syscall: 0.4039 microseconds"
#   2. context switching  '"size=0k ovr=1.21' followed by "<nproc> <usec>" rows
#   3. file system        '"File system latency' followed by
#                         "<size>\t<nfiles>\t<creates/sec>\t<removes/sec>"
#                         (column meaning per src/lat_fs.c:88-108)
#
# Everything else in the file is a bw_* / lat_mem_rd style bandwidth curve --
# those measure the machine rather than the kernel, which is why the sweep was
# configured with BENCHMARK_HARDWARE=NO.  They are not parsed, but every
# skipped section is REPORTED rather than dropped quietly, so a section that
# should have been compared cannot disappear unnoticed.
#
# The N iteration files give us the same value+scatter shape PTS's <RawString>
# gives: value = mean across iterations, CV = sample CV across iterations.

LMBENCH_FAMILIES = (
    (re.compile(r"^Simple (syscall|read|write|stat|fstat|open/close)$"),
     "Syscall latency"),
    (re.compile(r"^Select on "),                    "Select latency"),
    (re.compile(r"^Signal handler "),               "Signal latency"),
    (re.compile(r"^Protection fault$"),             "Fault latency"),
    (re.compile(r"^Pagefaults\b"),                  "Fault latency"),
    (re.compile(r"^Process fork"),                  "Process creation"),
    (re.compile(r"^(Pipe|AF_UNIX sock stream) latency$"), "IPC latency"),
    (re.compile(r"^(Pipe|AF_UNIX sock stream) bandwidth$"), "IPC bandwidth"),
    (re.compile(r"^(UDP|TCP|RPC/udp|RPC/tcp) latency using "), "Network latency"),
    (re.compile(r"^TCP/IP connection cost"),        "Network latency"),
    (re.compile(r"^File .*bandwidth$"),             "File I/O"),
)

# Direction is derived from the unit, and an unrecognised unit is fatal for the
# same reason an unrecognised PTS <Proportion> is: guessing HIB for a LIB
# metric reports a slowdown as a speedup.
LMBENCH_UNITS = {
    "microseconds": "LIB",
    "millisecs":    "LIB",
    "nanoseconds":  "LIB",
    "MB/sec":       "HIB",
    "KB/sec":       "HIB",
    "files/sec":    "HIB",
}

_LMB_SCALAR = re.compile(r"^(?P<label>[^:]+):\s+(?P<val>-?[0-9.]+)\s+(?P<unit>\S+)$")
_LMB_CTX_HDR = re.compile(r'^"size=(?P<size>\S+)\s+ovr=(?P<ovr>[0-9.]+)$')
_LMB_CTX_ROW = re.compile(r"^(?P<nproc>\d+)\s+(?P<usec>[0-9.]+)$")
_LMB_FS_ROW = re.compile(r"^(?P<size>\S+)\t(?P<n>-?\d+)\t(?P<mk>-?\d+)\t(?P<rm>-?\d+)$")
_LMB_SOCK_ROW = re.compile(r"^(?P<size>[0-9.]+)\s+(?P<val>[0-9.]+)\s+MB/sec$")

# lat_http/bw_* print one "Avg xfer:" summary line per iteration, each with
# different numbers.  Collapsed to one entry so the skipped-section list
# reports a section once rather than once per run.
_LMB_AVG_XFER = re.compile(r"^Avg xfer:")


def lmbench_family(label):
    for pat, fam in LMBENCH_FAMILIES:
        if pat.search(label):
            return fam
    return "Other"


def normalise_label(label):
    """Strip the testbed-specific temp path out of a metric name.

    lmbench embeds $FILE / $FSDIR in two labels ("File /var/tmp/XXX write
    bandwidth", "Pagefaults on /var/tmp/XXX").  Two machines configured with
    different FSDIR would otherwise produce keys that never pair up.
    """
    label = re.sub(r"\s+on\s+\S+$", "", label)
    label = re.sub(r"^File\s+\S+\s+", "File ", label)
    return label.strip()


def section_kind(name, skipped):
    """Classify a section header.  Returns a parser state, recording anything
    we will not compare so it shows up in the report rather than vanishing."""
    if name == "File system latency":
        return "fs"
    if name == "Socket bandwidth using localhost":
        return "sock"
    if _LMB_AVG_XFER.match(name):
        name = "Avg xfer: ... (lat_http per-iteration summary)"
    skipped.add(name)
    return ("skip", name)


def parse_lmbench_file(path, skipped):
    """One iteration file -> {(family, metric, unit, direction): value}."""
    out = {}
    section = None          # None | "ctx" | "fs" | ("skip", name)
    ctx_size = None

    with open(path, errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line.strip():
                section = None
                continue
            if line.startswith("["):        # lmbench's own header block
                continue

            if line.startswith('"'):
                name = line[1:].strip()
                m = _LMB_CTX_HDR.match(line)
                if m:
                    section, ctx_size = "ctx", m.group("size")
                else:
                    section = section_kind(name, skipped)
                continue

            if section == "ctx":
                m = _LMB_CTX_ROW.match(line)
                if m:
                    out[("Context switching",
                         f"size={ctx_size} {m.group('nproc')} procs",
                         "microseconds", "LIB")] = float(m.group("usec"))
                continue

            if section == "fs":
                m = _LMB_FS_ROW.match(line)
                if m:
                    for what, col in (("create", "mk"), ("delete", "rm")):
                        v = float(m.group(col))
                        if v < 0:           # lat_fs prints -1 when it failed
                            continue
                        out[("File system latency",
                             f"{what} {m.group('size')}",
                             "files/sec", "HIB")] = v
                continue

            if section == "sock":
                m = _LMB_SOCK_ROW.match(line)
                if m:
                    out[("Network bandwidth",
                         f"socket {m.group('size')}MB xfer",
                         "MB/sec", "HIB")] = float(m.group("val"))
                continue

            m = _LMB_SCALAR.match(line)
            if m:
                unit = m.group("unit")
                direction = LMBENCH_UNITS.get(unit)
                if direction is None:
                    skipped.add(f"{m.group('label').strip()} [unit {unit!r}]")
                    continue
                label = normalise_label(m.group("label"))
                out[(lmbench_family(label), label, unit, direction)] = \
                    float(m.group("val"))
                continue

            if section is None:
                # An unquoted section header.  Some are real OS benchmarks
                # (bw_tcp prints its title bare), most introduce a hardware
                # bandwidth curve -- section_kind() decides which.
                section = section_kind(line.strip(), skipped)

    return out


def display_path(path, script_dir):
    """Render a path for the report without leaking the operator's home dir.

    parse-results.py output is committed to results/ as the reference summary,
    so an absolute path here would put whoever ran it into the artifact.  Paths
    inside the artifact are shown relative to its root; anything outside is
    shown by basename only.
    """
    root = os.path.abspath(os.path.join(script_dir, "..", ".."))
    rel = os.path.relpath(os.path.abspath(path), root)
    return rel if not rel.startswith(os.pardir) else os.path.basename(
        os.path.normpath(path))


def lmbench_dir_default(script_dir):
    """Where run-lmbench.sh leaves results, mirroring its own resolution."""
    tree = os.environ.get("RNGUARD_LMBENCH_DIR",
                          os.path.join(script_dir, "..", "lmbench",
                                       "lmbench-3.0-a9"))
    return os.path.join(tree, "results", f"{os.uname().machine}-linux-gnu")


def collect_lmbench(lmb_dir, baseline_id, defense_id):
    """Both identifiers' iteration files -> rows in the shared record shape."""
    skipped = set()
    per_ident = {}

    for ident in (baseline_id, defense_id):
        files = sorted(glob.glob(os.path.join(lmb_dir, f"{ident}.[0-9]*")))
        if not files:
            return None, (f"no lmbench result files matching '{ident}.N' in "
                          f"{lmb_dir}"), None
        acc = defaultdict(list)
        for path in files:
            for key, val in parse_lmbench_file(path, skipped).items():
                acc[key].append(val)
        per_ident[ident] = (acc, len(files))

    base_acc, n_base_files = per_ident[baseline_id]
    def_acc, n_def_files = per_ident[defense_id]

    rows = []
    for key in sorted(set(base_acc) & set(def_acc)):
        family, metric, unit, direction = key
        b, d = base_acc[key], def_acc[key]
        b_mean, d_mean = sum(b) / len(b), sum(d) / len(d)
        oh = overhead_pct(b_mean, d_mean, direction)
        if oh is None:
            continue
        b_cv, d_cv = stdev_pct(b), stdev_pct(d)
        noise = combined_noise(b_cv, d_cv)
        rows.append({
            "suite": "lmbench",
            "title": family, "desc": metric, "scale": unit, "prop": direction,
            "base": b_mean, "def": d_mean, "oh": oh,
            "base_cv": b_cv, "def_cv": d_cv,
            "n": min(len(b), len(d)),
            "noise": noise,
            "in_noise": noise is not None and abs(oh) < noise,
        })

    meta = {
        "suite": "lmbench",
        "source": lmb_dir,          # replaced with a display form in main()
        "baseline": baseline_id, "defense": defense_id,
        "n_base": len(base_acc), "n_def": len(def_acc),
        "iters": f"{n_base_files} baseline / {n_def_files} defense",
        "only_base": sorted(set(base_acc) - set(def_acc)),
        "only_def": sorted(set(def_acc) - set(base_acc)),
        "skipped": sorted(skipped),
    }
    return rows, None, meta


# ---------------------------------------------------------------------------
# PTS
# ---------------------------------------------------------------------------

def collect_pts(args):
    """PTS composite.xml -> rows in the shared record shape (+ meta)."""
    sweep = sweep_path(args.results_dir, args.sweep)
    (groups, kernels, seen, empty,
     abstract, undirected, dup) = collect(sweep, args.baseline, args.defense)

    # Fatal: we could not establish which direction is "better".  Reporting a
    # guess here would invert the sign of the affected metrics, so refuse.
    if undirected:
        msg = ["error: %d result(s) have no usable <Proportion>, so the sign "
               "of their" % len(undirected),
               "       overhead cannot be determined:"]
        for title, desc, scale, prop in undirected:
            msg.append(f"         {title} / {desc} [{scale}]  Proportion={prop}")
        msg.append("       Refusing to guess -- assuming HIB for a LIB metric "
                   "reports a slowdown")
        msg.append("       as a speedup.  Fix the test profile's <Proportion>, "
                   "or drop the test.")
        sys.exit("\n".join(msg))

    # Fatal: two results share a key, so one would silently overwrite the
    # other and its measurement would vanish from the table.
    if dup:
        msg = ["error: %d result(s) collapse onto an already-used key "
               "(identifier, arguments," % len(dup),
               "       description, scale) and would silently overwrite an "
               "earlier measurement:"]
        for title, desc, scale, targs in dup:
            msg.append(f"         {title} / {desc} [{scale}]  Arguments={targs!r}")
        sys.exit("\n".join(msg))

    for want in (args.baseline, args.defense):
        if want not in seen:
            sys.exit(f"error: identifier '{want}' not present in {sweep}.\n"
                     f"       identifiers found: {sorted(seen) or '(none)'}\n"
                     "       Has that half of the sweep been run yet?")

    # Guard the cross-reboot failure mode.  The strongest check is name-free:
    # the two halves must have run on DIFFERENT kernels.  If they did not,
    # there is nothing to compare and every number below is measuring drift.
    k_base = kernels.get(args.baseline, "")
    k_def = kernels.get(args.defense, "")
    if k_base and k_def and k_base == k_def:
        sys.exit(f"error: '{args.baseline}' and '{args.defense}' were BOTH "
                 f"recorded on kernel '{k_base}'.\n"
                 "       Both halves of the sweep ran the same kernel -- the "
                 "comparison is meaningless.")

    # Weaker, name-based sanity check.  Identifiers are conventionally named
    # after the kernel they measure (rnguard -> 6.12.11-rnguard-mpk), so a
    # mismatch usually means the halves were swapped.  Compare against the
    # identifier itself rather than a hardcoded 'vanilla'/'rnguard', so this
    # keeps working for other defenses (--defense mte) instead of firing a
    # false positive and, worse, no longer guarding anything.
    for ident in (args.baseline, args.defense):
        kern = kernels.get(ident, "")
        if kern and ident.lower() not in kern.lower():
            print(f"warning: identifier '{ident}' was recorded on kernel "
                  f"'{kern}', whose name does not contain '{ident}' -- check "
                  "that the two halves are not swapped", file=sys.stderr)

    base, dfn = groups["baseline"], groups["defense"]
    common = sorted(set(base) & set(dfn),
                    key=lambda k: (base[k]["title"], k[2], k[3]))

    if not common:
        sys.exit("error: no metrics measured in BOTH groups. "
                 f"baseline metrics={len(base)} defense metrics={len(dfn)}. "
                 "Check --baseline/--defense substrings.")

    rows = []
    for key in common:
        ident, targs, desc, scale = key
        b, d = base[key], dfn[key]
        oh = overhead_pct(b["value"], d["value"], b["proportion"])
        if oh is None:
            continue
        noise = combined_noise(b["cv"], d["cv"])
        rows.append({
            "suite": "pts",
            "title": b["title"], "desc": desc, "scale": scale,
            "prop": b["proportion"],
            "base": b["value"], "def": d["value"], "oh": oh,
            "base_cv": b["cv"], "def_cv": d["cv"],
            "n": min(b["n"], d["n"]),
            "noise": noise,
            # True when the measured delta is smaller than the run-to-run
            # scatter of the samples it was computed from.
            "in_noise": noise is not None and abs(oh) < noise,
        })

    meta = {
        "suite": "pts",
        "source": os.path.basename(sweep),
        "baseline": args.baseline, "defense": args.defense,
        "baseline_kernel": kernels.get(args.baseline, "?"),
        "defense_kernel": kernels.get(args.defense, "?"),
        "n_base": len(base), "n_def": len(dfn),
        "empty": empty, "abstract": abstract,
        "only_base": [f'{base[k]["title"]} / {k[2]}'
                      for k in sorted(set(base) - set(dfn))],
        "only_def": [f'{dfn[k]["title"]} / {k[2]}'
                     for k in sorted(set(dfn) - set(base))],
        "skipped": [],
    }
    return rows, meta


# ---------------------------------------------------------------------------
# reporting (shared by both suites)
# ---------------------------------------------------------------------------

def report(rows, meta, args):
    print(f"Suite    : {meta['suite']}")
    print(f"Source   : {meta['source']}")
    kb = meta.get("baseline_kernel")
    kd = meta.get("defense_kernel")
    print(f"Baseline : {meta['baseline']:<10}"
          + (f" kernel={kb:<24}" if kb else " " * 32)
          + f" ({meta['n_base']} metrics)")
    print(f"Defense  : {meta['defense']:<10}"
          + (f" kernel={kd:<24}" if kd else " " * 32)
          + f" ({meta['n_def']} metrics)")
    if meta.get("iters"):
        print(f"Iterations: {meta['iters']}")
    print(f"Comparable metrics    : {len(rows)}\n")

    wt = max((len(r["title"]) for r in rows), default=4)
    wd = max((len(r["desc"]) for r in rows), default=6)
    wu = max((len(r["scale"]) for r in rows), default=4)

    hdr = (f'{"Test":<{wt}}  {"Metric":<{wd}}  {"Unit":<{wu}}  {"dir":<3}  '
           f'{"Baseline":>14} {"+-%":>6}  {"Defense":>14} {"+-%":>6}  '
           f'{"Overhead%":>10}')
    print(hdr)
    print("-" * len(hdr))

    for r in sorted(rows, key=lambda r: (r["title"], r["desc"])):
        # "~" = delta is smaller than the samples' own scatter (unreadable);
        # "!" = a regression big enough to clear that scatter.
        if r["in_noise"]:
            flag = "~"
        elif r["oh"] >= 5:
            flag = "!"
        else:
            flag = " "
        bcv = "   n/a" if r["base_cv"] is None else f'{r["base_cv"]:>6.2f}'
        dcv = "   n/a" if r["def_cv"] is None else f'{r["def_cv"]:>6.2f}'
        print(f'{r["title"]:<{wt}}  {r["desc"]:<{wd}}  {r["scale"]:<{wu}}  '
              f'{("HIB" if r["prop"]=="HIB" else "LIB"):<3}  '
              f'{r["base"]:>14,.2f} {bcv}  {r["def"]:>14,.2f} {dcv}  '
              f'{r["oh"]:>9.2f}{flag}')

    # A negative overhead is the defense measuring faster than the baseline,
    # which it cannot really be doing -- with --clamp-negative those count as
    # 0 in the aggregates instead of offsetting real regressions.  The table
    # above is untouched either way.
    def agg(oh):
        return 0.0 if (args.clamp_negative and oh < 0) else oh

    n_clamped = sum(1 for r in rows if r["oh"] < 0)
    if args.clamp_negative:
        print(f"\n[--clamp-negative] {n_clamped}/{len(rows)} metrics measured "
              "negative (defense faster) and count as 0.00% in every average "
              "below; the table above is unchanged.")

    ohs = [agg(r["oh"]) for r in rows]
    n = len(ohs)
    mean = sum(ohs) / n
    ohs_sorted = sorted(ohs)
    median = (ohs_sorted[n // 2] if n % 2
              else (ohs_sorted[n // 2 - 1] + ohs_sorted[n // 2]) / 2)
    worst = max(rows, key=lambda r: r["oh"])
    best = min(rows, key=lambda r: r["oh"])
    n_regress = sum(1 for o in ohs if o > 0)

    print("-" * len(hdr))

    # Per-test-family mean overhead (overhead is often bimodal: syscall/IPC
    # heavy tests pay a lot, pure-compute tests pay ~nothing).
    by_family = defaultdict(list)
    for r in rows:
        by_family[r["title"]].append(agg(r["oh"]))
    print("\nPer-test-family mean overhead:")
    for title in sorted(by_family, key=lambda t: -sum(by_family[t]) / len(by_family[t])):
        fam = by_family[title]
        print(f"  {title:<34} {sum(fam)/len(fam):+7.2f}%   "
              f"(n={len(fam)})")

    noisy = [r for r in rows if r["in_noise"]]
    solid = [r for r in rows if not r["in_noise"]]
    cvs = [r["base_cv"] for r in rows if r["base_cv"] is not None] + \
          [r["def_cv"] for r in rows if r["def_cv"] is not None]

    print("\nDispersion (per-metric CV of the iteration samples):")
    if cvs:
        cvs_sorted = sorted(cvs)
        med_cv = (cvs_sorted[len(cvs) // 2] if len(cvs) % 2
                  else (cvs_sorted[len(cvs) // 2 - 1] + cvs_sorted[len(cvs) // 2]) / 2)
        print(f"  median CV        : {med_cv:.2f}%   max {max(cvs):.2f}%   "
              f"(iterations per metric: {min(r['n'] for r in rows)}"
              f"-{max(r['n'] for r in rows)})")
    else:
        print("  (no per-iteration samples found -- cannot estimate noise)")
    print(f"  within noise     : {len(noisy)}/{len(rows)} metrics have |overhead| "
          "smaller than their own sample scatter (marked ~)")
    if solid:
        s_oh = sorted(agg(r["oh"]) for r in solid)
        s_mean = sum(s_oh) / len(s_oh)
        s_med = (s_oh[len(s_oh) // 2] if len(s_oh) % 2
                 else (s_oh[len(s_oh) // 2 - 1] + s_oh[len(s_oh) // 2]) / 2)
        print(f"  excluding those  : mean {s_mean:+.2f}%  median {s_med:+.2f}%  "
              f"over {len(solid)} metrics")

    if meta.get("empty"):
        by_test = Counter(t for t, _ in meta["empty"])
        print(f"\nWARNING: {len(meta['empty'])} result(s) recorded NO value for "
              "any kernel and were dropped:")
        for t, cnt in sorted(by_test.items(), key=lambda kv: -kv[1]):
            print(f"  {t:<34} {cnt} metric(s)")
        print("  These tests ran but stored nothing -- check the test wrapper "
              "(cf. pts/nginx '-c 1').")

    if meta.get("abstract"):
        print(f"\nNOTE: {len(meta['abstract'])} result(s) have "
              "Proportion=ABSTRACT (not a directional")
        print("      metric -- pass/fail or image-quality) and were not "
              "compared:")
        for title, desc, scale, _ in meta["abstract"]:
            print(f"  {title} / {desc} [{scale}]")

    if meta.get("skipped"):
        print(f"\nNOTE: {len(meta['skipped'])} lmbench section(s) were not "
              "compared.  These are bw_mem / bcopy /")
        print("      lat_mmap / file-read curves: they measure memory and "
              "storage throughput rather")
        print("      than kernel behaviour, which is why the sweep set "
              "BENCHMARK_HARDWARE=NO.  Listed")
        print("      so a section that SHOULD be compared cannot go missing "
              "unnoticed:")
        for name in meta["skipped"]:
            print(f"  {name}")

    clamp_note = "  [negatives clamped to 0]" if args.clamp_negative else ""
    print("\nSummary (overhead = defense performance penalty vs baseline):"
          + clamp_note)
    print(f"  mean overhead    : {mean:+.2f}%   over {n} metrics")
    print(f"  median overhead  : {median:+.2f}%")
    print(f"  worst regression : {worst['oh']:+.2f}%  "
          f"({worst['title']} / {worst['desc']})")
    print(f"  best / fastest   : {best['oh']:+.2f}%  (as measured)  "
          f"({best['title']} / {best['desc']})")
    print(f"  metrics slower   : {n_regress}/{n}  "
          f"(positive overhead)")

    if meta.get("only_base") or meta.get("only_def"):
        print("\nNot compared (measured in only one group):")
        for name in meta.get("only_base", []):
            print(f"  baseline-only: {name}")
        for name in meta.get("only_def", []):
            print(f"  defense-only : {name}")


def emit_csv(rows, header):
    if header:
        print("suite,test,metric,unit,direction,baseline,baseline_cv_pct,"
              "defense,defense_cv_pct,overhead_pct,noise_pct,within_noise,n")
    for r in rows:
        bcv = "" if r["base_cv"] is None else f'{r["base_cv"]:.2f}'
        dcv = "" if r["def_cv"] is None else f'{r["def_cv"]:.2f}'
        nz = "" if r["noise"] is None else f'{r["noise"]:.2f}'
        print(f'{r["suite"]},{r["title"]},{r["desc"]},{r["scale"]},{r["prop"]},'
              f'{r["base"]:.4f},{bcv},{r["def"]:.4f},{dcv},'
              f'{r["oh"]:.2f},{nz},{int(r["in_noise"])},{r["n"]}')


def main():
    args = parse_args()
    here = os.path.dirname(os.path.abspath(__file__))

    # --reference reads the results shipped with the artifact rather than
    # whatever this machine has produced, so the numbers behind the paper can
    # be reproduced without running (or even installing) either suite.
    if args.reference:
        ref = os.path.join(here, "..", "results")
        args.results_dir = os.path.join(ref, "pts")
        args.lmbench_dir = os.path.join(ref, "lmbench")
        if args.sweep is None:
            tag_file = os.path.join(args.results_dir, "SWEEP-TAG")
            if os.path.isfile(tag_file):
                args.sweep = open(tag_file).read().strip()

    if args.lmbench_dir is None:
        args.lmbench_dir = lmbench_dir_default(here)

    want_pts = args.suite in ("pts", "both")
    want_lmb = args.suite in ("lmbench", "both")

    sections = []

    if want_lmb:
        rows, err, meta = collect_lmbench(args.lmbench_dir,
                                          args.baseline, args.defense)
        if err:
            # In 'both' mode a missing suite is a skip, not a failure: the two
            # halves are installed and run independently.
            if args.suite == "lmbench":
                sys.exit(f"error: {err}")
            print(f"note: skipping lmbench -- {err}", file=sys.stderr)
        elif not rows:
            msg = "no lmbench metrics measured in BOTH groups"
            if args.suite == "lmbench":
                sys.exit(f"error: {msg} under {args.lmbench_dir}")
            print(f"note: skipping lmbench -- {msg}", file=sys.stderr)
        else:
            meta["source"] = display_path(meta["source"], here)
            sections.append((rows, meta))

    if want_pts:
        sections.append(collect_pts(args))

    if not sections:
        sys.exit("error: no results found for either suite")

    if args.csv:
        for i, (rows, _) in enumerate(sections):
            emit_csv(rows, header=(i == 0))
        return

    for i, (rows, meta) in enumerate(sections):
        if i:
            print("\n" + "=" * 100 + "\n")
        report(rows, meta, args)


if __name__ == "__main__":
    main()
