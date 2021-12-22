#ifndef _H_MIDI
#define _H_MIDI

#include <cstdint>
#include <istream>
#include <memory>
#include <vector>

namespace Midi {
    
    enum MTCUnit {
        QNOTE = 0,
        FPS24 = -24,
        FPS25 = -25,
        DRP30 = -29,
        FPS30 = -30
    };
    
    struct MidiHeader {
        public:
            uint8_t format;
            uint16_t ntrks;
            uint16_t ticksPerUnit;
            int unit;
            float miliseconds(uint32_t ticks, uint32_t usecPerQNote) const;
    };
    
    enum MessageType {
        NOTE_OFF = 0x80,
        NOTE_ON = 0x90,
        POLY_PRESSURE = 0xA0,
        CONTROL = 0xB0,
        PROGRAM = 0xC0,
        CHANNEL_PRESSURE = 0xD0,
        PITCH = 0xE0,
        END_OF_TRACK = 0xFF2F, // 3 Bytes
        TEMPO = 0xFF51, // 3 Bytes
    };
    
    struct MidiMessage {
        public:
            uint32_t deltaTime;
            uint16_t msgType;
            std::vector<uint8_t> data;
            MidiMessage(uint32_t deltaTime, uint16_t msgType, const std::vector<uint8_t>& data) :
                deltaTime {deltaTime}, msgType {msgType}, data {data} {}
    };
    
    bool readHeader(std::istream& stream, MidiHeader& header);
    bool readTrack(std::istream& stream, std::vector<MidiMessage>& track);
    std::vector<MidiMessage> joinTracks(const std::vector<std::vector<MidiMessage>>& tracks);
    int maxPolyphony(const std::vector<MidiMessage>& msgs);
    float noteToFrequency(int midiNote, int cents);
    
}

#endif