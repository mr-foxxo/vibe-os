[English](AGENTS.md) | adaptação do contrato de agente do Kof4j (KofLang/Kof4j) para o fluxo real do VibeOS

# AGENTS.md — VibeOS

last: contrato do agente baseado no fluxo Kof4j + estado real do repo (main unica, planos em docs/)
doing: fechamento de finalizacao — ver `docs/FINALIZATION_EXECUTION_PLAN.md`
next: proxima prioridade aberta em `docs/ACTION_PLAN.md`
location: repository
state: active

intent: agent-operating-contract

## Constraint

- **regra-universal**: o kernel nunca pode quebrar o userland (`universal-rule.md`). Se conflito entre simplificar o kernel e preservar o userland, preservar o userland.
- no-stubs: nada de placeholder, TODO fingindo implementacao, retorno-zero de fachada. Unsupported = diagnostico explicito (`ENOTTY`/`ENOSYS`), nunca estado falso.
- zero-regression: toda entrega roda sem quebrar boot, apps existentes e ABI publicada.
- abi-additive-only: syscalls, estruturas, formatos de mensagem e handshakes de boot sao append-only, versionados ou com fallback (`docs/abi-contracts.md`). Nunca remover suporte silenciosamente.
- build-before-delivery: compilar antes de entregar; nunca claimar verde sem execucao real.
- validate-with-change: mudanca andamental exige matriz `make validate-*` correspondente rodando limpa.
- honest-docs: documentar limitacao real em vez de fingir suporte ("Limitacoes honestas" no README e o padrao).
- maintainer-controls-design: arquitetura, ABI nova e freeze de comportamento sao decisao da mantenedora.
- maintainer-controls-main-merge: merge em `main` e promocao de release sao da mantenedora.

## Decision

- D-BRANCH-MAIN: `main` e a unica branch de longa duracao. Features vivem em branches curtas, entram em `main` via merge/PR da mantenedora, e a branch e apagada logo apos o merge.
- D-PLAN-DOCS: o trabalho em aberto vive em `docs/ACTION_PLAN.md` (prioridades com checkbox) e `docs/FINALIZATION_EXECUTION_PLAN.md` (pendencia real + "Definicao Pratica de Acabou"). Bugs conhecidos ficam em `docs/KNOWN_BUGS.md` com observacao/causa raiz/corricao.
- D-VALIDATE-GATE: nada entra em `main` quebrando os perfis QEMU validados (`make run`, `run-headless-*`, `validate-*`).
- D-FOXXO-ASYNC-HOOK: integracao de agentes externos roda via `foxxod` + `SYSCALL_MESSAGE_POST/RECV` (112/113), como task separada; nunca dentro dos loops criticos de `inputsvc`, `videosvc` ou `init`. Ver `docs/guidelines/FOXXO_KERNEL_ASYNC_HOOK.md`.
- D-NO-MANDATORY-REBUILD: evolucao que exigir rebuild do userland para bootar e proibida por padrao; usar nova versao de ABI em paralelo.
- D-DOC-LANG: docs tecnicas de referencia em ingles (`docs/`); planos e contratos de execucao em portugues (`docs/guidelines/`, ACTION_PLAN, FINALIZATION).

---

## Operating loop

mode: autonomous

loop:

- le `AGENTS.md`, `universal-rule.md`, `docs/ACTION_PLAN.md`, `docs/FINALIZATION_EXECUTION_PLAN.md`, `docs/KNOWN_BUGS.md`
- `git fetch` + `git pull --rebase --autostash`; inspeciona `git log` para ver o que entrou
- garante que o trabalho comeca em `main` (ou branch de feature nova a partir dela)
- escolhe a prioridade aberta de maior valor no ACTION_PLAN / FINALIZATION
- executa um escopo coeso completo (uma responsabilidade por unidade)
- `make` (ou `make full`) — build limpo obrigatorio antes de qualquer claim
- roda o sistema no perfil certo (`make run`, headless, GPU/audio/usb conforme a area)
- roda a matriz `make validate-*` aplicavel e confere o relatorio em `build/`
- atualiza o plano/bug doc no mesmo commit (checkbox, causa raiz, evidencia)
- commit + push; se era branch de feature: merge em `main` via PR/manutencao e apaga a branch
- re-le o plano; continua

rule:

- nunca pergunte quando evidencia do repo responde
- nunca termine um turno com unidade nao commitada
- nunca termine um turno apenas com resumo
- concluido = provado (log, relatorio, boot real), nao pretendido
- uma tarefa em progresso por vez

stop:

- decisao de arquitetura/ABI nova requer mantenedora
- mudanca que seria a primeira quebra de userland (regra universal)
- validacao impossivel sem hardware real e sem cobertura QEMU — registre no plano e pare
- regressao externa sem explicacao — catalogue em `docs/KNOWN_BUGS.md` antes de mexer

---

## Authority

authority:

- maintainer: melmonfre
- architecture: maintainer
- abi-change / behavior-freeze: maintainer
- main-merge: maintainer

rule:

- IA acelera implementacao; IA nao define arquitetura nem semantica de ABI
- toda entrega carrega prova (build + validacao + relatorio)
- mudancas em shared layout (BOOTINFO, AppFS header, structs de syscall) so com versao nova publicada

---

## Multi-agent state

claim:

- le os planos antes de comecar; item ja marcado `[x]` ou em progresso nao e seu
- um agente externo (ex.: foxxo) integra via `foxxod`/mensagens, nunca via edicao direta de loops criticos do kernel

sync (before commit):

- `git fetch` + `git pull --rebase --autostash`
- `grep` nos arquivos tocados por `^<<<<<<<`
- re-rodar os gates na arvore pos-rebase

conflict:

- preserve os dois lados; reforce a mudanca propria por cima
- nunca `checkout --ours` / `--theirs` em bloco inteiro
- conteudo rebasado nao e "stale": trate como entrada nova

collision:

- dois agentes nunca na mesma giant file (`kernel/entry.c`, `userland/userland.c`, Makefiles) sem coordenacao previa

---

## Build e verificacao

toolchain:

- `nasm`, `make`, `python3`, `qemu-system-i386|x86_64`, `mkfs.fat`
- recomendado `i686-elf-*`; fallback host gcc 32-bit e suportado

build:

- `make` / `make all` — imagem completa
- `make full` — limpa e recompila tudo
- `make img` / `make imb` — imagem bootavel / imagem final para gravacao
- `make legacy-data-img` — `build/data-partition.img` (AppFS + persist + assets)
- `make clean`

run:

- `make run` — perfil T61-ish (`-cpu core2duo -smp 2 -machine pc -vga std`)
- override: `QEMU_RUN_SMP=1 QEMU_RUN_CPU=pentium QEMU_RUN_MACHINE=q35`
- debug: `make run-debug`, `make debug` (gdb stub `:1234` + `gdb build/kernel.elf`)
- regressao headless: `make run-headless-{core2duo,pentium,atom,ahci,usb,virtio-net,audio}-debug`

validate (relatorios em `build/*.md`):

- broad boot/CPU: `make validate-phase6`
- SMP: `make validate-smp`
- GPU: `make validate-gpu-backends`, `make validate-video`, `make validate-startx-*`
- audio: `make validate-audio-stack`, `make validate-audio-hda-*`, `make validate-audio-matrix`
- network: `make validate-network-surface`, `make validate-network-terminal-surface`, `make validate-network-datapath` (perfil virtio; `NIC=e1000` ou `NIC=rtl8139` rodam o gate do driver correspondente)
- ABI: `make validate-abi-compat-surface`, `make validate-abi-final` (static check de `docs/abi-inventory.md`)
- apps: `make validate-modular-apps`, `make validate-craft-*`, `make validate-doom-*`

hardware real:

- loop de baixo desgaste: `make build/stage2.bin build/boot.bin` + `python3 tools/patch_boot_sectors.py --target /dev/sdX --vbr build/boot.bin --stage2 build/stage2.bin` (seco, `--dry-run` primeiro; `--mbr` so se `boot/mbr.asm` mudou)
- rebuild completo de `boot.img` apenas quando conteudo FAT32/AppFS mudou; ao patchear stage2 sem `boot.bin`, o sector count nao pode mudar

---

## Quality gate

flow:

- reproduza -> corrija causa raiz -> prove em QEMU -> rode a matriz aplicavel -> busque vizinhos -> commit -> push

- Q0: bug corrigido na causa raiz; cenario de reproducao conhecido
- Q1: cada fix tem prova (relatorio `build/`, log serial, ou validacao); teste e implementacao no mesmo commit
- Q2: build verde antes do push (`make` completo, nao parcial)
- Q3: cobertura quando aplicavel — boot com/sem desktop, safe mode, rescue shell, modo de video, SMP on/off, audio, storage AHCI/ATA
- Q5: sem verde falso; assert fraco ou skip escondido e proibido
- Q7: stub proibido; gap real e registrado como diagnostico honesto + entrada em `docs/KNOWN_BUGS.md` ou `docs/abi-inventory.md`

---

## Invariantes arquiteturais (mudar so com decisao da mantenedora)

boot contract:

- `BIOS -> MBR -> VBR/stage1 -> stage2 -> KERNEL.BIN -> kernel -> init -> AppFS -> userland.app/startx`
- enderecos load-bearing: MBR realocado `0x7000`, stage2 `0x9000`, kernel `0x10000`, `BOOTINFO` `0x8D00` (layout packed em `headers/kernel/bootinfo.h` e contrato de boot — mudar = nova versao)
- duas particoes: FAT32 de boot + particao raw de dados (AppFS, persistencia, assets)

memory / apps:

- arenas de app fixas em `0x10000000` (main), `0x14000000` (desktop), `0x18000000` (boot)
- apps externos sobem CPL3/ring3 com stack propria; fault containment por task
- reservas: fim do kernel, stack de userland, heap guard, framebuffer de boot, AP trampoline fora do alocador fisico

abi:

- headers de app versionados (`vibe_app_header_legacy` = ABI 1 fallback; `vibe_app_header` = ABI 2); ABI 3+ reservada
- syscall legacy retorna `eax` (>=0 sucesso, -1 erro); negative-errno BSD so em ABI nova paralela
- `struct stat` legado de 2 campos congelado; `struct stat_compat` e o caminho BSD
- ioctl sem backend falha `ENOTTY`/`ENOSYS` — nunca finge estado persistente

smp:

- SMP so liga com `BOOTINFO_FLAG_EXPERIMENTAL_SMP` (toggle `M` no stage2); padrao BSP-only com fallback PIC
- falha de AP nunca derruba o BSP

video/gpu:

- backends DRM nativos com fallback automatico para `fast_lfb`/VESA; i915 commit gated por `INTEL_I915_EXPERIMENTAL_COMMIT`
- rings de eventos com backpressure — caminho ativo nao pode overflow silencioso

microkernel:

- estado hibrido assumido: fronteiras de servico reais, hosts embutidos ainda kernel-side; `backend-shim` nao existe em steady state
- migracoes continuam documentadas em `docs/guidelines/MICROKERNEL_MIGRATION.md` (arquivo raiz `docs/MICROKERNEL_MIGRATION.md` e pointer stub — nao duplicar)

---

## Documentation state model

- `docs/*.{md}` (ingles): referencial implementado/validado — comece por `docs/README.md`
- `docs/guidelines/` (portugues): planos, migracoes, contratos de execucao, QUICK_BUILD
- `universal-rule.md`: regra-mestre — sempre vigente
- plano implementado e validado -> consolide no doc de referencia; conclusao nunca permanece só como plano
- nao crie documento de planejamento para fugir da implementacao
- pointer stubs mantidos onde o caminho antigo era referenciado (no full-copy duplicado)

---

## Commit e entrega

before_commit:

- Q0–Q7 do gate acima satisfeitos
- build completo verde + matriz aplicavel rodando
- arvore pos-rebase inspecionada (sem `<<<<<<<`)
- plano/bug doc atualizado

commit:

- unidade coesa completa; implementacao + prova + doc no mesmo commit
- mensagem diz o que e por que, com SHA/causa quando for fix

push:

- push direto em branch de trabalho; `main` so atraves do fluxo da mantenedora
- apos merge, apagar a branch de feature (padrao atual do repo)

---

## Final self-check

ready:

- compila
- bootou no QEMU no perfil aplicavel
- matriz `validate-*` aplicavel rodou limpa
- causa raiz corrigida, nao sintoma
- ABI intacta ou nova versao publicada (nunca quebra silenciosa)
- userland existente nao quebrou (regra universal)
- doc/plano atualizado no mesmo commit
- sem stub, sem verde falso, sem conflito de merge
- remote sincronizado; branch antiga deletada

if_any_false: not-ready — corrija antes de entregar

## Cheat sheet

- regra-mestre: `O kernel serve o userland. Se o kernel quebra o userland, o kernel esta errado.`
- fluxo: `BIOS -> MBR -> VBR/stage1 -> stage2 -> KERNEL.BIN -> init -> AppFS -> userland.app/startx`
- build: `make` — roda: `make run` — prova: `make validate-<area>` — relatorio: `build/*.md`
- antes de commit: `git fetch && git pull --rebase --autostash` + grep por `<<<<<<<`
- nunca: `--ours`/`--theirs` em conflito inteiro, stub, ABI quebrada, plano novo sem implementacao
