#include <iostream>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <ctime>

#include "jpegutil.hpp"
#include "aviutil.hpp"
#include "synthutil.hpp"

#include <CL/cl.hpp>

#define PARAMS_PER_BALL 6

struct Ball {
    float x, y;
    float dX, dY;
    float rad;
    std::uint8_t r, g, b;
    
    Ball(float x, float y, float dX, float dY, float rad, std::uint8_t r, std::uint8_t g, std::uint8_t b) :
        x{x}, y{y}, dX{dX}, dY{dY}, rad{rad}, r{r}, g{g}, b{b} {}
    
    void step(float maxX, float maxY)
    {
        x += dX;
        if (x < 0 || x >= maxX) {
            x = std::max(0.0f, std::min(maxX, x));
            dX = -dX;
        }
        y += dY;
        if (y < 0 || y >= maxY) {
            y = std::max(0.0f, std::min(maxY, y));
            dY = -dY;
        }
    }
    
    void attract(float ox, float oy, float mult = 1.0f)
    {
        float dmag = std::hypot(dX, dY);
        float pX = ox - x;
        float pY = oy - y;
        float omag = std::hypot(pX, pY);
        dmag *= mult / omag;
        dX = pX * dmag;
        dY = pY * dmag;
    }
        
};

struct VideoState : Synth::Visualizer {
    
    int numFrames;
    std::vector<Ball> balls;
    bool playingDrums;

    cl::Platform platform;
    cl::Device device;
    cl::Context context;
    cl::Program::Sources sources;
    cl::Program program;
    cl::CommandQueue q;
    cl::Kernel kernel;
    cl::Buffer input;
    cl::Buffer output;
    
    Jpeg::JpegSettings subjpegsettings;
    Jpeg::Jpeg subimg;
    
    VideoState(
            float samplerate,
            float fps,
            int width,
            int height,
            int bps,
            std::ostream& stream,
            int jpegQuality = 90,
            float maxVel = 1.0f / 3,
            float maxRad = 1.0f / 10) :
        Visualizer (samplerate, fps, width, height, bps, stream, jpegQuality),
        subjpegsettings (std::pair<int, int>(width, height), nullptr, Jpeg::DPI, {1, 1}, jpegQuality),
        subimg (subjpegsettings),
        numFrames {0},
        playingDrums {false} {
            for (size_t n = 0; n < 5; n++) {
                balls.push_back({(float)rand() / RAND_MAX * width, (float)rand() / RAND_MAX * height,
                    (float)rand() / RAND_MAX * width * maxVel / fps,
                    (float)rand() / RAND_MAX * height * maxVel / fps,
                    (float)rand() / RAND_MAX * width * maxRad,
                    (std::uint8_t)rand(), (std::uint8_t)rand(), (std::uint8_t)rand()});
            }
            std::vector<cl::Platform> platforms;
            cl::Platform::get(&platforms);
            platform = platforms[0];
            std::vector<cl::Device> devices;
            platform.getDevices(CL_DEVICE_TYPE_ALL, &devices);
            device = devices[0];
            context = {{device}};
            std::string source = 
                "void kernel metaballs(global const float *balldata, global uchar *rgb,\n"
                "       uint numBalls, uint width, uint height){\n"
                "   int id = get_global_id(0);\n"
                "   float x = id % width;\n"
                "   float y = id / width;\n"
                "   float accum = 0.1;\n"
                "   float r = 0, g = 0, b = 0;\n"
                "   for (uint i = 0; i < numBalls; i++) {\n"
                "       float mag = balldata[i * 6 + 2] / max(1.0f, \n"
                "           hypot(x - balldata[i * 6], y - balldata[i * 6 + 1]));\n"
                "       accum += mag;\n"
                "       r += balldata[i * 6 + 3] * mag;\n"
                "       g += balldata[i * 6 + 4] * mag;\n"
                "       b += balldata[i * 6 + 5] * mag;\n"
                "   }\n"
                "   rgb[id * 3] = (accum >= 1) ? (r / accum) : (x * 255 / width);\n"
                "   rgb[id * 3 + 1] = (accum >= 1) ? (g / accum) : (y * 255 / height);\n"
                "   rgb[id * 3 + 2] = accum >= 1.0 ? (b / accum) : 0;\n"
                "}\n";
            std::cout << source;
            sources.push_back({source.c_str(), source.length()});
            program = {context, sources};
            if (program.build({device}) != CL_SUCCESS) {
                std::cerr << "Error building program: " << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << "\n";
            }
            q = {context, device};
            kernel = {program, "metaballs"};
            input = {context, CL_MEM_READ_ONLY, sizeof(cl_float) * balls.size() * PARAMS_PER_BALL};
            output = {context, CL_MEM_READ_WRITE, size_t{3} * width * height};
        }
    
    virtual ~VideoState() {}
    
    virtual void callback(const std::vector<float>& samples,
        const std::map<std::pair<int, int>, Synth::PlayingNote>& notes)
    {
        size_t samplesPerFrame = (samplerate + framerate - 1) / framerate;
        for (auto it : samples) {
            buffer.push_back(it * sampleNorm);
        }
        bool curDrums = false;
        for (auto it : notes) {
            if (it.first.first == 9) {
                if (!playingDrums) {
                    float x = (float)rand() / RAND_MAX * width;
                    float y = (float)rand() / RAND_MAX * height;
                    float dir = (rand() & 1) ? 1 : -1;
                    for (auto &it : balls) {
                        it.attract(x, y, dir);
                    }
                }
                curDrums = true;
                break;
            }
        }
        playingDrums = curDrums;
        size_t startId = 0;
        for (; startId + samplesPerFrame <= buffer.size(); startId += samplesPerFrame) {
            cl_float ballBuf[balls.size() * 3];
            for (size_t i = 0; i < balls.size(); i++) {
                Ball& ball = balls[i];
                ball.step(width, height);
                ballBuf[i * PARAMS_PER_BALL] = ball.x;
                ballBuf[i * PARAMS_PER_BALL + 1] = ball.y;
                ballBuf[i * PARAMS_PER_BALL + 2] = ball.rad;
                ballBuf[i * PARAMS_PER_BALL + 3] = ball.r;
                ballBuf[i * PARAMS_PER_BALL + 4] = ball.g;
                ballBuf[i * PARAMS_PER_BALL + 5] = ball.b;
            }
            std::fill(rgb.begin(), rgb.end(), 254);
            // for (int i = 0; i < width * height; i++) {
                // rgb[i * 3] = i & 255;
                // rgb[i * 3 + 1] = (i / 5) & 255;
                // rgb[i * 3 + 2] = (i / 11) & 255;
            // }
            q.enqueueWriteBuffer(input, CL_TRUE, 0, sizeof(cl_float) * balls.size() * PARAMS_PER_BALL, ballBuf);
            kernel.setArg(0, input);
            kernel.setArg(1, output);
            kernel.setArg(2, (cl_uint)balls.size());
            kernel.setArg(3, (cl_uint)width);
            kernel.setArg(4, (cl_uint)height);
            q.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(width * height), cl::NullRange);
            q.finish();
            q.enqueueReadBuffer(output, CL_TRUE, 0, width * height * 3, rgb.data());
            fmavi.writeVideoFrame(out, rgb.data());
            std::cout << '#' << (numFrames) << " writing\n";
            subimg.encodeRGB(rgb.data());
            std::ofstream out(std::string("frames/frame") + std::to_string(numFrames) + ".jpg", std::ios_base::out | std::ios_base::binary);
            subimg.write(out);
            std::cout << '#' << (numFrames++) << " written\n";
            out.close();
        }
        fmavi.writeSamples(out, std::vector<int32_t>(buffer.begin(), buffer.begin() + startId));
        buffer.erase(buffer.begin(), buffer.begin() + startId);
    }
   
};

int main(int argc, char**argv)
{
    srand(time(NULL));
    std::ifstream stream("../../../Python/SpeechProjects/Formants/midi.mid", std::ios::binary);
    std::ifstream pstream("patch.txt");
    std::ofstream out("out.avi", std::ios::binary);
    if (!stream.is_open()) {
        std::cerr << "Couldn't open\n";
    }
    std::vector<Synth::Patch> patches = Synth::readPatches(pstream);
    std::cerr << patches[0];
    int params[] = {
        12, 1920, 1080, 16, 100
    };
    for (int i = 0; i < 5 && i + 1 < argc; i++) {
        params[i] = atoi(argv[i + 1]);
    }
    static VideoState vs (44100, params[0], params[1], params[2], params[3], out, params[4], 1.0f / 3, 1.0f / 20);
    // auto callback = [](const std::vector<float>& samples) mutable {vs.callback(samples);};
    Synth::play(stream, 44100, Synth::Visualizer::play, patches, static_cast<void*>(&vs));
    vs.finish();
    stream.close();
    pstream.close();
    out.close();
}