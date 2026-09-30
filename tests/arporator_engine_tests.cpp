#include "arporator_engine.h"

#include <cstdlib>
#include <iostream>
#include <vector>

using namespace arporator;

#define CHECK(x) do { if (!(x)) {     std::cerr << "CHECK failed: " #x << " @ line " << __LINE__ << "\n";     return EXIT_FAILURE; } } while (0)

static std::vector<int> noteOns(const std::vector<MidiOutput>& events) {
    std::vector<int> result;
    for (const auto& e : events)
        if (e.type == MidiInput::Type::NoteOn)
            result.push_back(e.pitch);
    return result;
}

int main() {
    Engine engine;
    Settings settings;
    settings.patternLength = 4;
    settings.stepsPerQuarter = 4.0; // 1/16
    settings.mode = Mode::Up;
    settings.restartOnTrigger = true;
    settings.scalePolicy = ScalePolicy::ChordOnly;
    for (auto& s : settings.steps) {
        s.enabled = true;
        s.velocity = 1.0f;
        s.gate = 0.5f;
        s.ratchet = 1;
        s.probability = 1.0f;
    }
    engine.setSettings(settings);

    std::vector<MidiOutput> out;

    // Trigger at sample 100: pattern must begin there at Step 1 / first chord tone.
    engine.process(48000.0, 120.0, 1000,
                   {
                       {MidiInput::Type::NoteOn, 100, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOn, 100, 0, 64, 1.0f},
                       {MidiInput::Type::NoteOn, 100, 0, 67, 1.0f}
                   },
                   out);
    CHECK(!out.empty());
    CHECK(out.front().type == MidiInput::Type::NoteOn);
    CHECK(out.front().sampleOffset == 100);
    CHECK(out.front().pitch == 60);

    // Continue enough blocks to get C-E-G-C and wrap pattern length independently.
    std::vector<int> pitches = noteOns(out);
    for (int i = 0; i < 20; ++i) {
        engine.process(48000.0, 120.0, 1000, {}, out);
        auto p = noteOns(out);
        pitches.insert(pitches.end(), p.begin(), p.end());
    }
    CHECK(pitches.size() >= 4);
    CHECK(pitches[0] == 60);
    CHECK(pitches[1] == 64);
    CHECK(pitches[2] == 67);
    CHECK(pitches[3] == 60);

    // New trigger later must restart from Step 1 when restartOnTrigger is enabled.
    engine.process(48000.0, 120.0, 1000,
                   {{MidiInput::Type::NoteOn, 250, 0, 72, 0.8f}},
                   out);
    auto restart = noteOns(out);
    CHECK(!restart.empty());
    bool restartAt250 = false;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn && e.sampleOffset == 250)
            restartAt250 = true;
    }
    CHECK(restartAt250);
    CHECK(engine.running());

    // Arbitrary pattern lengths 1..32 are legal, including odd lengths.
    settings.patternLength = 7;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 1000,
                   {{MidiInput::Type::NoteOn, 0, 0, 60, 1.0f}},
                   out);
    for (int i = 0; i < 40; ++i)
        engine.process(48000.0, 120.0, 1000, {}, out);
    CHECK(engine.currentStep() >= 0 && engine.currentStep() < 7);

    // Probability 0 must suppress a step deterministically.
    settings.patternLength = 1;
    settings.steps[0].probability = 0.0f;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 7000,
                   {{MidiInput::Type::NoteOn, 0, 0, 60, 1.0f}},
                   out);
    CHECK(noteOns(out).empty());

    // 4x ratchet emits four note-ons in one step.
    settings.steps[0].probability = 1.0f;
    settings.steps[0].ratchet = 4;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 6000,
                   {{MidiInput::Type::NoteOn, 0, 0, 60, 1.0f}},
                   out);
    CHECK(noteOns(out).size() == 4);

    // Note-off must stop playback and emit safety note-offs; no stuck generated notes.
    engine.process(48000.0, 120.0, 256,
                   {{MidiInput::Type::NoteOff, 10, 0, 60, 0.0f}},
                   out);
    CHECK(!engine.running());
    bool sawOff = false;
    for (const auto& e : out)
        sawOff = sawOff || e.type == MidiInput::Type::NoteOff;
    CHECK(sawOff);

    // Events inside one host block must be processed in chronological order:
    // a NoteOff later in the block may not erase a step that was due earlier.
    settings.patternLength = 1;
    settings.steps[0].ratchet = 1;
    settings.steps[0].probability = 1.0f;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 7000,
                   {
                       {MidiInput::Type::NoteOn, 100, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOff, 6200, 0, 60, 0.0f}
                   },
                   out);
    auto chronological = noteOns(out);
    CHECK(!chronological.empty());
    CHECK(out.front().sampleOffset == 100);
    CHECK(!engine.running());

    // Continue mode: adding a note must not restart the running phrase.
    settings = Settings{};
    settings.patternLength = 8;
    settings.stepsPerQuarter = 4.0;
    settings.mode = Mode::Up;
    settings.restartOnTrigger = false;
    settings.scalePolicy = ScalePolicy::ChordOnly;
    for (auto& s : settings.steps) {
        s.enabled = true;
        s.velocity = 1.0f;
        s.gate = 0.5f;
        s.ratchet = 1;
        s.probability = 1.0f;
    }
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 7000,
                   {
                       {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOn, 1000, 0, 64, 1.0f}
                   },
                   out);
    auto continued = noteOns(out);
    CHECK(continued.size() == 2);
    CHECK(continued[0] == 60);
    CHECK(continued[1] == 64);
    bool restartedAt1000 = false;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn && e.sampleOffset == 1000)
            restartedAt1000 = true;
    }
    CHECK(!restartedAt1000);

    // Swing delays odd-numbered steps without moving Step 1.
    settings.restartOnTrigger = true;
    settings.swing = 0.5f;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 8000,
                   {
                       {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 64, 1.0f}
                   },
                   out);
    std::vector<int> onOffsets;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn)
            onOffsets.push_back(e.sampleOffset);
    }
    CHECK(onOffsets.size() == 2);
    CHECK(onOffsets[0] == 0);
    CHECK(onOffsets[1] == 7470);

    // Direction modes use the held chord deterministically.
    auto collectMode = [&](Mode mode, int samples) {
        Settings m = settings;
        m.mode = mode;
        m.swing = 0.0f;
        m.restartOnTrigger = true;
        m.patternLength = 8;
        engine.setSettings(m);
        engine.reset();
        engine.process(48000.0, 120.0, samples,
                       {
                           {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                           {MidiInput::Type::NoteOn, 0, 0, 64, 1.0f},
                           {MidiInput::Type::NoteOn, 0, 0, 67, 1.0f}
                       },
                       out);
        return noteOns(out);
    };

    auto up = collectMode(Mode::Up, 19000);
    CHECK(up.size() >= 4);
    CHECK(up[0] == 60 && up[1] == 64 && up[2] == 67 && up[3] == 60);

    auto down = collectMode(Mode::Down, 19000);
    CHECK(down.size() >= 4);
    CHECK(down[0] == 67 && down[1] == 64 && down[2] == 60 && down[3] == 67);

    auto upDown = collectMode(Mode::UpDown, 25000);
    CHECK(upDown.size() >= 5);
    CHECK(upDown[0] == 60 && upDown[1] == 64 && upDown[2] == 67 &&
          upDown[3] == 64 && upDown[4] == 60);

    // A MIDI event before a delayed swing step must not make that step vanish.
    settings = Settings{};
    settings.patternLength = 8;
    settings.stepsPerQuarter = 4.0;
    settings.mode = Mode::Up;
    settings.restartOnTrigger = false;
    settings.swing = 0.5f;
    settings.scalePolicy = ScalePolicy::ChordOnly;
    for (auto& s : settings.steps) {
        s.enabled = true;
        s.velocity = 1.0f;
        s.gate = 0.5f;
        s.ratchet = 1;
        s.probability = 1.0f;
    }
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 8000,
                   {
                       {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOn, 7000, 0, 64, 1.0f}
                   },
                   out);
    bool sawSwingStep = false;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn &&
            e.sampleOffset == 7470 && e.pitch == 64) {
            sawSwingStep = true;
        }
    }
    CHECK(sawSwingStep);

    // Random mode is deterministic after reset with the same explicit seed.
    settings.swing = 0.0f;
    settings.mode = Mode::Random;
    settings.randomSeed = 0x125A1234u;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 25000,
                   {
                       {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 64, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 67, 1.0f}
                   },
                   out);
    const auto randomA = noteOns(out);
    engine.reset();
    engine.process(48000.0, 120.0, 25000,
                   {
                       {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 64, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 67, 1.0f}
                   },
                   out);
    const auto randomB = noteOns(out);
    CHECK(randomA == randomB);

    // AllNotesOff followed by a fresh NoteOn at the same sample must retrigger,
    // not leave the new note held while the arp remains stopped.
    settings = Settings{};
    settings.patternLength = 4;
    settings.stepsPerQuarter = 4.0;
    settings.restartOnTrigger = true;
    for (auto& s : settings.steps) {
        s.enabled = true;
        s.velocity = 1.0f;
        s.gate = 1.0f;
        s.ratchet = 1;
        s.probability = 1.0f;
    }
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 1000,
                   {{MidiInput::Type::NoteOn, 0, 0, 60, 1.0f}},
                   out);
    engine.process(48000.0, 120.0, 1000,
                   {
                       {MidiInput::Type::AllNotesOff, 100, 0, 0, 0.0f},
                       {MidiInput::Type::NoteOn, 100, 0, 64, 1.0f}
                   },
                   out);
    bool retriggeredAfterStop = false;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn &&
            e.sampleOffset == 100 && e.pitch == 64) {
            retriggeredAfterStop = true;
        }
    }
    CHECK(retriggeredAfterStop);
    CHECK(engine.running());

    // Humanize/Groove/Strum at 0% must be bit-for-bit neutral in event timing
    // and velocity relative to the deterministic base engine.
    settings = Settings{};
    settings.patternLength = 4;
    settings.stepsPerQuarter = 4.0;
    settings.mode = Mode::Up;
    settings.restartOnTrigger = true;
    settings.humanize = 0.0f;
    settings.groove = 0.0f;
    settings.strum = 0.0f;
    for (auto& s : settings.steps) {
        s.enabled = true;
        s.velocity = 1.0f;
        s.gate = 1.0f;
        s.ratchet = 1;
        s.probability = 1.0f;
    }
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 13000,
                   {
                       {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 64, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 67, 1.0f}
                   },
                   out);
    std::vector<int> neutralOffsets;
    std::vector<float> neutralVelocities;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn) {
            neutralOffsets.push_back(e.sampleOffset);
            neutralVelocities.push_back(e.velocity);
        }
    }
    CHECK(neutralOffsets.size() >= 3);
    CHECK(neutralOffsets[0] == 0);
    CHECK(neutralOffsets[1] == 6000);
    CHECK(neutralOffsets[2] == 12000);
    CHECK(neutralVelocities[0] == 1.0f);
    CHECK(neutralVelocities[1] == 1.0f);
    CHECK(neutralVelocities[2] == 1.0f);

    // Humanize must be deterministic for a fixed seed, change timing and/or
    // velocity, and keep the phrase start/downbeat much tighter than later steps.
    settings.humanize = 1.0f;
    settings.randomSeed = 0x125A3344u;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 13000,
                   {
                       {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 64, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 67, 1.0f}
                   },
                   out);
    auto humanA = out;
    engine.reset();
    engine.process(48000.0, 120.0, 13000,
                   {
                       {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 64, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 67, 1.0f}
                   },
                   out);
    CHECK(humanA.size() == out.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        CHECK(humanA[i].type == out[i].type);
        CHECK(humanA[i].sampleOffset == out[i].sampleOffset);
        CHECK(humanA[i].pitch == out[i].pitch);
        CHECK(humanA[i].velocity == out[i].velocity);
    }

    bool humanChanged = false;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn &&
            (e.sampleOffset != 0 || e.velocity != 1.0f)) {
            humanChanged = true;
        }
    }
    CHECK(humanChanged);

    // Groove uses a deterministic eight-step pocket template. Step 1 remains
    // on-grid; Step 2 is delayed and accented/de-accented according to template.
    settings.humanize = 0.0f;
    settings.groove = 1.0f;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 8000,
                   {
                       {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 64, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 67, 1.0f}
                   },
                   out);
    std::vector<int> grooveOffsets;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn)
            grooveOffsets.push_back(e.sampleOffset);
    }
    CHECK(grooveOffsets.size() >= 2);
    CHECK(grooveOffsets[0] == 0);
    CHECK(grooveOffsets[1] == 6720);

    // Strum spreads later chord ranks without moving the root/first rank.
    settings.groove = 0.0f;
    settings.strum = 1.0f;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 13000,
                   {
                       {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 64, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 67, 1.0f}
                   },
                   out);
    std::vector<int> strumOffsets;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn)
            strumOffsets.push_back(e.sampleOffset);
    }
    CHECK(strumOffsets.size() >= 3);
    CHECK(strumOffsets[0] == 0);
    CHECK(strumOffsets[1] == 6360);
    CHECK(strumOffsets[2] == 12720);

    // EVOLVE is non-destructive and deliberately slow: the first four full
    // pattern cycles are identical to the stored pattern, then a deterministic
    // phase may alter only unlocked performance properties.
    settings = Settings{};
    settings.patternLength = 1;
    settings.stepsPerQuarter = 4.0;
    settings.mode = Mode::Up;
    settings.restartOnTrigger = true;
    settings.evolve = 1.0f;
    settings.randomSeed = 0x125A4455u;
    settings.steps[0].enabled = true;
    settings.steps[0].velocity = 0.70f;
    settings.steps[0].gate = 0.80f;
    settings.steps[0].ratchet = 1;
    settings.steps[0].probability = 1.0f;
    settings.steps[0].octaveOffset = 0;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 31000,
                   {{MidiInput::Type::NoteOn, 0, 0, 60, 1.0f}},
                   out);
    std::vector<MidiOutput> evolveOns;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn)
            evolveOns.push_back(e);
    }
    CHECK(evolveOns.size() >= 5);
    CHECK(evolveOns[0].sampleOffset == 0);
    CHECK(evolveOns[1].sampleOffset == 6000);
    CHECK(evolveOns[2].sampleOffset == 12000);
    CHECK(evolveOns[3].sampleOffset == 18000);
    CHECK(evolveOns[0].velocity == 0.70f);
    CHECK(evolveOns[1].velocity == 0.70f);
    CHECK(evolveOns[2].velocity == 0.70f);
    CHECK(evolveOns[3].velocity == 0.70f);
    CHECK(evolveOns[4].sampleOffset == 24000);
    CHECK(evolveOns[4].velocity != 0.70f);

    // Per-step lock also freezes non-destructive Evolve.
    settings.steps[0].locked = true;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 31000,
                   {{MidiInput::Type::NoteOn, 0, 0, 60, 1.0f}},
                   out);
    evolveOns.clear();
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn)
            evolveOns.push_back(e);
    }
    CHECK(evolveOns.size() >= 5);
    CHECK(evolveOns[4].velocity == 0.70f);

    // A fresh RESTART phrase resets Evolve to its untouched phase 0.
    settings.steps[0].locked = false;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 31000,
                   {{MidiInput::Type::NoteOn, 0, 0, 60, 1.0f}},
                   out);
    engine.process(48000.0, 120.0, 1000,
                   {{MidiInput::Type::NoteOn, 100, 0, 64, 1.0f}},
                   out);
    bool restartBaseVelocity = false;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn &&
            e.sampleOffset == 100 &&
            e.velocity == 0.70f) {
            restartBaseVelocity = true;
        }
    }
    CHECK(restartBaseVelocity);

    // EVOLVE at 0% is exactly neutral across many cycles.
    settings = Settings{};
    settings.patternLength = 2;
    settings.stepsPerQuarter = 4.0;
    settings.mode = Mode::Up;
    settings.restartOnTrigger = true;
    settings.evolve = 0.0f;
    settings.randomSeed = 0x125A7788u;
    for (auto& s : settings.steps) {
        s.enabled = true;
        s.velocity = 0.73f;
        s.gate = 0.81f;
        s.ratchet = 1;
        s.probability = 1.0f;
        s.noteOffset = 0;
        s.octaveOffset = 0;
    }
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 97000,
                   {
                       {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 64, 1.0f}
                   },
                   out);
    std::vector<MidiOutput> neutralEvolve;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn)
            neutralEvolve.push_back(e);
    }
    CHECK(neutralEvolve.size() >= 16);
    for (const auto& e : neutralEvolve) {
        CHECK(e.velocity == 0.73f);
        CHECK(e.pitch == 60 || e.pitch == 64);
    }

    // Locking every Evolve dimension must make 100% Evolve audibly neutral.
    settings.evolve = 1.0f;
    settings.evolveLocks.rhythm = true;
    settings.evolveLocks.velocity = true;
    settings.evolveLocks.gate = true;
    settings.evolveLocks.ratchet = true;
    settings.evolveLocks.probability = true;
    settings.evolveLocks.octave = true;
    settings.evolveLocks.note = true;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 97000,
                   {
                       {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 64, 1.0f}
                   },
                   out);
    std::vector<MidiOutput> lockedEvolve;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn)
            lockedEvolve.push_back(e);
    }
    CHECK(lockedEvolve.size() == neutralEvolve.size());
    for (std::size_t i = 0; i < lockedEvolve.size(); ++i) {
        CHECK(lockedEvolve[i].sampleOffset == neutralEvolve[i].sampleOffset);
        CHECK(lockedEvolve[i].pitch == neutralEvolve[i].pitch);
        CHECK(lockedEvolve[i].velocity == neutralEvolve[i].velocity);
    }

    // Even at 100% with rhythm unlocked, a one-step pattern may never evolve
    // into a completely silent phase.
    settings = Settings{};
    settings.patternLength = 1;
    settings.stepsPerQuarter = 4.0;
    settings.mode = Mode::Up;
    settings.restartOnTrigger = true;
    settings.evolve = 1.0f;
    settings.randomSeed = 0x125A9901u;
    settings.steps[0].enabled = true;
    settings.steps[0].velocity = 1.0f;
    settings.steps[0].gate = 1.0f;
    settings.steps[0].ratchet = 1;
    settings.steps[0].probability = 1.0f;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 193000,
                   {{MidiInput::Type::NoteOn, 0, 0, 60, 1.0f}},
                   out);
    std::size_t oneStepOns = 0;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn)
            ++oneStepOns;
    }
    CHECK(oneStepOns >= 32);

    // Per-step NOTE selects a relative position in the arp order rather than
    // a chromatic semitone. This keeps CHORD ONLY / SCALE behaviour musical.
    settings = Settings{};
    settings.patternLength = 3;
    settings.stepsPerQuarter = 4.0;
    settings.mode = Mode::Up;
    settings.restartOnTrigger = true;
    settings.scalePolicy = ScalePolicy::ChordOnly;
    for (auto& s : settings.steps) {
        s.enabled = true;
        s.velocity = 1.0f;
        s.gate = 1.0f;
        s.ratchet = 1;
        s.probability = 1.0f;
        s.noteOffset = 0;
    }
    settings.steps[0].noteOffset = 0;
    settings.steps[1].noteOffset = 1;
    settings.steps[2].noteOffset = -2;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 13000,
                   {
                       {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 64, 1.0f},
                       {MidiInput::Type::NoteOn, 0, 0, 67, 1.0f}
                   },
                   out);
    auto selectedNotes = noteOns(out);
    CHECK(selectedNotes.size() >= 3);
    CHECK(selectedNotes[0] == 60);
    CHECK(selectedNotes[1] == 67);
    CHECK(selectedNotes[2] == 60);

    // Rendering must be block-size independent: identical musical settings
    // and input events yield identical absolute NoteOn positions.
    auto renderWithBlock = [](int blockSize) {
        Engine e;
        Settings s;
        s.patternLength = 7;
        s.stepsPerQuarter = 4.0;
        s.mode = Mode::UpDown;
        s.restartOnTrigger = true;
        s.swing = 0.30f;
        s.humanize = 0.40f;
        s.groove = 0.50f;
        s.strum = 0.20f;
        s.randomSeed = 0x125A5566u;
        for (auto& step : s.steps) {
            step.enabled = true;
            step.velocity = 0.8f;
            step.gate = 0.7f;
            step.ratchet = 1;
            step.probability = 1.0f;
        }
        e.setSettings(s);
        e.reset();

        std::vector<long long> absoluteOns;
        std::vector<MidiOutput> localOut;
        int cursor = 0;
        const int total = 60000;
        bool first = true;
        while (cursor < total) {
            const int n = std::min(blockSize, total - cursor);
            std::vector<MidiInput> in;
            if (first) {
                in = {
                    {MidiInput::Type::NoteOn, 0, 0, 60, 1.0f},
                    {MidiInput::Type::NoteOn, 0, 0, 64, 1.0f},
                    {MidiInput::Type::NoteOn, 0, 0, 67, 1.0f}
                };
                first = false;
            }
            e.process(48000.0, 120.0, n, in, localOut);
            for (const auto& ev : localOut) {
                if (ev.type == MidiInput::Type::NoteOn)
                    absoluteOns.push_back(
                        static_cast<long long>(cursor + ev.sampleOffset));
            }
            cursor += n;
        }
        return absoluteOns;
    };

    const auto block64 = renderWithBlock(64);
    const auto block257 = renderWithBlock(257);
    const auto block1024 = renderWithBlock(1024);
    CHECK(block64 == block257);
    CHECK(block64 == block1024);
    CHECK(!block64.empty());

    // Fractional sample grids at 44.1 kHz remain sample-accurate.
    settings = Settings{};
    settings.patternLength = 4;
    settings.stepsPerQuarter = 4.0;
    settings.mode = Mode::Up;
    for (auto& s : settings.steps) {
        s.enabled = true;
        s.velocity = 1.0f;
        s.gate = 1.0f;
        s.ratchet = 1;
        s.probability = 1.0f;
    }
    engine.setSettings(settings);
    engine.reset();
    engine.process(44100.0, 120.0, 18000,
                   {{MidiInput::Type::NoteOn, 0, 0, 60, 1.0f}},
                   out);
    std::vector<int> rate441;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn)
            rate441.push_back(e.sampleOffset);
    }
    CHECK(rate441.size() >= 4);
    CHECK(rate441[0] == 0);
    CHECK(rate441[1] == 5512);
    CHECK(rate441[2] == 11025);
    CHECK(rate441[3] == 16537);

    // Tempo change preserves fractional phase of the pending step. Starting at
    // 120 BPM, halfway to the next 1/16 step, switching to 240 BPM should leave
    // half of the new 3000-sample step = 1500 samples remaining.
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 3000,
                   {{MidiInput::Type::NoteOn, 0, 0, 60, 1.0f}},
                   out);
    engine.process(48000.0, 240.0, 2000, {}, out);
    bool tempoAdjusted = false;
    for (const auto& e : out) {
        if (e.type == MidiInput::Type::NoteOn &&
            e.sampleOffset == 1500) {
            tempoAdjusted = true;
        }
    }
    CHECK(tempoAdjusted);

    // SCALE policy honors the supplied pitch-class mask. C major excludes F#,
    // while C Lydian includes it.
    settings = Settings{};
    settings.patternLength = 1;
    settings.stepsPerQuarter = 4.0;
    settings.mode = Mode::Up;
    settings.scalePolicy = ScalePolicy::Scale;
    settings.keyRoot = 0;
    settings.scaleMask = 0x0AB5u; // C major
    settings.steps[0].enabled = true;
    settings.steps[0].velocity = 1.0f;
    settings.steps[0].gate = 1.0f;
    settings.steps[0].probability = 1.0f;
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 1000,
                   {{MidiInput::Type::NoteOn, 0, 0, 66, 1.0f}},
                   out);
    auto majorConstrained = noteOns(out);
    CHECK(!majorConstrained.empty());
    CHECK(majorConstrained[0] == 65);

    settings.scaleMask = 0x0AD5u; // C Lydian includes F#
    engine.setSettings(settings);
    engine.reset();
    engine.process(48000.0, 120.0, 1000,
                   {{MidiInput::Type::NoteOn, 0, 0, 66, 1.0f}},
                   out);
    auto lydianConstrained = noteOns(out);
    CHECK(!lydianConstrained.empty());
    CHECK(lydianConstrained[0] == 66);

    std::cout << "ArporatorEngineTests PASS\n";
    return EXIT_SUCCESS;
}
