# r3_talk

Talk with chatGPT in your Linux speaker. Light gpio control requires running as root user.


## Config

The `r3_talk` tool depends on some libraries. You can config it:

```bash
# Open the project root directory, run r3_config.sh
./r3_config.sh

# if "Permission denied", run
chmod +x r3_config.sh
```


## OPENAI_API_KEY

To run this, you need to set your OPENAI_API_KEY environment variable:

```
# Open /etc/profile
vim /etc/profile

# Add OPENAI_API_KEY
export OPENAI_API_KEY="my-openai-key"

# Make variables effective
source /etc/profile
```


## Piper(TTS)

Before building, You must copy the dependency lib of Piper to the desired path:

```bash
# Open the project root directory, run
cp -r piper/lib/lib*  /usr/lib/aarch64-linux-gnu
cp -r piper/lib/espeak-ng-data/  /usr/share
```

## Building

You can build it like this:

```bash
# Build the "r3_talk" executable
make r3_talk

# Run it
./r3_talk -m ./models/ggml-tiny.en.bin -ac 512 -t 4 -c 0 -pm ./piper/models/en-us-amy-low.onnx 
```
