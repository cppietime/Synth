#ifndef _H_SYNTH
#define _H_SYNTH

#include <map>
#include <ostream>
#include <utility>
#include <vector>

#include "aviutil.hpp"

#include "midi.hpp"

namespace Synth {
    
    const static float USEC_TO_MSEC = 0.001,
        SEC_TO_MSEC = 1000.0;
    
    class PlayingNote;
    
    typedef float (*floatfunc)(float); // Function that takes a float and returns a float
    typedef float (*resfunc)(float, float, float); // Function that takes phase, wave param, and previous sample, and returns a float
    typedef void (*callback)(const std::vector<float>&, void*,
        const std::map<std::pair<int, int>, PlayingNote>& notes); // Function that consumes samples

    class Envelope {
        private:
            std::vector<std::pair<float, float>> envelope; // Pairs of time-amplitude
            size_t sustainId; // envelope# after which to sustain
            float releaseTime;
        public:
            Envelope(
                    const std::vector<std::pair<float, float>>& env = {{0, 1}},
                    const size_t sid = 0) :
                envelope {env}, sustainId {sid} {
                    releaseTime = 0;
                    for (size_t i = sustainId + 1; i < envelope.size(); i++) {
                        releaseTime += envelope[i].first;
                    }
            }
            
            static Envelope read(std::istream& stream);
            
            float amplitude(float elapsedTime, bool isActive) const;
            bool isAlive(float elapsedTime, bool isActive) const;
            friend std::ostream& operator<<(std::ostream& stream, const Envelope& obj);
    };
    
    std::ostream& operator<<(std::ostream& stream, const Envelope& obj);
    
    class LFO {
        private:
            float frequency;
            float depth;
            floatfunc shape;
            float offset; // Starting phase
            float dc;
        public:
            LFO(const float frequency = 0,
                const float depth = 0,
                const floatfunc shape = zero,
                const float offset = 0,
                const float dc = 0) :
            frequency {frequency}, depth {depth}, shape {shape}, offset {offset}, dc {dc} {}
            
            static LFO read(std::istream& stream);
            
            float operator()(float phase) const;
            
            static float sine(float phase);
            static float sawUp(float phase);
            static float sawDown(float phase);
            static float triangle(float phase);
            static float zero(float phase);
            
            static LFO silence;
            friend std::ostream& operator<<(std::ostream& stream, const LFO& obj);
    };
    
    std::ostream& operator<<(std::ostream& stream, const LFO& obj);
    
    class Synth {
        private:
            Envelope dca; // Modulates amplitude
            Envelope dcw; // Modulates wave param
            Envelope dco; // Modulates frequency
            LFO vibrato;
            LFO tremelo;
        public:
            resfunc shape;
            Synth(const resfunc shape = resonantSaw,
                const Envelope& dca = {{{0, 1}}},
                const Envelope& dcw = {{{0, 0}}},
                const Envelope& dco = {{{0, 0}}},
                const LFO& vibrato = LFO::silence,
                const LFO& tremelo = LFO::silence) :
            shape {shape}, dca {dca}, dcw {dcw}, dco {dco}, vibrato {vibrato}, tremelo {tremelo} {}
            
            static Synth read(std::istream& stream);
            
            float freqDelta(float time, float eTime, bool isActive) const;
            float amplitude(float time, float eTime, bool isActive) const;
            float waveParam(float time, float eTime, bool isActive) const;
            bool isAlive(float eTime, bool isActive) const;
            
            static float sinSaw(float phase, float param, float previous);
            static float resonantSaw(float phase, float param, float previous);
            static float noise(float phase, float param, float previous);
            friend std::ostream& operator<<(std::ostream& stream, const Synth& obj);
    };
    
    std::ostream& operator<<(std::ostream& stream, const Synth& obj);
    
    struct PatchState {
        public:
            float phase;
            float previous; // Previous sample value
            float time;
            float eTime;
            bool isActive;
    };
    
    class Patch {
        private:
            std::vector<Synth> synths; // Alternates through consecutive synths per period
        public:
            Patch(const std::vector<Synth>& synths = {{}}) :
                synths {synths} {}
                
            static Patch read(std::istream& stream);
            
            bool operator()(PatchState& state, float frequency, float samplerate) const;
            friend std::ostream& operator<<(std::ostream& stream, const Patch& obj);
    };
    
    std::ostream& operator<<(std::ostream& stream, const Patch& obj);
    
    class PlayingNote {
        private:
            const Patch& patch;
            float frequency;
            bool isAlive; // Can still be heard
            PatchState state;
        public:
            PlayingNote(const Patch& patch, float frequency, float phase = 0,
                bool isAlive = true, bool isActive = true) :
            patch {patch},
            frequency {frequency},
            isAlive {isAlive},
            state {phase, 0.0, 0.0, 0.0, isActive}
            {}
            
            void writeFloats(std::vector<float>& dst, float samplerate, int maxNotes);
            inline bool alive()
            {
                return isAlive;
            }
            inline void stop()
            {
                state.isActive = false;
                state.eTime = 0;
            }
    };
    
    std::vector<Patch> readPatches(std::istream& stream);
    
    void play(std::istream& midiStream,
        float samplerate,
        callback func,
        const std::vector<Patch>& patches,
        void *data);
    
    void play(const std::vector<Midi::MidiMessage>& msgs,
        const Midi::MidiHeader& header,
        float samplerate,
        callback func,
        const std::vector<Patch>& patches,
        void *data);
    
    class Visualizer {
        protected:
            float samplerate;
            float framerate;
            float sampleNorm;
            int bps;
            int width, height;
            std::vector<int32_t> buffer;
            std::vector<std::uint8_t> rgb;
            Avi::FlacMjpegAvi fmavi;
            std::ostream& out;
        
        public:
            Visualizer(
                float samplerate,
                float fps,
                int width,
                int height,
                int bps,
                std::ostream& stream,
                int jpegQuality = 90) :
            samplerate {samplerate},
            framerate {fps},
            bps {bps},
            width {width},
            height {height},
            sampleNorm ((1 << (bps - 1)) - 1),
            rgb (width * height * 3),
            out {stream},
            fmavi {
                width, height, fps, bps, samplerate, 1, Avi::NORMAL, jpegQuality
            } {
                fmavi.prepare(out);
            }
            
            virtual ~Visualizer() {}
            
            virtual void callback(const std::vector<float>& samples,
                const std::map<std::pair<int, int>, PlayingNote>& notes) = 0;
            
            void finish()
            {
                fmavi.writeSamples(out, buffer);
                fmavi.finish(out);
            }
        
            static void play(const std::vector<float>& samples,
                void *data,
                const std::map<std::pair<int, int>, PlayingNote>& notes)
            {
                Visualizer *vs = static_cast<Visualizer*>(data);
                vs->callback(samples, notes);
            }
        
    };

}

#endif