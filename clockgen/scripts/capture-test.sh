#!/usr/bin/env bash
CAPTURE_SECONDS=1920
ALSA_SAMPLE_RATE=46875
ALSA_PERIOD=12000
ALSA_BUFFER=$((ALSA_SAMPLE_RATE * 5))
CLOCK_GEN_ALSA_DEVICE="${CLOCK_GEN_ALSA_DEVICE:-vcr0-clockgen-audio}"
CLOCK_GEN_ALSA_DEVICE_FALLBACK="${CLOCK_GEN_ALSA_DEVICE_FALLBACK:-hw:CARD=CXADCADCClockGe}"
OUT="/mnt/cx_storage/Testicles/test"
# OUT="/mnt/cx_storage/Kim_Brown/VHS-C/_CLOCKGEN/clockgen-capture-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
sudo -v

if ! arecord -L | grep -Fxq "$CLOCK_GEN_ALSA_DEVICE"; then
  CLOCK_GEN_ALSA_DEVICE="$CLOCK_GEN_ALSA_DEVICE_FALLBACK"
  echo "WARNING: ALSA alias was not found; falling back to '$CLOCK_GEN_ALSA_DEVICE_FALLBACK'." >&2
  echo "         Install host aliases in /etc/asound.conf for stable routing." >&2
fi

if sudo fuser /dev/cxadc0 > /dev/null 2>&1; then
  echo "ERROR: /dev/cxadc0 is already open by another process:" >&2
  sudo fuser -v /dev/cxadc0 2>&1 >&2
  return 1 2>/dev/null || exit 1
fi

if sudo fuser /dev/snd/pcmC*D*c > /dev/null 2>&1; then
  echo "ERROR: An ALSA PCM capture device is already open by another process:" >&2
  sudo fuser -v /dev/snd/pcmC*D*c 2>&1 >&2
  return 1 2>/dev/null || exit 1
fi

amixer -D "$CLOCK_GEN_ALSA_DEVICE" sset 'CXADC-Clock 0 Select Playback Source,0' CXADC-40MHz
amixer -D "$CLOCK_GEN_ALSA_DEVICE" cset name='Audio Control Capture Switch' on

printf "Started: %s\nDuration: %s seconds\nCXADC: /dev/cxadc0\nClock: 40MHz\nAudio: %s Hz, 3ch, S24_3LE WAV\n" \
  "$(date -Is)" "$CAPTURE_SECONDS" "$ALSA_SAMPLE_RATE" > "$OUT/capture-info.txt"
printf "ALSA device: %s\n" "$CLOCK_GEN_ALSA_DEVICE" >> "$OUT/capture-info.txt"

timeout --signal=INT --kill-after=10s "$CAPTURE_SECONDS" \
  sudo dd if=/dev/cxadc0 bs=4M 2>"$OUT/rf-dd.log" |
pv --timer --rate --bytes --buffer-size 8m \
  > "$OUT/rf-video-40MHz.u8" &
rf_pid=$!

timeout --signal=INT --kill-after=10s "$CAPTURE_SECONDS" \
  arecord -D "$CLOCK_GEN_ALSA_DEVICE" \
    -c 3 \
    `# deliberately 3ch: this test exercises the head-switch channel` \
    -r "$ALSA_SAMPLE_RATE" \
    -f S24_3LE \
    --period-size="$ALSA_PERIOD" \
    --buffer-size="$ALSA_BUFFER" \
    "$OUT/linear-audio-${ALSA_SAMPLE_RATE}sps-3ch-24bit-le.wav" \
    2>"$OUT/audio-arecord.log" &
audio_pid=$!

cleanup() {
  kill -INT "$rf_pid" "$audio_pid" 2>/dev/null
  wait "$rf_pid" "$audio_pid" 2>/dev/null
}
trap cleanup INT TERM

if [[ -t 0 ]]; then
  printf 'Press q to stop capture early.\n' >&2
fi

while kill -0 "$rf_pid" 2>/dev/null || kill -0 "$audio_pid" 2>/dev/null; do
  if [[ -t 0 ]] && read -r -s -n 1 -t 1 key 2>/dev/null && [[ "$key" == [qQ] ]]; then
    printf '\nStopping capture early.\n' >&2
    printf 'Early exit: user pressed q at %s\n' "$(date -Is)" >> "$OUT/capture-info.txt"
    kill -INT "$rf_pid" 2>/dev/null
    kill -INT "$audio_pid" 2>/dev/null
    break
  fi
done

wait "$rf_pid"; rf_status=$?
wait "$audio_pid"; audio_status=$?

printf "Finished: %s\nRF status: %s\nAudio status: %s\n" \
  "$(date -Is)" "$rf_status" "$audio_status" >> "$OUT/capture-info.txt"

ls -lh "$OUT"
echo "Saved capture to $OUT"
