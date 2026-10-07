# Modulations, mapping and reproducibility

[Back to the README](../README.md)

## Mapping and reproducibility

Mapped symbols have **unit average constellation energy**, before amplitude gain.
A particular random buffer need not have exactly unit empirical symbol energy.
There is no per-buffer renormalization. QPSK has two bits per symbol; 8-PSK
three; 16-QAM four; 32-QAM five; 64-QAM six; 256-QAM eight. Bits are consumed left to right in
symbol order.

| Modulation | Bits | Complex symbol before gain |
|---|---|---|
| BPSK | 0 | +1 + j0 |
| BPSK | 1 | -1 + j0 |
| QPSK | 00 | (+1 + j)/sqrt(2) |
| QPSK | 01 | (+1 - j)/sqrt(2) |
| QPSK | 11 | (-1 - j)/sqrt(2) |
| QPSK | 10 | (-1 + j)/sqrt(2) |

8-PSK uses unit radius and a fixed zero phase origin. At phases `k*pi/4`,
k = 0..7 counterclockwise, the Gray labels are:

| Phase index k | Bits |
|---|---|
| 0 | 000 |
| 1 | 001 |
| 2 | 011 |
| 3 | 010 |
| 4 | 110 |
| 5 | 111 |
| 6 | 101 |
| 7 | 100 |

Every adjacent pair, including the 7→0 wrap-around, differs in exactly one bit.

For 16-QAM, bits `b0 b1` select I and `b2 b3` select Q independently:

| Axis bit pair | Axis level before gain |
|---|---|
| 00 | -3/sqrt(10) |
| 01 | -1/sqrt(10) |
| 11 | +1/sqrt(10) |
| 10 | +3/sqrt(10) |

For 64-QAM, the first three bits select I and the next three select Q. Axis
levels in increasing amplitude order, divided by `sqrt(42)` for unit average
constellation energy:

| Axis bits | Unscaled level |
|---|---:|
| 000 | -7 |
| 001 | -5 |
| 011 | -3 |
| 010 | -1 |
| 110 | +1 |
| 111 | +3 |
| 101 | +5 |
| 100 | +7 |

256-QAM works the same way with four bits per axis: the first four bits select I
and the next four select Q. Each axis uses the binary-reflected Gray label of
the level index, so levels ascend from `-15` to `+15` in steps of 2 and every
level is divided by `sqrt(170)`. These mappings make horizontal/vertical nearest
neighbors differ by one bit. QPSK, 8-PSK and the QAM orders have defined I and
Q components; BPSK Q is zero. Gain scales sample amplitude once; power scales by
gain squared.

### PAM, on-off keying and differential PSK

These use the same pulse-shaped path as the other linear waveforms.

| Waveform | Bits | Symbol before gain |
|---|---|---|
| OOK | 0 / 1 | `0` / `+sqrt(2)` (mean energy 1; the symbols have a non-zero mean, so the spectrum carries a 0 Hz line) |
| 4-PAM | 00, 01, 11, 10 | `-3`, `-1`, `+1`, `+3`, divided by `sqrt(5)` on I; Q is zero |

DBPSK and DQPSK carry the data in the phase *change* between symbols. The
reference phase before the first symbol is 0 for DBPSK and 45 degrees for DQPSK
(so the DQPSK points coincide with the QPSK points). With `phi[k] = phi[k-1] + delta`:

| Waveform | Bits | Phase change `delta` |
|---|---|---|
| DBPSK | 0 / 1 | 0 / +180 degrees |
| DQPSK | 00, 01, 11, 10 | 0, +90, +180, +270 degrees |

The symbol is `exp(j*phi[k])`, so the first bit pair already moves the phase
away from the reference. A receiver recovers the data from `s[k] * conj(s[k-1])`,
which is unchanged by a constant phase rotation of the channel. The
matched-filter constellation of a DQPSK signal with a carrier offset therefore
spins, yet each symbol-to-symbol step stays near its transmitted value.

### 32-QAM, OQPSK and pi/4-DQPSK

**32-QAM** is the cross constellation: the 6 x 6 grid of odd levels
`-5, -3, -1, +1, +3, +5` on each axis without its four corner points, divided by
`sqrt(20)` for unit average energy (the energies sum to 640 over 32 points). Five
bits select a point: the first two choose the quadrant (`00` +,+; `01` -,+; `11`
-,-; `10` +,-) and the last three one of eight points of that quadrant. With the
point written as `(x, y)` in the first quadrant, labels `000`..`111` map to
`(1,1) (3,1) (5,1) (5,3) (1,3) (3,3) (1,5) (3,5)`, mirrored into the other
quadrants. A cross constellation cannot be fully Gray labelled (one point has four
nearest neighbours but a 3-bit label only three one-bit neighbours), so this
assignment, found by exhaustive search, is the best possible: neighbours across an
axis always differ in one bit, eight of the ten neighbour pairs inside a quadrant
differ in one bit, and the other two in two bits.

**OQPSK** (offset QPSK) uses the QPSK mapping, but the quadrature stream lags the
in-phase stream by half a symbol. The two bits of a symbol therefore change at
different instants, so the phase can only move by 90 degrees at a time and the
envelope varies less than in QPSK. It needs an even samples-per-symbol value, and
the buffer is `SPS / 2` samples longer than the QPSK buffer because of the delay.
The matched-filter decision for Q is taken `SPS / 2` samples after the one for I,
and the eye diagram centres the Q traces accordingly.

**pi/4-DQPSK** carries the data in phase changes of `+45`, `+135`, `-135` or `-45`
degrees for bit pairs `00`, `01`, `11`, `10`. The reference phase before the first
symbol is 0. Every step is an odd multiple of 45 degrees, so the symbols alternate
between two QPSK sets (offset by 45 degrees), never change phase by 180 degrees, and
the envelope never passes through the origin. Symbols have unit energy.

**8-DPSK** carries three bits per symbol as a phase change of `0` to `7` steps of
45 degrees, using the same Gray label order as 8-PSK (`000, 001, 011, 010, 110,
111, 101, 100` turn the phase by `0, 1, 2, 3, 4, 5, 6, 7` steps). The reference
phase before the first symbol is 0, so a first label of `000` starts at phase 0.
The symbol is `exp(j*phi[k])` with unit energy, and like DQPSK the data is
recovered from `s[k] * conj(s[k-1])`, which a constant channel phase does not change.

**4-ASK** is unipolar amplitude shift keying: Gray labels `00, 01, 11, 10` select
the amplitudes `0, 1, 2, 3`, divided by `sqrt(3.5)` for unit mean energy (the energies
`0, 1, 4, 9` average 3.5). Q is zero. Unlike the bipolar 4-PAM, the symbols have a non-zero
mean, so the spectrum has a strong 0 Hz line, as with OOK (which is 2-ASK).

## Frequency modulation: 2-FSK, 4-FSK and MSK

<p align="center">
  <img width="900" src="images/gui-fsk-frequency.png" alt="Frequency tab of a 2-FSK signal at 30 dB SNR, showing the measured frequency against the nominal tones" />
</p>

FSK carries the data in the carrier frequency instead of in a pulse-shaped
amplitude. The output has a constant envelope and **no pulse filter**: exactly
`symbols x SPS` samples, with no RRC tail, filter delay or guard symbols. The
pulse settings do not apply and the GUI hides them.

For M tones (M = 2 for 2-FSK and MSK, 4 for 4-FSK) the tone of symbol value `m`
(`m = 0` is the lowest tone) is

    f_m = (m - (M-1)/2) * tone_spacing_hz

and bits are assigned in ascending-tone Gray order: `0, 1` for binary tones and
`00, 01, 11, 10` for 4-FSK. The samples are

    phase[0] = 0;  phase[n+1] = phase[n] + 2*pi*f[n] / Fs;  x[n] = gain * exp(j*phase[n])

where `f[n]` is the tone of the symbol containing sample `n` and `Fs = symbol
rate x SPS`. The phase is carried across symbol boundaries (continuous-phase
FSK), so there are no phase jumps. The modulation index is
`h = tone_spacing_hz / symbol_rate_baud`; only the tone spacing is a setting and
`h` is displayed from it. The default spacing of 1000 Hz at 1000 Bd gives `h = 1`.
**MSK** is binary FSK with `h = 0.5` fixed: the tones sit at plus and minus
`symbol_rate / 4` and the phase moves exactly +/-90 degrees per symbol. MSK
ignores the stored tone spacing.

Validation requires every tone centre to be strictly below `Fs/2`
(`(M-1)/2 x spacing < Fs/2`). That check alone does not make the spectrum
bandlimited: the instantaneous frequency steps between tones, so spectral
skirts can still alias at a low SPS. Use SPS 8 or more and inspect the
Spectrum tab. FSK supports explicit bits (exactly `symbols x bits per symbol`
digits), seeded random bits with the same bit stream as the linear waveforms,
AWGN (reference interval: the whole signal, since the envelope is constant) and the
channel impairments. Clean FSK has a PAPR of 0 dB.

In the GUI the Constellation and Eye tabs, which need symbol-rate matched-filter
observations, are replaced by a **Frequency** tab that plots the frequency
estimated from the phase step between consecutive samples,
`arg(x[n+1] * conj(x[n])) * Fs / (2 pi)`, against the nominal tone of each
symbol. Measurements show power and PAPR; EVM does not apply. Export metadata
use `"family": "fsk"` with the tone spacing and modulation index, and batch
frames are unguarded slices that restart from phase zero.

Random input uses `std::mt19937(seed)`, consuming one engine output per bit and
mapping its least significant bit to `0` or `1`. It does not use an
implementation-dependent distribution. The first five bits for seed 5489 are
`00010`. Fixed settings reproduce bits and mapping across conforming engines;
small floating-point differences in filtering can occur across compilers or
math libraries, so cross-platform byte-identical samples are not guaranteed.
