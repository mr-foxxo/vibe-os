# Plano de Port do Java para o VibeOS

Objetivo:
- disponibilizar `java` e `javac` dentro de `/lang/apps`
- integrar os binarios ao shell e ao appfs
- evitar regressao no boot, shell, desktop e build principal

Estado atual validado:
- [x] localizar a arvore real do Java em [lang/vendor/jdk8u](/Users/mel/Documents/vibe-os/lang/vendor/jdk8u)
- [x] confirmar como os runtimes atuais entram no sistema via `lang/apps`, `appfs` e `lang_loader`
- [x] confirmar que o ABI atual de apps do VibeOS ainda e pequeno demais para um `jdk8u` completo
- [x] identificar os pontos principais do runtime atual:
  - [lang/include/vibe_app.h](/Users/mel/Documents/vibe-os/lang/include/vibe_app.h)
  - [lang/include/vibe_app_runtime.h](/Users/mel/Documents/vibe-os/lang/include/vibe_app_runtime.h)
  - [userland/modules/lang_loader.c](/Users/mel/Documents/vibe-os/userland/modules/lang_loader.c)
  - [Makefile](/Users/mel/Documents/vibe-os/Makefile)

## Fase 1: Base do Runtime

- [x] ampliar o ABI de apps em `lang/include/vibe_app.h`
  - adicionar I/O de arquivos estilo POSIX minimo
  - [x] adicionar `write_file` no host API para apps gravarem artefatos locais
  - [x] adicionar `create_dir` no host API para artefatos em arvores com package
  - [x] adicionar `stat`
  - [x] adicionar `open/close/read/write/lseek`
  - [x] adicionar variaveis de ambiente
  - [x] adicionar tempo/clock minimo
  - [x] deixar execucao de subprocesso explicitamente indisponivel no runtime atual

- [x] ampliar o runtime C em `lang/sdk/app_runtime.c`
  - [x] expor `vibe_app_write_file(...)` para apps gerarem saida no fs do VibeOS
  - [x] implementar wrappers de `FILE*` com backend real
  - [x] implementar `fopen/fread/fwrite/fseek/ftell`
  - [x] expor helpers fd-style (`open/read/write/close/lseek/stat/getenv`) para a camada compat
  - [x] implementar tempo monotonic/ticks e `sleep/usleep`
  - [x] implementar threads e sincronizacao cooperativas minimas para apps
  - [x] implementar `malloc/free/realloc` sobre a arena ampliada de apps
  - [x] implementar `errno` basico e ponteiro `__errno()`
  - [x] implementar `exit`, `abort`, `atexit`

- [x] aumentar a arena de app e revisar layout de carga
  - [x] revisar `VIBE_APP_ARENA_SIZE`
  - [x] revisar `VIBE_APP_LOAD_ADDR`
  - [x] validar que apps maiores nao quebram o boot nem o kernel

## Fase 2: Shell e Loader

- [x] tornar o `lang_loader` dinamico o suficiente para mais runtimes
  - remover lista fixa minima de comandos externos
  - [x] permitir descoberta limpa de novos apps em `appfs`

- [x] integrar `/lang/apps/java` e `/lang/apps/javac` no build
  - [x] adicionar regras no [Makefile](/Users/mel/Documents/vibe-os/Makefile)
  - [x] adicionar os bins em `LANG_APP_BINS`
  - [x] garantir entrada em `/bin` e reconhecimento pelo shell

## Fase 3: Ferramenta Java Inicial

- [x] criar esqueleto de `lang/apps/java/java_main.c`
  - [x] parse de argv
  - [x] integracao com host runtime
  - [x] mensagens de erro claras

- [x] criar esqueleto de `lang/apps/javac/javac_main.c`
  - [x] parse de argv
  - [x] caminho de compilacao inicial

- [x] adicionar suporte minimo a leitura de `.class` e classpath
  - [x] leitura de arquivos locais
  - [x] resolucao de caminhos
  - [x] classpath simples
  - [x] suporte basico a `package` e `-d`

## Fase 4: JVM / JRE

- [x] decidir estrategia do runtime Java
  - [x] usar uma JVM menor/interpretada como etapa intermediaria
  - deixar `hotspot` completo como etapa posterior

- [ ] portar dependencias de plataforma exigidas pela JVM
  - [x] threads cooperativas minimas
  - [x] sincronizacao cooperativa minima
  - memoria virtual
  - [x] tempo/monotonic clock
  - [x] file descriptors
  - biblioteca nativa basica

- [x] integrar a execucao de `java`
  - [x] carregar classes
  - [x] inicializar runtime
  - [x] executar `main`
  - [x] interpretar `.class` real com constant pool e bytecode JVM subset

## Fase 5: Javac

- [ ] avaliar se `langtools` do `jdk8u` cabe como primeiro alvo
  - identificar dependencias do compilador
  - medir impacto de memoria
  - validar se precisa de bootstrap JDK externo no host de build

- [x] integrar `javac`
  - [x] leitura de `.java`
  - [x] escrita de `.class` real para o subset suportado
  - [x] diagnosticos no console

## Fase 6: Compatibilidade Unix Necessaria

- [x] mapear tudo que o `jdk8u` espera e o VibeOS ainda nao fornece
  - [x] arquivos: `open/read/write/close/lseek/stat/fstat/getenv` ja existem sobre o host ABI
  - [x] tempo: `time/clock/clock_gettime/clock_getres/nanosleep/gettimeofday/usleep` ja existem na camada `compat`
  - [x] processos: ainda indisponiveis no runtime atual
  - [x] sinais: headers existem, sem semantica completa de processo/sinal ainda
  - [x] rede: ainda indisponivel no VibeOS atual
  - [x] locale: ainda muito parcial
  - [x] encoding: ainda parcial, sem stack completa de charset do JDK
  - [x] threads: pthread cooperativa minima e sincronizacao minima ja existem

- [x] implementar primeiro o que for necessario para `java` offline
  - [x] sem rede
  - [x] sem GUI
  - [x] sem ferramentas extras
  - [x] reaproveitar `compat/src/posix/unistd.c` sobre o host ABI em vez de manter leitura-only
  - [x] adicionar `time/sched/pthread/gettimeofday` minimos na `compat`

## Fase 7: Validacao

- [x] validar build principal do sistema
  - `make build/boot.img`

- [ ] validar que o shell nao regrediu
  - comandos existentes continuam funcionando

- [x] validar `java`
  - [x] executar programa simples `HelloWorld` via formato VibeJava subset
  - [x] executar `.class` real do subset JVM

- [x] validar `javac`
  - [x] compilar `HelloWorld.java`
  - [x] gerar `.class` real do subset JVM

- [x] validar fluxo completo
  - [x] compilar com `javac`
  - [x] executar com `java`
  - [x] manter `make build/boot.img` verde depois da ampliacao do runtime

## Ordem recomendada

1. expandir ABI e runtime de app
2. integrar `java` e `javac` como apps vazios mas linkando no sistema
3. fechar I/O e classpath
4. escolher estrategia real da JVM
5. trazer execucao de bytecode
6. trazer `javac`
7. estabilizar shell e build

## Observacoes honestas

- `jdk8u` completo nao e um patch pequeno; isso exige primeiro uma camada de runtime maior no VibeOS
- [x] primeiro marco realista: `java` e `javac` no sistema como apps integrados ao shell, com ABI ampliado
- [x] marco intermediario entregue: compilar e rodar um `HelloWorld.java` dentro do VibeOS pelo formato VibeJava subset, sem regressao do sistema
- `jdk8u` / HotSpot completos continuam dependendo das fases de runtime, compat Unix, memoria e threads ainda nao concluídas
