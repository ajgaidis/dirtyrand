#!/bin/bash
set -euo pipefail

# Run the full benchmark set (lmbench then PTS) for whichever kernel is
# currently booted. A complete comparison is TWO invocations separated by a
# reboot:
#
#     boot 1 (vanilla kernel)  $ ./run-bmks.sh --new-sweep
#     ... reboot into the rnguard kernel ...
#     boot 2 (rnguard kernel)    $ ./run-bmks.sh
#     $ ./parse-results.py
#
# --new-sweep mints a fresh PTS sweep tag; a bare run merges into the tag in
# .sweep-tag so both halves land in one result dir. Which HALF you are running
# is therefore an explicit choice, while WHICH KERNEL it is never is -- that
# comes from uname -r, the same source run-pts.sh uses, so lmbench and PTS
# cannot end up disagreeing about what they just measured.

cd "$(dirname "${BASH_SOURCE[0]}")"

detect_variant() {
	# The legacy *krng-* spellings are the pre-rename internal names; kept so a
	# kernel built before the RNGuard rename still labels its half correctly.
	case "$(uname -r)" in
		*rnguard-vanilla*|*krng-vanilla*) echo vanilla ;;
		*rnguard-mpk*|*krng-pkeys*)       echo rnguard ;;
		*) return 1 ;;
	esac
}

usage() {
	cat <<USAGE
usage: ${0##*/} [--new-sweep] [--variant vanilla|rnguard] [--skip-lmbench|--skip-pts]

  --new-sweep    start a fresh PTS sweep (use on the FIRST of the two boots)
  --variant V    override kernel auto-detection (normally unnecessary)
  --skip-lmbench run only the PTS half
  --skip-pts     run only the lmbench half
USAGE
}

NEW_SWEEP=0 VARIANT_ARG="" DO_LMBENCH=1 DO_PTS=1
while [[ $# -gt 0 ]]; do
	case "$1" in
		--new-sweep)    NEW_SWEEP=1; shift ;;
		--variant)      VARIANT_ARG="${2:?--variant needs a value}"; shift 2 ;;
		--skip-lmbench) DO_LMBENCH=0; shift ;;
		--skip-pts)     DO_PTS=0; shift ;;
		-h|--help)      usage; exit 0 ;;
		*) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
	esac
done

DETECTED="$(detect_variant || true)"
VARIANT="${VARIANT_ARG:-${DETECTED}}"

if [[ -z "${VARIANT}" ]]; then
	cat >&2 <<ERR
[!] Cannot tell which kernel this is: uname -r = $(uname -r)
    Expected *rnguard-vanilla* or *rnguard-mpk*. You are probably on the distro
    kernel. Reboot into an RNGuard kernel, or force it with --variant.
ERR
	exit 1
fi

if [[ -n "${VARIANT_ARG}" && -n "${DETECTED}" && "${VARIANT_ARG}" != "${DETECTED}" ]]; then
	echo "[!] --variant ${VARIANT_ARG} contradicts the booted kernel ($(uname -r) => ${DETECTED})." >&2
	echo "[!] Ctrl-C now if that is wrong." >&2
	sleep 5
fi

# Starting a new sweep abandons (does not delete) whatever pair is in flight.
if [[ ${NEW_SWEEP} -eq 1 && -s .sweep-tag ]]; then
	echo "[!] --new-sweep: the in-flight sweep '$(< .sweep-tag)' will be left"
	echo "[!] as-is and a new tag minted. Ctrl-C if you meant to continue it."
	sleep 5
fi
if [[ ${NEW_SWEEP} -eq 0 && ! -s .sweep-tag && ${DO_PTS} -eq 1 ]]; then
	echo "[!] No .sweep-tag found -- this looks like the first of the two boots." >&2
	echo "[!] Re-run with --new-sweep to start a sweep." >&2
	exit 1
fi

if [[ ${NEW_SWEEP} -eq 1 ]]; then
	SWEEP_DESC="(new tag will be minted)"
elif [[ -s .sweep-tag ]]; then
	SWEEP_DESC="$(< .sweep-tag)"
else
	SWEEP_DESC="(none)"
fi
echo "[+] kernel  : $(uname -r)"
echo "[+] variant : ${VARIANT}"
echo "[+] sweep   : ${SWEEP_DESC}"

# lmbench names its output files ${variant}.N and will overwrite an existing
# set for the same variant without asking, so refuse rather than clobber.
LMB_RESULTS="${RNGUARD_LMBENCH_DIR:-../lmbench/lmbench-3.0-a9}/results/$(uname -m)-linux-gnu"
if [[ ${DO_LMBENCH} -eq 1 ]] && compgen -G "${LMB_RESULTS}/${VARIANT}.[0-9]*" >/dev/null; then
	echo "[!] lmbench results for '${VARIANT}' already exist in ${LMB_RESULTS}:" >&2
	ls -1 "${LMB_RESULTS}/${VARIANT}."[0-9]* | sed 's/^/[!]   /' >&2
	echo "[!] They would be overwritten. Move them aside, or pass --skip-lmbench." >&2
	exit 1
fi

if [[ ${DO_LMBENCH} -eq 1 ]]; then
	echo "[+] === lmbench (${VARIANT}) ==="
	./run-lmbench.sh "${VARIANT}"
	sleep 10   # token settle; run-pts.sh does the real thermal cooldown
fi

if [[ ${DO_PTS} -eq 1 ]]; then
	echo "[+] === PTS (${VARIANT}) ==="
	if [[ ${NEW_SWEEP} -eq 1 ]]; then
		./run-pts.sh --variant "${VARIANT}" --new-tag
	else
		./run-pts.sh --variant "${VARIANT}"
	fi
fi

echo "[+] Done: ${VARIANT} half complete."
if [[ ${DO_PTS} -eq 1 && -s .sweep-tag ]]; then
	echo "[+] Sweep tag: $(< .sweep-tag)"
fi
