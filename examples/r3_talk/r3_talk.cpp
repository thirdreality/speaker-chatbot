// Talk with AI
//

#include "common.h"
#include "common-sdl.h"
#include "whisper.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <regex>
#include <string>
#include <thread>
#include <vector>
#include <regex>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <algorithm>

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "piper.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

// piper tts
using namespace std;

struct RunConfig {
    // Path to .onnx voice file
    filesystem::path modelPath;

    // Path to JSON voice config file
    filesystem::path modelConfigPath;

    // Numerical id of the default speaker (multi-speaker voices)
    optional<piper::SpeakerId> speakerId;

    // Amount of noise to add during audio generation
    optional<float> noiseScale;

    // Speed of speaking (1 = normal, < 1 is faster, > 1 is slower)
    optional<float> lengthScale;

    // Variation in phoneme lengths
    optional<float> noiseW;

    // Seconds of silence to add after each sentence
    optional<float> sentenceSilenceSeconds;

    // Path to espeak-ng data directory (default is next to piper executable)
    optional<filesystem::path> eSpeakDataPath;

    // Path to libtashkeel ort model
    // https://github.com/mush42/libtashkeel/
    optional<filesystem::path> tashkeelModelPath;

    optional<int> volume;
};

void printUsage(char *argv[]) {
    fprintf(stderr, "\n");
    fprintf(stderr, "usage: %s [options]\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "piper options:\n");
    fprintf(stderr, "  -h,       --help               show this message and exit\n");
    fprintf(stderr, "  -pm FILE  --piper-model        FILE  path to onnx model file\n");
    fprintf(stderr, "  -pc FILE  --config             FILE  path to model config file (default: model path + .json)\n");
    fprintf(stderr, "  -sid NUM  --speakerid   NUM    id of speaker (default: 0)\n");
    fprintf(stderr, "  --noise_scale           NUM    generator noise (default: 0.667)\n");
    fprintf(stderr, "  --length_scale          NUM    phoneme length (default: 1.0)\n");
    fprintf(stderr, "  --noise_w               NUM    phoneme width noise (default: 0.8)\n");
    fprintf(stderr, "  --silence_seconds       NUM    seconds of silence after each sentence (default: 0.2)\n");
    fprintf(stderr, "  --espeak_data           DIR    path to espeak-ng data directory\n");
    fprintf(stderr, "  --tashkeel_model        FILE   path to libtashkeel onnx model (arabic)\n");
    fprintf(stderr, "  --volume                NUM    volume value of the output audio (1-100)\n");
    fprintf(stderr, "\n");
}

void ensureArg(int argc, char *argv[], int argi) {
    if ((argi + 1) >= argc) {
        printUsage(argv);
        exit(0);
    }
}

// Parse command-line arguments
void parseArgs(int argc, char *argv[], RunConfig &runConfig) {
    optional<filesystem::path> modelConfigPath;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-pm" || arg == "--piper-model") {
            ensureArg(argc, argv, i);
            runConfig.modelPath = filesystem::path(argv[++i]);
        } else if (arg == "-pc" || arg == "--config") {
            ensureArg(argc, argv, i);
            modelConfigPath = filesystem::path(argv[++i]);
        } else if (arg == "-sid" || arg == "--speakerid") {
            ensureArg(argc, argv, i);
            runConfig.speakerId = (piper::SpeakerId)stol(argv[++i]);
        } else if (arg == "--noise_scale" || arg == "--noise-scale") {
            ensureArg(argc, argv, i);
            runConfig.noiseScale = stof(argv[++i]);
        } else if (arg == "--length_scale" || arg == "--length-scale") {
            ensureArg(argc, argv, i);
            runConfig.lengthScale = stof(argv[++i]);
        } else if (arg == "--noise_w" || arg == "--noise-w") {
            ensureArg(argc, argv, i);
            runConfig.noiseW = stof(argv[++i]);
        } else if (arg == "--sentence_silence" || arg == "--sentence-silence") {
            ensureArg(argc, argv, i);
            runConfig.sentenceSilenceSeconds = stof(argv[++i]);
        } else if (arg == "--espeak_data" || arg == "--espeak-data") {
            ensureArg(argc, argv, i);
            runConfig.eSpeakDataPath = filesystem::path(argv[++i]);
        } else if (arg == "--tashkeel_model" || arg == "--tashkeel-model") {
            ensureArg(argc, argv, i);
            runConfig.tashkeelModelPath = filesystem::path(argv[++i]);
        } else if (arg == "--volume") {
            ensureArg(argc, argv, i);
            int vol = std::stoi(argv[++i]);
            runConfig.volume = vol > 100 ? 100:vol;
            fprintf(stderr, "Set the volume value to %d\n", runConfig.volume.value());
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv);
            exit(0);
        }
    }

    // Verify model file exists
    ifstream modelFile(runConfig.modelPath.c_str(), ios::binary);
    if (!modelFile.good()) {
        throw runtime_error("Model file doesn't exist");
    }

    if (!modelConfigPath) {
        runConfig.modelConfigPath =
            filesystem::path(runConfig.modelPath.string() + ".json");
    } else {
        runConfig.modelConfigPath = modelConfigPath.value();
    }

    // Verify model config exists
    ifstream modelConfigFile(runConfig.modelConfigPath.c_str());
    if (!modelConfigFile.good()) {
        throw runtime_error("Model config doesn't exist");
    }
}


// Function to make HTTP POST request to OpenAI API
size_t WriteCallback(char* contents, size_t size, size_t nmemb, std::string* output){
    size_t totalSize = size * nmemb;
    output->append((char*) contents, totalSize);
    return totalSize;
}

std::string makeOpenAIRequest(const std::string& prompt) {
    // Set your OpenAI API key
    std::string content;
    std::string apiKey;
    if(const char* env_p = std::getenv("OPENAI_API_KEY")) {
        apiKey = std::string{env_p};
    } else {
        throw runtime_error("Please set your OPENAI_API_KEY");
    }

    if (apiKey.empty()) {
        throw runtime_error("Please set your OPENAI_API_KEY");
    }

    // Set the endpoint for Chat API
    std::string endpoint = "https://api.openai.com/v1/chat/completions";
    // Set the input parameters
    std::string apiKeyArg = "Authorization: Bearer " + apiKey;
    std::string data = R"({"model": "gpt-3.5-turbo", "messages": [{"role": "system", "content": "You are a helpful assistant."}, {"role": "user", "content": ")"+prompt+R"("}]})";

    CURL* curl = curl_easy_init();
    if (curl) {
        CURLcode res;
        // Set the required headers
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, apiKeyArg.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        // Set the API endpoint to send the POST request
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        
        // Set POST data
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
        // Set the write callback function to handle the response
        std::string response;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        // Perform the request
        res = curl_easy_perform(curl);
        
        // Check for errors
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            return content;
        }
        else {
            // Print the response
            string::size_type index = response.find("choices");
            if (index != string::npos) {
                //fprintf(stderr, "find response choices\n");
            } else {
                fprintf(stderr, "not find response choices\n");
                fprintf(stdout, "response url: %s\n", response.c_str());
                return content;
            }
        }

        nlohmann::json jsonData = nlohmann::json::parse(response);
        // Get the 'content' of 'assistant' in 'message'
        content = jsonData["choices"][0]["message"]["content"];

        // Clean up
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    return content;
}

// command-line parameters
struct whisper_params {
    int32_t n_threads  = std::min(4, (int32_t) std::thread::hardware_concurrency());
    int32_t prompt_ms  = 5000;
    int32_t voice_ms   = 8000;
    int32_t capture_id = -1;
    int32_t max_tokens = 32;
    int32_t audio_ctx  = 0;

    float vad_thold    = 0.6f;
    float freq_thold   = 100.0f;

    bool speed_up      = false;
    bool translate     = false;
    bool print_special = false;
    bool print_energy  = false;
    bool no_timestamps = true;

    std::string language  = "en";
    std::string model_wsp = "models/ggml-base.en.bin";
    std::string light     = "./examples/r3_talk/light";
    std::string fname_out;
    std::string prompt_word = "hi whisper";
};

void whisper_print_usage(int argc, char ** argv, const whisper_params & params);

bool whisper_params_parse(int argc, char ** argv, whisper_params & params) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            whisper_print_usage(argc, argv, params);
            //exit(0);
        }
        else if (arg == "-t"   || arg == "--threads")       { params.n_threads     = std::stoi(argv[++i]); }
        else if (arg == "-pms" || arg == "--prompt-ms")     { params.prompt_ms     = std::stoi(argv[++i]); }
        else if (arg == "-vms" || arg == "--voice-ms")      { params.voice_ms      = std::stoi(argv[++i]); }
        else if (arg == "-c"   || arg == "--capture")       { params.capture_id    = std::stoi(argv[++i]); }
        else if (arg == "-mt"  || arg == "--max-tokens")    { params.max_tokens    = std::stoi(argv[++i]); }
        else if (arg == "-ac"  || arg == "--audio-ctx")     { params.audio_ctx     = std::stoi(argv[++i]); }
        else if (arg == "-vth" || arg == "--vad-thold")     { params.vad_thold     = std::stof(argv[++i]); }
        else if (arg == "-fth" || arg == "--freq-thold")    { params.freq_thold    = std::stof(argv[++i]); }
        else if (arg == "-su"  || arg == "--speed-up")      { params.speed_up      = true; }
        else if (arg == "-tr"  || arg == "--translate")     { params.translate     = true; }
        else if (arg == "-ps"  || arg == "--print-special") { params.print_special = true; }
        else if (arg == "-pe"  || arg == "--print-energy")  { params.print_energy  = true; }
        else if (arg == "-l"   || arg == "--language")      { params.language      = argv[++i]; }
        else if (arg == "-m"   || arg == "--model-whisper") { params.model_wsp     = argv[++i]; }
        else if (arg == "-ld"  || arg == "--light")         { params.light         = argv[++i]; }
        else if (arg == "-f"   || arg == "--file")          { params.fname_out     = argv[++i]; }
        else if (arg == "-pw"  || arg == "--prompt")        { params.prompt_word   = argv[++i]; }
    }

    return true;
}

void whisper_print_usage(int /*argc*/, char ** argv, const whisper_params & params) {
    fprintf(stderr, "\n");
    fprintf(stderr, "usage: %s [options]\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "whisper options:\n");
    fprintf(stderr, "  -h,       --help          [default] show this help message and exit\n");
    fprintf(stderr, "  -t N,     --threads N     [%-7d] number of threads to use during computation\n", params.n_threads);
    fprintf(stderr, "  -pms N,   --prompt-ms N   [%-7d] prompt duration in milliseconds\n",             params.prompt_ms);
    fprintf(stderr, "  -vms N,   --voice-ms N    [%-7d] voice duration in milliseconds\n",              params.voice_ms);
    fprintf(stderr, "  -c ID,    --capture ID    [%-7d] capture device ID\n",                           params.capture_id);
    fprintf(stderr, "  -mt N,    --max-tokens N  [%-7d] maximum number of tokens per audio chunk\n",    params.max_tokens);
    fprintf(stderr, "  -ac N,    --audio-ctx N   [%-7d] audio context size (0 - all)\n",                params.audio_ctx);
    fprintf(stderr, "  -vth N,   --vad-thold N   [%-7.2f] voice activity detection threshold\n",        params.vad_thold);
    fprintf(stderr, "  -fth N,   --freq-thold N  [%-7.2f] high-pass frequency cutoff\n",                params.freq_thold);
    fprintf(stderr, "  -su,      --speed-up      [%-7s] speed up audio by x2 (reduced accuracy)\n",     params.speed_up ? "true" : "false");
    fprintf(stderr, "  -tr,      --translate     [%-7s] translate from source language to english\n",   params.translate ? "true" : "false");
    fprintf(stderr, "  -ps,      --print-special [%-7s] print special tokens\n",                        params.print_special ? "true" : "false");
    fprintf(stderr, "  -pe,      --print-energy  [%-7s] print sound energy (for debugging)\n",          params.print_energy ? "true" : "false");
    fprintf(stderr, "  -l LANG,  --language LANG [%-7s] spoken language\n",                             params.language.c_str());
    fprintf(stderr, "  -m FILE,  --model-whisper [%-7s] whisper model file\n",                          params.model_wsp.c_str());
    fprintf(stderr, "  -ld FILE, --light led     [%-7s] command for light\n",                           params.light.c_str());
    fprintf(stderr, "  -f FNAME, --file FNAME    [%-7s] text output file name\n",                       params.fname_out.c_str());
    fprintf(stderr, "  -pw LANG, --prompt LANG   [%-7s] prompt word\n",                                 params.prompt_word.c_str());
    fprintf(stderr, "\n");
}

std::string transcribe(whisper_context * ctx, const whisper_params & params, const std::vector<float> & pcmf32, float & prob, int64_t & t_ms) {
    const auto t_start = std::chrono::high_resolution_clock::now();

    prob = 0.0f;
    t_ms = 0;

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    wparams.print_progress   = false;
    wparams.print_special    = params.print_special;
    wparams.print_realtime   = false;
    wparams.print_timestamps = !params.no_timestamps;
    wparams.translate        = params.translate;
    wparams.no_context       = true;
    wparams.single_segment   = true;
    wparams.max_tokens       = params.max_tokens;
    wparams.language         = params.language.c_str();
    wparams.n_threads        = params.n_threads;

    wparams.audio_ctx        = params.audio_ctx;
    wparams.speed_up         = params.speed_up;

    if (whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size()) != 0) {
        return "";
    }

    int prob_n = 0;
    std::string result;

    const int n_segments = whisper_full_n_segments(ctx);
    for (int i = 0; i < n_segments; ++i) {
        const char * text = whisper_full_get_segment_text(ctx, i);

        result += text;

        const int n_tokens = whisper_full_n_tokens(ctx, i);
        for (int j = 0; j < n_tokens; ++j) {
            const auto token = whisper_full_get_token_data(ctx, i, j);

            prob += token.p;
            ++prob_n;
        }
    }

    if (prob_n > 0) {
        prob /= prob_n;
    }

    const auto t_end = std::chrono::high_resolution_clock::now();
    t_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

    return result;
}


audio_async audio(30*1000);

// led control
std::string s_light;
enum RgbType { CLOSE, RED, GREEN, BLUE, RED_GREEN, RED_BLUE, GREEN_BLUE, RED_GREEN_BLUE};
int light_set(enum RgbType type) {
    int red = 0, green = 0, blue = 0;
    switch (type) {
        case RED:
            red = 1;
            break;
        case GREEN:
            green = 1;
            break;
        case BLUE:
            blue = 1;
            break;
        case RED_GREEN:
            red = 1;
            green = 1;
            break;
        case RED_BLUE:
            red = 1;
            blue = 1;
            break;
        case GREEN_BLUE:
            green = 1;
            blue = 1;
            break;
        case RED_GREEN_BLUE:
            red = 1;
            green = 1;
            blue = 1;
            break;
        default:
            break;
    }

    return system((s_light + " \"" + std::to_string(red) + "\" \"" + std::to_string(green) + "\" \"" + std::to_string(blue) + "\"").c_str());
}

// ----------------------------------------------------------------------------
void reduceVolume(std::vector<int16_t>& pcmData, double factor) {
    for (int16_t& sample : pcmData) {
        sample = static_cast<int16_t>(sample * factor);
    }
}

void rawOutputProc(vector<int16_t> &sharedAudioBuffer, mutex &mutAudio, condition_variable &cvAudio,
                     bool &audioReady, bool &audioFinished, int volume) {
    vector<int16_t> internalAudioBuffer;
    bool is_running  = true;
    while (is_running) {
        // handle Ctrl + C
        is_running = sdl_poll_events();

        if (!is_running) {
            audio.pause();
            light_set(CLOSE);
            throw runtime_error("sdl_interrupt");
        }

        unique_lock lockAudio{mutAudio};
        cvAudio.wait(lockAudio, [&audioReady] { return audioReady; });
        reduceVolume(sharedAudioBuffer, (volume*1.5)/200.0);

        if (sharedAudioBuffer.empty() && audioFinished) {
            break;
        }

        copy(sharedAudioBuffer.begin(), sharedAudioBuffer.end(), back_inserter(internalAudioBuffer));

        sharedAudioBuffer.clear();
        if (!audioFinished) {
            audioReady = false;
        }

        fprintf(stderr, "%s: play_write  %ld buff\n", __func__, sizeof(int16_t) * internalAudioBuffer.size());
        audio.play_write((const char *)internalAudioBuffer.data(), sizeof(int16_t) * internalAudioBuffer.size());
        internalAudioBuffer.clear();
    }

} // rawOutputProc

int piper_tts(piper::PiperConfig &piperConfig, piper::Voice &piperVoice, std::string text_to_speak, int volume) {
    piper::SynthesisResult result;
    mutex mutAudio;
    condition_variable cvAudio;
    bool audioReady = false;
    bool audioFinished = false;
    vector<int16_t> audioBuffer;
    vector<int16_t> sharedAudioBuffer;

    thread rawOutputThread(rawOutputProc, ref(sharedAudioBuffer), ref(mutAudio), ref(cvAudio),
                             ref(audioReady),ref(audioFinished), ref(volume));
    auto audioCallback = [&audioBuffer, &sharedAudioBuffer, &mutAudio, &cvAudio, &audioReady]() {
        unique_lock lockAudio(mutAudio);
        copy(audioBuffer.begin(), audioBuffer.end(), back_inserter(sharedAudioBuffer));
        audioReady = true;
        fprintf(stderr, "%s: audioReady\n", __func__);
        cvAudio.notify_one();
    };

    piper::textToAudio(piperConfig, piperVoice, text_to_speak, audioBuffer, result, audioCallback);
    // Signal thread that there is no more audio
    {
        unique_lock lockAudio(mutAudio);
        audioReady = true;
        audioFinished = true;
        cvAudio.notify_one();
    }
    rawOutputThread.join();
    return audio.play_wait();
}


void piper_init(RunConfig &runConfig, piper::PiperConfig &piperConfig, piper::Voice &piperVoice) {
    fprintf(stderr, "%s: piper loadVoice\n", __func__);
    loadVoice(piperConfig, runConfig.modelPath.string(),
                runConfig.modelConfigPath.string(), piperVoice, runConfig.speakerId);

#ifdef _MSC_VER
auto exePath = []() {
    wchar_t moduleFileName[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, moduleFileName, std::size(moduleFileName));
    return filesystem::path(moduleFileName);
}();
#elifdef __APPLE__
auto exePath = []() {
    char moduleFileName[PATH_MAX] = {0};
    uint32_t moduleFileNameSize = std::size(moduleFileName);
    _NSGetExecutablePath(moduleFileName, &moduleFileNameSize);
    return filesystem::path(moduleFileName);
}();
#else
auto exePath = filesystem::canonical("/proc/self/exe");
#endif

    if (piperVoice.phonemizeConfig.phonemeType == piper::eSpeakPhonemes) {

        if (runConfig.eSpeakDataPath) {
            // User provided path
            piperConfig.eSpeakDataPath = runConfig.eSpeakDataPath.value().string();
        } else {
            // Assume next to piper executable
            piperConfig.eSpeakDataPath =
                std::filesystem::absolute(
                    exePath.parent_path().append("espeak-ng-data"))
                    .string();

        }
    } else {
        // Not using eSpeak
        piperConfig.useESpeak = false;
    }

    // Enable libtashkeel for Arabic
    if (piperVoice.phonemizeConfig.eSpeak.voice == "ar") {
        piperConfig.useTashkeel = true;
        if (runConfig.tashkeelModelPath) {
            // User provided path
            piperConfig.tashkeelModelPath = runConfig.tashkeelModelPath.value().string();
        } else {
            // Assume next to piper executable
            piperConfig.tashkeelModelPath =
                std::filesystem::absolute( exePath.parent_path().append("libtashkeel_model.ort")).string();
        }
    }
    fprintf(stderr, "%s: piper initialize\n", __func__);
    piper::initialize(piperConfig);
    // Scales
    if (runConfig.noiseScale) {
        piperVoice.synthesisConfig.noiseScale = runConfig.noiseScale.value();
    }

    if (runConfig.lengthScale) {
        piperVoice.synthesisConfig.lengthScale = runConfig.lengthScale.value();
    }

    if (runConfig.noiseW) {
        piperVoice.synthesisConfig.noiseW = runConfig.noiseW.value();
    }

    if (runConfig.sentenceSilenceSeconds) {
        piperVoice.synthesisConfig.sentenceSilenceSeconds = runConfig.sentenceSilenceSeconds.value();
    }

}
// ----------------------------------------------------------------------------

int main(int argc, char ** argv) {
    whisper_params params;
    if (whisper_params_parse(argc, argv, params) == false) {
        return 1;
    }

    if (whisper_lang_id(params.language.c_str()) == -1) {
        fprintf(stderr, "error: unknown language '%s'\n", params.language.c_str());
        whisper_print_usage(argc, argv, params);
        exit(0);
    }

    // piper init
    RunConfig runConfig;
    parseArgs(argc, argv, runConfig);

    fprintf(stderr, "%s: piper init start\n", __func__);
    piper::PiperConfig piperConfig;
    piper::Voice piperVoice;
    piper_init(runConfig, piperConfig, piperVoice);
    fprintf(stderr, "%s: piper init finished\n\n", __func__);

    // whisper init

    struct whisper_context * ctx_wsp = whisper_init_from_file(params.model_wsp.c_str());

    // print some info about the processing
    {
        fprintf(stderr, "\n");
        if (!whisper_is_multilingual(ctx_wsp)) {
            if (params.language != "en" || params.translate) {
                params.language = "en";
                params.translate = false;
                fprintf(stderr, "%s: WARNING: model is not multilingual, ignoring language and translation options\n", __func__);
            }
        }
        fprintf(stderr, "%s: processing, %d threads, lang = %s, task = %s, timestamps = %d ...\n",
                __func__,
                params.n_threads,
                params.language.c_str(),
                params.translate ? "translate" : "transcribe",
                params.no_timestamps ? 0 : 1);

        fprintf(stderr, "\n");
    }

    // init audio
    fprintf(stderr, "%s: init audio\n", __func__);
    //audio_async audio(30*1000);
    if (!audio.init(params.capture_id, WHISPER_SAMPLE_RATE)) {
        fprintf(stderr, "%s: audio.init() failed!\n", __func__);
        return 1;
    }

    if (!audio.play_init(params.capture_id)) {
        fprintf(stderr, "%s: audio.play_init() failed!\n", __func__);
        return 1;
    }
    s_light = params.light;

    audio.resume();

    // wait for 1 second to avoid any buffered noise
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    audio.clear();

    bool is_running  = true;
    bool have_prompt = false;
    bool ask_prompt  = true;
    bool is_listening = false;
    float prob0 = 0.0f;

    std::vector<float> pcmf32_cur;
    std::vector<float> pcmf32_prompt;

    const std::string k_prompt = params.prompt_word;

    fprintf(stderr, "\n%s: main loop\n", __func__);
    // main loop
    while (is_running) {
        // handle Ctrl + C
        is_running = sdl_poll_events();

        if (!is_running) {
            break;
        }

        // delay
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (ask_prompt) {
            fprintf(stdout, "\n%s: Say the following phrase: '%s%s%s'\n\n", __func__, "\033[1m", k_prompt.c_str(), "\033[0m");

            ask_prompt = false;
        } else if (is_listening) {
            fprintf(stdout, "\n%s: Listening ... \n\n", __func__);
            is_listening = false;
            light_set(BLUE);
        }

        {
            audio.get(2000, pcmf32_cur);

            if (::vad_simple(pcmf32_cur, WHISPER_SAMPLE_RATE, 1000, params.vad_thold, params.freq_thold, params.print_energy)) {
                fprintf(stdout, "%s: Speech detected! Processing ...\n", __func__);
                const auto t_start = std::chrono::high_resolution_clock::now();

                int64_t t_ms = 0;
                std::string text_to_speak;

                if (!have_prompt) {
                    // wait for activation phrase
                    audio.get(params.prompt_ms, pcmf32_cur);

                    auto txt = ::trim(::transcribe(ctx_wsp, params, pcmf32_cur, prob0, t_ms));

                    txt = std::regex_replace(txt, std::regex("[^a-zA-Z\\s]"), "");
                    transform(txt.begin(), txt.end(), txt.begin(),::tolower);
                    fprintf(stdout, "%s: Heard '%s%s%s', (t = %d ms)\n", __func__, "\033[1m", txt.c_str(), "\033[0m", (int) t_ms);

                    if (txt.length() > params.prompt_word.length()+3) {
                        txt = txt.substr(0, params.prompt_word.length()+3);
                    }

                    const float sim = similarity(txt, k_prompt);

                    if (txt.length() < 0.6*k_prompt.length() || txt.length() > 1.4*k_prompt.length() || sim < 0.6f) {
                        fprintf(stdout, "%s: WARNING: prompt not recognized, try again\n", __func__);
                        ask_prompt = true;
                    } else {
                        fprintf(stdout, "\n");
                        fprintf(stdout, "%s: The prompt has been recognized!\n", __func__);
                        fprintf(stdout, "%s: Waiting for voice commands ...\n", __func__);
                        fprintf(stdout, "\n");

                        // save the audio for the prompt
                        pcmf32_prompt = pcmf32_cur;
                        have_prompt = true;
                        is_listening = true;
                    }
                    audio.clear();
                    continue;
                } else {
                    light_set(GREEN_BLUE);
                    // we have heard the activation phrase
                    audio.get(params.voice_ms, pcmf32_cur);

                    std::string text_heard;
                    text_heard = ::trim(::transcribe(ctx_wsp, params, pcmf32_cur, prob0, t_ms));
                    fprintf(stdout, "%s: Text '%s%s%s', (t = %d ms)\n", __func__, "\033[1m", text_heard.c_str(), "\033[0m", (int) t_ms);

                    // remove text between brackets using regex
                    {
                        std::regex re("\\[.*?\\]");
                        text_heard = std::regex_replace(text_heard, re, "");
                    }

                    // remove text between brackets using regex
                    {
                        std::regex re("\\(.*?\\)");
                        text_heard = std::regex_replace(text_heard, re, "");
                    }

                    // remove all characters, except for letters, numbers, punctuation and ':', '\'', '-', ' '
                    text_heard = std::regex_replace(text_heard, std::regex("[^a-zA-Z0-9\\.,\\?!\\s\\:\\'\\-]"), "");

                    // take first line
                    text_heard = text_heard.substr(0, text_heard.find_first_of('\n'));

                    // remove leading and trailing whitespace
                    text_heard = std::regex_replace(text_heard, std::regex("^\\s+"), "");
                    text_heard = std::regex_replace(text_heard, std::regex("\\s+$"), "");

                    if (text_heard.empty()) {
                        fprintf(stdout, "%s: Heard nothing, skipping ...\n", __func__);
                        audio.clear();
                        is_listening = true;
                        continue;
                    }

                    fprintf(stdout, "%s: Heard '%s%s%s', (t = %d ms)\n", __func__, "\033[1m", text_heard.c_str(), "\033[0m", (int) t_ms);

                    text_to_speak = makeOpenAIRequest(text_heard.c_str());
                    fprintf(stdout, "%s: Response '%s%s%s'\n", __func__, "\033[1m", text_to_speak.c_str(), "\033[0m");

                    if (text_to_speak.empty()) {
                        fprintf(stdout, "%s: No response, skipping ...\n", __func__);
                        audio.clear();
                        is_listening = true;
                        continue;
                    }

                    if (sdl_poll_events() == false) {
                        break;
                    }
                }
                int volume = 50;
                if (runConfig.volume) {
                    volume = runConfig.volume.value();
                }
                light_set(RED_GREEN_BLUE);
                const auto t_end = std::chrono::high_resolution_clock::now();
                int64_t t_transform_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
                fprintf(stdout, "%s: before piper start (t_transform_ms = %d ms)\n", __func__, (int) t_transform_ms);
                if(piper_tts(piperConfig, piperVoice, text_to_speak, volume) == -1) {
                    break;
                }

                light_set(CLOSE);
                is_listening = true;
                audio.clear();
            }
        }
    }

    audio.pause();
    light_set(CLOSE);

    whisper_print_timings(ctx_wsp);
    whisper_free(ctx_wsp);

    return 0;
}

