# Presets and I/Q export

[Back to the README](../README.md)

## Presets

The **Presets** section accepts a complete path (default `default.preset`). Save
writes the versioned text format below. Load parses into a temporary configuration
and applies it only after every field passes validation. Errors leave current
settings and the previous result intact. Saving replaces the selected preset.

```ini
[IQ Generator Preset]
Version=2
Modulation=BPSK
NumberOfSymbols=256
SymbolRateBaud=1000
SamplesPerSymbol=8
Pulse=RRC
RRCBeta=0.20000000000000001
SpanSymbols=10
AmplitudeGain=1
DataSource=Random
Seed=5489
Bits=
NoiseSampleCount=2048
NoiseSampleRateHz=8000
NoisePower=1
AwgnEnabled=false
AwgnSnrDb=10
NoiseSeed=5490
```

All version-2 fields are required; version 3 additionally requires the impairment
fields listed in "Channel impairments". Version 4 is written for 2-FSK, 4-FSK and
MSK: it adds `ToneSpacingHz` and always includes the impairment fields, and FSK
waveforms are rejected in older versions. Version 1 only knows BPSK, QPSK and
16-QAM. Modulation accepts `BPSK`, `QPSK`, `8-PSK`, `16-QAM`, `32-QAM`, `64-QAM`, `256-QAM`,
`OOK`, `4-PAM`, `DBPSK`, `DQPSK`, `pi/4-DQPSK`, `8-DPSK`, `OQPSK`, `4-ASK`, `2-FSK`, `4-FSK`, `MSK`, `WGN` case-insensitively; pulse accepts `RRC`,
`Rectangular`; source accepts `Random`, `Explicit`; booleans accept `true`,
`false`. Numbers use a locale-independent decimal point.
Unknown/duplicate fields, unsupported versions, non-finite numbers, and trailing
numeric junk are rejected. `Bits` must be empty or binary, even when dormant in
random mode. Fields belonging to an inactive waveform family persist but are
still validated, so presets fail transactionally. Files are limited to 1 MiB.
LF and CRLF are supported.

Version-1 presets and legacy `[SignalGenerator Preset]` files continue to
import, receiving noise-disabled defaults (`AwgnEnabled=false`, `NoiseSeed=5490`
and the WGN defaults above); version-1 files must not contain version-2 fields
or the new waveform names. Legacy files import `NumberOfSymbols`,
`SamplesPerSymbol`, and `RRCBeta`; the obsolete `ConstellationStart` field is
ignored. Legacy settings still must satisfy current numerical validation. New
saves use version 2, or version 3 when channel impairments are active.

## I/Q export

**Export I/Q...** captures the last completed result when the dialog opens.
Choose a destination path and format. The dialog checks both sample and metadata
paths and requires explicit confirmation to overwrite. Extension selection is
manual. Exporting while generation is pending exports the captured completed
result, even if a newer result finishes while the dialog is open.

CSV is UTF-8/ASCII text with header `time_s,i,q`, followed by one complex sample
per row. Decimal precision preserves the stored float32 components and double
timestamps on round trip. Binary is headerless little-endian IEEE-754 float32,
interleaved as `I0,Q0,I1,Q1,...`, eight bytes per complex sample.

Both formats write `<destination>.json`. Its version-2 object records the
`format` (`csv` or `cf32_le`), `waveform`, `family` (`linear` or `noise`),
`sample_count`, `sample_rate_hz`, `scale` (gain already applied),
`sample_units`, and `timing`. Linear results add `symbol_energy`, the complete
`configuration` (including seed, explicit bits and PRNG rule), and an `awgn`
provenance block (requested SNR, reference power and interval, added noise
power, noise seed) when noise was applied. Noise results add a `noise_source`
block instead and never claim symbol energy or matched-symbol observations. Do
not multiply exported samples by `scale` again. Write, flush and close failures
are reported. The two-file operation is not atomic; a storage failure can leave
partial files, including after confirmed replacement. Retry after correcting the
destination.

Example binary import with NumPy:

```python
import json
import numpy as np
with open("signal.iq.json", encoding="utf-8") as f:
    metadata = json.load(f)
values = np.fromfile("signal.iq", dtype="<f4").reshape(-1, 2)
iq = values[:, 0] + 1j * values[:, 1]
assert len(iq) == metadata["sample_count"]
```

### SigMF

`--format sigmf` (or **SigMF** in the export dialog) writes the same little-endian
float32 samples as `cf32` into `<name>.sigmf-data` and a [SigMF](https://sigmf.org)
1.0.0 description into `<name>.sigmf-meta`. Give a destination ending in `.sigmf-data`;
the meta file is its sibling and both follow the same overwrite rules as the other
formats. The `global` object holds `core:datatype` (`cf32_le`), `core:sample_rate`,
`core:version`, `core:description` and `core:recorder`, followed by siggen's own
fields in the `siggen:` namespace: `siggen:preset` (the configuration in the preset
text format, which is what lets `siggen analyze` and the Python package re-measure the
file) and `siggen:metadata` (the version-2 object described above). `captures` has one
segment starting at sample 0, and `annotations` is empty. Other SigMF tools ignore
the `siggen:` fields. Batches keep `csv` and `cf32`.
