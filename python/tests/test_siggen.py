import os
import subprocess
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

    @unittest.skipUnless(os.environ.get("SIGGEN_CLI"), "SIGGEN_CLI points at the siggen executable")
    def test_matches_the_command_line_bit_for_bit(self):
        cases = [
            (["--modulation", "QPSK", "--snr-db", "15"], dict(modulation="QPSK", snr_db=15)),
            (["--modulation", "16-QAM", "--sps", "4", "--cfo-hz", "7", "--iq-gain-db", "1", "--snr-db", "off"],
             dict(modulation="16-QAM", sps=4, cfo_hz=7, iq_gain_db=1, snr_db=None)),
            (["--modulation", "2-FSK", "--tone-spacing-hz", "500", "--snr-db", "20"],
             dict(modulation="2-FSK", tone_spacing_hz=500, snr_db=20)),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            for n, (cli_args, options) in enumerate(cases):
                out = Path(tmp) / f"c{n}.sigmf-data"
                subprocess.run([os.environ["SIGGEN_CLI"], *cli_args, "--format", "sigmf", "-o", str(out)], check=True,
                               capture_output=True)
                np.testing.assert_array_equal(siggen.load(out).samples, siggen.generate(**options).samples, str(cli_args))


if __name__ == "__main__":
    unittest.main()
