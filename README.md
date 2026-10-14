# Alfred — reconhecimento de voz e resposta falada em C

Protótipo para Raspberry Pi OS de 64 bits. Escuta o microfone continuamente; quando reconhece “Alfred”, responde “Pois não?” usando eSpeak NG e escuta a pergunta. A pergunta “Que horas são?” consulta o relógio local do sistema usando `time()`/`localtime()` em C e fala a hora. Para outras frases, pergunta “Você quer dizer: ...?”. Não usa Python.

## 1. Dependências do sistema

```sh
sudo apt update
sudo apt install build-essential libasound2-dev alsa-utils libespeak-ng-dev espeak-ng wget unzip
```

## 2. Baixar Vosk e o modelo de português

```sh
mkdir -p ~/alfred-c
cd ~/alfred-c
wget https://github.com/alphacep/vosk-api/releases/download/v0.3.45/vosk-linux-aarch64-0.3.45.zip
unzip vosk-linux-aarch64-0.3.45.zip
wget https://raw.githubusercontent.com/alphacep/vosk-api/v0.3.45/src/vosk_api.h
wget https://alphacephei.com/vosk/models/vosk-model-small-pt-0.3.zip
unzip vosk-model-small-pt-0.3.zip
```

Copie `main.c` para `~/alfred-c`. Confira a arquitetura com `uname -m`; o pacote de biblioteca acima é para `aarch64`.

## 3. Compilar

Se `vosk_api.h` e `libvosk.so` estiverem no diretório atual:

```sh
gcc -O2 -Wall main.c -I. -L. -Wl,-rpath,'$ORIGIN' -lvosk -lasound -lespeak-ng -o alfred
```

Se o ZIP extrair os arquivos para uma subpasta, ajuste os caminhos `-I` e `-L`.

## 4. Executar

```sh
./alfred vosk-model-small-pt-0.3
```

Diga “Alfred” e aguarde “Pois não?”. Em seguida, pergunte “Que horas são?” para ouvir a hora do Raspberry Pi. Para qualquer outra frase reconhecida, Alfred fala “Você quer dizer: ...?” e imprime a transcrição no terminal. Depois, volta a esperar “Alfred”. Pressione Ctrl+C para sair. A hora depende do relógio e do fuso horário configurados no Raspberry Pi.

Para listar microfones ALSA:

```sh
arecord -l
```

Para selecionar outro dispositivo, passe-o como segundo argumento:

```sh
./alfred vosk-model-small-pt-0.3 plughw:1,0
```

## Limitações do protótipo

- O Vosk reconhece áudio continuamente enquanto espera por “Alfred”; o programa ignora os resultados que não contêm a palavra. É simples para testar, mas não é um detector de palavra-chave de baixo consumo.
- A ativação depende do texto reconhecido e pode falhar ou ocorrer por engano.
- Para melhores resultados, diga “Alfred”, faça uma pequena pausa e depois diga a frase.
- A fala de confirmação usa eSpeak NG; o som é sintético/robótico.
- Se o microfone não aceitar 16 kHz, tente um dispositivo ALSA `plughw`, que pode converter a taxa de amostragem.
