# MISRA baseline

The audit reports zero MISRA findings, so the committed baselines accept nothing
and CI gates on a clean run rather than a ratchet. There is one per analysed
configuration:

- `misra-baseline.json` - default GNU `__atomic` backend
- `misra-baseline.c11-atomics.json` - C11 `<stdatomic.h>` profile
- `misra-baseline.no-atomics.json` - `STATUS_USE_NO_ATOMICS` profile

If a deliberate review accepts new findings, regenerate the matching baseline:

```sh
misch baseline
misch baseline --profile c11-atomics
misch baseline --profile no-atomics
misch run --baseline
```

Commit the resulting JSON so CI can reject findings above the accepted counts.
Regenerate only after a deliberate review; do not edit these files by hand.
