# VIBE OS - Sistema Operacional Vibe Coded 

## AVISO
Não leve esse repositório a sério, foi uma demonstração de como criar código low-level com IA de modo a mostrar as falhas e problemas existentes. O código quebrou no final quando foi solicitada a criação de um sistema de arquivos simples, portanto isso aqui vale para melhorias e avaliação da qualidade de código!!! 

## Descrição

Projeto mínimo de bootloader x86 em dois estágios com arquitetura modular:

- **Stage 1 (`boot/stage1.asm`)**: boot sector BIOS (512 bytes), carrega o stage 2 da mídia e entra em protected mode (32-bit).
- **Stage 2 (`stage2/`)**: kernel em C com framebuffer VGA, IDT/PIC, IRQ0 (timer), IRQ1 (teclado), IRQ12 (mouse) e ABI de syscall (`int 0x80`).
- **Kernel (`kernel/`)**: subsistema de kernel modularizado com:
  - Driver manager e drivers específicos (video, input, timer, interrupt)
  - Executivo (ELF loader)
  - Gerenciador de memória (paging, heap, physical memory)
  - Processador e scheduler
  - IPC (Inter-Process Communication)
  - Sistema de arquivos (VFS)
- **Userland**: módulos e aplicações em C rodam em ring3:
  - **bibliotecas/utilitários** (`syscalls`, `utils`, `fs`, `terminal`, `ui`, etc.)
  - **Aplicações** construídas no blob: `console`, `shell`, `busybox`, `desktop`, `filemanager`, `taskmgr`, `clock`.
  - **Console**: driver de modo-texto usado pelo shell.
  - **Shell**: prompt interativo com histórico e parser de argumentos.
  - **busybox**: dispatcher single-binary com comandos internos (`pwd`, `ls`, `cd`, `mkdir`, `touch`, `rm`, `cat`, `echo`, `clear`, `uname`, `help`, `exit`, `startx`, `history`).
  - **desktop**: interface gráfica invocada com `startx`.

De forma padrão o sistema inicializa em console de texto, roda o shell e espera comandos.

## Estrutura

A estrutura de diretórios reflete a arquitetura modular com headers centralizados:

```text
.
├── boot/
│   └── stage1.asm              # Boot sector (BIOS)
├── headers/                    # CENTRALIZADO: todos os headers
│   ├── include/
│   │   └── userland_api.h      # Definições de syscalls
│   ├── kernel/                 # Headers do kernel
│   │   ├── cpu/
│   │   ├── drivers/            # video, input, interrupt, timer, debug
│   │   ├── exec/               # ELF loader
│   │   ├── fs/                 # Filesystem
│   │   ├── ipc/                # Inter-process communication
│   │   ├── memory/             # heap, paging, physmem
│   │   ├── process/            # Process management
│   │   └── *.h                 # Headers de topo (kernel.h, hal.h, etc.)
│   ├── stage2/                 # Headers do stage2
│   │   ├── include/            # graphics, io, irq, keyboard, mouse, syscalls, timer, types, userland, video
│   │   └── modules/            # common.h
│   └── userland/               # Headers de userland
│       ├── applications/       # apps, clock, filemanager, taskmgr
│       └── modules/            # busybox, console, fs, shell, ui, utils, syscalls, etc.
├── kernel/                     # Implementação: apenas .c (headers em headers/kernel/)
│   ├── cpu/
│   ├── drivers/
│   ├── exec/
│   ├── fs/
│   ├── ipc/
│   ├── memory/
│   ├── process/
│   ├── entry.c
│   ├── hal.c
│   ├── panic.c
│   └── syscall.c
├── kernel_asm/                 # Assembly do kernel
│   ├── context_switch.asm
│   └── isr.asm
├── stage2/                     # Implementação: apenas .c (headers em headers/stage2/)
│   ├── *.c                     # Implementações (mouse, timer, graphics, syscalls, etc.)
│   └── isr.asm                 # Stubs de interrupção
├── userland/                   # Implementação: apenas .c (headers em headers/userland/)
│   ├── userland.c
│   ├── applications/           # desktop, terminal, clock, filemanager, taskmgr
│   └── modules/                # console, shell, busybox, ui, fs, etc.
├── linker/
│   ├── stage2.ld
│   └── userland.ld
├── Makefile
└── README.md
```

### Organização de Headers

Todos os arquivos `.h` (headers) foram centralizados em `headers/` mantendo hierarquia por subsistema:

- **`headers/kernel/`**: 19 headers - Kernel e seus módulos
- **`headers/stage2/`**: 12 headers - Boot e fase 2
- **`headers/userland/`**: 15 headers - Aplicações e bibliotecas
- **`headers/include/`**: 1 header - APIs comuns (userland_api.h)

**Total: 46 headers organizados por subsistema**

## Pré-requisitos

- `nasm`
- Toolchain i386 ELF (recomendado):
  - `i686-elf-gcc`
  - `i686-elf-ld`
  - `i686-elf-objcopy`
- `qemu-system-i386`
- `make`

> No macOS, prefira usar a toolchain `i686-elf-*` do Homebrew.
> O override `make CC=gcc LD=ld OBJCOPY=objcopy` só funciona quando o linker tem suporte a ELF i386 (GNU binutils/LLD compatível).

## Execução no macOS (passo a passo)

### 1. Instalar dependências base

Instale as Command Line Tools da Apple:

```bash
xcode-select --install
```

### 2. Instalar Homebrew (se ainda não tiver)

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### 3. Instalar toolchain e emulador

```bash
brew update
brew install nasm i686-elf-gcc qemu
```

### 4. Garantir Homebrew no PATH

Apple Silicon (M1/M2/M3...):

```bash
echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
eval "$(/opt/homebrew/bin/brew shellenv)"
```

Mac Intel:

```bash
echo 'eval "$(/usr/local/bin/brew shellenv)"' >> ~/.zprofile
eval "$(/usr/local/bin/brew shellenv)"
```

### 5. Verificar instalação

```bash
nasm -v
i686-elf-gcc --version
i686-elf-ld --version
i686-elf-objcopy --version
qemu-system-i386 --version
```

### 6. Build e execução

No diretório do projeto:

```bash
make
make run
```

Se faltar alguma dependência, o `make` falha com mensagem direta sugerindo o comando do Homebrew.

## Build

```bash
make
```

Isso gera:

- `build/boot.bin` (boot sector)
- `build/userland.bin` (binário da userland)
- `build/stage2.bin` (kernel + blob da userland embutido)
- `build/boot.img` (imagem final bootável)

## Executar no QEMU

```bash
make run
```

Na versão atual o kernel inicializa o console de texto e imediatamente lança o shell.
O fluxo típico é:

1. Você será recebido por um prompt `user@vibeos:/some/path $`.
2. Digite comandos suportados (use `help` para lista completa).
3. `startx` alterna para o desktop gráfico que já existia antes — o comportamento é idêntico ao descrito abaixo, com barra de tarefas, menu `START`, aplicativo Terminal e Relógio.

> Se o `make run` falhar por falta de `qemu` veja a seção de pré‑requisitos mais acima.

## Como a userland funciona neste projeto

O processo de build permanece basicamente igual, mas a userland agora é composta de múltiplos módulos compilados juntos:

1. O `Makefile` coleta todos os `.c` sob `userland/` e seus subdiretórios; o linker produz `userland.bin` posicionado em `0x20000`.
2. O kernel (`stage2`) embute o `userland.bin` dentro do seu próprio binário.
3. Em runtime, o kernel copia o blob para `0x20000` e chama `userland_entry`, que por sua vez inicializa o sistema de arquivos simples e, em seguida, chama `console_init()` e `shell_main()`.
4. A comunicação entre userland e kernel continua sendo feita via syscalls definidas em `include/userland_api.h`.

## Interface Gráfica na userland

- O `stage1` muda para VGA modo `13h` (320x200x256).
- Desktop com barra de tarefas na parte inferior.
- Menu de aplicativos acionado por `START`.
- Apps `TERMINAL` e `RELOGIO`.
- Botões de fechar (`X`) em cada janela.
- Cursor de mouse desenhado na userland.
- Multitarefa cooperativa no loop da userland (Terminal e Relógio simultâneos).

## Syscalls disponíveis

- `SYSCALL_GFX_CLEAR`: limpa framebuffer com cor.
- `SYSCALL_GFX_RECT`: desenha retângulo preenchido.
- `SYSCALL_GFX_TEXT`: desenha texto simples (fonte bitmap no kernel).
- `SYSCALL_INPUT_MOUSE`: retorna estado do mouse (`x`, `y`, botões).
- `SYSCALL_INPUT_KEY`: retorna próximo caractere do teclado (fila).
- `SYSCALL_SLEEP`: pausa até próxima interrupção (`hlt`).
- `SYSCALL_TIME_TICKS`: retorna ticks do timer do kernel (100 Hz).

## Driver de Mouse no Kernel

- Arquivos:
  - `stage2/stage2.c` (PIT, init PS/2, PIC remap, IDT, handlers C e syscall dispatcher)
  - `stage2/isr.asm` (stubs de IRQ0, IRQ1, IRQ12 e `int 0x80`)
- Fluxo:
  1. Kernel remapeia PIC para vetores `0x20-0x2F`.
  2. Configura PIT em 100 Hz e incrementa ticks via IRQ0.
  3. Registra IRQ1 (`0x21`) e IRQ12 (`0x2C`) na IDT.
  4. Inicializa o mouse PS/2 (`0xF6`, `0xF4`).
  5. A cada IRQ12, coleta pacote de 3 bytes e atualiza `x/y/buttons`.
  6. Userland consulta via syscall `SYSCALL_INPUT_MOUSE`.

## App Relógio (multitarefa)

- O app Relógio lê tempo usando `SYSCALL_TIME_TICKS`.
- Atualiza 1x por segundo (`ticks / 100`).
- Continua atualizando enquanto você usa o Terminal.
- Pode ser aberto/fechado no menu `START`.

## App Terminal (shell via syscall)

- O shell roda dentro da userland mas, diferentemente da versão antiga, ele opera no console de texto (não usa janelas).
- Entrada de teclado é capturada através de `SYSCALL_INPUT_KEY`.
- O parser de argumentos suporta strings entre aspas e histórico de linhas (com `up`/`down` se implementado).
- Um componente busybox dispatcha os seguintes comandos internos:
  - `help` — lista todos os comandos disponíveis
  - `pwd`, `ls`, `cd`, `mkdir`, `touch`, `rm`, `cat`
  - `echo`, `clear`, `uname`
  - `exit` — encerra o shell
  - `startx` — alterna para o modo gráfico chamando `desktop_main()`
  - `history` — imprime o histórico de comandos registrados

(O suporte a `write`/`append` foi removido para simplificação, já que o filesystem é in-memory.)

## Sistema de Arquivos (VFS)

- Estrutura orientada a diretórios e arquivos em árvore.
- Operações de leitura/escrita suportadas no shell.
- Suporte a caminhos relativos e absolutos (ex.: `/home/user`, `../docs`).
- Diretório atual controlado por `CD` e exibido no prompt.
- Estrutura inicial criada por `fs_init()`:
  - `/` (raiz)
  - `/bin` (onde o blob `busybox` reside)
  - `/home` e `/home/user`
  - `/tmp`
  - `/dev` (caixa–preta, apenas para demonstração)
- Implementação atual é **em memória** (não persistente após reboot).

## Debug com GDB

```bash
make debug
```

Esse alvo sobe o QEMU com `-s -S` (aguardando conexão do GDB em `localhost:1234`).

## Limitações deste exemplo

- BIOS legado (não UEFI).
- Leitura CHS do stage2 setor-a-setor (assume stage2 contíguo na imagem).
- IDT/PIC com IRQ0/IRQ1/IRQ12 + syscall, sem gerenciamento completo de exceções.
- Userland **sem isolamento de privilégio** (ainda não roda em ring3).
- Driver de mouse PS/2 sem wheel/scroll (pacote padrão de 3 bytes).
- Sem aceleração gráfica; renderização por software em VGA 13h.
- VFS sem persistência em disco e com limites fixos de nós/tamanho por arquivo.

## Limpar artefatos

```bash
make clean
```

## Troubleshooting (macOS)

- Erro: `nasm: No such file or directory`
  - Rode: `brew install nasm`
- Erro: `i686-elf-gcc: command not found`
  - Rode: `brew install i686-elf-gcc`
- Erro: `qemu-system-i386: command not found`
  - Rode: `brew install qemu`
- Homebrew instalado, mas comando não encontrado
  - Recarregue seu shell: `source ~/.zprofile` e tente de novo.
- Erro: `stage2.bin (...) excede ... bytes`
  - Reduza tamanho de kernel/userland ou aumente `STAGE2_SECTORS` no `Makefile`.
- Entrada não responde no QEMU
  - Clique dentro da janela do QEMU para capturar foco.
  - Se o cursor estiver preso, use `Ctrl + Option + G` (macOS) para liberar/capturar novamente.
- Mouse não se move na interface
  - Confirme que a janela do QEMU está com foco/captura.
  - Teste com: `make run` e clique uma vez na tela antes de mover o mouse.
