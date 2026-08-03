# Polaris Studio Models

Place the local GGUF model files used by Polaris Studio here. You need one of
each type:

| Type | Example | Role |
|------|---------|------|
| LM | acestep-5Hz-lm-4B-Q8_0.gguf | Generates lyrics and audio codes |
| Text encoder | Qwen3-Embedding-0.6B-Q8_0.gguf | Encodes captions for the DiT |
| DiT | acestep-v15-turbo-Q8_0.gguf | Renders audio codes into sound |
| VAE | vae-BF16.gguf | Decodes latents to 48kHz stereo audio |

Download from: https://huggingface.co/Serveurperso/ACE-Step-1.5-GGUF/tree/main

Or use the download script:

```bash
./models.sh
```

## Local speech-language listener

Reference analysis uses a local Whisper language listener. It runs on CPU by
default so it does not compete with ACE-Step for VRAM. Download
`ggml-large-v3-turbo-q5_0.bin` from
https://huggingface.co/ggerganov/whisper.cpp/tree/main and place it beside the
GGUF files. The server discovers it automatically. Only `en`, `mk`, and
`unknown` are exposed by this application. To opt into the shared Vulkan
backend, start the server with `ACESTEP_LANGUAGE_GPU=1` or `--language-gpu`.
