# CSQC PR1VM validation handoff

Base under test: `angeld29/ezquake-source` branch `csqc/pr1vm-rewrite`, commit
`7cae363088ac6c5ccaa660a283eb4788d77f0959`.

This branch is a validation workspace only. Do not open an upstream PR from it
and do not mix in FTE v7, KTX weapon behaviour, or EZCSQC changes.

## Why this exists

Angeld's fifth review pass changes CSQC `sprintf` typing for raw integer slots
and updates PR1VM canaries. Nano asked for a safe generic CSQC baseline, with
v6/vanilla csprogs first.

## Required validation

1. Build the isolated client (`mingw64-x64-relwithdebinfo`).
2. Connect to the local TF2003 CSQC v6 server; select Scout manually and run
   `csqc_selftest`. Record the exact pass/fail/skip result.
3. Run KTX with its installed v6 `csprogs.dat`; confirm load and connection.
4. Run the CSQC-disabled control against KTX.
5. Exercise the `sprintf` integer canaries against FTE behaviour. Report
   divergence with minimal input and output, but do not patch it without first
   checking the intended FTE semantics.

## Existing local evidence

- The TF2003 CSQC suite previously reached `313 pass / 2 fail / 2 skip` after
  the generic client security fixes. The remaining wire failures were traced to
  an mvdsv string-clientstat crash and fixed independently in mvdsv.
- `mvdsv` fix is isolated separately; do not cherry-pick it into this client
  validation branch.
- Current build setup is isolated in `E:\Projetos Linux\estudoantilag\angeld-pr1vm-rewrite`.

## Reporting

Keep findings in the workspace `REGISTRO.md` in Portuguese. A useful report to
Nano/angeld needs the tested commit, module/version, control result, and a
minimal reproducer for every failure.

## Step 5 result: `sprintf` vs FTE `PF_sprintf_internal` (static review)

Reference: fte-team/fteqw `engine/common/pr_bgcmd.c` `PF_sprintf_internal`
(master, read 2026-10-27). Compared against `src/csqc_builtins.c` `csqc_sprintf`
at `7cae363`. Not patched; this needs angeld/Nano to confirm the intended semantics.

Matches FTE:
- `h` -> float, `l`/`L` -> raw int32, last modifier wins; `j`/`z`/`t` consumed.
- `%i` defaults to raw int; every other conversion defaults to float.
- `%v` with `l` reads the three slots as ints (FTE `GETARG_INTVECTOR`).
- Raw-int `%lu`/`%lx` read as uint32 (FTE `GETARG_UINT`), so `-1` -> `ffffffff`.
- A missing argument reads as 0; an unknown conversion stops formatting.

Divergence 1, float-domain integer conversions (`%d %u %x %X`, and `%hi`):
FTE converts the float to `qint64_t`/`quint64_t` and prints with `%lld`/`%llx`.
ezQuake converts with `(int)fv` / `(unsigned)(int)fv`. Values outside int32 then
differ, and `(int)` of an out-of-range float is undefined behaviour in C (x86
gives INT_MIN). Minimal reproducer (host cast results, gcc x86-64):

| QC call | ezQuake | FTE |
|---|---|---|
| `sprintf("%d", 3000000000)` | `-2147483648` | `3000000000` |
| `sprintf("%u", 3000000000)` | `2147483648` | `3000000000` |
| `sprintf("%x", -1)` | `ffffffff` | `ffffffffffffffff` |
| `sprintf("%u", -1)` | `4294967295` | `18446744073709551615` |

The negative float -> `quint64_t` cast in FTE is also UB in C, so the
`%x`/`%u` -1 rows are FTE's x86 behaviour, not a contract. The `%d` row (large
float) is the one worth fixing: cast through `long long`, clamped or
range-checked, to remove the UB and match FTE.

Divergence 2, `q` modifier: FTE reads an int64/double across two slots;
ezQuake consumes it (documented in the comment, v6 has no 64-bit type). OK
for v6.

Documented subsets (not bugs): no `*`/`%n$` width or precision, no
`%o %p %P %S %E %F %G %V %I`, and `%c` does not do FTE's UTF-8 encoding.

Steps 1-4 (mingw build, TF2003 `csqc_selftest`, KTX v6, CSQC-disabled
control) need the Windows workspace and local servers; they were not run here.
