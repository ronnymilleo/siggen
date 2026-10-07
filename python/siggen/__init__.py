"""Python access to the siggen signal generator.

The package is a thin ``ctypes`` layer over the C++ library, so signals,
measurements and files are exactly what the CLI and GUI produce::

    import siggen
    tx = siggen.generate(modulation="16-QAM", sps=8, snr_db=20)
    tx.samples                # complex64 NumPy array
    tx.symbol_accuracy().evm_percent

Build the shared library first (``cmake --preset dev`` or ``headless``) and, if
it is not found automatically, point ``SIGGEN_LIBRARY`` at ``libsiggen_c``.
"""
from __future__ import annotations

import ctypes
import dataclasses
import glob
import os
import sys
from pathlib import Path
from typing import Optional

import numpy as np

__all__ = ["Signal", "SymbolAccuracy", "generate", "load", "psd", "default_config", "WINDOWS", "version"]

WINDOWS = {"hann": 0, "hamming": 1, "blackman": 2, "rectangular": 3}
_FORMATS = {"csv": 0, "sigmf": 1}
_ERROR = 1024


def _find_library() -> str:
    names = ("libsiggen_c.so", "libsiggen_c.dylib", "siggen_c.dll")
    explicit = os.environ.get("SIGGEN_LIBRARY")
    if explicit:
        return explicit
    here = Path(__file__).resolve().parent
    root = here.parent.parent
    candidates = [here / n for n in names]
    for pattern in ("build/*/lib", "build/lib"):
        for folder in sorted(glob.glob(str(root / pattern)), key=os.path.getmtime, reverse=True):
            candidates += [Path(folder) / n for n in names]
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    raise ImportError(
        "libsiggen_c not found. Build it with `cmake --preset headless -DSIGGEN_BUILD_PYTHON=ON` "
        "(then `cmake --build --preset headless`) or set SIGGEN_LIBRARY to its path."
    )


_lib = ctypes.CDLL(_find_library())
_c_char_p, _c_size_t, _c_double = ctypes.c_char_p, ctypes.c_size_t, ctypes.c_double
_handle = ctypes.c_void_p
_f32 = np.ctypeslib.ndpointer(np.float32, flags="C_CONTIGUOUS")
_f64 = np.ctypeslib.ndpointer(np.float64, flags="C_CONTIGUOUS")


def _sig(name, restype, *argtypes):
    function = getattr(_lib, name)
    function.restype = restype
    function.argtypes = list(argtypes)
    return function


_version = _sig("siggen_version", _c_char_p)
_default_preset = _sig("siggen_default_preset", ctypes.c_int, ctypes.c_char_p, _c_size_t)
_generate = _sig("siggen_generate", _handle, _c_char_p, ctypes.c_char_p, _c_size_t)
_load = _sig("siggen_load", _handle, _c_char_p, ctypes.c_char_p, _c_size_t)
_free = _sig("siggen_free", None, _handle)
_sample_count = _sig("siggen_sample_count", _c_size_t, _handle)
_sample_rate = _sig("siggen_sample_rate", _c_double, _handle)
_filter_delay = _sig("siggen_filter_delay", _c_size_t, _handle)
_copy_samples = _sig("siggen_copy_samples", None, _handle, _f32)
_set_samples = _sig("siggen_set_samples", ctypes.c_int, _handle, _f32, _c_size_t, ctypes.c_char_p, _c_size_t)
_symbol_count = _sig("siggen_symbol_count", _c_size_t, _handle)
_copy_symbols = _sig("siggen_copy_symbols", None, _handle, _f32)
_preset_text = _sig("siggen_preset_text", ctypes.c_long, _handle, ctypes.c_char_p, _c_size_t)
_waveform_name = _sig("siggen_waveform_name", ctypes.c_long, _handle, ctypes.c_char_p, _c_size_t)
_bits = _sig("siggen_bits", ctypes.c_long, _handle, ctypes.c_char_p, _c_size_t)
_power = _sig("siggen_power_statistics", ctypes.c_int, _handle, _f64)
_accuracy = _sig("siggen_symbol_accuracy", ctypes.c_int, _handle, _f64, ctypes.c_char_p, _c_size_t)
_eye = _sig("siggen_eye", ctypes.c_long, _handle, _c_size_t, ctypes.c_void_p, _c_size_t, ctypes.c_void_p, ctypes.c_void_p,
            _c_size_t, ctypes.POINTER(_c_size_t))
_matched = _sig("siggen_matched_symbols", ctypes.c_long, _handle, ctypes.c_void_p, ctypes.c_void_p, _c_size_t)
_psd = _sig("siggen_psd", ctypes.c_long, _f32, _c_size_t, _c_double, _c_size_t, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p,
            _c_size_t, ctypes.c_char_p, _c_size_t)
_export = _sig("siggen_export", ctypes.c_int, _handle, _c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_char_p, _c_size_t)


def version() -> str:
    """Version of the siggen library this package is bound to."""
    return _version().decode()


def _text(function, handle) -> Optional[str]:
    needed = function(handle, None, 0)
    if needed < 0:
        return None
    buffer = ctypes.create_string_buffer(needed + 1)
    function(handle, buffer, needed + 1)
    return buffer.value.decode()


def _fail(buffer, what: str):
    message = buffer.value.decode() or "unknown error"
    raise ValueError(f"{what}: {message}")


@dataclasses.dataclass(frozen=True)
class SymbolAccuracy:
    """Matched-filter symbol accuracy, as shown in the GUI's Measurements panel."""
    symbol_count: int
    evm_rms: float                 # Fraction (0.05 = 5 %).
    evm_db: float                  # 20 log10(evm_rms).
    snr_after_matched_db: float    # -evm_db.
    sps_offset_db: float           # 10 log10(SPS): sample SNR + offset = post-filter SNR.

    @property
    def evm_percent(self) -> float:
        return 100.0 * self.evm_rms

    @property
    def sample_snr_db(self) -> float:
        """Per-sample SNR implied by the post-filter SNR."""
        return self.snr_after_matched_db - self.sps_offset_db


def _default_text() -> str:
    buffer = ctypes.create_string_buffer(8192)
    _default_preset(buffer, len(buffer))
    return buffer.value.decode()


def _parse(text: str) -> dict:
    values = {}
    for line in text.splitlines():
        if "=" in line and not line.startswith("#"):
            key, _, value = line.partition("=")
            values[key.strip()] = value.strip()
    return values


# Friendly keyword -> preset key. Values are converted by _format_value.
_KEYS = {
    "modulation": "Modulation", "symbols": "NumberOfSymbols", "symbol_rate": "SymbolRateBaud", "sps": "SamplesPerSymbol",
    "pulse": "Pulse", "roll_off": "RRCBeta", "span": "SpanSymbols", "gain": "AmplitudeGain", "seed": "Seed",
    "noise_seed": "NoiseSeed", "samples": "NoiseSampleCount", "sample_rate": "NoiseSampleRateHz", "noise_power": "NoisePower",
    "tone_spacing_hz": "ToneSpacingHz", "cfo_hz": "CfoHz", "phase_noise_hz": "PhaseNoiseLinewidthHz",
    "iq_gain_db": "IqGainDb", "iq_phase_deg": "IqPhaseDeg", "dc_i": "DcOffsetI", "dc_q": "DcOffsetQ",
    "adc_bits": "AdcBits", "impairment_seed": "ImpairmentSeed",
}


def default_config() -> dict:
    """Keyword arguments accepted by :func:`generate`, with the library's defaults."""
    values = _parse(_default_text())
    config = {name: values[key] for name, key in _KEYS.items() if key in values}
    config.setdefault("tone_spacing_hz", "1000")
    for name in ("cfo_hz", "phase_noise_hz", "iq_gain_db", "iq_phase_deg", "dc_i", "dc_q", "adc_bits"):
        config.setdefault(name, "0")
    config.setdefault("impairment_seed", "5491")
    config["snr_db"] = float(values["AwgnSnrDb"]) if values.get("AwgnEnabled") == "true" else None
    config["bits"] = None
    return config


def _build_preset(preset: Optional[str], overrides: dict) -> str:
    """Compose a version-4 preset (accepted for every waveform) from defaults, an optional preset and keyword overrides."""
    values = _parse(_default_text())
    values.update({"ToneSpacingHz": "1000", "CfoHz": "0", "PhaseNoiseLinewidthHz": "0", "IqGainDb": "0", "IqPhaseDeg": "0",
                   "DcOffsetI": "0", "DcOffsetQ": "0", "AdcBits": "0", "ImpairmentSeed": "5491"})
    if preset is not None:
        text = preset if "[IQ Generator Preset]" in preset else Path(preset).read_text(encoding="utf-8")
        loaded = _parse(text)
        loaded.pop("Version", None)
        values.update(loaded)
    for name, value in overrides.items():
        if name == "snr_db":
            values["AwgnEnabled"] = "false" if value is None else "true"
            if value is not None:
                values["AwgnSnrDb"] = repr(float(value))
        elif name == "bits":
            if value is None:
                values["DataSource"], values["Bits"] = "Random", ""
            else:
                values["DataSource"], values["Bits"] = "Explicit", str(value)
        elif name == "pulse":
            values["Pulse"] = {"rrc": "RRC", "rectangular": "Rectangular"}.get(str(value).lower(), str(value))
        elif name in _KEYS:
            values[_KEYS[name]] = str(value)
        else:
            raise TypeError(f"unknown option {name!r}; valid: {sorted(list(_KEYS) + ['snr_db', 'bits', 'preset'])}")
    values.pop("Version", None)
    return "[IQ Generator Preset]\nVersion=4\n" + "\n".join(f"{k}={v}" for k, v in values.items()) + "\n"


class Signal:
    """A generated or loaded I/Q signal. Create one with :func:`generate` or :func:`load`."""

    def __init__(self, handle):
        self._h = handle
        self._samples: Optional[np.ndarray] = None

    def __del__(self):
        handle, self._h = getattr(self, "_h", None), None
        if handle:
            _free(handle)

    def __repr__(self) -> str:
        return f"Signal({self.waveform or 'recording'}, {len(self.samples)} samples @ {self.sample_rate:g} Hz)"

    @property
    def sample_rate(self) -> float:
        return _sample_rate(self._h)

    @property
    def samples(self) -> np.ndarray:
        """I/Q as a complex64 array (a copy; edit freely and pass to :meth:`with_samples`)."""
        if self._samples is None:
            n = _sample_count(self._h)
            raw = np.empty(2 * n, dtype=np.float32)
            _copy_samples(self._h, raw)
            self._samples = raw.view(np.complex64).copy()
        return self._samples.copy()

    @property
    def symbols(self) -> np.ndarray:
        """Ideal mapped symbols (unit mean energy, before gain); empty for noise and FSK."""
        n = _symbol_count(self._h)
        raw = np.empty(2 * n, dtype=np.float32)
        _copy_symbols(self._h, raw)
        return raw.view(np.complex64).copy()

    @property
    def bits(self) -> str:
        return _text(_bits, self._h) or ""

    @property
    def waveform(self) -> Optional[str]:
        return _text(_waveform_name, self._h)

    @property
    def preset(self) -> Optional[str]:
        """Configuration in the preset text format (None for a recording without siggen metadata)."""
        return _text(_preset_text, self._h)

    @property
    def filter_delay(self) -> int:
        return int(_filter_delay(self._h))

    @property
    def time(self) -> np.ndarray:
        return np.arange(_sample_count(self._h)) / self.sample_rate

    def power_statistics(self) -> dict:
        out = np.zeros(3)
        if _power(self._h, out) < 0:
            raise ValueError("signal has no samples")
        return {"mean_power": float(out[0]), "peak_power": float(out[1]), "papr_db": float(out[2])}

    def symbol_accuracy(self) -> Optional[SymbolAccuracy]:
        """EVM and post-filter SNR of the current samples; None when the waveform has no constellation."""
        out = np.zeros(5)
        err = ctypes.create_string_buffer(_ERROR)
        status = _accuracy(self._h, out, err, _ERROR)
        if status < 0:
            _fail(err, "symbol_accuracy")
        return SymbolAccuracy(int(out[0]), *map(float, out[1:])) if status else None

    def eye(self, max_traces: int = 200):
        """Overlaid matched-filter traces: ``(time_in_symbols, in_phase[traces, points], quadrature[traces, points])``."""
        points = _c_size_t(0)
        count = _eye(self._h, max_traces, None, 0, None, None, 0, ctypes.byref(points))
        if count < 0:
            raise ValueError("eye diagram needs a signal generated by siggen with its original length")
        t = np.empty(points.value)
        i = np.empty((count, points.value))
        q = np.empty((count, points.value))
        _eye(self._h, max_traces, t.ctypes.data, t.size, i.ctypes.data, q.ctypes.data, i.size, ctypes.byref(points))
        return t, i, q

    def matched_symbols(self):
        """Matched-filter observations at the decision instants: ``(values, symbol_indices)``.

        ``values`` are complex64 (gain applied); ``symbol_indices`` say which entry of :attr:`symbols`
        each observation belongs to. Needs a signal generated by siggen with its original length."""
        count = _matched(self._h, None, None, 0)
        if count < 0:
            raise ValueError("matched-filter observations need a linear signal generated by siggen with its original length")
        values = np.empty(2 * count, dtype=np.float32)
        indices = np.empty(count, dtype=np.uintp)
        _matched(self._h, values.ctypes.data, indices.ctypes.data, count)
        return values.view(np.complex64).copy(), indices.astype(np.int64)

    def psd(self, window: str = "hann", segment: int = 1024):
        return psd(self.samples, self.sample_rate, window, segment)

    def with_samples(self, samples) -> "Signal":
        """A copy of this signal's configuration carrying different samples (e.g. after adding noise in NumPy).

        ``symbol_accuracy`` then measures those samples against the original symbols, which needs the
        original length and timing."""
        text = self.preset
        if text is None:
            raise ValueError("this recording carries no siggen configuration")
        clone = generate(preset=text)
        clone._replace(samples)
        return clone

    def _replace(self, samples):
        data = np.ascontiguousarray(np.asarray(samples, dtype=np.complex64))
        raw = data.view(np.float32).reshape(-1)
        err = ctypes.create_string_buffer(_ERROR)
        if _set_samples(self._h, raw, data.size, err, _ERROR) < 0:
            _fail(err, "set samples")
        self._samples = None

    def export(self, path, format: str = "sigmf", overwrite: bool = False) -> None:
        """Write ``csv`` or ``sigmf`` exactly as ``siggen --format`` does (SigMF paths end in .sigmf-data)."""
        err = ctypes.create_string_buffer(_ERROR)
        if _export(self._h, os.fspath(path).encode(), _FORMATS[format], int(overwrite), err, _ERROR) < 0:
            _fail(err, "export")


def generate(preset: Optional[str] = None, **options) -> Signal:
    """Generate a signal. ``preset`` is a preset file path or its text; keywords override it (see :func:`default_config`)."""
    err = ctypes.create_string_buffer(_ERROR)
    handle = _generate(_build_preset(preset, options).encode(), err, _ERROR)
    if not handle:
        _fail(err, "generate")
    return Signal(handle)


def load(path) -> Signal:
    """Read a SigMF recording (``.sigmf-meta`` or ``.sigmf-data``) or a batch ``.cf32`` frame with its ``.json`` sidecar."""
    err = ctypes.create_string_buffer(_ERROR)
    handle = _load(os.fspath(path).encode(), err, _ERROR)
    if not handle:
        _fail(err, "load")
    return Signal(handle)


def psd(samples, sample_rate: float, window: str = "hann", segment: int = 1024):
    """Two-sided Welch PSD, identical to the GUI's Spectrum tab. Returns ``(frequency_hz, density)``."""
    if window not in WINDOWS:
        raise ValueError(f"window must be one of {sorted(WINDOWS)}")
    data = np.ascontiguousarray(np.asarray(samples, dtype=np.complex64))
    raw = data.view(np.float32).reshape(-1)
    err = ctypes.create_string_buffer(_ERROR)
    bins = _psd(raw, data.size, sample_rate, segment, WINDOWS[window], None, None, 0, err, _ERROR)
    if bins < 0:
        _fail(err, "psd")
    freq, density = np.empty(bins), np.empty(bins)
    _psd(raw, data.size, sample_rate, segment, WINDOWS[window], freq.ctypes.data, density.ctypes.data, bins, err, _ERROR)
    return freq, density
