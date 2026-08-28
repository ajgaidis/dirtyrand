# DirtyRand

The attack itself, in three parts.

| Directory | Paper | What it is |
|---|---|---|
| `lib/` | §4.1 | A user-space implementation of the kernel's ChaCha20 hierarchy, as a standalone command-line tool. Seeded with the key an attacker zapped in, it replays the exact byte stream the kernel will subsequently produce, at L1, L2 or L3. |
| `primitives/kmod/` | §3 | A synthetic module exposing exactly the restricted write of the threat model: up to 33 bytes of attacker-known, not attacker-chosen data at a known address. Use it to study a case study in isolation from the 1-day. |
| `primitives/poc/` | §5 | The same primitive backed by a real vulnerability — a forward-port of CVE-2022-34918 in netfilter's `nf_tables`, which turns a plain pipe write into a controlled physical-memory write. |
| `reseed-halt/` | §4.2 | Sets the `pending` bit of the `crng_reseed` work item so the kernel drops its own re-queue, stopping the 60-second reseed and making a corruption permanent. Includes the timing side channel that locates the reseed window. |

Both primitives expose the same interface, so every case study builds twice —
`<name>-kmod` and `<name>-poc` — and switching between them is a relink.

## Where the RNG model lives

The same ChaCha20 model appears in three places, and deliberately so — each is
self-contained, so a case study can be built and studied without the rest of
the tree:

| Copy | Form | Used by |
|---|---|---|
| `lib/chacha.c` | CLI tool, has `main()` | run directly, for exploring predictions by hand |
| `../case-studies/canary-kernel/lib/chacha/chacha_lib.c` | linkable library | the kernel-canary exploit |
| `../case-studies/slub-freelist/lib/chacha/chacha_lib.c` | linkable library | the DCCP/SLUB exploits |

The two library copies are byte-identical. The CLI differs from them in one
line: on an invalid round count it aborts with an error, where the library
returns silently — appropriate to each, and unreachable in both, since every
caller requests 20 rounds.
