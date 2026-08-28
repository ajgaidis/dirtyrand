#!/bin/bash
# Apply the out-of-tree fixes this harness depends on.
#
# Four PTS profiles/commands are broken in ways that FAIL SILENTLY -- they
# record an empty value, or no value at all, while batch-run still exits 0.
# Each patch below is the reason a metric exists in the reference results.
# Re-run this after ANY `phoronix-test-suite install <test>`: a profile's
# install.sh regenerates its wrapper and silently reverts the patch.
#
#   ./apply-patches.sh            apply everything
#   ./apply-patches.sh --check    report state only, change nothing
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PTS_HOME="${PTS_HOME:-$HOME/.phoronix-test-suite}"
INSTALLED="${PTS_HOME}/installed-tests/pts"
PTS_CORE="${PTS_CORE:-/usr/share/phoronix-test-suite}"

CHECK=0
[[ "${1:-}" == "--check" ]] && CHECK=1

rc=0
note() { printf '[+] %s\n' "$*"; }
warn() { printf '[!] %s\n' "$*" >&2; }
skip() { printf '[-] %s\n' "$*"; }

# apply <patch> <target-file> [sudo]
apply() {
	local patch="$1" target="$2" use_sudo="${3:-}"
	local SUDO=""; [[ -n "$use_sudo" ]] && SUDO="sudo"

	if [[ ! -f "$target" ]]; then
		skip "not installed, skipping: $target"
		return 0
	fi
	if $SUDO patch --dry-run -sf -p1 -R "$target" < "$patch" >/dev/null 2>&1; then
		note "already patched: $target"
		return 0
	fi
	if ! $SUDO patch --dry-run -sf -p1 "$target" < "$patch" >/dev/null 2>&1; then
		warn "does NOT apply cleanly (upstream changed?): $target"
		rc=1
		return 0
	fi
	if [[ $CHECK -eq 1 ]]; then
		warn "NEEDS PATCH: $target"
		rc=1
		return 0
	fi
	$SUDO cp -n "$target" "${target}.orig.bak"
	$SUDO patch -sf -p1 "$target" < "$patch" && note "patched: $target"
}

echo "=== PTS test wrappers ==="
# redis: without `save ""` the wrapper's SIGTERM shutdown writes a dump.rdb
# that the NEXT run loads, so the keyspace accumulates across runs until redis
# only ever answers LOADING and redis-benchmark writes a header-only CSV.
apply "$HERE/patches/pts/redis-1.5.0.patch" "$INSTALLED/redis-1.5.0/redis"
apply "$HERE/patches/pts/redis-1.5.1.patch" "$INSTALLED/redis-1.5.1/redis"
# nginx: the wrapper hardcodes -t $NUM_CPU_CORES, but wrk needs
# connections >= threads, so the low-connection menu entries die and record
# an empty <Value>. Clamp threads to the -c value.
apply "$HERE/patches/pts/nginx-3.1.0.patch" "$INSTALLED/nginx-3.1.0/nginx"

echo "=== PTS core command ==="
# compare-results-two-way dies with
#   ValueError: str_repeat(): Argument #2 ($times) must be >= 0
# on any result file with few enough rows. Optional -- only needed if you
# want that command; parse-results.py does not use it.
apply "$HERE/patches/pts/compare_results_two_way.php.patch" \
      "$PTS_CORE/pts-core/commands/compare_results_two_way.php" sudo

echo "=== gnupg build shims (Debian 12) ==="
# pts/gnupg builds gnupg 2.2.27, whose m4 macros probe for gpg-error-config /
# ksba-config -- both dropped by bookworm for gpgrt-config + pkg-config.
# configure then claims "You need libgpg-error" even with libgpg-error-dev
# and libksba-dev installed. Only the probe is broken.
for shim in gpg-error-config ksba-config; do
	if command -v "$shim" >/dev/null 2>&1; then
		note "already on PATH: $shim"
	elif [[ $CHECK -eq 1 ]]; then
		warn "MISSING shim: $shim (would install to ~/.local/bin)"
		rc=1
	else
		mkdir -p "$HOME/.local/bin"
		install -m 0755 "$HERE/patches/shims/$shim" "$HOME/.local/bin/$shim"
		note "installed: ~/.local/bin/$shim"
		case ":$PATH:" in
			*":$HOME/.local/bin:"*) ;;
			*) warn "~/.local/bin is not on PATH -- add it before installing pts/gnupg" ;;
		esac
	fi
done

echo
if [[ $rc -eq 0 ]]; then
	note "All patches present."
else
	[[ $CHECK -eq 1 ]] && warn "Run without --check to apply." \
	                   || warn "Some patches did not apply -- see above."
fi
exit $rc
