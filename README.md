# VIBE OS - Sistema Operacional Vibe Coded 

## AVISO
Não leve esse repositório a sério, foi uma demonstração de como criar código low-level com IA de modo a mostrar as falhas e problemas existentes. O código quebrou no final quando foi solicitada a criação de um sistema de arquivos simples, portanto isso aqui vale para melhorias e avaliação da qualidade de código!!! 

## Descrição

Projeto mínimo de bootloader x86 em dois estágios:

- **Stage 1 (`boot/stage1.asm`)**: boot sector BIOS (512 bytes), carrega o stage 2 da mídia e entra em protected mode (32-bit).
- **Stage 2 (`stage2/stage2.c`)**: kernel mínimo em C com framebuffer VGA, IDT/PIC, IRQ0 (timer), IRQ1 (teclado), IRQ12 (mouse) e syscall ABI (`int 0x80`).
- **Stage 2 ASM (`stage2/isr.asm`)**: stubs de interrupção e syscall.
- **Userland (`userland/userland.c`)**: desktop simples com barra de tarefas, menu de aplicativos e apps Terminal + Relógio com VFS em memória.

## Estrutura

```text
.
├── boot/
│   └── stage1.asm
├── include/
│   └── userland_api.h
├── linker/
│   ├── stage2.ld
│   └── userland.ld
├── stage2/
│   ├── isr.asm
│   └── stage2.c
├── userland/
│   └── userland.c
├── Makefile
└── README.md
```

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

Você deve ver:

- barra de tarefas na parte de baixo,
- botão `START`,
- menu de aplicativos,
- app `TERMINAL` (shell interativo),
- app `RELOGIO` (atualização em tempo real),
- botões `X` para fechar janelas.

## Como a userland funciona neste projeto

1. O build gera `userland.bin` de forma independente (linkado para `0x20000`).
2. O `stage2` (kernel) embute esse binário na própria imagem.
3. Em runtime, o kernel copia a userland para `0x20000` e chama `userland_entry`.
4. A userland usa syscalls via `int 0x80` definidas em `include/userland_api.h`.

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

- O app Terminal roda dentro da userland.
- Entrada de teclado vem de `SYSCALL_INPUT_KEY`.
- Renderização de janela/texto usa `SYSCALL_GFX_RECT` e `SYSCALL_GFX_TEXT`.
- Comandos atuais:
  - `HELP`
  - `PWD`
  - `LS [dir]`
  - `CD <dir>`
  - `MKDIR <dir>`
  - `TOUCH <arquivo>`
  - `RM <arquivo|dir>`
  - `CAT <arquivo>`
  - `WRITE <arquivo> <texto>`
  - `APPEND <arquivo> <texto>`
  - `CLEAR`
  - `UNAME`
  - `EXIT`

## Sistema de Arquivos (VFS)

- Estrutura orientada a diretórios e arquivos em árvore.
- Operações de leitura/escrita suportadas no shell.
- Suporte a caminhos relativos e absolutos (ex.: `/home/user`, `../docs`).
- Diretório atual controlado por `CD` e exibido no prompt.
- Arquivos e diretórios iniciais:
  - `/home`
  - `/home/user`
  - `/docs`
  - `/README`
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
