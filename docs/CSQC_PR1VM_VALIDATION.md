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
