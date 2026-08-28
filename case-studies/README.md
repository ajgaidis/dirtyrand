# Case studies

The seven end-to-end exploits of Table 1. Each shows a security mechanism
losing its guarantee once the RNG beneath it is predictable.

| Directory | Paper | Predicted value | Level | Outcome |
|---|---|---|---|---|
| `canary-user/` | §5.1.1 | `AT_RANDOM` | L2 | mitigation bypass |
| `canary-kernel/` | §5.1.1 | task canary | L3 | mitigation bypass |
| `aslr/` | §5.1.2 | stack base | L3 | mitigation bypass |
| `slub-freelist/` | §5.1.3 | `s->random` | L3 | mitigation bypass |
| `dns/` | §5.2.1 | ephemeral port, TXID | L2, L3 | cache poisoning |
| `tcp/` | §5.2.2 | ephemeral port, ISN | L2, L3 | RST hijack |
| `tls/` | §5.2.3 | GnuTLS PRNG seed | L2 | PMS recovery |

`setup.sh` provisions the three-VM routed topology that `dns/`, `tcp/` and
`tls/` require. The rest need a single VM.
