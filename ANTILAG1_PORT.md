# Antilag 1 client scope

QW-Group/ezQuake already carries the Antilag 1 protocol work. This branch
tracks validation and any small compatibility fixes required by the matching
KTX and MVDSV ports.

The client scope is limited to the existing Antilag 1 protocol behaviour:
accurate timings, weapon prediction, and simple projectiles.

## Validation boundary

The base commit already includes the client-side protocol negotiation,
predicted weapon state, predicted projectiles and the user-facing opt-in
controls. This branch intentionally adds documentation only: it is the
client-side reference point for validating the matching qwprot, MVDSV and KTX
branches, not a second implementation of the same feature.
