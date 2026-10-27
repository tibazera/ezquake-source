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

## PR #1163 state at the fifth pass (7cae3630)

Source: angeld's fifth-pass comment (2026-10-26) and dsvensson's re-check of
eb282eb9.

- S44 (sprintf typing): angeld reports it fixed in 7cae363. The FTE comparison
  above agrees for the domain selection. The float -> int32 cast (Divergence 1)
  was not raised in the review, so it is new input for the S44 thread.
- S87 (itof/ftoi): still unimplemented by design. The stated reason is now
  byte-offset entities. dsvensson's point stands: fteqcc emits these calls for
  casts, so modules built with fteextensions.qc get 0/world.
- S71 (#0 names): fixed in 1e877c87. The call aborts like PF_Fixme, but the
  module is disabled instead of the client disconnecting.
- Still open per dsvensson: S17 (string ring / #500 copies; #500 was removed in
  a9a485b), S24 (csprogs loaded after begin), S30, S51 (world writable
  window, OP_STATE check), svcfte_effect 74/75 local defines (waiting on qwprot).
- 48 "Acknowledged" low/medium items are deferred to
  docs/plans/csqc_pr1163_residual_findings.md, waiting on dsvensson's OK.
- angeld's baseline: csqc_selftest pass=313 fail=2 skip=2 (R6 buf chain,
  WS2-C setkeybind gate, which is in PR-2). Our run should match this.

## Fixes on this branch after 7cae363 (to validate locally)

Commits fb3f9e8, 3e66300, 98b3529, 2a8a74e, eea1513. Only syntax/-O2 compile
checks were run (Linux gcc); nothing was executed.

| Area | Fix | How to check |
|---|---|---|
| sprintf #627 | float-domain %d/%u/%x as int64, saturating | `sprintf("%d %u %x", 3000000000, -1, -1)` -> `3000000000 18446744073709551615 ffffffffffffffff` |
| cvar #45 | no vm deref before NULL check | - |
| te_bloodshower #406 | howmany from PARM3 | effect count follows the 4th arg |
| input_timelength | clamp before int cast | NaN/huge value -> msec 1/255, no crash |
| csqc_client.c | include qsound.h (pointer truncation on 64-bit) | svc 77 sound precache on a 64-bit build |
| MSG_ReadFloat | bounded, sets msg_badread | readfloat loop past the end returns -1 |
| SCR_CenterPrint | line count bounded | cprint of 1100 newlines, no crash |
| strftime #478 | bounded copy, NULL tm, safe conversions | `strftime(0, "%Q %Y %")` does not abort on Windows |
| Draw_CachePicSafe | .lmp size checked | `drawpic(..., "gfx/palette.lmp", ...)` fails soft; HUD/console pics still load |
| spawn #14 | errors on a full pool | - |
| strunzone | notices only with developer 1 | - |
| Mod_FindName | long names match their truncated slot | `.model` with a 70-char name, no slot leak |
| strncmp #228 | s2ofs applied | `strncmp("abc", "xbc", 2, 1, 1)` == 0 |
| bitshift #218 | no UB for shift >= 32 / negative | `bitshift(1, 40)` == 0 |
| checkcommand #294 | alias -> 2 | an alias name returns 2 |
| Disconnect | clears sent-command history | reconnect, prediction stays sane |
| TranslateMarkup | colour state initialised | `^s` before any colour |
| strreplace #484/#485 | integer-safe bound | - |
| csprogs cache | no write on a truncated path | - |

Expected selftest baseline is still pass=313 fail=2 skip=2; a selftest that
asserted an old behaviour (e.g. checkcommand of an alias == 0, strunzone
console spam) may need its expectation updated.
