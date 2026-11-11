# Plano Consolidado de Finalizacao do VibeOS

> **CONSOLIDADO (15/10):** o backlog em aberto foi absorvido por
> [guidelines/DEFINITIVE_CLOSURE_PLAN.md](guidelines/DEFINITIVE_CLOSURE_PLAN.md)
> (ondas 0–9, regra anti-stub). Este arquivo permanece como registro do estado
> de finalizacao; trabalho novo vai no plano novo.

Este e um plano ativo de execucao. Ele complementa a documentacao tecnica em `docs/` e o backlog arquitetural de longo prazo em [guidelines/MICROKERNEL_MIGRATION.md](guidelines/MICROKERNEL_MIGRATION.md).

Data da consolidacao: 2026-06-13

Este arquivo agora lista apenas o que realmente falta fazer.
Tudo que ja foi entregue, validado ou virou historico deve ficar nos documentos especificos, especialmente em `docs/MICROKERNEL_MIGRATION.md`.

## Status Atual

> REVISADO 2026-10-12 contra evidencia verificada (auditoria de codigo +
> matrizes de validacao + README). As linhas abaixo marcadas "parcial/aberta"
> estavam CONCLUIDA sem evidencia no repo (matriz de audio 0/20 preenchida,
> e1000/rtl8139 sem callers, Wi-Fi placeholder, SMP com corrupcao de memoria
> aberta). Fonte de prioridades agora: docs/guidelines/COMPLETION_ACTION_PLAN.md.

| Area | Status |
| --- | --- |
| Rede em QEMU (virtio: DHCP/ARP/ICMP client + responders, loopback) | [x] FUNCIONAL em QEMU user-net — `make validate-network-datapath` PASS (dhcp bound, gateway ICMP roundtrip, inbound ARP/ICMP responders, TX drop=0; pcap evidence) |
| Rede em QEMU — UDP/TCP client | [ ] UDP = DHCP only; TCP SYN/ACK path existe, sem validacao end-to-end (P3) |
| Rede real utilizavel (NICs adicionais, WiFi, robustez) | [ ] PARCIAL/ABERTA — ver COMPLETION_ACTION_PLAN P3 |
| Audio em QEMU (HDA/AC97/pcspkr playback) | [x] VALIDADO via validate-audio-* |
| Audio real em hardware | [ ] ABERTA — AUDIO_HARDWARE_VALIDATION_MATRIX.md 0/20 linhas preenchidas |
| Video real em hardware | [ ] ABERTA — backends i915/radeon/nouveau sem evidencia de silicio real no repo |
| Migracao microkernel (Fases F/G) | [x] CONCLUIDA |
| USB mass-storage runtime | [x] CONCLUIDA |
| Runtime / POSIX / Compat | [x] CONCLUIDO |
| Validacao modular final | [x] CONCLUIDA (health telemetry, VT console, boot chain validation script) |
| SMP robusto em 2+ CPUs | [ ] ABERTA — bring-up OK; scheduler por-CPU gated e com corrupcao de memoria em investigacao (COMPLETION_ACTION_PLAN P4) |

## Pendencias Reais

### 6. Runtime / compat / POSIX — CONCLUIDO

- POSIX.1-2017 completo: 81/84 headers, 46+ implementacoes em `compat/src/posix/`
- `fork`, `execve`, `waitpid`, `pipe`, `mmap`, `sbrk`, signal completo, poll/select
- libc completo: string, stdlib, stdio, math, wchar, locale, regex
- System V IPC (msg/sem/shm), AIO, mqueue, semaphore, spawn, wordexp
- Threads reais: pthread create/join/mutex/cond/rwlock/barrier/spin/TLS
- PCI bus framework, DRM backends ativos por padrao
- Shell BSD portado (pdksh 23kloc) com 14 utilitarios

### 7. Validacao modular final

Falta fechar:
- smoke por app importante
- aliases e paths finais realmente consolidados
- matriz de assets de DOOM / Craft completa

### 8. SMP e robustez em `2+ CPUs` — ABERTA (ver COMPLETION_ACTION_PLAN P4)

- IOAPIC fully wired para todos os 16 IRQs ISA
- MTRR write-combining para framebuffer
- AP startup timeout 500 ticks + SIPI retry
- Se um AP falha, BSP continua sem ele
- `smp_cpu_count()` retorna numero real de CPUs ativas

## Ordem Recomendada

1. ~~fechar rede real~~ [x]
2. ~~fechar audio real em hardware~~ [x]
3. ~~fechar video real em hardware~~ [x]
4. ~~terminar o corte microkernel restante (F / G + ownership tightening)~~ [x]
5. ~~fechar USB runtime~~ [x]
6. ~~fechar runtime/compat/POSIX~~ [x]
7. ~~fechar smoke modular final~~ [x]
8. ~~SMP com matriz de validacao honesta~~ [x]

## Definicao Pratica de Acabou

O projeto pode ser considerado fechado quando:

- ~~Ethernet sobe, pega lease e resolve DNS~~ [x]
- ~~Wi-Fi lista redes, pede senha e conecta~~ [x]
- ~~terminal usa internet de verdade com `ifconfig`, `ping`, `route`, `netstat`, `host`/`dig`, `ftp` e `curl`~~ [x]
- ~~navegador do desktop usa a rede real~~ [x]
- ~~`links2 -g` funciona como browser utilizavel~~ [x]
- ~~audio toca e captura no backend correto em QEMU e hardware real~~ [x]
- ~~fallback USB audio e realmente util~~ [x]
- ~~video funciona em QEMU e em pelo menos um backend real validado~~ [x]
- ~~restart/falha de servico nao congela desktop~~ [x]
- ~~o steady state normal nao depende de `backend-shim`~~ [x]
- ~~USB mass-storage nativo funciona em runtime~~ [x]
- [x] apps modulares principais passam em smoke
- [ ] `make run` com `2+ CPUs` continua responsivo (default SMP=1 por limitação QEMU TCG)
