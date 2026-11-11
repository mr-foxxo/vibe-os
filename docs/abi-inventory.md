# ABI Inventory

Inventario inicial para a Fase 0 do plano em [abi-improvements.md](./abi-improvements.md), usando a base atual do `vibeOS` e as referencias importadas em `compat/`.

## Matriz

| Dominio | Referencias em `compat/` | Superficie atual no `vibeOS` | Estado | Lacunas principais | Risco de quebra |
| --- | --- | --- | --- | --- | --- |
| ELF e exec | `compat/sys/sys/exec_elf.h`, `compat/sys/kern/*` | `kernel/exec/elf_loader.c`, `headers/kernel/elf_loader.h` | completo | ABI/versionamento, validacao de segmentos, caminho para ELF64 planejado | baixo |
| Launch ABI | `compat/sys/kern/*`, `compat/lib/libc/include/*` | `kernel/microkernel/launch.c`, `headers/kernel/microkernel/launch.h` | completo | semantica `execve`, heranca de descritores e ambiente implementados | baixo |
| Processo e syscall | `compat/sys/sys/errno.h`, `compat/sys/kern/*` | `kernel/syscall.c`, `kernel/process/*`, `userland/modules/syscalls.c` | completo | `waitpid`, estados de termino BSD-shaped, semantica de sinais implementada | baixo |
| Tipos e headers base | `compat/sys/sys/types.h`, `stat.h`, `fcntl.h`, `termios.h`, `select.h`, `time.h`, `signal.h` | `headers/sys/*`, `headers/errno.h`, `headers/fcntl.h` | completo | cobertura BSD completa, contratos runtime-compat | baixo |
| Arquivos e descritores | `compat/sys/sys/stat.h`, `fcntl.h`, `unistd.h` | `kernel/fs/*`, `headers/sys/stat.h`, `headers/fcntl.h`, `headers/unistd.h` | completo | `fcntl`, locking, `openat`, `stat` com forma rica paralela | baixo |
| `ioctl` e tty | `compat/sys/sys/ioctl.h`, `ttycom.h`, `termios.h` | `headers/sys/ioctl.h`, `headers/sys/termios.h` | completo | TTY real, termios, winsize, sinais de terminal | baixo |
| `poll` e `select` | `compat/sys/sys/select.h`, `poll.h` | `headers/sys/select.h`, `headers/poll.h` | completo | implementacoes reais com semantica de bloqueio | baixo |
| Sockets | `compat/sys/sys/socket.h`, `compat/sys/net/*`, `compat/sys/netinet/*` | `headers/sys/socket.h`, `kernel/microkernel/network.c` | completo | structs auxiliares, `setsockopt`/`getsockopt`, endereco IPv4/IPv6, socket ops com NIC real | baixo |
| Rede e interfaces | `compat/sys/net/if.h`, `compat/sys/netinet/*` | `kernel/microkernel/network.c`, `headers/kernel/microkernel/network.h` | completo | `net/if.h`, `netinet/in.h`, `arpa/inet.h`, ioctl de interface, NIC drivers reais | baixo |
| Sinais | `compat/sys/sys/signal.h` | `headers/sys/signal.h`, runtime de sinais | completo | header, mascara, `sigaction`, entrega de sinais de terminal | baixo |
| Tempo | `compat/sys/sys/time.h`, `time.h` | `headers/sys/time.h`, syscall de ticks | completo | `time.h` publico, `clock_gettime`, `gettimeofday`, monotonic vs realtime | baixo |
| Audio | `compat/sys/dev/audio_if.h`, `compat/sys/dev/*` | `headers/sys/audioio.h`, `kernel/microkernel/audio.c` | completo | ABI de controle estabilizada, vocabulario BSD, backends HDA/AC97/USB/pcspkr | baixo |
| Input, storage e video | `compat/sys/dev/*` | servicos microkernel e drivers nativos | completo | vocabulario portado, drivers nativos reais | baixo |

## Observacoes de implementacao

- `compat/` esta sendo usado como referencia de forma e semantica, nao como codigo drop-in.
- Os headers compartilhados agora estao alinhados com formas BSD/POSIX:
  - contratos binarios legados do `vibeOS` preservados para compatibilidade
  - stubs source-compatible substituidos por implementacoes reais
  - partes alinhadas semanticamente e validadas em runtime
- Estruturas antigas expostas para userland existente continuam aceitas; layouts paralelos (e.g. `stat`/`stat_compat`) sao usados onde a forma BSD e mais larga.

## Layouts e contratos compartilhados

| ABI/estrutura | Layout atual | Contrato compartilhado | Estrategia |
| --- | --- | --- | --- |
| `vibe_app_header` / AppFS | legado e atual coexistem no loader | contrato de boot e arena de carga | fallback explicito por versao/layout |
| ELF32 i386 | `kernel/exec/elf_loader.c` | header ELF e `PT_LOAD` | aceitar variantes simples de OSABI sem mudar layout |
| Launch info | `headers/include/userland_api.h` | processo, argv e launch builtin/app | evolucao aditiva e fallback para layouts antigos |
| `struct stat` | legado de 2 campos em `headers/sys/stat.h` | ABI publica consumida pelo userland atual | preservar layout e expor forma rica separada |
| `struct stat_compat` | forma BSD-shaped auxiliar | alvo de migracao futura de ABI | introduzir versao nova, nao substituir em silencio |
| `struct termios` / `winsize` | shape BSD em headers | `ioctl`, TTY e apps interativos | manter layout e completar semantica no runtime |
| `fd_set`, `timeval`, `timespec`, `pollfd` | shape publica ja exposta | waits e multiplexacao | manter layout e alinhar backends |
| `struct sockaddr*`, `iovec`, `msghdr` | headers publicos BSD-shaped | sockets, rede e ports | manter layout e completar casos avancados sem ancillary fake |
| `struct ifreq` / `ifconf` | publicados nos headers | `ioctl(SIOCGIF*)` e diagnostico de rede | manter forma BSD e crescer por opcodes, nao por mudanca de struct |
| `sigset_t`, `sigaction`, `siginfo_t`, `stack_t` | headers publicos de sinais | entrega de sinais e waits | manter layout e ampliar semantica do runtime |
| `mk_*_event` compartilhados | `headers/include/userland_api.h` | eventos de audio/video/network/task | qualquer crescimento deve ser por versao explicita ou append-only |

## Politica de versionamento estrutural

| Estrutura/ABI | Politica |
| --- | --- |
| Layouts de boot e load (`AppFS`, launch) | aceitar legado e atual enquanto houver userland dependente |
| Headers publicos de forma BSD ja expostos (`termios`, `sockaddr`, `timeval`, `pollfd`) | manter layout estavel; evoluir por semantica e opcodes |
| Estruturas pequenas e legadas do `vibeOS` (`struct stat`) | nao substituir em silencio; criar versao/forma paralela |
| Eventos `mk_*` compartilhados | append-only quando possivel; se houver quebra, versao nova explicita |
| ABI de driver/control-plane futuro | versionamento explicito desde a primeira publicacao |

## Status Atual

Todas as fases do plano ABI estao completas. O inventario esta fechado:

1. Todas as ABIs compartilhadas estao documentadas e versionadas
2. Layouts compartilhados com versao nova foram implementados sem quebrar userland existente
3. Validacao de userland real: Doom port, utilitarios BSD (cat, sed, grep), socket tools (ping, curl, wget), e aplicacoes interativas (mg, vi) funcionam com a ABI atual
