#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace arporator {

constexpr int kMaxSteps = 32;
constexpr int kMidiNotes = 128;

enum class Mode : std::uint8_t {
    Up,
    Down,
    UpDown,
    DownUp,
    Played,
    Random
};

enum class ScalePolicy : std::uint8_t {
    ChordOnly,
    Scale,
    Chromatic
};

struct VariationLocks {
    bool rhythm {false};
    bool velocity {false};
    bool gate {false};
    bool ratchet {false};
    bool probability {false};
    bool octave {false};
    bool note {false};
};

struct Step {
    bool enabled {true};
    bool locked {false};        // Variate/Evolve may not modify this step
    float velocity {1.0f};      // 0..1 multiplier
    float gate {1.0f};          // 0.01..1 per-step multiplier
    std::uint8_t ratchet {1};   // 1..4
    float probability {1.0f};   // 0..1
    std::int8_t noteOffset {0};  // -4..+4 index offset in arp note order
    std::int8_t octaveOffset {0};
};

struct Settings {
    Mode mode {Mode::Up};
    ScalePolicy scalePolicy {ScalePolicy::ChordOnly};
    int patternLength {16};     // 1..32
    int octaveRange {1};        // 1..4
    double stepsPerQuarter {4.0}; // 1/16 default
    float swing {0.0f};         // 0..1, delays odd steps
    float humanize {0.0f};      // 0..1 deterministic timing + velocity variation
    float groove {0.0f};        // 0..1 fixed musical microtiming/accent template
    float strum {0.0f};         // 0..1 pitch-rank-dependent note spread
    float evolve {0.0f};        // 0..1 subtle deterministic multi-cycle evolution
    VariationLocks evolveLocks {};
    float globalGate {1.0f};    // 0.01..1 global multiplier
    bool restartOnTrigger {true};
    int keyRoot {0};            // 0=C ... 11=B
    std::uint16_t scaleMask {0x0AB5}; // major scale pitch-class mask by default
    std::uint32_t randomSeed {0x125A0001u};
    std::array<Step, kMaxSteps> steps {};
};

struct MidiInput {
    enum class Type : std::uint8_t { NoteOn, NoteOff, AllNotesOff };
    Type type {Type::NoteOn};
    int sampleOffset {0};
    int channel {0};
    int pitch {60};
    float velocity {1.0f};
};

struct MidiOutput {
    MidiInput::Type type {MidiInput::Type::NoteOn};
    int sampleOffset {0};
    int channel {0};
    int pitch {60};
    float velocity {1.0f};
    int noteId {-1};
};

class Engine {
public:
    Engine();

    void setSettings(const Settings& settings) noexcept;
    const Settings& settings() const noexcept { return settings_; }

    void reset() noexcept;

    void process(double sampleRate,
                 double tempo,
                 int numSamples,
                 const std::vector<MidiInput>& input,
                 std::vector<MidiOutput>& output) noexcept;

    int currentStep() const noexcept { return currentStep_; }
    bool running() const noexcept { return running_; }

private:
    struct HeldNote {
        std::uint16_t count {0};
        float velocity {1.0f};
        std::uint32_t order {0};
    };

    struct ActiveOutput {
        bool active {false};
        int channel {0};
        int pitch {0};
        int noteId {-1};
        double offSample {0.0};
    };

    Settings settings_ {};
    std::array<std::array<HeldNote, kMidiNotes>, 16> held_ {};
    std::array<ActiveOutput, 64> activeOutputs_ {};

    double absoluteSample_ {0.0};
    double nextStepSample_ {0.0};
    int currentStep_ {0};
    int directionIndex_ {0};
    int directionSign_ {1};
    bool running_ {false};
    std::uint32_t orderCounter_ {0};
    std::uint32_t rng_ {0x125A0001u};
    int nextNoteId_ {1};
    std::uint64_t patternCycle_ {0u};

    bool anyHeld() const noexcept;
    void clearHeld() noexcept;
    void stopAll(double when, double blockStart, int blockSize,
                 std::vector<MidiOutput>& output) noexcept;
    void stopExpired(double blockStart, double blockEnd, int blockSize,
                     std::vector<MidiOutput>& output) noexcept;

    int collectOrderedPitches(int channel,
                              std::array<int, kMidiNotes>& out) const noexcept;
    int choosePitch(int channel, int logicalStep) noexcept;
    int constrainPitch(int pitch,
                       const std::array<int, kMidiNotes>& chord,
                       int chordCount) const noexcept;
    bool probabilityPasses(float probability) noexcept;
    std::uint32_t nextRandom() noexcept;

    void emitStep(double stepSample,
                  double stepDuration,
                  double blockStart,
                  int blockSize,
                  std::vector<MidiOutput>& output) noexcept;
};

} // namespace arporator
