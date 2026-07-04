# Scripts

Helper scripts related to the clockgen firmware and capture workflow.

## [capture-vhs.sh](capture-vhs.sh)

A simple bash script to record all 3 streams from a VHS using the clock generator.
- RF Video from a CXADC
- RF Audio from a CXADC
- Linear Audio

Common environment overrides:
- CLOCK_GEN_ALSA_DEVICE (default: hw:CARD=CXADCADCClockGe)
- CXCARD_VIDEO_DEVICE / CXCARD_AUDIO_DEVICE
- CXCARD_VIDEO_CLOCK / CXCARD_AUDIO_CLOCK

To discover the ALSA device name on your host:

```bash
arecord -L | grep -i -E "clock|cxadc|uac"
```

## [collect-info.sh](collect-info.sh)

A simple bash script to collect system info for troubleshooting.
Download to your linux box and run with:

```bash
bash collect-info.sh
```

It will create a *.tar.gz* that contains the relevant info.

If you want to, you can run one of the following commands to download and execute on the fly.
However, be aware that you are blindly trusting the source URL and script to [not be malicious](https://0x46.net/thoughts/2019/04/27/piping-curl-to-shell/).
Even though convenient, review the script content before running this:

```bash
# if you have curl installed
curl https://raw.githubusercontent.com/happycube/cxadc-linux3/master/clockgen/scripts/collect-info.sh | bash
# if you have wget installed
wget -O - https://raw.githubusercontent.com/happycube/cxadc-linux3/master/clockgen/scripts/collect-info.sh | bash
```
 
