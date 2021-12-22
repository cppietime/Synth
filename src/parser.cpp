#include <cctype>
#include <iostream>
#include <istream>
#include <utility>
#include <vector>

#include "synthutil.hpp"

namespace Synth {
    
    static void skipWhitespace(std::istream& stream)
    {
        while (!stream.eof()) {
            if (!isspace(stream.get())) {
                stream.unget();
                return;
            }
        }
    }
    
    static int getChar(std::istream& stream)
    {
        skipWhitespace(stream);
        if (stream.eof()) {
            throw "Reached EOF before next char";
        }
        return stream.get();
    }
    
    static bool numReady(std::istream& stream)
    {
        skipWhitespace(stream);
        int next = stream.get();
        stream.unget();
        return isdigit(next);
    }
    
    static int getDelim(std::istream& stream)
    {
        if (numReady(stream)) {
            return 0;
        }
        if (stream.eof()) {
            throw "Reached EOF before next char";
        }
        return stream.get();
    }
    
    template <class T, class U>
    static std::pair<T, U> getPair(std::istream& stream)
    {
        std::pair<T, U> pair;
        stream >> pair.first;
        getDelim(stream);
        stream >> pair.second;
        return pair;
    }
    
    Envelope Envelope::read(std::istream& stream)
    {
        std::vector<std::pair<float, float>> pairs;
        size_t sustain = 0;
        while (!stream.eof()) {
            std::pair<float, float> pair = getPair<float, float>(stream);
            pairs.push_back(pair);
            if (getDelim(stream) == '\'') {
                sustain = pairs.size() - 1;
            }
            if (getDelim(stream) == '!') {
                return {pairs, sustain};
            }
        }
        return {pairs, sustain};
    }
    
    std::ostream& operator<<(std::ostream& stream, const Envelope& obj)
    {
        for (auto it : obj.envelope) {
            stream << it.first << "," << it.second << " : ";
        }
        stream << " SUS " << obj.sustainId << "\n";
        return stream;
    }
    
    LFO LFO::read(std::istream& stream)
    {
        LFO lfo;
        stream >> lfo.frequency;
        getDelim(stream);
        stream >> lfo.depth;
        if (!stream.eof() && getDelim(stream) == '!') {
            return lfo;
        }
        int funcId = 0;
        stream >> funcId;
        floatfunc funcs[5] = {
            sine,
            sawUp,
            sawDown,
            triangle,
            zero
        };
        lfo.shape = funcs[funcId];
        if (!stream.eof() && getDelim(stream) == '!') {
            return lfo;
        }
        stream >> lfo.offset;
        if (!stream.eof() && getDelim(stream) == '!') {
            return lfo;
        }
        stream >> lfo.dc;
        if (!stream.eof()) {
            getDelim(stream);
        }
        return lfo;
    }
    
    std::ostream& operator<<(std::ostream& stream, const LFO& obj)
    {
        stream << "[" << obj.frequency << "hz, " << obj.depth << ", " << obj.offset << ", " << obj.dc << "]\n";
        return stream;
    }
    
    Synth Synth::read(std::istream& stream)
    {
        Synth synth;
        while (!stream.eof()) {
            int id = getChar(stream);
            switch (id) {
                case '!':
                    return synth;
                case 'A':
                    synth.dca = Envelope::read(stream);
                    break;
                case 'W':
                    synth.dcw = Envelope::read(stream);
                    break;
                case 'O':
                    synth.dco = Envelope::read(stream);
                    break;
                case 'V':
                    synth.vibrato = LFO::read(stream);
                    break;
                case 'T':
                    synth.tremelo = LFO::read(stream);
                    break;
                case 'F': {
                    int funcId;
                    stream >> funcId;
                    resfunc funcs[3] = {sinSaw, resonantSaw, noise};
                    synth.shape = funcs[funcId];
                    getDelim(stream);
                    break;
                }
            }
        }
        return synth;
    }
    
    std::ostream& operator<<(std::ostream& stream, const Synth& obj)
    {
        stream << "[\n\tA" << obj.dca;
        stream << "\tO" << obj.dco;
        stream << "\tW" << obj.dcw;
        stream << "\tV" << obj.vibrato;
        stream << "\tT" << obj.tremelo;
        stream << "]\n";
        return stream;
    }
    
    Patch Patch::read(std::istream& stream)
    {
        Patch patch{{}};
        while (!stream.eof()) {
            if (getChar(stream) == '!') {
                return patch;
            }
            stream.unget();
            patch.synths.push_back(Synth::read(stream));
        }
        return patch;
    }
    
    std::ostream& operator<<(std::ostream& stream, const Patch& obj)
    {
        stream << "{#" << obj.synths.size() << "\n";
        for (auto it : obj.synths) {
            stream << it;
        }
        stream << "}\n";
        return stream;
    }
    
    std::vector<Patch> readPatches(std::istream& stream)
    {
        std::vector<Patch> patches;
        try {
            while (!stream.eof()) {
                if (getChar(stream) == '!') {
                    return patches;
                }
                stream.unget();
                patches.push_back(Patch::read(stream));
            }
            return patches;
        } catch (const std::string& error) {
            std::cerr << "Error reading patches: " << error << "\n";
            if (patches.empty()) {
                patches.push_back(Patch());
            }
            return patches;
        }
    }

}