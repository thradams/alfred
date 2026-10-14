#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include "vosk_api.h"
#include <espeak-ng/speak_lib.h>

#define SAMPLE_RATE 16000
#define FRAME_BYTES 4000 /* 125 ms: 16 kHz, mono, signed 16-bit */
#define TEXT_CAP 4096

static int speech_init(void)
{
    if (espeak_Initialize(AUDIO_OUTPUT_PLAYBACK, 0, NULL, 0) == -1) {
        fprintf(stderr, "Aviso: nao foi possivel inicializar o eSpeak NG.\n");
        return 0;
    }
    if (espeak_SetVoiceByName("pt-br") != EE_OK)
        espeak_SetVoiceByName("pt");
    return 1;
}

static void speak_text(const char *text)
{
    espeak_Synth(text, strlen(text) + 1, 0, POS_CHARACTER, 0,
                 espeakCHARS_UTF8, NULL, NULL);
    espeak_Synchronize();
}

static void extract_text(const char *json, char *out, size_t cap)
{
    const char *p = strstr(json, "\"text\"");
    size_t n = 0;
    out[0] = '\0';
    if (!p || !(p = strchr(p, ':'))) return;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p++ != '"') return;
    while (*p && *p != '"' && n + 1 < cap) {
        if (*p == '\\' && p[1]) p++;
        out[n++] = *p++;
    }
    out[n] = '\0';
}


static void normalize_text(const char *src, char *dst, size_t cap)
{
    size_t i = 0;
    while (*src && i + 1 < cap) {
        unsigned char c = (unsigned char)*src;
        if (c < 0x80) {
            if (isalnum(c)) dst[i++] = (char)tolower(c);
            else if (i > 0 && dst[i - 1] != ' ') dst[i++] = ' ';
            ++src;
        } else if (src[0] == (char)0xC3 && src[1]) {
            unsigned char d = (unsigned char)src[1];
            /* Common Portuguese accented letters, folded to ASCII. */
            if (d == 0xA1 || d == 0xA0 || d == 0xA2 || d == 0xA3 || d == 0xA4 || d == 0x81 || d == 0x80 || d == 0x82 || d == 0x83 || d == 0x84) dst[i++] = 'a';
            else if (d == 0xA9 || d == 0xAA || d == 0xA8 || d == 0x89 || d == 0x8A || d == 0x88) dst[i++] = 'e';
            else if (d == 0xAD || d == 0xAC || d == 0xAE || d == 0x8D || d == 0x8C || d == 0x8E) dst[i++] = 'i';
            else if (d == 0xB3 || d == 0xB2 || d == 0xB4 || d == 0xB5 || d == 0xB6 || d == 0x93 || d == 0x92 || d == 0x94 || d == 0x95 || d == 0x96) dst[i++] = 'o';
            else if (d == 0xBA || d == 0xB9 || d == 0xBB || d == 0xBC || d == 0x9A || d == 0x99 || d == 0x9B || d == 0x9C) dst[i++] = 'u';
            else if (d == 0xA7 || d == 0x87) dst[i++] = 'c';
            else if (i > 0 && dst[i - 1] != ' ') dst[i++] = ' ';
            src += 2;
        } else {
            if (i > 0 && dst[i - 1] != ' ') dst[i++] = ' ';
            ++src;
        }
    }
    while (i > 0 && dst[i - 1] == ' ') --i;
    dst[i] = '\0';
}

static int is_time_question(const char *text)
{
    char normalized[TEXT_CAP];
    normalize_text(text, normalized, sizeof(normalized));
    return strstr(normalized, "que horas sao") != NULL ||
           strstr(normalized, "horas sao") != NULL ||
           strcmp(normalized, "que horas") == 0;
}

static void answer_time(int speech_enabled)
{
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    char answer[128];
    if (!local) {
        snprintf(answer, sizeof(answer), "Nao consegui consultar o relogio do sistema.");
    } else if (local->tm_hour == 1) {
        snprintf(answer, sizeof(answer), "Agora e uma hora e %d minutos.", local->tm_min);
    } else if (local->tm_hour == 0) {
        snprintf(answer, sizeof(answer), "Agora e meia-noite e %d minutos.", local->tm_min);
    } else {
        snprintf(answer, sizeof(answer), "Agora sao %d horas e %d minutos.", local->tm_hour, local->tm_min);
    }
    printf("%s\n", answer);
    fflush(stdout);
    if (speech_enabled) speak_text(answer);
}

static void handle_question(const char *text, int speech_enabled)
{
    char answer[TEXT_CAP + 64];
    if (!text[0]) return;
    if (is_time_question(text)) {
        answer_time(speech_enabled);
        return;
    }
    snprintf(answer, sizeof(answer), "Voce quer dizer: %s?", text);
    printf("%s\n", answer);
    fflush(stdout);
    if (speech_enabled) speak_text(answer);
}

static char *find_case_insensitive(char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    if (n == 0) return haystack;
    for (char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            ++i;
        if (i == n) return p;
    }
    return NULL;
}

static void remove_wake_word(char *text)
{
    char *p = find_case_insensitive(text, "alfred");
    if (!p) return;
    char *end = p + strlen("alfred");
    while (*end && (isspace((unsigned char)*end) || *end == ',' || *end == ':' || *end == ';'))
        ++end;
    if (p == text) {
        memmove(text, end, strlen(end) + 1);
    } else {
        /* Preserve any words that came before the wake word, if recognition added them. */
        memmove(p, end, strlen(end) + 1);
    }
}

int main(int argc, char **argv)
{
    const char *model_path = argc > 1 ? argv[1] : "vosk-model-small-pt-0.3";
    const char *device = argc > 2 ? argv[2] : "default";
    VoskModel *model;
    VoskRecognizer *recognizer;
    snd_pcm_t *pcm = NULL;
    short audio[FRAME_BYTES / sizeof(short)];
    int active = 0;
    int speech_enabled = speech_init();

    vosk_set_log_level(-1);
    model = vosk_model_new(model_path);
    if (!model) {
        fprintf(stderr, "Erro: nao consegui carregar o modelo: %s\n", model_path);
        return 1;
    }
    recognizer = vosk_recognizer_new(model, (float)SAMPLE_RATE);
    if (!recognizer) {
        fprintf(stderr, "Erro: nao consegui criar o reconhecedor.\n");
        vosk_model_free(model);
        return 1;
    }

    int err = snd_pcm_open(&pcm, device, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        fprintf(stderr, "Erro abrindo microfone ALSA '%s': %s\n", device, snd_strerror(err));
        vosk_recognizer_free(recognizer);
        vosk_model_free(model);
        return 1;
    }
    err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED, 1, SAMPLE_RATE,
                             1, 500000);
    if (err < 0) {
        fprintf(stderr, "Erro configurando audio 16 kHz mono: %s\n", snd_strerror(err));
        snd_pcm_close(pcm);
        vosk_recognizer_free(recognizer);
        vosk_model_free(model);
        return 1;
    }

    fprintf(stderr, "Alfred iniciado. Diga 'Alfred' e depois sua frase. Ctrl+C para sair.\n");
    fprintf(stderr, "Microfone: %s | modelo: %s\n", device, model_path);

    for (;;) {
        snd_pcm_sframes_t frames = snd_pcm_readi(pcm, audio,
                                      (snd_pcm_uframes_t)(sizeof(audio) / sizeof(audio[0])));
        if (frames == -EPIPE) {
            snd_pcm_prepare(pcm);
            continue;
        }
        if (frames < 0) {
            fprintf(stderr, "Erro lendo microfone: %s\n", snd_strerror((int)frames));
            snd_pcm_recover(pcm, (int)frames, 1);
            continue;
        }
        if (frames == 0) continue;

        int final = vosk_recognizer_accept_waveform(
            recognizer, (const char *)audio, (int)(frames * sizeof(short)));
        const char *json = final ? vosk_recognizer_result(recognizer)
                                 : vosk_recognizer_partial_result(recognizer);
        char text[TEXT_CAP];
        extract_text(json, text, sizeof(text));

        if (!active) {
            /* Use completed utterances to reduce accidental activations. */
            if (final && find_case_insensitive(text, "alfred")) {
                remove_wake_word(text);
                fprintf(stderr, "\n[Alfred ativado]\n");
                if (speech_enabled)
                    speak_text("Pois nao?");
                if (text[0]) {
                    handle_question(text, speech_enabled);
                    fprintf(stderr, "[Aguardando 'Alfred']\n");
                } else {
                    active = 1;
                    fprintf(stderr, "[Pode falar]\n");
                }
            }
            /* Ignore speech that did not contain the wake word. */
        } else if (final) {
            if (text[0]) handle_question(text, speech_enabled);
            active = 0;
            fprintf(stderr, "[Aguardando 'Alfred']\n");
        }
    }

    /* Unreachable during normal operation; Ctrl+C terminates the process. */
    snd_pcm_close(pcm);
    if (speech_enabled) espeak_Terminate();
    vosk_recognizer_free(recognizer);
    vosk_model_free(model);
    return 0;
}
