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
    for (int i = 0; i < 12; ++i) {
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
    CHECK(out.front().sampleOffset == 250);
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

    std::cout << "ArporatorEngineTests PASS\n";
    return EXIT_SUCCESS;
}
