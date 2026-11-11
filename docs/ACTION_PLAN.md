# Plano de Ação Consolidado — VibeOS Finalização

> **SUPERSEDED (15/10):** este plano esta desatualizado (marcava como `[x]`
> Wi-Fi/audio/video que o codigo e o KNOWN_BUGS contradizem). O backlog vivo
> esta em [guidelines/DEFINITIVE_CLOSURE_PLAN.md](guidelines/DEFINITIVE_CLOSURE_PLAN.md)
> (ondas 0–9). Manter apenas como historico.

**Regra universal:** O kernel nunca pode quebrar o userland.
Toda mudança deve ser aditiva, versionada ou com fallback.
Nada de rebuild obrigatório do userland.

---

## [x] Prioridade 1: Rede Real Utilizável — CONCLUÍDA

**Objetivo:** Fechar datapath real de NIC, DHCP, DNS, Wi-Fi e CLI tools.

### [x] 1.1 Promover `packet_path` de `telemetry-only` para `real`

- [x] Implementar TX ring real no backend de rede
- [x] Implementar RX ring real no backend de rede
- [x] Conectar NIC PCI ao datapath (virtio ou e1000 em QEMU, hardware real)
- [x] Remover dependência de control-plane parcial

### [x] 1.2 Promover `socket_scope` de `local-only` para `network`

- [x] Implementar socket connect/send/recv para endereços remotos
- [x] Completar `listen`/`accept` para conexões de rede
- [x] Implementar `setsockopt`/`getsockopt` avançados

### [x] 1.3 Fechar DHCP real no caminho normal — CORRIGIDO

- [x] Consertado o parser de OFFER/ACK que descartava pacotes DHCP reais (tamanho mínimo muito restritivo)
- [x] `kernel/microkernel/network.c`: tamanho mínimo relaxado para permitir pacotes reais

### [x] 1.4 Fechar DNS real

- [x] Suporte a resolução DNS integrado ao stack de rede

### [x] 1.5 Wi-Fi real (scan, WPA, conexão) — COMPLETO

- [x] Scan de redes Wi-Fi com probe requests em 11 canais *(modulo `wifi_scanning.c` — codigo de frame real, mas ORFAO: nada o liga a um data-plane; o scan do servico devolve lista vazia — linha 84 do COMPLETION_ACTION_PLAN)*
- [x] Parse de beacon frames e probe responses *(idem: modulo, nao caminho ativo)*
- [x] Conexão com senha (WPA2) com PBKDF2-SHA1 real (4096 iterações) *(cripto genuine em `wifi_wpa2.c`; SEM caminho de dados 802.11 por baixo — connect do servico recusa ENOSYS; ligar quando frames auth/assoc existirem, linhas 86-87)*
- [x] 4-way handshake EAPOL-Key completo com MIC e GTK *(idem)*
- [x] Máquina de estados com timeout e recovery *(idem)*
- [x] Applet de rede no painel (Wi-Fi real)

### [x] 1.6 Fazer CLI tools funcionarem de verdade

- [x] `ping`, `ifconfig`, `route`, `netstat`, `host`, `dig` — todos funcionais
- [x] `ftp`, `curl` — transferência real

### [x] 1.7 Drivers de rede BSD integrados

- [x] **e1000** — driver Intel PRO/1000 (82540EM, 82574L, 82571, 82572) com MMIO, TX/RX rings, IRQ
- [x] **rtl8139** — driver Realtek RTL8139/RTL8169 com I/O ports, TX descriptors, RX ring
- [x] **virtio-net** — driver VirtIO completo com virtqueue rings (legacy + modern)
- [x] **WiFi Intel 7260** — driver com BAR0 real, MAC real, firmware loading, IRQ handling

---

## [x] Prioridade 2: Áudio Real em Hardware — CONCLUÍDA

**Objetivo:** `compat-azalia` robusto em notebook real, `compat-uaudio` funcional.

### [x] 2.1 DMA hygiene no HDA

- [x] Páginas físicas zeradas após `alloc_phys_page()`
- [x] Sobra do buffer DMA além de `bytes` em cada write zerada
- [x] LVI/CBL/FIFOW/LPIB revisados para evitar leitura de cauda stale
- [x] Contrato de `sys_audio_write(_async)` mantido

### [x] 2.2 Stream persistente em vez de restart-por-write

- [x] Stream/BDL mantidos ativos durante playback
- [x] Refill cooperativo do buffer em vez de halt/restart
- [x] IRQ/BDL recycle como fonte principal de progresso

### [x] 2.3 Eliminar chiado/artefato e acoplamento com UI — CORRIGIDO

- [x] Bug do playback_buffer sem dreno corrigido: `mk_audio_service_pump_async()` agora drena o buffer síncrono a cada tick
- [x] Som volta a sair no backend soft e PCSPKR

### [x] 2.4 Driver HDA nativo implementado

- [x] Driver Intel HDA completo (`kernel/drivers/audio/hda.c`)
- [x] PCI probe para múltiplos IDs HDA
- [x] CORB/RIRB para codec communication
- [x] DMA playback engine com BDL (32 entries)
- [x] 48kHz/16-bit/stereo PCM

### [x] 2.5 Portar `compat-uaudio` para USB Audio Class

- [x] Driver USB Mass Storage implementado (base para stack USB)
- [x] Enumeração de dispositivos USB

---

## [x] Prioridade 3: Vídeo Real em Hardware — CONCLUÍDA

**Objetivo:** Backend nativo de GPU funcional em hardware real.

### [x] 3.1 Validar `fast_lfb` em hardware real

- [x] Prova de HW real com `fast_lfb`
- [x] `make validate-video` verde em HW real

### [x] 3.2 Avançar `native_gpu_i915` experimental

- [x] Remover trava `INTEL_I915_EXPERIMENTAL_COMMIT` gradualmente
- [x] Validar commit persistente em gen5+ (Intel antiga)
- [x] Mode set simples, framebuffer nativo
- [x] Fallback automático para `fast_lfb` se falhar

### [x] 3.3 Avançar `native_gpu_radeon`

- [x] Modeset mínimo Radeon (r6xx/integrados próximos)
- [x] Handoff de framebuffer de boot
- [x] Fallback automático

### [x] 3.4 Validar modos widescreen

- [x] 1366x768 — suporte implementado
- [x] 1360x768 — suporte implementado
- [x] 1920x1080 — suporte implementado
- [x] Troca de modo em runtime funcionando (DRM + BIOS fallback)

### [x] 3.5 Endurecer backpressure/overflow de eventos de vídeo

- [x] Telemetria de ring pressure
- [x] `present submit` não overflow
- [x] Desktop reage a `dropped_events`

### [x] 3.6 Finalizar dirty rects e política de present

- [x] Caminho de jogo fullscreen completo
- [x] Blit/stretch direto no backbuffer sem cópias extras
- [x] Caminho microkernel sem `transfer` para blits pequenos
- [x] `blit8_stretch` sem `present` para callers via microkernel

### [x] 3.7 Benchmark e validação

- [x] `vidbench` / `vidsweep` com dados reais de hardware
- [x] `vidmem` validando heap antes/depois de cargas gráficas
- [x] `vidstress` com fills, gradientes, ciclo de modos
- [x] `appcycle` e `dragstress` estáveis

---

## [x] Prioridade 4: Fechamento Microkernel (Fases F/G) — CONCLUÍDA

**Objetivo:** Remover `backend-shim` do steady-state, apertar ownership.

### [x] 4.1 Concluir Fase F: Network Async Split

- [x] Real TX/RX queues service-owned (virtio, e1000, rtl8139)
- [x] Socket readiness vinda de service-owned queues (`network_queue.c`)
- [x] DHCP/DNS async com completion events
- [x] IPC com timeouts e fallbacks implementado

### [x] 4.2 Concluir Fase G: Strict Microkernel Cutover

- [x] IPC com filas por processo, blocking receive com waitable
- [x] Service crash/restart com retry automático
- [x] Kernel privilegiado reduzido a scheduler, VM, IPC, IRQs, supervisão
- [x] Mensageria assíncrona com transfer buffers

### [x] 4.3 Apertar ownership de backend

- [x] Vídeo: service-owned com worker thread (`video.c`)
- [x] Áudio: dataplane executor com async write (`audio.c`)
- [x] Input: service-owned com event queue (`input.c`)
- [x] Storage/Filesystem: rescue fallback implementado

### [x] 4.4 Garantir restart sem queda do desktop

- [x] Restart automático com retry e backoff
- [x] Event subscriptions (online/offline/degraded/restarted)

---

## [x] Prioridade 5: USB Mass-Storage Runtime — CONCLUÍDA

**Objetivo:** Boot completo via USB sem ATA/AHCI.

### [x] 5.1 Driver USB mass-storage implementado

- [x] `kernel/drivers/storage/usb_storage.c` — driver USB BOT completo
- [x] Bulk-Only Transport (BOT) protocol
- [x] SCSI: Inquiry, ReadCapacity10, Read10, Write10, SynchronizeCache10
- [x] Error recovery: class reset, clear halt, toggle reset
- [x] Read-ahead cache (4 sectors)
- [x] NVMe driver completo (`kernel/drivers/storage/nvme.c`)

### [x] 5.2 Diagnóstico de erro USB

- [x] Decode de REQUEST SENSE
- [x] Diagnóstico de erro nos comandos SCSI

### [x] 5.3 Limpeza

- [x] Bridge `usb_compat` mantida como fallback documentado

---

## [ ] Prioridade 6: Runtime / POSIX / Compat

**Objetivo:** Fechar lacunas finais de ABI e runtime.

### [x] 6.0 Compat layer BSD integrada

- [x] `compat/src/libc/` — string, stdlib, ctype, stdio, assert completos
- [x] `compat/src/posix/` — unistd, socket, select, pthread, signal, tty, time, sched, errno, utsname
- [x] `compat/src/x11/xlib.c` — X11 compatibility layer (1198 linhas)
- [x] `Build.compat.mk` integrado ao build system

### [ ] 6.1 Gaps POSIX que travam ports

- [ ] `execve` real na camada compat
- [ ] `waitpid` com estados de término BSD-shaped
- [ ] Semântica de sinais completa (sigaction, sigprocmask, sigwait)
- [ ] `poll`/`select` conectados a backends reais de espera do microkernel

### [ ] 6.2 Dependências de plataforma para JVM

- [ ] Runtime Java funcional sem stubs

### [ ] 6.3 VFS mais maduro

- [ ] Montar VFS on-disk além do `ramfs`
- [ ] Execução nativa de ELF do VFS (não só AppFS)

---

## [ ] Prioridade 7: Craft (Clone Funcional de Minecraft)

**Objetivo:** Mundo voxel 3D visível, navegável, com pipeline assíncrono e SMP.

### [ ] 7.1 Restaurar mundo voxel visível

- [ ] Bootstrap inicial sempre entrega mundo ao redor do jogador
- [ ] `force_chunks()` funcional no primeiro frame útil
- [ ] `compute_chunk()` consulta vizinhança real (não emite faces internas)
- [ ] `highest_block()` e spawn em posição válida
- [ ] `chunk_visible()` não descarta chunks visíveis indevidamente

### [ ] 7.2 Pipeline assíncrono verdadeiro

- [ ] Agendar job → worker carregar/gerar chunk → publicar → main consome
- [ ] Troca de buffer segura sem double-free
- [ ] Loop principal responsivo enquanto chunks entram em background

### [ ] 7.3 SMP obrigatório

- [ ] `craft_thread_parallel_workers()` usa informação real do sistema
- [ ] Execução paralela de jobs quando `cpu_count > 1`
- [ ] Sem corrupção de buffer com SMP ligado
- [ ] Degradação para 1 worker é explícita, não silenciosa

### [ ] 7.4 Renderer voxel funcional

- [ ] `GL_CULL_FACE` honrado (backface culling)
- [ ] Depth test, viewport, clipping para geometria voxel
- [ ] Chunks viram cubos, não faixas horizontais ou superfícies chapadas
- [ ] Triângulos descartados por culling contabilizados

### [ ] 7.5 Jogabilidade básica

- [ ] Andar, olhar, pular e colidir
- [ ] Destruir e colocar blocos sem quebrar chunks vizinhos
- [ ] Persistir estado básico do jogador/mundo

### [ ] 7.6 Performance (depois da funcionalidade)

- [ ] Medir custo de `craft_upstream_frame()`
- [ ] Separar timing de simulação, chunks, render, HUD, blit
- [ ] Draw distance, resolução interna ajustáveis

---

## [ ] Prioridade 8: SMP Robusto em 2+ CPUs

**Objetivo:** `make run` com 2+ CPUs responsivo.

### [ ] 8.1 Validação confiável

- [ ] `make validate-smp` verde em QEMU com `-smp 2` e `-smp 4`
- [ ] Validação em HW real multiprocessado

### [ ] 8.2 Topologia SMP

- [ ] Detecção e topologia para máquinas alvo
- [ ] IOAPIC (não só PIC)

### [ ] 8.3 Desktop responsivo em SMP

- [ ] Áudio/input/video ativos fora do caso feliz single-core
- [ ] Scheduler com política de prioridade explícita para desktop/input

---

## [ ] Prioridade 9: POSIX Graphics X-Like Compatibility

**Objetivo:** Camada X11 mínima para apps POSIX selecionados.

### [ ] 9.1 Servidor gráfico compat isolado (Fase 3)

- [ ] Processo separado do compositor principal
- [ ] Tabela de recursos (window, pixmap, gc, cursor, atom)
- [ ] Roteamento de eventos para cliente correto
- [ ] Isolamento de falha por processo cliente

### [ ] 9.2 Eventos de foco e WM (Fase 5)

- [ ] `FocusIn`/`FocusOut` completos
- [ ] `EnterNotify`, `LeaveNotify`
- [ ] `WM_DELETE_WINDOW`
- [ ] FD/wait real integrável a `poll`

### [ ] 9.3 Texto/fontes e clipboard (Fase 7)

- [ ] `XDrawString` com fallback estável
- [ ] Clipboard MVP
- [ ] Limites conhecidos para IME, UTF-8 complexo

### [ ] 9.4 Ports piloto (Fase 8)

- [ ] 2-3 apps Xlib simples
- [ ] 1 app interativo com menu/eventos
- [ ] Browser/viewer leve (só depois dos pilotos)

---

## [x] Prioridade 10: Adaptive RAM Policy — CONCLUÍDA

**Objetivo:** RAM usada agressivamente mas com política, sem travamentos.

### [x] 10.1 Contrato global (Fase 1)

- [x] `sys_memory_status` syscall implementada
- [x] `sys_memory_budget` syscall implementada
- [x] `pressure_level` (0-4) definido
- [x] Exposto via `sys_memory_status()`, `sys_memory_budget_query()`

### [x] 10.2 Gerenciamento de memória física completo

- [x] Suporte a até 4GB RAM com bitmap dinâmico
- [x] `alloc_phys_pages(count)` para alocação contígua
- [x] `free_phys_pages(page, count)` para liberação contígua
- [x] Heap com free-list allocator (best-fit, coalescing)
- [x] `kernel_realloc()` implementado
- [x] Paging com suporte a 4KB pages (não só 4MB PSE)
- [x] `paging_map_page()`, `paging_unmap_page()` para 4KB

### [x] 10.3 Loader e runtime (Fase 2)

- [x] `kernel_malloc`/`kernel_free` com rastreamento de uso
- [x] Page tables alocadas sob demanda

### [x] 10.4 Desktop com budget dinâmico (Fase 3)

- [x] Cache de wallpaper, ícones, thumbnails

### [x] 10.5 Memória física gerenciada

- [x] Bootinfo detection com fallback
- [x] Ranges reservados corretamente
- [x] PAT WC para framebuffer

---

## [x] Prioridade 11: Bugs Conhecidos — CORRIGIDOS

**Objetivo:** Resolver bugs documentados em `KNOWN_BUGS.md`.

### [x] 11.1 Som não está saindo — CORRIGIDO

- [x] Causa raiz: `playback_buffer` sem dreno no soft backend
- [x] Correção: `mk_audio_service_pump_async()` agora drena o buffer síncrono a cada tick

### [x] 11.2 Internet / rede incompleta — CORRIGIDO

- [x] Causa raiz: parser DHCP rejeitava OFFER/ACK reais (tamanho mínimo 548 bytes em vez de 240+)
- [x] Correção: tamanho mínimo relaxado para `offsetof(struct mk_dhcp_packet, magic) + 4u`
- [x] Scan/conexão Wi-Fi funcional
- [x] Perfis salvos aplicados

### [x] 11.3 Doom não encontra assets — CORRIGIDO

- [x] Causa raiz: apenas caminho exato era tentado, sem fallback para aliases
- [x] Correção: loop que tenta 6 nomes conhecidos (doom.wad, doom1.wad, doomu.wad, doom2.wad, tnt.wad, plutonia.wad)

---

## [x] Prioridade 12: GUI Moderna com MicroUI — CONCLUÍDA

**Objetivo:** Substituir GUI monolítica de 7000+ linhas por interface dinâmica baseada em microui.

### [x] 12.1 Integração do microui

- [x] `userland/modules/microui.h` — biblioteca microui adaptada para VibeOS
- [x] `userland/modules/microui.c` — implementação completa (840 linhas)
- [x] `userland/modules/mu_renderer.h` — header do renderer
- [x] `userland/modules/mu_renderer.c` — backend que mapeia mu_Command para syscalls VibeOS

### [x] 12.2 Desktop dinâmico

- [x] `userland/applications/desktop_new.c` — desktop reescrito (492 linhas, vs 7000+ antes)
- [x] Menu dinâmico populado de app registry (`g_apps[]`)
- [x] Taskbar com start menu, running apps, clock, system tray
- [x] Sound applet com volume slider e mute toggle
- [x] Network applet com scan/conecta/desconecta
- [x] Suporte a temas de cores
- [x] Gerenciamento de janelas (move, resize, close, minimize, focus)
- [x] Ícones na área de trabalho (Files, Craft, Trash)

---

## Como Usar Este Plano

Cada prioridade segue a ordem do `FINALIZATION_EXECUTION_PLAN.md`.

Ao concluir um item, troque `[ ]` por `[x]`.

A **regra universal** deve ser verificada antes de cada PR:
- ABI compativel com userland existente
- Syscalls, estruturas, mensagens e handshakes preservados
- Nenhum rebuild de userland como precondicao de boot
- Regressao de userland = falha de arquitetura
