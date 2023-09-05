# 3R-chatGPT

We have developed a new example called `r3_talk` based on the [whisper.cpp](https://github.com/ggerganov/whisper.cpp) project. This integration combines the power of the OpenAI API with the open-source TTS (Text-to-Speech) project called [piper](https://github.com/rhasspy/piper). 

## Preparation

The purpose of `r3_talk` is to provide seamless speech capabilities on our custom-designed [Linux speaker](https://www.3reality.com/online-store/Smart-Speaker-DEV-Kit-p572273110). If you are interested in experiencing it, you need to prepare:

* A [`Linux speaker`](https://www.3reality.com/online-store/Smart-Speaker-DEV-Kit-p572273110) with Armbian firmware
* A self-developed [`debug board`](https://www.3reality.com/online-store/Smart-Speaker-DEV-Kit-p572273110) available for assistance

The `factory speaker` is equipped with pre-installed Armbian firmware, and we have done some initialization configurations to enhance user convenience. Upon receiving the speaker, you can directly access the `root` user by using the initial password of `1234qwer` in order to utilize the speaker effortlessly. 

If the folder `3R-chatGPT` is found in the root directory, this allows you to engage in a conversation with chatGPT instantly using [`./r3_talk`](#build-and-run) command. Before running, please ensure to configure your [`OPENAI_API_KEY`](#openai_api_key) and establish a [network connection](images/README.md#2-wifi-configuration).

For more in-depth instructions, please consult the [images/README.md](images/README.md) file, which also explains how to re-flash the speaker if necessary. This ensures a smooth experience with the `r3_talk` project and maximizes the functionality of our `Linux speaker`.

## Config

The example `r3_talk` example depends on some libraries. You can config it:

```bash
# Download code
git clone https://github.com/thirdreality/3R-chatGPT.git

# Open the project root directory, run r3_config.sh
cd 3R-chatGPT
./r3_config.sh

# if "Permission denied", run
chmod +x r3_config.sh
```

The `r3_config.sh` script includes some dependency installations and configuration of the gpio for the speaker. You can refer to the content and install and configure according to your own needs. The GPIO configuration includes enabling the sound and RGB. We write it into `/etc/profile.d/r3_gpio.sh` to ensure that the script is automatically executed every time you log in.


## OPENAI_API_KEY

To run chatGPT, you need to set your `OPENAI_API_KEY` environment variable:

```bash
# Open /etc/profile
vim /etc/profile

# Add OPENAI_API_KEY
export OPENAI_API_KEY="your-openai-key"

# Make variables effective
source /etc/profile
```

## Piper(TTS)

Before building, You must copy the dependency lib of `Piper` to the desired path:

```bash
# Open the project root directory
cd 3R-chatGPT

# Run
cp -r piper/lib/lib*  /usr/lib/aarch64-linux-gnu
cp -r piper/lib/espeak-ng-data/  /usr/share
```

## Models

The `whisper` model needs to be downloaded first, such as `tiny.en`. You can also choose other [whisper models](models), which will provide better ASR recognition results, but the conversion time will be longer. [Quantized models](https://github.com/ggerganov/whisper.cpp#quantization) require less memory and disk space and depending on the hardware can be processed more efficiently, if necessary. 

```bash
# Download whisper model
bash ./models/download-ggml-model.sh tiny.en
```

The `piper` model `en-us-amy-low.onnx` has already been provided in the code, but you can also choose to use other [piper models](https://github.com/rhasspy/piper/releases/tag/v0.0.2). Remember to specify the path of the downloaded model during runtime.


## Build and Run

You can build it like this:

```bash
# Build the "r3_talk" executable, or use the following method
make r3_talk

# Encoder processing can be accelerated on the CPU via OpenBLAS.
WHISPER_OPENBLAS=1 make r3_talk
```

Now, you can run the following command and then have a conversation with the speaker. Remember to specify the path of the downloaded model.
```bash
# Run
./r3_talk -m ./models/ggml-tiny.en.bin -ac 512 -t 4 -c 0 -pm ./piper/models/en-us-amy-low.onnx 
```

## Help

```java
$ ./r3_talk -h

usage: ./r3_talk [options]

whisper options:
  -h,       --help          [default] show this help message and exit
  -t N,     --threads N     [4      ] number of threads to use during computation
  -pms N,   --prompt-ms N   [5000   ] prompt duration in milliseconds
  -vms N,   --voice-ms N    [8000   ] voice duration in milliseconds
  -c ID,    --capture ID    [-1     ] capture device ID
  -mt N,    --max-tokens N  [32     ] maximum number of tokens per audio chunk
  -ac N,    --audio-ctx N   [0      ] audio context size (0 - all)
  -vth N,   --vad-thold N   [0.60   ] voice activity detection threshold
  -fth N,   --freq-thold N  [100.00 ] high-pass frequency cutoff
  -su,      --speed-up      [false  ] speed up audio by x2 (reduced accuracy)
  -tr,      --translate     [false  ] translate from source language to english
  -ps,      --print-special [false  ] print special tokens
  -pe,      --print-energy  [false  ] print sound energy (for debugging)
  -l LANG,  --language LANG [en     ] spoken language
  -m FILE,  --model-whisper [models/ggml-base.en.bin] whisper model file
  -ld FILE, --light led     [./examples/r3_talk/light] command for light
  -f FNAME, --file FNAME    [       ] text output file name
  -pw LANG, --prompt LANG   [hi thirdreality] prompt word


usage: ./r3_talk [options]

piper options:
  -h,       --help               show this message and exit
  -pm FILE  --piper-model        FILE  path to onnx model file
  -pc FILE  --config             FILE  path to model config file (default: model path + .json)
  -sid NUM  --speakerid   NUM    id of speaker (default: 0)
  --noise_scale           NUM    generator noise (default: 0.667)
  --length_scale          NUM    phoneme length (default: 1.0)
  --noise_w               NUM    phoneme width noise (default: 0.8)
  --silence_seconds       NUM    seconds of silence after each sentence (default: 0.2)
  --espeak_data           DIR    path to espeak-ng data directory
  --tashkeel_model        FILE   path to libtashkeel onnx model (arabic)
  --volume                NUM    volume value of the output audio (1-100)

```

## Dependence

This project is based on the `whisper.cpp` project and integrated with the `piper` project. For more details, please refer to:

### [whisper.cpp](https://github.com/ggerganov/whisper.cpp)
High-performance inference of [OpenAI's Whisper](https://github.com/openai/whisper) automatic speech recognition (ASR) model.

### [piper](https://github.com/rhasspy/piper)
A fast, local neural text to speech system that sounds great and is optimized for the Raspberry Pi 4.
Piper is used in a variety of projects.
