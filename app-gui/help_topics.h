/**
 * @file    help_topics.h
 * @brief   Texts of the "What is this?" panels opened from the ? button beside a generator control.
 */

#ifndef SIGGEN_HELP_TOPICS_H
#define SIGGEN_HELP_TOPICS_H

#include <string_view>

namespace GUI {

/**
 * @struct  HelpTopic
 * @brief   A longer explanation of one setting, opened from the ? button beside its control.
 * @details Hover tooltips stay short; these say what the setting means, what it trades off and what to try. The
 *          body is plain text; paragraphs are separated by a blank line.
 */
struct HelpTopic {
    std::string_view Id; // ImGui ID of the button and its popup, unique among topics
    std::string_view Title;
    std::string_view Body;
};

inline constexpr HelpTopic SymbolRateHelp{
    "symbol_rate", "Symbol rate",
    "How many symbols are sent per second, in baud (Bd). Each symbol carries log2(M) bits, so the bit rate is symbol "
    "rate x bits per symbol.\n\n"
    "The symbol rate only sets the time scale and the sample rate (Fs = symbol rate x samples per symbol). Occupied "
    "bandwidth is about symbol rate x (1 + roll-off), so doubling the rate doubles the width of the spectrum."};

inline constexpr HelpTopic SPSHelp{
    "sps", "Samples per symbol (SPS)",
    "The oversampling factor: how many samples describe each symbol period. The sample rate is Fs = symbol rate x SPS, "
    "and the spectrum shows -Fs/2 to +Fs/2.\n\n"
    "More SPS gives smoother curves and more empty spectrum around the signal, at the cost of more samples. It also "
    "changes what a given SNR means: the same noise power is spread over a wider band, so the matched filter (which "
    "averages SPS samples) gains 10 log10(SPS) dB, and Es/N0 is SNR + 10 log10(SPS).\n\n"
    "An RRC pulse needs at least 2 samples per symbol; the zeros the pipeline inserts between symbols are exactly SPS "
    "- 1 per symbol."};

inline constexpr HelpTopic PulseHelp{
    "pulse", "Pulse shape",
    "The pulse is the waveform drawn for each symbol. The mapped symbols are placed at one impulse per symbol period "
    "(zeros in between) and this filter smooths them into a continuous signal.\n\n"
    "Root-raised cosine (RRC): a band-limited pulse. Used with a matching RRC filter at the receiver, the combination "
    "has no inter-symbol interference (ISI) at the decision instants, and the spectrum is compact.\n\n"
    "Rectangular: each symbol is held flat for one symbol period. The eye is wide open and there is no filter delay, "
    "but the spectrum has wide sinc sidelobes.\n\n"
    "Compare the Pipeline tab for both: step 4 is where the pulse acts."};

inline constexpr HelpTopic RollOffHelp{
    "roll_off", "RRC roll-off",
    "The roll-off (beta, from 0 to 1) is the excess bandwidth of the RRC filter. The occupied bandwidth is about "
    "symbol rate x (1 + beta).\n\n"
    "Low beta: a narrow spectrum, but a pulse whose tails ring for many symbols, so it needs a longer span, is more "
    "sensitive to timing errors and has a higher peak-to-average power ratio.\n\n"
    "High beta: a wider spectrum, but a short, well-behaved pulse. beta = 1 uses twice the minimum bandwidth. "
    "Practical systems often use 0.2 to 0.35.\n\n"
    "Try it: in the Pipeline tab compare step 4 for beta = 0.1 and beta = 1, then look at the width of the Spectrum "
    "tab."};

inline constexpr HelpTopic SpanHelp{
    "span", "RRC span",
    "The filter length in symbol periods: the pulse is truncated after span symbols, so it has span x SPS + 1 taps.\n\n"
    "A longer span follows the ideal pulse more closely: less residual ISI (lower EVM floor on a clean signal) and "
    "lower spectral sidelobes. It costs span x SPS extra samples at the start and end of the signal, and delays the "
    "output by span / 2 symbols (the Pipeline tab can compensate for it).\n\n"
    "Low roll-off needs a longer span because the tails decay more slowly. If the span is too short for the roll-off, "
    "the truncation shows up as a higher EVM on a clean signal."};

inline constexpr HelpTopic SnrHelp{
    "snr", "SNR, Es/N0 and Eb/N0",
    "Three ways to say how noisy a signal is. They differ by what the noise is compared to.\n\n"
    "SNR (the value you enter): clean signal power divided by added noise power, both measured on the complex samples "
    "over the whole sample rate. It depends on how oversampled the signal is.\n\n"
    "Es/N0: energy per symbol over the noise power density. Since Es = P x Tsymbol and N0 = Pnoise / Fs, Es/N0 = SNR x "
    "SPS, or in dB: SNR + 10 log10(SPS).\n\n"
    "Eb/N0: energy per bit over the noise power density: Es/N0 divided by the bits per symbol, or in dB: Es/N0 - 10 "
    "log10(bits per symbol).\n\n"
    "Why it matters: SNR changes with the oversampling and Eb/N0 does not. To compare modulations fairly (for example "
    "BPSK against 64-QAM) compare them at the same Eb/N0. A BER curve is always drawn against Eb/N0 or Es/N0, never "
    "against sample-rate SNR.\n\n"
    "These conversions assume the signal power is spread evenly over the band, as it is for the shaped signals here; "
    "siggen adds white noise at exactly the requested SNR."};

inline constexpr HelpTopic PipelineHelp{
    "pipeline", "Pipeline view",
    "Follow one transmission from data to antenna. All five rows share the same horizontal axis, in symbol periods, so "
    "what you see in one row lines up with the others.\n\n"
    "1. Data bits: the bits to send. Each symbol takes log2(M) of them.\n"
    "2. Mapped symbols: each group of bits becomes one complex symbol (I and Q) from the constellation. Nothing "
    "between symbols yet.\n"
    "3. Zeros inserted: SPS - 1 zeros are placed after each symbol, so there is one sample per sample period. Every "
    "symbol is now a single impulse.\n"
    "4. Pulse filter: each impulse is replaced by the pulse (RRC or rectangular). This is where bandwidth, ISI and the "
    "filter delay come from.\n"
    "5. With noise: AWGN and channel impairments are added to the clean signal of row 4, giving the signal that is "
    "exported.\n\n"
    "The filter delays row 4 by span / 2 symbols; the 'Compensate filter delay' option shifts rows 4 and 5 back so "
    "each pulse lines up with its symbol."};

inline constexpr HelpTopic AllHelpTopics[] = {SymbolRateHelp, SPSHelp, PulseHelp,   RollOffHelp,
                                              SpanHelp,       SnrHelp, PipelineHelp};

} // namespace GUI

#endif // SIGGEN_HELP_TOPICS_H
