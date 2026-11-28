# VibeOS

VibeOS e um sistema operacional x86 BIOS em 32-bit com bootloader proprio, AppFS modular para apps e uma arquitetura hibrida em estilo microkernel: o kernel ainda concentra mediacao privilegiada e alguns backends de hardware, enquanto `init`, service-hosts e apps sobem por interfaces explicitas de servico.

O repositorio ja nao e mais a demo minima original. Hoje o fluxo real e:

`BIOS -> MBR -> VBR/stage1 -> stage2 -> KERNEL.BIN -> kernel -> init -> AppFS -> userland.app/startx -> shell e apps`

## Aviso

Este repositorio e um projeto experimental. Ha bastante codigo funcional, mas tambem existem partes incompletas, inconsistentes ou ainda em transicao arquitetural.

Leia o projeto como:

- experimento de bootloader + kernel + runtime modular
- base para estudo, depuracao e refatoracao
- sistema em evolucao, nao um OS "acabado"

## Estado atual

Ja esta materialmente funcionando:

- boot BIOS funcional com imagem particionada
- pipeline `MBR -> FAT32 boot -> stage2 -> kernel`
- kernel com scheduler, memoria paginada, ELF loader, VFS, IPC e servicos bootstrap
- apps externos carregados do AppFS com caminho real de `ring3`/CPL3 nas arenas de usuario
- shell externa via `userland.app` carregada do AppFS no boot normal
- desktop grafico, terminal, file manager, editor, task manager, jogos e apps modulares
- scroll wheel do mouse de ponta a ponta no kernel e no desktop
- matriz principal de validacao em QEMU para boot, apps, audio e video

Ainda em fechamento:

- rede real completa
- audio robusto em hardware real, especialmente `compat-azalia`
- video nativo em hardware real fora do QEMU
- SMP estavel em hardware real multiprocessado
- alguns gaps de runtime/POSIX e smoke por app

O plano consolidado atual esta em [docs/FINALIZATION_EXECUTION_PLAN.md](docs/FINALIZATION_EXECUTION_PLAN.md).

## Documentacao principal

Os arquivos tecnicos detalhados em `docs/` estao em ingles. Se voce quer a visao mais fiel ao codigo atual, comece por:

- [docs/README.md](docs/README.md)
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

Planos, migracoes e guidelines historicas agora ficam em [docs/guidelines/](docs/guidelines/), incluindo:

- [docs/guidelines/README.md](docs/guidelines/README.md)
- [docs/guidelines/QUICK_BUILD.md](docs/guidelines/QUICK_BUILD.md)
- [docs/guidelines/MICROKERNEL_MIGRATION.md](docs/guidelines/MICROKERNEL_MIGRATION.md)
- [docs/guidelines/COMPAT_PLAN.md](docs/guidelines/COMPAT_PLAN.md)
- [docs/guidelines/smp.md](docs/guidelines/smp.md)

## Arquitetura

### Modelo atual

- os limites de servico para `storage`, `filesystem`, `video`, `input`, `console`, `network` e `audio` existem e fazem parte do caminho normal
- o kernel ainda executa partes privilegiadas de drivers e backends, especialmente na mediacao de hardware
- apps externos carregados nas arenas AppFS agora sobem com `ring3` real, stack de usuario propria e fault containment por task
- o sistema ainda nao tem isolamento completo por espaco de enderecamento por processo nem extracao total de todos os hosts/servicos para user mode
- na pratica, a arquitetura atual continua hibrida: ha fronteiras de servico reais e apps modulares ja isolados em CPL3, mas varios hosts embutidos e backends privilegiados ainda compartilham estado kernel-side

### Boot

- `boot/mbr.asm`: MBR BIOS
- `boot/stage1.asm`: VBR/FAT32 bootstrap
- `boot/stage2.asm`: loader principal e handoff para o kernel

### Kernel

- `kernel/`: kernel principal
- `kernel/microkernel/`: limites de servico e bridges atuais
- `kernel/process/`: processos e scheduler
- `kernel/memory/`: paging, physmem e heap
- `kernel/drivers/`: input, video, storage, timer, debug, PCI, USB
- `kernel/cpu/`, `kernel/apic.c`, `kernel/smp.c`: topologia, LAPIC e bring-up SMP

### Userland e apps

- `userland/userland.c`: `userland.app`, shell externa autostartada no boot
- `userland/modules/`: runtime e bibliotecas comuns
- `userland/applications/`: desktop e apps nativos/modulares
- `applications/ported/`: utilitarios e apps portados

### Storage em tempo de execucao

Hoje existem dois "mundos" de storage:

1. particao de boot FAT32
2. particao raw de dados para AppFS, persistencia e assets

## Build

Requisitos minimos praticos:

- `nasm`
- `make`
- `python3`
- `qemu-system-i386` ou `qemu-system-x86_64`
- `mkfs.fat` ou equivalente
- `mtools` opcional para operar a FAT por userspace (`mcopy`/`mmd`)

Toolchain:

- recomendado: `i686-elf-*`
- fallback suportado em Linux: toolchain host GNU 32-bit (`gcc`, `ld`, `objcopy`, `nm`, `ar`, `ranlib`)

No macOS, coloque o Homebrew primeiro no PATH para usar o Python com Pillow. A imagem pode ser formatada pelo `newfs_msdos` do sistema. Em Apple Silicon, o QEMU emula o guest x86 de 32 bits.

```bash
brew install make nasm qemu i686-elf-gcc pillow
export PATH="$(brew --prefix)/bin:$PATH"
gmake -j"$(sysctl -n hw.logicalcpu)"
gmake run
```

Build principal:

```bash
make
```

Alvos uteis:

- `make` ou `make all`: build completo da imagem
- `make full`: limpa e recompila tudo
- `make img`: gera a imagem bootavel
- `make imb`: gera a imagem final para gravacao/uso externo
- `make legacy-data-img`: gera so `build/data-partition.img`
- `make clean`: limpa artefatos

Artefatos importantes:

- `build/mbr.bin`
- `build/boot.bin`
- `build/stage2.bin`
- `build/kernel.bin`
- `build/kernel.elf`
- `build/data-partition.img`
- `build/boot.img`
- `build/generated/app_catalog.h`
- `build/lang/userland.app`

Referencia curta de comandos: [docs/guidelines/QUICK_BUILD.md](docs/guidelines/QUICK_BUILD.md).

## Rodando no QEMU

Execucao normal:

```bash
make run
```

O perfil padrao de `make run` agora e propositalmente mais proximo de um notebook antigo classe T61:

- `-cpu core2duo`
- `-smp 2,sockets=1,cores=2,threads=1,maxcpus=2`
- `-machine pc`
- `-vga std`

Exemplos de override:

```bash
make run QEMU_RUN_SMP=1
make run QEMU_RUN_CPU=pentium
make run QEMU_RUN_MACHINE=q35
```

Debugs uteis:

```bash
make run-debug
make run-headless-debug
make run-headless-core2duo-debug
make run-headless-ahci-debug
make run-headless-usb-debug
```

## Fluxo de boot atual

O fluxo esperado hoje e:

1. BIOS carrega a imagem e entra pelo caminho `MBR -> FAT32 boot`.
2. `stage2` prepara o ambiente e entrega controle ao kernel.
3. O kernel sobe servicos bootstrap.
4. O `init` tenta carregar `userland.app` do AppFS.
5. A shell externa entra como caminho normal de boot.
6. `startx` sobe o desktop grafico.

O shell embutido permanece como fallback/rescue path, nao como steady-state esperado.

## Validacao

Alvos uteis:

```bash
make validate-phase6
make validate-smp
make validate-audio-stack
make validate-audio-hda-startup
make validate-gpu-backends
```

Esses fluxos escrevem relatorios em `build/`.

## Hardware real

Para iterar no bootloader sem regravar a imagem inteira:

```bash
make build/stage2.bin build/boot.bin
python3 tools/patch_boot_sectors.py --target /dev/sdX --vbr build/boot.bin --stage2 build/stage2.bin
```

Se voce mudou `kernel.bin`, assets ou o conteudo FAT32/AppFS, gere `build/boot.img` de novo.

## Limitacoes honestas

- BIOS legado apenas, sem UEFI
- o kernel atual nao e um microkernel estrito: existem limites de servico reais, mas parte importante do backend ainda vive em bridges kernel-side
- SMP ainda esta em estabilizacao para hardware real
- rede real completa ainda nao esta fechada
- audio em notebook real ainda exige endurecimento por chipset/backend
- video real fora do QEMU ainda esta em consolidacao
- o VFS do kernel ainda e minimo em varias frentes
- `ring3` real agora existe para apps externos carregados em arenas de usuario, mas nao para todo componente fora do microkernel; hosts e alguns servicos ainda estao em migracao

## Licenca

O repositorio usa GPLv3 no root em [LICENSE](LICENSE).

Arvores importadas de terceiros dentro de `compat/`, `lang/vendor/` e similares podem manter suas licencas originais. Veja tambem [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## English Version

- [README.en.md](README.en.md)
