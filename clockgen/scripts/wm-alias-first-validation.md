# WM Alias-First Migration Changelog and Validation Checklist

## Changelog (this branch)

- Clockgen capture scripts now warn when they fall back to raw ALSA identifiers.
- Script messaging now points operators to alias-based ALSA configuration as the preferred path.
- Existing fallback behavior is preserved for compatibility when aliases are not installed.

## WM Hardware Validation Checklist

Validation steps:
1. Run shell syntax checks.
   - `bash -n clockgen/scripts/capture-vhs.sh`
   - `bash -n clockgen/scripts/capture-test.sh`
   - `bash -n clockgen/scripts/collect-info.sh`
2. Run each script with alias configured.
   - Expected: no fallback warning is printed.
3. Run each script with alias intentionally unset.
   - Expected: warning clearly indicates fallback to raw ALSA and migration guidance.
4. Capture script logs for branch-test evidence.
   - Expected: warning text is present only in fallback scenario.

## Migration Notes

- Preferred audio path: ALSA aliases configured on host (for example via `/etc/asound.conf`).
- Compatibility fallback remains: raw `hw:CARD=...` values when alias is unavailable.
- Warning output is intentional and should be treated as migration debt, not immediate failure.
