import tempfile
import unittest
from pathlib import Path

import numpy as np

import siggen


class GenerateTests(unittest.TestCase):
    def test_default_signal(self):
        tx = siggen.generate()
        self.assertEqual(tx.samples.dtype, np.complex64)
        self.assertEqual(tx.waveform, "BPSK")
        self.assertEqual(tx.sample_rate, 8000)
        self.assertGreater(len(tx.samples), len(tx.symbols))
        self.assertAlmostEqual(tx.power_statistics()["mean_power"], np.mean(np.abs(tx.samples) ** 2), places=5)

    def test_options_reach_the_generator(self):
        tx = siggen.generate(modulation="16-QAM", symbols=64, sps=4, snr_db=None, pulse="rectangular")
        self.assertEqual(tx.waveform, "16-QAM")
        self.assertEqual(len(tx.symbols), 64)
        self.assertEqual(len(tx.samples), 64 * 4)
        self.assertEqual(len(tx.bits), 64 * 4)
        explicit = siggen.generate(modulation="QPSK", bits="00011011", symbols=4, snr_db=None)
        self.assertEqual(explicit.bits, "00011011")

    def test_deterministic_and_seeded(self):
        a = siggen.generate(modulation="QPSK", snr_db=10).samples
        b = siggen.generate(modulation="QPSK", snr_db=10).samples
        c = siggen.generate(modulation="QPSK", snr_db=10, seed=1).samples
        np.testing.assert_array_equal(a, b)
        self.assertFalse(np.array_equal(a, c))

    def test_every_family_generates(self):
        for name in ("BPSK", "OQPSK", "pi/4-DQPSK", "256-QAM", "4-ASK", "2-FSK", "MSK", "WGN"):
            self.assertGreater(len(siggen.generate(modulation=name).samples), 0, name)

    def test_errors_are_python_exceptions(self):
        with self.assertRaises(ValueError):
            siggen.generate(modulation="not-a-modulation")
        with self.assertRaises(ValueError):
            siggen.generate(modulation="OQPSK", sps=7)
        with self.assertRaises(TypeError):
            siggen.generate(bogus=1)

    def test_preset_text_round_trips(self):
        tx = siggen.generate(modulation="8-PSK", snr_db=12, cfo_hz=5)
        again = siggen.generate(preset=tx.preset)
        np.testing.assert_array_equal(tx.samples, again.samples)


class MeasurementTests(unittest.TestCase):
    def test_evm_tracks_requested_snr(self):
        for snr in (10, 20):  # Higher SNR meets the RRC truncation floor (about -43 dB EVM).
            tx = siggen.generate(modulation="16-QAM", symbols=1024, sps=8, snr_db=snr)
            accuracy = tx.symbol_accuracy()
            self.assertAlmostEqual(accuracy.sample_snr_db, snr, delta=1.0)
            self.assertAlmostEqual(accuracy.evm_percent, 100 * 10 ** (-(snr + 10 * np.log10(8)) / 20), delta=0.5 * accuracy.evm_percent)

    def test_edited_samples_change_the_measurement(self):
        tx = siggen.generate(modulation="QPSK", snr_db=None)
        clean = tx.symbol_accuracy().evm_rms
        rng = np.random.default_rng(0)
        noisy = tx.samples + 0.1 * (rng.standard_normal(len(tx.samples)) + 1j * rng.standard_normal(len(tx.samples))).astype(np.complex64)
        degraded = tx.with_samples(noisy).symbol_accuracy().evm_rms
        self.assertGreater(degraded, 5 * clean)
        # The original is untouched.
        self.assertEqual(tx.symbol_accuracy().evm_rms, clean)

    def test_changed_length_cannot_be_scored(self):
        tx = siggen.generate(modulation="QPSK")
        with self.assertRaises(ValueError):
            tx.with_samples(tx.samples[:-5]).symbol_accuracy()

    def test_no_constellation_means_no_accuracy(self):
        self.assertIsNone(siggen.generate(modulation="2-FSK").symbol_accuracy())
        self.assertIsNone(siggen.generate(modulation="WGN").symbol_accuracy())

    def test_eye_shape(self):
        t, i, q = siggen.generate(modulation="QPSK", sps=8, snr_db=None).eye(50)
        self.assertEqual(i.shape, q.shape)
        self.assertEqual(i.shape[1], len(t))
        self.assertAlmostEqual(t[0], -1)
        self.assertAlmostEqual(t[-1], 1)
        self.assertLessEqual(i.shape[0], 50)

    def test_matched_observations_recover_the_symbols(self):
        tx = siggen.generate(modulation="QPSK", sps=8, snr_db=None)
        values, indices = tx.matched_symbols()
        self.assertEqual(len(values), len(indices))
        ideal = tx.symbols[indices]
        # Noise-free: observations sit on the (gain-scaled) ideal points.
        self.assertLess(np.max(np.abs(values - ideal)), 0.05)
        with self.assertRaises(ValueError):
            siggen.generate(modulation="2-FSK").matched_symbols()

    def test_psd_finds_a_tone(self):
        fs = 8000.0
        n = np.arange(8192)
        tone = np.exp(2j * np.pi * 1000 * n / fs).astype(np.complex64)
        freq, density = siggen.psd(tone, fs, "blackman")
        self.assertAlmostEqual(freq[np.argmax(density)], 1000, delta=fs / 1024)
        with self.assertRaises(ValueError):
            siggen.psd(tone, fs, "triangular")

    def test_carrier_offset_degrades_coherent_evm(self):
        base = dict(symbols=512, sps=8, snr_db=40)
        offset = siggen.generate(modulation="QPSK", **base, cfo_hz=2).symbol_accuracy()
        clean = siggen.generate(modulation="QPSK", **base).symbol_accuracy()
        self.assertGreater(offset.evm_rms, 10 * clean.evm_rms)


class FileTests(unittest.TestCase):
    def test_sigmf_round_trip_keeps_samples_and_configuration(self):
        with tempfile.TemporaryDirectory() as tmp:
            tx = siggen.generate(modulation="64-QAM", snr_db=25)
            path = Path(tmp) / "tx.sigmf-data"
            tx.export(path, "sigmf")
            rx = siggen.load(Path(tmp) / "tx.sigmf-meta")
            np.testing.assert_array_equal(tx.samples, rx.samples)
            self.assertEqual(rx.waveform, "64-QAM")
            self.assertEqual(rx.sample_rate, tx.sample_rate)
            self.assertEqual(rx.symbol_accuracy().evm_rms, tx.symbol_accuracy().evm_rms)
            with self.assertRaises(ValueError):
                tx.export(path, "sigmf")
            tx.export(path, "sigmf", overwrite=True)

    def test_loaded_foreign_recording_has_no_configuration(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = (np.arange(16) + 1j * np.arange(16)).astype("<c8")
            (Path(tmp) / "x.sigmf-data").write_bytes(data.tobytes())
            (Path(tmp) / "x.sigmf-meta").write_text(
                '{"global": {"core:datatype": "cf32_le", "core:sample_rate": 100, "core:version": "1.0.0"},'
                ' "captures": [], "annotations": []}')
            rx = siggen.load(Path(tmp) / "x.sigmf-meta")
            np.testing.assert_array_equal(rx.samples, data)
            self.assertIsNone(rx.preset)
            self.assertIsNone(rx.symbol_accuracy())
            with self.assertRaises(ValueError):
                rx.export(Path(tmp) / "y.sigmf-data")
            with self.assertRaises(ValueError):
                siggen.load(Path(tmp) / "missing.sigmf-meta")


class BatchTests(unittest.TestCase):
    def test_sweep_writes_counted_frames(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "dataset"
            # BPSK has two SNR points, WGN ignores the SNR axis: three points, three frames.
            result = siggen.batch(out, modulations=["BPSK", "WGN"], seeds=[7], snrs_db=[0, 10], frame_size=64)
            self.assertEqual((result.point_count, result.frame_count), (3, 3))
            self.assertTrue((out / "manifest.jsonl").is_file())
            for name in ("frame_000000_000000.sigmf-data", "frame_000000_000000.sigmf-meta",
                         "frame_000002_000000.sigmf-data", "frame_000002_000000.sigmf-meta"):
                self.assertTrue((out / name).is_file(), name)
            self.assertEqual((out / "frame_000000_000000.sigmf-data").stat().st_size, 64 * 8)

    def test_frame_reads_back_with_load(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "dataset"
            siggen.batch(out, modulations=["QPSK"], snrs_db=[10], frame_size=2048)
            rx = siggen.load(out / "frame_000000_000000.sigmf-meta")
            self.assertEqual(len(rx.samples), 2048)
            self.assertGreater(rx.sample_rate, 0)
            freq, density = rx.psd()
            self.assertEqual(len(freq), len(density))

    def test_existing_directory_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "dataset"
            siggen.batch(out, frame_size=64)
            before = sorted(p.name for p in out.iterdir())
            with self.assertRaises(ValueError):
                siggen.batch(out, frame_size=64)
            self.assertEqual(sorted(p.name for p in out.iterdir()), before)

    def test_out_of_range_snr_is_rejected_before_any_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "dataset"
            with self.assertRaises(ValueError):
                siggen.batch(out, snrs_db=[10, 4000], frame_size=64)
            self.assertFalse(out.exists())

    def test_invalid_input_creates_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            for kwargs in (dict(modulations=["not-a-modulation"]),
                           dict(modulations=["BPSK", "BPSK"]),
                           dict(seeds=[7, 7]),
                           dict(frame_size=0),
                           dict(frames_per_point=0),
                           dict(gain=0, snrs_db=[10])):
                out = Path(tmp) / f"rejected-{abs(hash(str(kwargs)))}"
                with self.assertRaises((ValueError, TypeError)):
                    siggen.batch(out, **kwargs)
                self.assertFalse(out.exists(), str(kwargs))
            with self.assertRaises(ValueError):
                siggen.batch(Path(tmp) / "rejected-format", format="cf32")


class BerTest(unittest.TestCase):
    def test_noiseless_signal_has_no_errors(self):
        errors = siggen.generate(modulation="16-QAM", symbols=512, snr_db=None).bit_errors()
        self.assertGreater(errors.bit_count, 1000)
        self.assertEqual(errors.bit_errors, 0)

    def test_noise_raises_errors_and_not_available_for_fsk(self):
        noisy = siggen.generate(modulation="QPSK", symbols=4096, snr_db=-2).bit_errors()
        self.assertGreater(noisy.ber, 0.01)
        self.assertIsNone(siggen.generate(modulation="2-FSK").bit_errors())

    def test_theory_matches_known_values(self):
        self.assertAlmostEqual(siggen.theoretical_ber("BPSK", 9.6), 1e-5, delta=2e-6)
        values = siggen.theoretical_ber("QPSK", [0, 4, 8])
        self.assertTrue(np.all(np.diff(values) < 0))
        self.assertTrue(np.isnan(siggen.theoretical_ber("8-DPSK", 5)))
        with self.assertRaises(ValueError):
            siggen.theoretical_ber("nope", 5)

    def test_curve_follows_theory(self):
        curve = siggen.ber_curve([2, 5], modulation="QPSK", min_errors=300, max_bits=500_000)
        np.testing.assert_allclose(curve.ber, curve.theory, rtol=0.25)
        self.assertEqual(len(curve.snr_db), 2)
        with self.assertRaises(ValueError):
            siggen.ber_curve([5], modulation="MSK")


if __name__ == "__main__":
    unittest.main()
