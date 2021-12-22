#include <iostream>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <ctime>

#include "aviutil.hpp"
#include "synthutil.hpp"

#include <CL/cl.hpp>

struct VideoState : Synth::Visualizer {
    
    int numFrames;
    std::vector<std::array<float, 5>> balls;
    float thresh;
    cl::Platform platform;
    cl::Device device;
    cl::Context context;
    cl::Program::Sources sources;
    cl::Program program;
    cl::CommandQueue q;
    cl::Kernel kernel;
    cl::Buffer input;
    cl::Buffer output;
    
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
        numFrames {0} {
            for (size_t n = 0; n < 5; n++) {
                balls.push_back({(float)rand() / RAND_MAX * width, (float)rand() / RAND_MAX * height,
                    (float)rand() / RAND_MAX * width * maxVel / fps,
                    (float)rand() / RAND_MAX * height * maxVel / fps,
                    (float)rand() / RAND_MAX * width * maxRad});
            }
            thresh = 1;
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
                "   float y = id / height;\n"
                "   float accum = 0;\n"
                "   for (uint i = 0; i < numBalls; i++) {\n"
                "       accum += balldata[i * 3 + 2] / max(1.0f, \n"
                "           hypot(x - balldata[i * 3], y - balldata[i * 3 + 1]));\n"
                "   }\n"
                "   rgb[id * 3] = x * 255 / width;\n"
                "   rgb[id * 3 + 1] = y * 255 / height;\n"
                "   rgb[id * 3 + 2] = accum >= 1.0 ? 255 : 0;\n"
                "}\n";
            std::cout << source;
            sources.push_back({source.c_str(), source.length()});
            program = {context, sources};
            if (program.build({device}) != CL_SUCCESS) {
                std::cerr << "Error building program: " << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << "\n";
            }
            q = {context, device};
            kernel = {program, "metaballs"};
            input = {context, CL_MEM_READ_ONLY, sizeof(cl_float) * balls.size() * 3};
            output = {context, CL_MEM_READ_WRITE, size_t{3} * width * height};
        }
    
    virtual ~VideoState() {}
    
    virtual void callback(const std::vector<float>& samples)
    {
        size_t samplesPerFrame = (samplerate) / framerate + 1;
        for (auto it : samples) {
            buffer.push_back(it * sampleNorm);
        }
        size_t startId = 0;
        for (; startId + samplesPerFrame <= buffer.size(); startId += samplesPerFrame) {
            cl_float ballBuf[balls.size() * 3];
            for (size_t i = 0; i < balls.size(); i++) {
                std::array<float, 5>& ball = balls[i];
                ball[0] += ball[2];
                if (ball[0] < 0 || ball[0] >= width) {
                    ball[2] = -ball[2];
                }
                ball[1] += ball[3];
                if (ball[1] < 0 || ball[1] >= height) {
                    ball[3] = -ball[3];
                }
                ballBuf[i * 3] = ball[0];
                ballBuf[i * 3 + 1] = ball[1];
                ballBuf[i * 3 + 2] = ball[4];
            }
            std::fill(rgb.begin(), rgb.end(), 254);
            q.enqueueWriteBuffer(input, CL_TRUE, 0, sizeof(cl_float) * balls.size() * 3, ballBuf);
            kernel.setArg(0, input);
            kernel.setArg(1, output);
            kernel.setArg(2, (cl_uint)balls.size());
            kernel.setArg(3, (cl_uint)width);
            kernel.setArg(4, (cl_uint)height);
            q.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(width * height), cl::NullRange);
            q.finish();
            q.enqueueReadBuffer(output, CL_TRUE, 0, width * height * 3, rgb.data());
            fmavi.writeVideoFrame(out, rgb.data());
            std::cout << '#' << (numFrames++) << " rendered\n";
        }
        fmavi.writeSamples(out, std::vector<int32_t>(buffer.begin(), buffer.begin() + startId));
        buffer.erase(buffer.begin(), buffer.begin() + startId);
    }
   
};

int main()
{
    srand(time(NULL));
    std::ifstream stream("../../SpeechProjects/Formants/midi.mid", std::ios::binary);
    std::ifstream pstream("patch.txt");
    std::ofstream out("out.avi", std::ios::binary);
    if (!stream.is_open()) {
        std::cerr << "Couldn't open\n";
    }
    std::vector<Synth::Patch> patches = Synth::readPatches(pstream);
    std::cerr << patches[0];
    static VideoState vs (44100, 12, 500, 500, 16, out, 99);
    // auto callback = [](const std::vector<float>& samples) mutable {vs.callback(samples);};
    Synth::play(stream, 44100, Synth::Visualizer::play, patches, static_cast<void*>(&vs));
    vs.finish();
    stream.close();
    pstream.close();
    out.close();
}