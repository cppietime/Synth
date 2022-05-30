#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <istream>
#include <iostream>
#include <map>
#include <vector>

#include "synthutil.hpp"

namespace Synth {
    
    float Envelope::amplitude(float eTime, bool isActive) const
    {
        if (envelope.size() == 1)
        {
            return envelope[0].second;
        }
        size_t stage = isActive ? 0 : sustainId;
        size_t lastStage = isActive ? (sustainId + 1) : envelope.size();
        while (stage + 1 < lastStage && eTime >= envelope[stage + 1].first)
        {
            stage ++;
            eTime -= envelope[stage].first;
        }
        if (isActive && stage == sustainId)
        {
            return envelope[stage].second;
        }
        if (stage + 1 == lastStage)
        {
            return 0;
        }
        float pre = envelope[stage].second;
        float post = envelope[stage + 1].second;
        float interval = envelope[stage + 1].first;
        return pre + (post - pre) * (eTime / interval);
    }
    
    bool Envelope::isAlive(float eTime, bool isActive) const
    {
        if (isActive) {
            return true;
        }
        return eTime < releaseTime;
    }
    
    LFO LFO::silence {0, 0};
    
    float LFO::operator()(float phase) const
    {
        return dc + depth * shape(offset + phase * frequency);
    }
    
    float LFO::sine(float phase)
    {
        return std::sin(phase);
    }
    
    float LFO::sawUp(float phase)
    {
        phase /= M_PI;
        return fmod(phase, 2) - 1;
    }
    
    float LFO::sawDown(float phase)
    {
        return -sawUp(phase);
    }
    
    float LFO::triangle(float phase)
    {
        phase /= M_PI;
        return std::min(phase, 2 - phase) * 2 - 1;
    }
    
    float LFO::zero(float phase)
    {
        return 0;
    }
    
    float Synth::freqDelta(float time, float eTime, bool isActive) const
    {
        float vib = vibrato(time * 2 * M_PI);
        return dco.amplitude(eTime, isActive) + vib;
    }
    
    float Synth::amplitude(float time, float eTime, bool isActive) const
    {
        float trem = tremelo(time * 2 * M_PI);
        return dca.amplitude(eTime, isActive) * (1 + trem);
    }
    
    float Synth::waveParam(float time, float eTime, bool isActive) const
    {
        return dcw.amplitude(eTime, isActive);
    }
    
    bool Synth::isAlive(float eTime, bool isActive) const
    {
        return dca.isAlive(eTime, isActive);
    }
    
    float Synth::sinSaw(float phase, float param, float previous)
    {
        float sine = LFO::sine(phase);
        float saw = LFO::sawUp(phase);
        return sine + (saw - sine) * param;
    }
    
    float Synth::resonantSaw(float phase, float param, float previous)
    {
        float sine = LFO::sine(phase * param);
        phase /= M_PI * 2;
        return sine * (1 - fmod(phase, 1.0));
    }
    
    float Synth::noise(float phase, float param, float previous)
    {
        float next = rand() / (float)RAND_MAX;
        return previous + (next - previous) * param;
    }
    
    bool Patch::operator()(PatchState& state, float frequency, float samplerate) const
    {
        size_t synthNum = state.phase / (2 * M_PI * synths.size());
        float subPhase = state.phase - synthNum * synths.size();
        const Synth& synth = synths[synthNum];
        float amplitude = synth.amplitude(state.time, state.eTime, state.isActive);
        float param = synth.waveParam(state.time, state.eTime, state.isActive);
        float freqDelta = synth.freqDelta(state.time, state.eTime, state.isActive);
        float sample = synth.shape(subPhase, param, state.previous) * amplitude;
        float effFreq = frequency * pow(2, freqDelta / 12.0);
        float timeDelta = 1.0 / samplerate;
        state.phase = fmod(state.phase + 2 * M_PI * effFreq * timeDelta, 2 * M_PI * synths.size());
        state.time += timeDelta;
        state.eTime += timeDelta;
        state.previous = sample;
        return synth.isAlive(state.eTime, state.isActive);
    }
    
    void PlayingNote::writeFloats(std::vector<float>& samples, float samplerate, int maxNotes)
    {
        for (auto it = samples.begin(); it != samples.end(); it++) {
            isAlive = patch(state, frequency, samplerate);
            *it += state.previous / maxNotes;
        }
    }
    
    const static uint32_t DEFAULT_TEMPO = 500000;
    
    void play(std::istream& stream,
        float samplerate,
        callback func,
        const std::vector<Patch>& patches,
        void *data)
    {
        Midi::MidiHeader header;
        Midi::readHeader(stream, header);
        std::vector<std::vector<Midi::MidiMessage>> tracks;
        for (size_t i = 0; i < header.ntrks; i++) {
            std::vector<Midi::MidiMessage> track;
            Midi::readTrack(stream, track);
            tracks.push_back(track);
        }
        std::vector<Midi::MidiMessage> track = Midi::joinTracks(tracks);
        play(track, header, samplerate, func, patches, data);
    }
    
    void play(const std::vector<Midi::MidiMessage>& track,
        const Midi::MidiHeader& header,
        float samplerate,
        callback func,
        const std::vector<Patch>& patches,
        void *data)
    {
        float samplesPerMsec = samplerate / SEC_TO_MSEC;
        int maxNotes = Midi::maxPolyphony(track);
        std::map<int, int> programs;
        std::map<std::pair<int, int>, PlayingNote> playingNotes;
        std::vector<float> fSamples;
        uint32_t usecPerQNote = DEFAULT_TEMPO;
        for (auto msg : track) {
            if (msg.deltaTime) {
                float ms = header.miliseconds(msg.deltaTime, usecPerQNote);
                size_t numSamples = ms * samplesPerMsec;
                fSamples.resize(numSamples);
                std::fill(fSamples.begin(), fSamples.end(), 0);
                for (auto it = playingNotes.begin(); it != playingNotes.end(); it++) {
                    it->second.writeFloats(fSamples, samplerate, maxNotes);
                }
                func(fSamples, data, playingNotes);
                for (auto it = playingNotes.begin(); it != playingNotes.end();) {
                    if (!it->second.alive())
                    {
                        it = playingNotes.erase(it);
                    }
                    else {
                        it ++;
                    }
                }
            }
            if (msg.msgType == Midi::TEMPO) {
                usecPerQNote = ((uint32_t)msg.data[0] << 16) |
                    ((uint32_t)msg.data[1] << 8) |
                    ((uint32_t)msg.data[2]);
            }
            else if ((msg.msgType & 0xF0) == Midi::NOTE_ON ||
                (msg.msgType & 0xF0) == Midi::NOTE_OFF) {
                int channel = msg.msgType & 0xF;
                int nid = msg.data[0];
                if ((msg.msgType & 0xF0) == Midi::NOTE_ON) {
                    size_t index;
                    if (channel == 9) { // Drums
                        index = patches.size() - 1;
                    }
                    else if (programs.find(channel) == programs.end()) {
                        programs[channel] = 0;
                        index = 0;
                    }
                    else {
                        index = programs[channel];
                    }
                    const Patch& patch = patches[index];
                    PlayingNote note(patch, Midi::noteToFrequency(nid, 0));
                    playingNotes.insert({{channel, nid}, note});
                }
                else {
                    auto it = playingNotes.find({channel, nid});
                    if (it != playingNotes.end()) {
                        it->second.stop();
                    }
                }
            }
        }
    }
    
}