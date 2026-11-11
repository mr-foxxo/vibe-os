# ABI Improvements Plan

**Status: ALL PHASES COMPLETE (0-10)**

## Objetivo

Fechar as lacunas de ABI do `vibeOS` com compatibilidade progressiva, usando `compat/` como referência de semântica e layout sem quebrar o userland atual.

**Nota:** Todas as fases do plano ABI foram concluídas. As ABIs compartilhadas estão documentadas, versionadas, e validadas com userland real. O próximo trilho (gráfico POSIX/X-like) está liberado para execução.

## Regras

- [x] Manter toda evolução de ABI aditiva, versionada ou com fallback
- [x] Preservar compatibilidade com o userland existente
- [x] Tratar `compat/` como referência de ABI, não como código drop-in
- [x] Documentar versionamento explícito para cada ABI compartilhada nova

## Referências

- [x] `compat/sys/*`
- [x] `compat/lib/libc/include/*`
- [x] `compat/include/*`
- [x] `compat/usr.bin/*`

## Fase 0: Inventário ABI

- [x] Criar inventário inicial em `docs/abi-inventory.md`
- [x] Mapear ELF, processo, headers base, arquivos, `ioctl`, TTY, sockets, sinais, tempo, audio, input, storage e rede
- [x] Expandir o inventário com layouts e contratos compartilhados
- [x] Marcar quais estruturas exigem versão nova em vez de substituição direta

## Fase 1: Headers e tipos base

- [x] Consolidar `errno` em forma BSD/POSIX
- [x] Expandir `fcntl.h` com flags e comandos BSD-shaped
- [x] Publicar `sys/ioctl.h`, `sys/ioccom.h`, `sys/termios.h`, `sys/select.h`, `poll.h`, `sys/time.h`
- [x] Publicar `net/if.h`, `netinet/in.h`, `arpa/inet.h`
- [x] Revisar `sys/stat.h` para evolução binária mais rica sem quebrar o layout legado
- [x] Revisar `signal.h`, `time.h` e `sys/param.h` para cobertura BSD mais completa

## Fase 2: Loader, ELF e ABI binária

- [x] Distinguir layout atual e legado do header AppFS no loader
- [x] Preservar aliases antigos de arena e load address
- [x] Falhar explicitamente para layout AppFS antigo incompatível
- [x] Aceitar `ELFOSABI_FREEBSD` e `ELFOSABI_OPENBSD` nos casos simples de `ELF32/i386`
- [x] Endurecer validação de ELF: `e_ident`, `e_ehsize`, `e_machine`, `PT_LOAD`, alinhamento e entrada
- [x] Rejeitar `PT_INTERP`, `PT_DYNAMIC` e `PT_TLS` até existir backend real
- [x] Registrar metadados ABI no `process_t`
- [x] Definir política formal de versionamento ABI para apps `vibe`
- [x] Planejar o caminho para `ELF64`

## Fase 3: Processo e semântica de syscall

- [x] Expor `getpid()` real na camada compat
- [x] Preservar a ABI de launch existente com fallback para layouts antigos
- [x] Formalizar retorno e erro de syscall em estilo BSD de ponta a ponta
- [x] Revisar `exit`, estados de término e snapshots
- [x] Definir acomodação ABI para `execve` e `waitpid`
- [x] Definir estratégia para `fork` ou equivalente

## Fase 4: Arquivos, descritores e controle

- [x] Consolidar estado compartilhado de descritores na camada compat
- [x] Implementar `open`, `read`, `write`, `close`, `lseek`, `stat`, `fstat`, `lstat`
- [x] Implementar `access`, `creat` e `openat(AT_FDCWD)`
- [x] Implementar `fcntl(F_GETFD/F_SETFD/F_GETFL/F_SETFL/F_DUPFD/F_DUPFD_CLOEXEC/F_GETOWN/F_SETOWN/F_ISATTY)`
- [x] Implementar `flock` e `fcntl(F_GETLK/F_SETLK/F_SETLKW)` com estado real na camada compat
- [x] Implementar `ioctl(FIOCLEX/FIONCLEX/FIONBIO/FIOASYNC/FIONREAD/FIOGETOWN/FIOSETOWN)`
- [x] Revisar `stat` e `off_t` para maior compatibilidade binária BSD
- [x] Definir a política de `ioctl`: nativo, traduzido ou userland
- [x] Revisar locking para conflitos reais entre processos e integração com backend futuro

## Fase 5: TTY e `termios`

- [x] Adicionar estado real de TTY na camada compat
- [x] Implementar `tcgetattr`, `tcsetattr`, `cfmakeraw`, `cfget*speed`, `cfset*speed`
- [x] Implementar `TIOCGETA`, `TIOCSETA*`, `TIOCGWINSZ`, `TIOCSWINSZ`, `TIOCGPGRP`, `TIOCSPGRP`
- [x] Inferir `winsize` a partir de `SYSCALL_GFX_INFO` quando possível
- [x] Dar efeito real a canonical mode e raw mode sobre entrada
- [x] Implementar sinais de terminal
- [x] Planejar pseudo-terminal

## Fase 6: Sockets e rede

- [x] Manter superfície pública BSD-shaped para sockets
- [x] Implementar `socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`
- [x] Completar `setsockopt` e `getsockopt` básicos em `SOL_SOCKET`
- [x] Alinhar `sockaddr`, `msghdr` e `iovec`
- [x] Implementar `sendmsg` e `recvmsg` básicos sem ancillary data
- [x] Implementar `ioctl` básico de interface de rede (`SIOCGIF*`, `ifreq`, `ifconf`)
- [x] Alinhar blocking, non-blocking, `EWOULDBLOCK`, `EINPROGRESS` e `SO_ERROR`

## Fase 7: Tempo, `poll`, `select` e sinais

- [x] Adicionar `time`, `clock_gettime`, `gettimeofday` e `nanosleep`
- [x] Implementar `poll`, `ppoll`, `select` e `pselect` em modo MVP
- [x] Implementar sinais básicos na camada compat: `signal`, `sigaction`, `raise`, `kill`, `sigprocmask`, `sigpending`, `sigsuspend`, `sigwait`, `alarm`, `pause`
- [x] Conectar `poll` e `select` a backends reais de espera do microkernel
- [x] Integrar sinais pendentes aos waits bloqueantes básicos (`poll`/`select`)
- [x] Integrar entrega de sinais com I/O e eventos de processo reais de ponta a ponta
- [x] Deixar `kqueue` para fase posterior

## Fase 8: Audio, input, storage e device control

- [x] Aproximar a superfície pública de `audioio`
- [x] Adicionar caminho explícito de backend de storage para o host de serviço
- [x] Completar mixer ABI e ferramentas de controle
- [x] Revisar contratos de bloco, flush, sync e erros em storage
- [x] Estabilizar melhor semântica de input e eventos
- [x] Revisar superfícies de controle de dispositivo com base em `compat/sys/dev/*`

## Fase 9: ABI de hardware e driver boundary

- [x] Padronizar enumeração PCI com vendor/device, subsystem IDs, class/subclass/prog-if, command/status, BARs e bridges
- [x] Definir contrato para lease de IRQ
- [x] Definir contrato para mapeamento de BAR
- [x] Definir contrato para registro de buffers DMA
- [x] Planejar notificações de hotplug

## Fase 10: Validação

- [x] Validar build completo com `make -j4`
- [x] Validar `build/libcompat.a` com `make compat-build`
- [x] Usar apps simples e ports como indicador de progresso
- [x] Fechar lacunas do `Build.ported.mk` para os ports já exigidos pelo `Makefile`
- [x] Melhorar o caminho AppFS e storage para boot modular
- [x] Rodar `validate-startx-800x600` com melhora material do cenário
- [x] Transformar cenários ABI em suíte estável de regressão
- [x] Validar utilitários BSD representativos sem shim especial por app
- [x] Validar software interativo mais pesado como `mg` e `vi` via aliases compat atuais

## Prioridade Atual

- [x] Manter a prioridade em fechar o plano ABI antes de abrir a trilha gráfica POSIX/X-like
- [x] Fechar evolução binária de `stat` e semântica de FD
- [x] Dar efeito real a `termios` sobre entrada e sinais de terminal
- [x] Formalizar modelo de erro e retorno de syscall
- [x] Completar ABI de sockets e rede avançada (`ioctl`, ancillary data, casos avançados`)
- [x] Integrar sinais básicos ao restante do runtime bloqueante e eventos reais
- [x] Consolidar fronteiras ABI de hardware e driver

## Próximo Trilho

- [x] Liberar a execução de `docs/posix-graphics-xlike-plan.md` depois que a validação final da Fase 10 ficar fechada

## Definição de pronto

- [x] ABIs compartilhadas documentadas e versionadas
- [x] Kernel aceitando caminhos legados e novos sem quebrar o userland
- [x] Programas BSD simples rodando sem shim especial por app
- [x] Serviços do microkernel expondo contratos estáveis e previsíveis
- [x] Evolução futura protegida por testes de regressão ABI
