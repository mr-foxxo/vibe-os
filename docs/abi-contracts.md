# ABI Contracts

Contratos formais para fechar as pendencias das fases 2 a 10 do plano em [abi-improvements.md](./abi-improvements.md), sempre usando `compat/` como referencia de forma, semantica e evolucao, mas preservando a ABI ja publicada pelo `vibeOS`.

## Regras globais

- A ABI publica do `vibeOS` continua append-only, versionada ou com fallback explicito.
- `compat/` e referencia normativa de semantica BSD/POSIX, nao codigo drop-in.
- Onde o `vibeOS` ja publicou um layout menor ou diferente, a compatibilidade binaria atual tem precedencia e a forma BSD entra por ABI paralela.
- A primeira forma publicada de qualquer fronteira nova de hardware, evento ou controle ja nasce com versao explicita.

## Fase 2: Loader, ELF e ABI binaria

Referencias em `compat/`:
- `compat/sys/sys/exec_elf.h`
- `compat/sys/kern/kern_exec.c`

Contrato fechado:
- `AppFS` continua com dois layouts aceitos: legado (`struct vibe_app_header_legacy`) e atual (`struct vibe_app_header`), ambos versionados por `abi_version`.
- Apps `vibe` usam a politica abaixo:
  - ABI 1: cabecalho legado, sem `load_address`, com aliases historicos de arena/load address preservados.
  - ABI 2: cabecalho atual, com `load_address` explicito e sem remover os aliases antigos.
  - ABI futura: qualquer crescimento estrutural do header deve ser append-only e exigir novo `abi_version`.
- O loader ELF continua focado em `ELF32/i386` simples, aceitando `ELFOSABI_NONE`, `ELFOSABI_GNU`, `ELFOSABI_FREEBSD` e `ELFOSABI_OPENBSD` nos casos ja endurecidos em `kernel/exec/elf_loader.c`.
- `PT_INTERP`, `PT_DYNAMIC` e `PT_TLS` permanecem rejeitados ate existir backend real de dynamic linker/TLS.

Politica formal de versionamento ABI para apps `vibe`:

| ABI | Header | Garantia | Regra de evolucao |
| --- | --- | --- | --- |
| 1 | `struct vibe_app_header_legacy` | boot de apps legados | apenas fallback, sem reinterpretar campos |
| 2 | `struct vibe_app_header` | ABI atual | crescimento append-only, mantendo tamanhos antigos validos |
| 3+ | reservado | novos contratos explicitos | publicar novo `abi_version` antes de mudar layout compartilhado |

Caminho para `ELF64`:
- fase 1: manter `ELF64` explicitamente fora do loader atual e documentar essa rejeicao como comportamento suportado
- fase 2: publicar `PROCESS_ABI_ELF64` e structs/versionamento paralelos sem tocar no caminho `ELF32`
- fase 3: subir um loader `ELF64` separado, com validacao propria de `Elf64_Ehdr`/`Elf64_Phdr`
- fase 4: so depois disso considerar `ET_DYN`, `PT_INTERP` e TLS reais

## Fase 3: Processo e semantica de syscall

Referencias em `compat/`:
- `compat/sys/kern/kern_exit.c`
- `compat/sys/kern/kern_fork.c`
- `compat/sys/kern/kern_exec.c`
- `compat/sys/sys/signal.h`

Contrato fechado:
- A superficie syscall atual continua no modo legado de `eax` unico:
  - sucesso: `0` ou valor nao negativo
  - falha: `-1`
  - `errno`: responsabilidade da camada libc/compat, nao do trap frame atual
- Para ABIs futuras, o caminho BSD-shaped preferido e:
  - manter retorno bruto no registrador
  - entregar `errno` negativo ou canal secundario somente em ABI nova, nunca mudando silenciosamente a ABI atual
- Encerramento de processo continua publicado via lifecycle/event stream e snapshots do scheduler.

Politica de `exit`, termino e snapshots:
- a terminacao continua sendo observavel por `MK_TASK_EVENT_TERMINATED`
- snapshots continuam sendo a fonte estavel de introspecao de runtime
- quando o `vibeOS` publicar status de encerramento, isso entra como campo novo append-only em evento/snapshot ABI novo, sem reciclar campos existentes

Acomodacao ABI para `execve` e `waitpid`:
- `execve` no `vibeOS` atual e representado por `mk_launch_descriptor`/`mk_launch_context`
- a forma BSD futura deve ser um wrapper sobre launch:
  - `execve(path, argv, envp)` traduz para descriptor versionado
  - `envp` so entra quando existir armazenamento/env runtime real
- `waitpid` no `vibeOS` atual e acomodado por:
  - subscription em `SYSCALL_TASK_EVENT_SUBSCRIBE`
  - recepcao via `SYSCALL_TASK_EVENT_RECV`
  - correlacao por `pid` e snapshots
- quando `waitpid` virar syscall publica, ela deve ser um facade sobre esse mecanismo e publicar status/versionamento explicitos

Estrategia para `fork`:
- `fork` classico permanece fora de escopo da ABI atual
- o equivalente suportado continua sendo launch de novo task/processo com contexto explicito
- qualquer suporte futuro deve entrar como ABI separada, provavelmente `posix_spawn`/`vfork`-shaped antes de `fork` completo

## Fase 4: Arquivos, descritores e controle

Referencias em `compat/`:
- `compat/sys/sys/stat.h`
- `compat/sys/sys/ioctl.h`
- `compat/sys/sys/ttycom.h`

Contrato fechado:
- `struct stat` legado de dois campos continua congelado para nao quebrar userland atual.
- `struct stat_compat` e a forma BSD-shaped para evolucao futura.
- `off_t` atual permanece `long` assinado em i386, sem promover silenciosamente para 64 bits enquanto a ABI legada existir.

Politica de `stat` e `off_t`:
- ABI legado:
  - `struct stat`
  - `off_t` de largura atual
- ABI compat/futura:
  - `struct stat_compat`
  - crescimento por API ou versao nova, nao por substituicao do layout legado
- quando houver `stat64`/`off64_t`, isso entra por nome/versionamento novo

Politica de `ioctl`:

| Classe | Politica |
| --- | --- |
| `FIOC*`, `FION*`, `TIOCGETA`, `TIOCSETA*`, `TIOCGWINSZ`, `TIOCSWINSZ`, `TIOCGPGRP`, `TIOCSPGRP` | nativo/compat, com layout BSD-shaped ja publicado |
| `SIOCGIF*` e afins | traduzido para backend de rede |
| controles ainda sem backend real | falha explicita com `ENOTTY`/`ENOSYS`, nunca fakeando estado persistente que o kernel nao possui |
| superficies de driver futuras | userland/control-plane versionado desde a primeira publicacao |

Politica de locking:
- o estado atual na camada compat continua valido como semantica local
- conflitos reais entre processos devem ser centralizados em owner state compartilhado, nao em shim por descritor
- integracao com backend futuro deve preservar os comandos publicados (`flock`, `F_GETLK`, `F_SETLK`, `F_SETLKW`)

## Fase 5: TTY e `termios`

Referencias em `compat/`:
- `compat/sys/sys/termios.h`
- `compat/sys/sys/ttycom.h`
- `compat/lib/libutil/login_tty.c`

Contrato fechado:
- `struct termios` e `struct winsize` publicados em `headers/sys/*` seguem shape BSD e nao devem mudar de layout.
- Sinais de terminal entram como semantica, nao como troca de struct.
- pseudo-terminal nao entra como hack em `tty` atual; nasce como device/control-plane proprio.

Plano normativo:
- `VINTR` -> `SIGINT`, `VQUIT` -> `SIGQUIT`, `VSUSP` -> `SIGTSTP` quando o runtime de sinais puder entregar ponta a ponta
- `TIOCGPGRP`/`TIOCSPGRP` ficam como ownership de foreground group
- PTY futuro:
  - master/slave
  - `login_tty`/`forkpty`-shaped por cima
  - sem quebrar o TTY simples atual

## Fase 6: Sockets e rede

Referencias em `compat/`:
- `compat/sys/sys/socket.h`
- `compat/sys/net/if.h`
- `compat/sys/netinet/in.h`

Contrato fechado:
- `sockaddr`, `sockaddr_storage`, `msghdr`, `iovec` e `ifreq` permanecem BSD-shaped.
- ancillary data continua fora do contrato ate existir backend real.
- `SO_ERROR`, blocking/non-blocking e `EWOULDBLOCK`/`EINPROGRESS` continuam sendo a semantica publica de erro.

Adicoes aditivas registradas (append-only, sem tocar structs existentes):
- `SYSCALL_NETWORK_SOCKET_LIST = 232` + `MK_MSG_NET_SOCKET_LIST = 110` (item 1.5):
  iteracao `index -> struct mk_network_socket_list_entry` (proto/state/recv_ready/
  portas/ip remoto/pid), codigos estaveis espelhados em
  `headers/kernel/microkernel/network.h` e `lang/include/vibe_network_client.h`;
  reply com payload vazio significa "passou do fim" (mesmo padrao do SCAN).

Casos avancados:
- control messages (`cmsghdr`) so passam a valer com backend real e ABI nova se necessario
- `ioctl(SIOCGIF*)` continua como traducao para o estado da stack de rede do microkernel

## Fase 7: Tempo, `poll`, `select` e sinais

Referencias em `compat/`:
- `compat/sys/sys/select.h`
- `compat/sys/sys/poll.h`
- `compat/sys/sys/signal.h`
- `compat/include/poll.h`
- `compat/include/sys/select.h`

Contrato fechado:
- `fd_set`, `timeval`, `timespec`, `pollfd` e `sigaction` continuam com shape BSD/POSIX.
- waits bloqueantes podem ser interrompidos por sinais pendentes quando houver entrega ponta a ponta.
- `kqueue` fica explicitamente adiado para ABI futura.

## Fase 8: Audio, input, storage e device control

Referencias em `compat/`:
- `compat/sys/sys/audioio.h`
- `compat/sys/sys/ioctl.h`

Contrato fechado:
- audio segue vocabulario BSD-shaped onde ja publicado, sem fingir mixer/stream que o backend nao implementa
- storage deve diferenciar erro de transporte, erro de midia e indisponibilidade de backend
- input e eventos devem permanecer append-only/versionados

Politicas:
- mixer ABI: controles enumerados, level/mute/enum publicados por ids estaveis
- storage: `flush`/`sync`/erros entram como semantica de backend e nunca por reinterpretacao de retorno antigo
- surfaces de controle de device so entram quando houver backend rastreavel no microkernel

## Fase 9: ABI de hardware e driver boundary

Referencias em `compat/`:
- `compat/sys/dev/pci/pcireg.h`
- `compat/sys/dev/pci/pcivar.h`

Contrato fechado:
- qualquer ABI de hardware nova nasce versionada
- enumeracao PCI minima publica:
  - vendor/device
  - subsystem vendor/device
  - class/subclass/prog-if
  - command/status
  - BARs
  - bridge metadata quando aplicavel
- leases de IRQ, mapeamento de BAR, registro DMA e hotplug so entram como contratos separados, nunca misturados com structs anteriores

## Fase 10: Validacao e regressao

Suíte estavel proposta:
- validacao estatica de superficie ABI em `tools/validate_abi_contracts.py`
- boot e bring-up geral em `tools/validate_phase6.py`
- superficie de rede em `tools/validate_network_surface.py`
- audio/video/GPU permanecem nas suites ja existentes do repositorio

Meta de regressao:
- toda ABI nova ou paralela precisa de pelo menos uma verificacao estatica ou scenario testavel
- toda referencia normativa em `compat/` precisa aparecer documentada neste arquivo ou em `docs/abi-inventory.md`
