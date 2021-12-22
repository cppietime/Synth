#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <istream>
#include <iterator>
#include <iostream>
#include <ios>
#include <fstream>
#include <limits>
#include <set>

#include "synthutil.hpp"

namespace Midi {
    
    float MidiHeader::miliseconds(uint32_t ticks, uint32_t usecPerQNote) const
    {
        float units = (float)ticks / ticksPerUnit;
        if (unit == QNOTE) {
            return (float)usecPerQNote * units * Synth::USEC_TO_MSEC;
        }
        float framesPerSecond = -unit;
        if (unit == DRP30) {
            framesPerSecond = 29.97;
        }
        return units / framesPerSecond * Synth::SEC_TO_MSEC;
    }
    
    const static size_t MTHD_LENGTH = 6;
    
    static uint32_t betoh32(std::uint8_t *buff)
    {
        return buff[3] | (buff[2] << 8) | (buff[1] << 16) | (buff[0] << 24);
    }
    
    static uint32_t betoh16(std::uint8_t *buff)
    {
        return buff[1] | (buff[0] << 8);
    }
    
    bool readHeader(std::istream& stream, MidiHeader& header)
    {
        char buff[5];
        buff[4] = 0;
        stream.read(buff, 4);
        if (strncmp(buff, "MThd", 4)) {
            std::cerr << "MThd chunk not found, instead got";
            for (size_t i = 0; i < 4; i++) {
                std::cerr << (int)buff[i] << ", ";
            }
            std::cerr << "\n";
            return false;
        }
        stream.read(buff, 4);
        uint32_t length = betoh32(reinterpret_cast<uint8_t*>(buff));
        if (length != MTHD_LENGTH) {
            std::cerr << "Invalid length of MThd chunk\n";
            return false;
        }
        stream.read(buff, 2);
        header.format = betoh16(reinterpret_cast<uint8_t*>(buff));
        stream.read(buff, 2);
        header.ntrks = betoh16(reinterpret_cast<uint8_t*>(buff));
        stream.read(buff, 2);
        uint16_t div = betoh16(reinterpret_cast<uint8_t*>(buff));
        if (div & 0x8000) { // SMPTE
            header.unit = (((~div) >> 8) + 1) & 0x3f;
            header.ticksPerUnit = div & 0xff;
        }
        else {
            header.unit = QNOTE;
            header.ticksPerUnit = div;
        }
        return true;
    }
    
    uint32_t readVarLength(std::istream& stream, size_t& numOut)
    {
        size_t num = 0;
        uint32_t i = 0;
        while (true) {
            uint8_t byte = stream.get();
            num++;
            i = (i << 7) | (byte & 0x7f);
            if (!(byte & 0x80)) {
                numOut = num;
                return i;
            }
        }
    }
    
    bool readTrack(std::istream& stream, std::vector<MidiMessage>& track)
    {
        char buff[4];
        stream.read(buff, 4);
        if (strncmp(buff, "MTrk", 4)) {
            std::cerr << "MTrk chunk not found\n";
            return false;
        }
        stream.read(buff, 4);
        int32_t length = betoh32(reinterpret_cast<uint8_t*>(buff));
        size_t bytesRead;
        uint16_t running;
        uint32_t deltaTime = 0;
        while (length > 0) {
            if (stream.eof()) {
                std::cerr << "Stream ran out before finished reading track\n";
                return false;
            }
            std::vector<uint8_t> extraBytes;
            deltaTime += readVarLength(stream, bytesRead);
            length -= bytesRead;
            size_t numExtraBytes;
            uint16_t status = stream.get();
            length --;
            if (status == 0xFF) { // Meta event
                status = 0xFF00 | stream.get();
                length --;
                numExtraBytes = readVarLength(stream, bytesRead);
                length -= bytesRead;
            }
            else {
                numExtraBytes = 2;
                if (!(status & 0x80)) { // Running status
                    extraBytes.push_back(status);
                    status = running;
                }
                else {
                    running = status;
                    if ((status & 0xF0) == PROGRAM || (status & 0xF0) == CHANNEL_PRESSURE) {
                        numExtraBytes = 1;
                    }
                }
            }
            while (extraBytes.size() < numExtraBytes) {
                extraBytes.push_back(stream.get());
                length --;
            }
            if (
                (status & 0xF0) == PROGRAM ||
                (status & 0xF0) == NOTE_OFF ||
                (status & 0xF0) == NOTE_ON ||
                status == END_OF_TRACK ||
                status == TEMPO ) {
                    MidiMessage msg {deltaTime, status, extraBytes};
                    deltaTime = 0;
                    track.push_back(msg);
            }
            if (status == END_OF_TRACK && length) {
                std::cerr << "Premature end of track message with " << length << " bytes left\n";
                return false;
            }
        }
        if (track.empty() || track.back().msgType != END_OF_TRACK) {
            std::cerr << "Missing end of track message\n";
            return false;
        }
        return true;
    }
    
    std::vector<MidiMessage> joinTracks(const std::vector<std::vector<MidiMessage>>& tracks)
    {
        size_t ntrks = tracks.size();
        std::vector<MidiMessage> joined;
        std::vector<uint32_t> times(ntrks, 0);
        std::vector<size_t> indices(ntrks, 0);
        uint32_t time = 0;
        for (size_t i = 0; i < ntrks; i++) {
            times[ntrks] = tracks[i][0].deltaTime;
        }
        size_t trackNo;
        while (true) {
            auto least = std::min_element(times.begin(), times.end());
            trackNo = std::distance(times.begin(), least);
            if (indices[trackNo] == tracks[trackNo].size()) {
                break;
            }
            MidiMessage msg (tracks[trackNo][indices[trackNo]]);
            times[trackNo] += msg.deltaTime;
            msg.deltaTime = times[trackNo] - time;
            time = times[trackNo];
            indices[trackNo]++;
            joined.push_back(msg);
        }
        return joined;
    }
    
    int maxPolyphony(const std::vector<MidiMessage>& msgs)
    {
        std::set<std::pair<int, int>> notes;
        int polyphony = 1;
        for (auto msg : msgs) {
            if ((msg.msgType & 0xF0) == NOTE_ON) {
                notes.insert({msg.msgType & 0xF, msg.data[0]});
            }
            else if ((msg.msgType & 0xF0) == NOTE_OFF) {
                notes.erase({msg.msgType & 0xF, msg.data[0]});
            }
            polyphony = std::max(polyphony, (int)notes.size());
        }
        std::cerr << "Max polyphony = " << polyphony << "\n";
        return polyphony;
    }
    
    const static float A4_FREQUENCY = 440.0,
        A4_NOTE = 69,
        CENTS_MULTIPLIER = 0.01;
    
    float noteToFrequency(int midiNote, int cents)
    {
        return A4_FREQUENCY * pow(2, (midiNote + CENTS_MULTIPLIER * cents - A4_NOTE) / 12);
    }
    
}