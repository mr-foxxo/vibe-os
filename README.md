# VIBE OS - Um sistema operacional x86 simples usando IA e vibe coding (e muitas patas de raposo e mãos humanas)

## Aviso

Este repositório é um projeto experimental de sistema operacional x86 BIOS feito com forte apoio de IA. Há bastante código funcional, mas também há áreas incompletas, inconsistentes ou ainda em transição arquitetural.

Leia o projeto como:

- experimento de bootloader + kernel + runtime modular
- base para estudo, depuração e refatoração
- código que ainda não representa um sistema operacional "acabado"

## Resumo

O estado atual do projeto é este:

- boot **BIOS legado**, em múltiplos estágios
- **MBR -> VBR -> stage2 -> `KERNEL.BIN`**
- partição de boot **FAT32**
- segunda partição **raw** para **AppFS**, persistência e assets
- kernel 32-bit com:
  - inicialização de CPU, GDT, IDT, PIC e PIT
  - drivers de vídeo, teclado, mouse e storage
  - scheduler simples
  - camada de serviços estilo microkernel
  - tabela de syscalls
- bootstrap de userland via serviço interno `init`
- carregamento de apps externos via **AppFS**
- shell modular e caminho gráfico com `startx` e desktop

Em outras palavras: o sistema não é mais um blob único "kernel + userland embutida" no sentido antigo do README. Hoje o fluxo real é:

`BIOS -> MBR -> stage1/VBR -> stage2 -> KERNEL.BIN -> kernel -> init -> AppFS -> userland.app/startx -> shell e apps`

## Aviso sobre a documentação detalhada

Os arquivos detalhados em `docs/` estão **somente em inglês**.

Se você quer a visão técnica mais fiel ao código atual, comece por eles:

- [docs/overview.md](docs/overview.md)
- [docs/workflow.md](docs/workflow.md)
- [docs/mbr.md](docs/mbr.md)
- [docs/stage1.md](docs/stage1.md)
- [docs/stage2.md](docs/stage2.md)
- [docs/memory_map.md](docs/memory_map.md)
- [docs/kernel_init.md](docs/kernel_init.md)
- [docs/drivers.md](docs/drivers.md)
- [docs/runtime_and_services.md](docs/runtime_and_services.md)
- [docs/apps_and_modules.md](docs/apps_and_modules.md)

## Índice rápido

- [Arquitetura atual](#arquitetura-atual)
- [Mapa de diretórios](#mapa-de-diretórios)
- [Build](#build)
- [Execução no QEMU](#execução-no-qemu)
- [Artefatos gerados](#artefatos-gerados)
- [Documentação](#documentação)
- [Status real do projeto](#status-real-do-projeto)
- [English version](README.en.md)

## Arquitetura atual

### Boot

- [`boot/mbr.asm`](boot/mbr.asm)
  - relocação do MBR
  - inicialização de `BOOTINFO`
  - descoberta da partição ativa
  - carga do VBR
- [`boot/stage1.asm`](boot/stage1.asm)
  - leitura do `stage2` a partir dos setores reservados da partição FAT32
  - trace persistente de boot em RAM baixa
- [`boot/stage2.asm`](boot/stage2.asm)
  - parse mínimo de FAT32
  - leitura de `VIBEBG.BIN` e `KERNEL.BIN`
  - enumeração VESA
  - detecção de memória
  - ativação de A20
  - menu de boot
  - retorno para real mode para uso de BIOS e handoff final ao kernel

### Kernel

O kernel atual entra por [`kernel/entry.c`](kernel/entry.c) e faz:

- bring-up básico de CPU/GDT/IDT/PIC/PIT
- inicialização de vídeo e console de texto
- teclado PS/2 e mouse PS/2
- descoberta de storage nativo:
  - AHCI primeiro
  - ATA depois
- inicialização de memória, heap e arenas para apps externos
- scheduler
- registro e bootstrap de serviços
- syscalls
- lançamento do `init` interno

### Runtime e apps

O caminho atual de execução de apps não depende de um VFS completo no kernel.

O fluxo é:

- o kernel sobe o serviço interno `init`
- `init` prepara console e filesystem de userland
- `init` tenta lançar `userland.app` via AppFS
- `userland.app` sobe o shell
- em boot normal para desktop, `userland.app` autoexecuta `startx`
- `startx` e o desktop lançam apps gráficos modulares

### Storage em tempo de execução

Hoje existem dois "mundos" de storage:

1. partição de boot FAT32
   - usada pelo loader BIOS
   - contém `KERNEL.BIN`, `STAGE2.BIN`, manifests e assets de boot
2. partição raw de dados
   - AppFS
   - área de persistência
   - assets raw como wallpaper e dados de jogos

## Mapa de diretórios

```text
.
├── boot/                # MBR, VBR/stage1 e stage2 em assembly
├── kernel/              # kernel C
├── kernel_asm/          # assembly do kernel
├── headers/             # headers centralizados
├── userland/            # bootstrap, módulos e apps nativos
├── lang/                # runtime/AppFS SDK e apps de linguagem
├── applications/        # apps/ports auxiliares usados no build
├── compat/              # árvore de compatibilidade/ports
├── tools/               # empacotamento da imagem, AppFS e validações
├── linker/              # scripts de link
├── assets/              # imagens de boot e runtime
└── docs/
    ├── *.md             # documentação técnica detalhada (em inglês)
    └── guidelines/      # planos, guidelines e docs voltados a agentes
```

## Build

### Requisitos

- `nasm`
- `make`
- `python3`
- toolchain i386 ELF recomendada:
  - `i686-elf-gcc`
  - `i686-elf-ld`
  - `i686-elf-objcopy`
- `qemu-system-i386` ou `qemu-system-x86_64`
- ferramentas FAT para montar/copiar arquivos na partição de boot:
  - `mkfs.fat` ou equivalente
  - `mcopy`
  - `mmd`

### Linux

Exemplo mínimo em Debian/Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential make python3 nasm qemu-system-x86 mtools dosfstools binutils gcc-multilib
```

### macOS

Exemplo com Homebrew:

```bash
brew install nasm qemu mtools dosfstools i686-elf-gcc
```

### Comando principal

```bash
make
```

Alvos úteis:

- `make` ou `make all`: build completo da imagem
- `make full`: limpa e recompila tudo
- `make img`: gera a imagem bootável
- `make imb`: gera a imagem final para gravação/uso externo
- `make legacy-data-img`: gera só `build/data-partition.img`
- `make clean`: limpa artefatos

## Execução no QEMU

Modo normal:

```bash
make run
```

Modo headless com serial no terminal:

```bash
make run-headless-debug
```

Outros alvos de depuração úteis já definidos no `Makefile`:

- `make run-debug`
- `make run-headless-core2duo-debug`
- `make run-headless-pentium-debug`
- `make run-headless-atom-debug`
- `make run-headless-ahci-debug`
- `make run-headless-usb-debug`

## Artefatos gerados

Os mais importantes hoje são:

- `build/mbr.bin`
- `build/boot.bin`
- `build/stage2.bin`
- `build/kernel.bin`
- `build/data-partition.img`
- `build/boot.img`
- `build/generated/app_catalog.h`
- `build/lang/userland.app`

Também são gerados manifests de layout/política para a partição FAT32 de boot.

## Documentação

### Documentação técnica principal

- [docs/overview.md](docs/overview.md): mapa geral
- [docs/workflow.md](docs/workflow.md): fluxo completo do boot até apps
- [docs/memory_map.md](docs/memory_map.md): layout de memória e disco
- [docs/kernel_init.md](docs/kernel_init.md): sequência real de inicialização do kernel
- [docs/drivers.md](docs/drivers.md): drivers atuais
- [docs/runtime_and_services.md](docs/runtime_and_services.md): scheduler, serviços, init, syscalls e AppFS
- [docs/apps_and_modules.md](docs/apps_and_modules.md): catálogo de apps, módulos e runtimes

### Guidelines e planos

Eles são úteis como contexto histórico e planejamento, mas não devem ser lidos como descrição definitiva do estado atual do código. Esses
arquivos estão disponíveis em [docs/guidelines/](docs/guidelines/)

## Status real do projeto

O que já existe de forma material:

- boot BIOS em múltiplos estágios
- partição FAT32 de boot funcionando no pipeline
- carga de `KERNEL.BIN` por stage2
- `BOOTINFO` compartilhado entre loader e kernel
- framebuffer VESA preservado quando válido
- drivers básicos de vídeo, teclado, mouse, timer e storage
- scheduler e processos/tarefas simples
- camada de serviços estilo microkernel
- syscalls suficientes para shell, runtime gráfico e AppFS
- `init` interno + `userland.app` externo
- desktop e conjunto razoável de apps gráficos
- catálogo grande de apps externos, utilitários e ports

O que continua limitado ou em transição:

- o VFS do kernel ainda é mínimo
- o caminho principal de execução de apps ainda passa por AppFS, não por um loader ELF/VFS geral
- isolamento de processos e modelo "ring3 completo" não devem ser assumidos
- há várias partes de compatibilidade e ports ainda adaptadas de forma pragmática
- o projeto ainda contém muito código e documentação histórica de transição

### Futuro do projeto

O projeto no momento está sendo desenvolvido por mim e pela querida [Mel Santos](https://github.com/melmonfre). A ideia é evoluir o máximo
para termos uma versão completa de um pequeno sistema operacional modular. Além disso, como se trata do 
Sr. Raposo responsável pelo projeto, está previsto a construção de um pequeno SLM (Small Lanugage Model) executando
diretamente no kernel usando uma arquitetura simples de GPT (e o máximo de técnicas de incremento que conseguir). A ideia 
é implementar dentro do shell tornando o inteligente.

## Versão em inglês

- [README.en.md](README.en.md)
