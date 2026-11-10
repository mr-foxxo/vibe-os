# Plano de Fechamento Definitivo — VibeOS (Revisão 4)

last: 2026-11-03 — 0.15 HARNESS FEITO (backpressure por tecla + freeze nomeado, ns38-40) e 0.14 FEITO (causa raiz final: contrato de status do SYSCALL_TASK_SNAPSHOT; `PASS netmgrd status` 19_ns37). O wall que resta para os 22 comandos e do main-loop do QEMU (~770 ticks/tecla com wake->dispatch do kernel <=6), medido em ns40.
doing: backlog definitivo consolidado (v4) — ondas 0-9, um estado terminal por item, prova obrigatoria por linha
next: Onda 0 — 0.16: timestamp sendkey->rk por tecla no harness (medida direta main-loop-vs-kernel) + corrida completa do gate num host menos carregado; em paralelo, 0.2 (sweep TODO/stub) e 0.3 (knob de telemetria) estao destravados; depois 0.15 fecha com o report 22x e ai `make validate-abi-compat-surface` + `make validate-audio-stack` valem como gate
state: active
intent: execution-backlog

> **Substitui os planos fragmentados como fonte de trabalho em aberto.** Absorve:
> `COMPLETION_ACTION_PLAN.md`, `FINALIZATION_EXECUTION_PLAN.md`, `ACTION_PLAN.md`,
> `VIDEO_BACKEND_REWRITE_PLAN.md`, `NETWORK_AUDIO_PANEL_PLAN.md`,
> `CRAFT_OPTIMIZATION_PLAN.md`, `ICON_THEME_IMPLEMENTATION_PLAN.md`,
> `posix-graphics-xlike-plan.md`, `usb_boot.md`, `VIBELOADER_PLAN.md`,
> `guidelines/COMPAT_PLAN*.md`, `guidelines/MODULAR_APP_INTEGRATION_PLAN.md`,
> `guidelines/COMPAT_GAMES_MENU_PORT_PLAN.md`, `guidelines/java.md`,
> `AUDIO_HARDWARE_VALIDATION_MATRIX.md` e as seções abertas de
> `KNOWN_BUGS.md`/`hw_validation.md`/`guidelines/MICROKERNEL_MIGRATION.md`.
> Esta revisão foi escrita contra o **código e contra as medições da campanha #19**,
> não contra o texto dos planos anteriores; onde houver conflito, **esta vence**.
> `COMPLETION_ACTION_PLAN.md` fica como **log de evidência por prioridade**.

## Mudou desde a v3 (revisão 15/10 → 01/11)

| Fonte | Estado | Evidência |
| --- | --- | --- |
| 0.8 `BAD-FORENSICS` resíduo ring3 | ✅ FEITO | `ee09e8b4a` — frame kernel mid-`int 0x80` com ebp/esp do USER; branch kernel aceita janela do user só com `runs_in_user_mode=1`; boot pré-fix 6 linhas / pós-fix **0** com pid=12 vivo; KNOWN_BUGS #16 |
| 0.6 modo-i (sessão interativa nasce) | ✅ FEITO | `7eb20cc29` — anti-inanição era código MORTO (`account_runtime` zerava `last_start_tick`; teto 64 < span 112+3; `break` de score==0); fix não-zerar/divisor-4/teto-128/sem-break; `js` → `command exit js rc=0`, census `sel10` 4→116028; phase6 6/6 |
| 0.6 modo-ii | ✅ RE-LOCALIZADO (não era kernel) | artefato de RAM do harness: arena do app em 0x10000000 fora da RAM de 256MB; QEMU descarta writes em GPA não alocado silenciosamente; `chunk0 verify match` em 3072MB; witness permanente no `lang_loader` |
| 0.6 modo-iii = #19 | ❌ ABERTO — campanha medida | 7 mecanismos medidos/corrigidos (A–G, ver seção "Estado medido do #19"); inanição REFUTADA por sonda de latência wake→dispatch; gate `validate-network-surface` vermelho com sonda nomeada (leak `pending_signals` + trace `rk`) |
| 1.9 e1000 + rtl8139 ligados | ✅ FEITO | drivers `init` a partir do entry, DMA VA==PA, gates `make validate-network-datapath NIC=e1000` / `NIC=rtl8139` PASS; KNOWN_BUGS #12/#13 |
| 2.2 `smp_scheduler_enable()` chamado | ✅ FEITO | caller em `kernel/entry.c:899`; APs preemptam (validate-smp) |
| 2.3 IPI de reschedule | ✅ FEITO | vetor 0x82 (wake) + 0x83 (timer per-CPU); IPI 0x82 preempta CPU ocupado (switches=3 medidos; tabela dispatch morta esvaziada; UP imune por A/B) |
| 2.4 IOAPIC afinidade | ✅ FEITO (IRQs interativos) | `adb11cb11` — IRQ1/12 no primeiro AP online; PS/2 ganhou lock GLOBAL (`g_ps2_port_lock`); "online" = `kernel_cpu_state.started` (nunca o array stage); demais IRQs seguem no BSP por design |
| 2.5 smp4 | ✅ FEITO | 4/4 CPUs + enabled medidos; linha 191 STALE-FECHADA por reprodução |
| 8.2 USB mass-storage runtime | ✅ QEMU-FEITO | BOT no UHCI (`b78dc41b7`) + EHCI high-speed (`3590c4113`); stick emulado carrega fs/AppFS, smoke write+restore; xHCI/stick físico seguem BLOQUEADO-HW |
| P4 convenção `mk_service_launch_task` | ✅ FEITO | KNOWN_BUGS #14 |
| P4 execve-UAF latente | ✅ FEITO | KNOWN_BUGS #15 (`pending_stack_free` no terminate path) |
| P4 classe #5 face-2 | ✅ FEITO | clamp de capacidade `kernel_video_target_capacity` + W^X do .text com CR0.WP |
| P4 classe #5 face-1 | ❌ ABERTA (campanha) | camada de witnesses permanente no tree; escritor anônimo transiente só fecha com hw-watchpoint no T61 (Onda 8) |
| class-stream write-only sem consumidor | `[!]` mantenedora | telemetria satura `class_dropped_events` sem limite; nenhum consumidor; decisão fio-vs-remove na fila [!] |
| D/G constantes de política do #19 | `[!]` mantenedora | boost `-128` e budget `128` são os valores atuais documentados; mecanismo fixo, magnitude é decisão de arquitetura |

**Escala real do backlog** (checkboxes abertos por plano de origem, contagem de origem):
COMPLETION_ACTION_PLAN 56 · ACTION_PLAN 49 (superseded) · posix-xlike 56 ·
CRAFT 67 · ICON 39 · VIDEO 32 · NETWORK_AUDIO 47 · MICROKERNEL 3 · COMPAT 2 ·
MODULAR 6 · GAMES 7 · java 2. Esta revisão **não herda a contagem**: herda os
itens, cada um com DoD e prova. A Onda 6 concentra a maior parte.

---

## 0. Contrato deste plano

### 0.1 Regra anti-stub (o que "definitivo" significa)

Nenhum item fecha com placeholder. Cada item termina em **exatamente um** estado:

| Estado | Significado | Obrigatório |
| --- | --- | --- |
| **FEITO** | comportamento real, observável, com prova executada | link do report/log em `build/` + comando na mensagem de commit |
| **NÃO-OBJETIVO** | decidido fora de escopo, com justificativa escrita | entrada em "Não-objetivos declarados" + doc de referência corrigido |
| **BLOQUEADO-HW** | implementado, porém só se *prova* em máquina física | código no repo + linha da matriz marcada "implementado, aguardando silício" + log serial quando a máquina existir |
| **DECISÃO [!]** | exige decisão da mantenedora antes de codar | item parado, com as opções escritas na fila [!] (0.4) |

Proibido: `return 0` de fachada, TODO que finge implementação, log que afirma
mais do que o código faz, teste que passa sem exercitar o caminho, "unsupported"
silencioso. Falta de suporte = diagnóstico explícito (`ENOTTY`/`ENOSYS`/log
honesto) — nunca estado falso. **Precedente:** runtimes `js`/`ruby`/`python`
passaram de stub mentiroso para `ENOSYS` honesto (`918376735`) — este é o padrão
para todo item P7.

**Regra de medição (campanha #19, 01/11):** nenhuma hipótese vira "correção"
sem a linha-witness medida; e um instrumento só vale como evidência depois de
provada a sua alcançabilidade (0 hits de uma sonda não comprovadamente viva não
é evidência negativa).

### 0.2 Provas aceitas

- build completo verde (`make`, `-Werror`) **antes** de qualquer claim;
- `make validate-*` aplicável com report em `build/`;
- boot real no QEMU no perfil da área (`make run`, `run-headless-*`);
- para itens de hardware: log serial da máquina física anexado no report;
- para itens de sessão interativa: o gate canônico `make validate-network-surface`
  (o report `build/network-surface-validation.md` embute o serial) ou probe HMP
  persistente no repo (item 0.9); o serial **não** captura a saída de console
  dos apps — leitura do VGA buffer via HMP `xp 0xb8000` quando necessário.

### 0.3 Invariantes que nenhum item pode violar

- **regra universal**: o kernel nunca quebra o userland; ABI apenas
  aditiva/versionada/com fallback (`docs/abi-contracts.md`);
- `BOOTINFO`, header de AppFS e structs de syscall só mudam com versão nova publicada;
- uma unidade de trabalho coesa por commit; implementação + prova + doc no mesmo commit;
- `main` é a única branch de longa duração; merge/promoção é da mantenedora;
- **a shell rescue não pode morrer por comando com erro** (hoje ela sai em rc≠0,
  `userland/modules/shell.c:402` — item 6.1).

### 0.4 Fila de decisão [!] da mantenedora (única lista)

Tudo que exige decisão de arquitetura/escopo. Enquanto não decidido, o item fica
parado e o plano executa o próximo desbloqueado (regra da rodada).

| # | Decisão | Opções | Default atual no código |
| --- | --- | --- | --- |
| !1 | NIC abstraction (1.8) | (a) criar camada `send_frame/attach_mac/link_status` registrada; (b) manter init direto por driver (e1000/rtl8139/virtio já ligados) | init direto por driver funciona; abstração não existe |
| !2 | class-stream sem consumidor | (a) fio (consumir/drenar); (b) remove a stream + telemetria morta | write-only, `class_dropped_events` cresce sem limite |
| !3 | Default SMP / SRO | (a) default 2 CPUs; (b) manter 1 (SMP só com toggle experimental) | `QEMU_RUN_SMP ?= 1` |
| !4 | Default i915 | (a) manter `INTEL_I915_EXPERIMENTAL_COMMIT=1`; (b) voltar a 0 seguro | 1 no Makefile |
| !5 | Wi-Fi 802.11 real (1.11) | (a) subsistema novo (driver+supplicant+CCMP) — custo alto; (b) declarar NÃO-OBJETIVO e manter diagnóstico honesto | fachada atual vira diagnóstico honesto (1.10) |
| !6 | Browser (6.6) | (a) engine mínimo; (b) links2 como navegador do sistema; (c) NÃO-OBJETIVO declarado | Otter Qt5 vendorado, fora do build |
| !7 | Java (5.6) | (a) JVM real (custo alto); (b) experimental honesto declarado | `java`/`javac` caseiros |
| !8 | Magnitudes do #19 | (a) manter `-128`/`128`; (b) reajustar (constante única em scheduler.c/service.c) | `-128`, `128` |

### 0.5 Instrumentos de diagnóstico que FICAM no repo (não são stubs)

Ferramentas reais, orçadas, aditivas — mantidas e documentadas:
`input-diag` (`kernel/drivers/input/{input,keyboard}.c`), host-state peek no
trace de `service: request timeout` (`kernel/microkernel/service.c`),
`sched-dispatch: BAD-FORENSICS`, `sched-census`/`sched-score`/`sched-rip`
(`kernel/process/scheduler.c`), sonda de latência wake→dispatch
(`g_ipc_wake_tick`/`g_wake_max_d`/`wake-stale`), trace `rk` do `read_line`
(`userland/modules/shell.c`), `tools/diagnose_smp_freeze.py`, guardas de
heap/scheduler/waitable. Manutenção delas é trabalho contínuo (item 0.8),
não lixo a remover.

### 0.6 O que "tudo de vez" significa aqui

Execução em **ondas**, cada uma fechando com gate verde antes da próxima. Duas
áreas são *subsistemas novos*, não "itens" — Wi-Fi 802.11 real e um motor de
browser — e estão na fila [!] com custo honesto; as demais fecham por
implementação. **Fora da fila [!], nenhum item pode terminar em "parcial".**

### 0.7 Estado medido do #19 (0.6 modo-iii) — campanha 01/11

Sintoma: o gate digita `netmgrd status`, a linha nunca chega a `busybox_main`
(zero `shell: command`), o comando nunca roda. Medições (runs 19_run1..13):

| Mecanismo | Como foi medido | Correção (no tree) |
| --- | --- | --- |
| A. boost de wake perpétuo | score -32/-24 ficava colado no dispatch e mascarava o aging | consumo one-shot no dispatch |
| B. clamp `score<0→0` | waiter envelhecido empatava em 0 com tier-0 fresco; empate decidido por ordem de varredura | clamp removido (teto -31 estrito) |
| C. lost wakeup enqueue→block | `cliQ=1 cliW{pend=1 wake=0}` com head ainda RUNNING no pop | peek `state==BLOCKED` + auto-consumo pós-block |
| D. boost -32/-24 insuficiente | host tier-6 pontuando 64+ por ~384-512 ticks | `-128` (cap de inanição -13) |
| E. yield pós-poll sem tecla | shell no pool de aging tier-6 770-1170 ticks até o próximo poll | yield removido (shell + `vibe_app_read_line`) |
| F. budget de reply 32 ticks | retry re-enviava request em voo → flood `unexpected reply`, cliQ→16, pend→31 | budget 128 (precedente FILESYSTEM) |
| G. `fs_tick()` por iteração | shell encadeada atrás de storage (`waiting_src=1`) | throttle 1x/100 ticks no read_line |
| — inanição do scheduler | sonda wake→dispatch: `w6=1 w11=1`, zero `wake-stale` | **REFUTADA** (não é a causa) |
| — linha não submete | 13 banners `shell: ready`, 0 `shell: command`, `consumed=15 produced=18` | **ABERTO** — sondas nomeadas abaixo |

Sondas nomeadas da próxima rodada (já no tree): (1) leak de `pending_signals`
no caminho pop+complete_wait do `kernel_waitable_signal` (cada reply deixa 1
sinal fantasma; medido `cliW{pend=3 wake=0}`); (2) trace `rk <valor>` por poll
no `read_line` (budget 48) rodando no gate `make validate-network-surface`.
Gate só fica verde com `shell: command exit netmgrd rc=0` no report.

---

## 1. Ondas (ordem de execução)

| Onda | Tema | Fecha quando |
| --- | --- | --- |
| **0** | Fundação: bugs de boot, gates, instrumentos, consolidação | build/parse limpos + gate base verde (network-surface + phase6) |
| **1** | Rede definitiva (IPv4 client, ferramentas, NIC, honestidade Wi-Fi) | `validate-network-datapath` + `validate-network-surface` verdes |
| **2** | SMP definitivo (2+ CPUs distribuídas, sem corrida) | `validate-smp` verde em smp2/smp4 e `make run` SMP=2 responsivo |
| **3** | Áudio definitivo (playback+captura, azalia fechado, uaudio) | `validate-audio-*` verdes; matriz HW marcada "aguardando silício" |
| **4** | Vídeo definitivo (DRM, modos, protocolo de estresse) | `validate-video`/`validate-startx-*` + protocolo rodado |
| **5** | Runtimes definitivos (js/mruby/micropython/lua/sectorc/java) | `validate-modular-apps` com programa real por runtime |
| **6** | Userland/apps (Craft clone, MicroUI, órfãos, icons, fontes, jogos, shell) | apps abrem pelo menu e pela shell com smoke registrado |
| **7** | POSIX/compat definitivo (execve/waitpid/signals/poll, VFS, X-Like) | suíte QEMU de compat verde |
| **8** | Hardware real (T61/T400) | matrizes preenchidas com log serial real |
| **9** | Fechamento/honestidade (README final, planos aposentados) | zero afirmação sem evidência |

Dependências duras: **0 → 1 → 2** (SMP destrava a validação confiável do resto);
**1** antes de 6 (browser/curl) e antes de 7 (poll/socket); **3/4/5** podem
correr em paralelo depois de 2; **8** só depois de 0–7.

---

## Onda 0 — Fundação

| # | Item | Estado verificado (01/11) | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 0.1 | `kernel_usb_host_controller_count()` = 0 após UHCI | ✅ FEITO (`ddb1a26ef`) | contagem real | `usb: init complete (1 controllers, 0 devices)` | — |
| 0.2 | Sweep dos TODO/stub/n.i.; `make` sem warns | PARCIAL: kernel 0 TODO/FIXME (top-level e drivers first-party); fdopen virou stream fd-backed real em `app_runtime.c`+`desktop_app_runtime.c` (VIBE_FILE_FLAG_FD, prova `make test-host` 20/20); stubs orfãos `lang/libc/vibe_libc.c`, `lang/apps/js/vibe_libc_stubs.c`, `lang/apps/ruby/ruby_main_fixed.c` removidos (nunca compilados; `ruby_main_fixed` era o stub mentiroso que 918376735 aposentou). Vendor (links2/DOOM/craft-upstream/jdk) fora do escopo do sweep | cada TODO → feito / bug em KNOWN_BUGS / item aqui | `make` rc=0 sem warning + `make test-host` ALL PASS | — |
| 0.3 | Telemetria de boot (`g_timer_trace_budget` fixo em 8) | dificulta diagnóstico | knob quiet/verbose | doc + default | — |
| 0.4 | Consolidação de planos (pointers) | ✅ feito; esta v4 reverifica | cada plano antigo aponta para cá | grep | — |
| 0.5 | Gate dos harnesses interativos | ✅ harness (selector + gate + timeout) — `network-surface` alcança o shell | gates verdes **repetidos** | `make validate-network-surface` verde 3× | 0.6 | **Sessao interativa trava no meio** | ✅ FEITO 03/11 — modo-i ✅ (`7eb20cc29`), modo-ii ✅ (artefato de RAM do harness), modo-iii ✅ (campanha #19 partes 1-5; causas: A-G + dispatch steal + wake-timeout starvation + canal de status do snapshot). EVIDENCIA FINAL: `PASS netmgrd status` em `build/network-surface-validation.md` (19_ns37) — marker `shell: command exit netmgrd rc=0` chegou na serial. O gate COMPLETO (22 comandos) falha no comando 2 por wall de digitacao (item 0.15) — gargalo de harness/TCG, nao do kernel; proxima acao la. Historico completo em KNOWN_BUGS #19 partes 1-5. |
| 0.6 | **Sessão interativa trava no meio** | ❌ ABERTO (modo-iii = #19: frente INPUT ✅ 02/11, restou a frente NETWORK 0.12 — ver 0.7/0.12). Modo-i ✅ (`7eb20cc29`, KNOWN_BUGS #18); modo-ii ✅ artefato de RAM do harness | `netmgrd status` completa pelo gate | `shell: command exit netmgrd rc=0` no report | sonda 0.7 |
| 0.7 | `BAD-FORENSICS` / witness-layer | ✅ FEITO (`13ae59214` + `ee09e8b4a`, KNOWN_BUGS #16; camada steady-state completa, sem verde-falso) | — | boot 0 linhas falsas + phase6 6/6 | — |
| 0.8 | Manutenção da camada de witnesses (budgets documentados) | instrumentos no tree, budget-free onde importa | cada witness tem doc de budget no código | grep + doc | — |
| 0.9 | Repro interativo persistente no repo | harness HMP existia só em `/tmp/vg` e foi perdido na limpeza do tmpfs | script no repo (HMP+sendkey, marcadores, serial) usado pelos gates | `tools/` + uso no gate | — |
| 0.10 | #19: leak de `pending_signals` no chain-pop | ✅ FEITO 01/11 — decremento no pop com put-back na recusa do wake; os contadores voltaram a ser honestos | `cliW{pend}` nao cresce mais por reply | witness 19_ns10+: pend estavel 0 nos pops | — |
| 0.11 | #19: linha não submete (trace `rk`) | ✅ FEITO 02/11 — causas medidas e corrigidas: dispatch steal do boost (H), ISR de 1 byte + duplo consumidor da mailbox de teclas (I: `MK_MSG_INPUT_KEY_READ_WAIT` 109 / `SYSCALL_INPUT_KEY_WAIT` 231, aditivos, host = consumidor unico), fs_tick removido do caminho de leitura (J) | linha digitada completa submete e roda | gate 19_ns22: `shell: command netmgrd` + status imprimindo ate `socket rx capacity:` | — |
| 0.12 | #19: frente NETWORK da cauda do status | ✅ FEITO 02/11 — a cauda nao era rede: era **loop infinito no `vsnprintf` do runtime de app** (`vibe_vsnprintf_internal`, measure pass com `str==NULL` nunca decrementava `pos`; `sched-rip p=13 eip=100003d6` repetindo + disassembly `test %esi,%esi; je` pulando `dec %ecx`). Fix: indice avanca incondicionalmente, so o store e condicional. Prova 19_ns25: status imprime ate `state file: /runtime/netmgrd-status.txt` e o app retorna limpo | status completo + app retorna | report do gate | — |
| 0.13 | Wake de timeout sem boost — starvation dos poll loops | ✅ FEITO 02/11 — medido em cadeia: `tmo-stale pid=11 d=304 cls=2` (ns28), `d=380 cls=9` (ns30), `pid=6 d=63 cls=3` (ns31). Fix: wake por TIMED_OUT de espera com budget (classes IPC/SUPERVISION/INPUT) recebe o MESMO one-shot slice do wake SIGNALED (sweep arma o budget, score -128, consumo no dispatch; depois consunido vale o +12 retry-penalty so p/ IPC). Prova ns32: ZERO `tmo-stale` no run inteiro; `rk` mostra a linha digitada ate `netmgrd ` com cada char consumido; wT11 304→3 | linha submete sem stale + marcador de exit do 1o comando | report do gate com janela 1800s (ns33 em curso) | — |
| 0.14 | Marcador de exit da shell pos-terminacao | ✅ FEITO 03/11 — CAUSA RAIZ FINAL: `sys_task_snapshot` retornava `count` no sucesso e todo o userland le `!=0` como falha → helpers de vida viam "alive" eterno → o host do app nunca morria apos o filho e o TERMINATED(12) esperado pela shell nunca existia. Fix: status 0 no sucesso (contagem ja em `summary->total_tasks`); taskmgr adaptado no mesmo commit. Retricao registrada nas partes 8-10 do KNOWN_BUGS (o "alive=1" era artefato do canal). PROVA 19_ns37: `task-event terminated pid=12` + `shell: command exit netmgrd rc=0` na serial do gate | marker no report | run 19_ns37 | — |
| 0.15 | Gate completo (22 comandos) vs wall do host 2-core | HARNESS FEITO; hipotese 0.17 (byte preso no buffer do 8042) REFUTADA por medicao direta ns44 (03/11): drain-first redundante no host NAO mudou o pace (~43s/char igual, wait-loop do kernel ja era drain-first) — o wall e starvation do main-loop TCG (kernel medido w<=6), NAO o caminho do input. O cap de 5s do probe era falso-positivo (ns43 marcou 'freeze' no 'e' enquanto o comando 1 fechava normal) -> recalibrado p/ 30s. Comando 1 `PASS netmgrd status` agora em 6 corridas consecutivas (36/37/38/40/41/43/44). Fechamento = 22x PASS numa janela de host ocioso (~2-3h, medicao de ambiente) | 22x PASS num report | corrida ociosa de fim de semana | 0.6 ✅ |

> 0.5/0.6 são o destravador silencioso de várias ondas: **três validadores**
> (`network-surface`, `abi-compat-surface`, `audio-stack`) não valem como gate
> enquanto a sessão interativa travar.

---

## Onda 1 — Rede definitiva

Fonte: `COMPLETION_ACTION_PLAN.md` P3, `NETWORK_AUDIO_PANEL_PLAN.md` (rede).
Já fechado (14–15/10, não reabrir): TX self-wipe, ARP/ICMP responders, loopback
127/8 real, roundtrip gateway, ARP cache multi-entrada+timeout, TX ring SMP-safe
8-frames, `validate-network-datapath` (com `drop=0` como gate).

| # | Item | Estado | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 1.1 | RX orientado a IRQ | polling 100 Hz, ring 8 slots | RX por interrupção do device com fallback de polling | `validate-network-datapath` + latência | — |
| 1.2 | TCP cliente real (backlog>1, sockets>8, timers RTT/RTO) | SYN/ACK existe, não validado end-to-end | conexão TCP real cliente↔servidor | novo `validate-network-tcp` | — |
| 1.3 | UDP cliente real (além de DHCP) | só DHCP | send/recv UDP a arbitrário | caso no validador | — |
| 1.4 | DNS do DHCP + resolv.conf-equivalente + servidores config. | hardcoded 10.0.2.3 | resolver lê servidor do lease e de config | `host`/`dig` resolvendo nome real | — |
| 1.5 | `ifconfig`/`route`/`netstat` com tabela real | imprimem snapshots | leem estado real do serviço | saída × estado | — |
| 1.6 | `curl`/`wget` com HTTP pela própria stack | curl só `file://`, wget inexistente | HTTP GET real | GET contra host do user-net | dep 1.2 |
| 1.7 | Gate HTTP via links2 | aberto | links2 busca página real | report | dep 1.2/1.6 |
| 1.8 | NIC abstraction `[!]` | esboçada em e1000.c:573–610 | `send_frame/attach_mac/link_status`; network.c fala com drivers registrados | virtio pelo caminho novo | **[!1]** |
| 1.9 | e1000 + rtl8139 ligados de verdade | ✅ FEITO | init a partir do entry, VA==PA de DMA, IRQ de RX | `validate-network-datapath NIC=e1000` / `NIC=rtl8139` PASS (KNOWN_BUGS #12/#13) | — |
| 1.10 | Wi-Fi: fim da fachada | `wifi_read_pci_bar0` BDF fixo 0:0:0, scan hardcoded, connect inventa IP | painel reporta "não disponível" e o data-plane não inicializa em produção | código + doc | — |
| 1.11 | Wi-Fi 802.11 real (driver+supplicant+CCMP) | `wifi_wpa2.c` tem cripto real órfã | associação real com AP | hwsim/HW | **[!5]** |
| 1.12 | Panel/UI de rede | scans hardcoded | reporta estado real | UI × estado | dep 1.10 |

---

## Onda 2 — SMP definitivo

Fonte: `COMPLETION_ACTION_PLAN.md` P4 + `KNOWN_BUGS.md` (classe #5) + `guidelines/smp.md`.

| # | Item | Estado | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 2.1 | Classe #5 face-1 (corrupção real-time smp2, flag ON) | aberta; camada de witnesses completa; escritor anônimo transiente | causa raiz corrigida ou classe eliminada por design | hw-watchpoint no T61 (Onda 8) + `validate-smp` smp2 verde | **HW** |
| 2.2 | `smp_scheduler_enable()` chamado após bring-up | ✅ FEITO (`entry.c:899`) | APs preempcam | `validate-smp` | — |
| 2.3 | Handler de IPI de reschedule | ✅ FEITO (0x82 wake + 0x83 timer) | reschedule imediato entre CPUs | IPI 0x82 preempta CPU ocupado (switches=3) | — |
| 2.4 | IOAPIC: afinidade de IRQ | ✅ FEITO p/ IRQ1/12 (`adb11cb11`); demais no BSP por design | IRQ roteado por afinidade | `validate-smp` + IRQ stats; keys drops=0 | — |
| 2.5 | smp4: bring-up dos 3 APs | ✅ FEITO | 4 CPUs online | `validate-smp` smp4 (4/4+enabled) | — |
| 2.6 | `QEMU_RUN_SMP` default 2 | default 1 | `make run` responsivo em 2 CPUs | `make run` + taskmgr 2 cores | **[!3]** |
| 2.7 | `validate-smp` como gate confiável | smp2 confinado ao shell-stall #5; UP 6/6 | verde repetível | 3 corridas limpas | dep 2.1 |

---

## Onda 3 — Áudio definitivo

Fonte: `NETWORK_AUDIO_PANEL_PLAN.md` (áudio), `AUDIO_HARDWARE_VALIDATION_MATRIX.md`, P5.

| # | Item | Estado | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 3.1 | HDA captura (input stream + BDL) | azalia só playback | record real | `validate-audio-stack-roundtrip` | — |
| 3.2 | IRQ compartilhado INTx em HDA/AC97 | `audio.c:5363` assume handler único | handler compartilhado | boot sem IRQ storm | — |
| 3.3 | Fechar `compat-azalia` | ~35 sub-itens abertos | rota canônica estável, sem re-probe agressivo | `validate-audio-hda-*` | — |
| 3.4 | `compat-uaudio` (USB audio) MVP | MVP UHCI-only | playback USB real | `validate-audio-stack` com USB | dep 0/2 |
| 3.5 | Gargalo PCM (chiado; DMA stale + start/stop) | aberto | playback limpo | gravação + inspeção | dep 3.3 |
| 3.6 | Matriz `AUDIO_HARDWARE_VALIDATION_MATRIX.md` (HW-01..HW-20) | 0/20 | cada linha com log real | logs | **HW** |
| 3.7 | Telemetria de áudio (IRQ/starvation/underrun) | ausente | contadores expostos | painel/log | — |

---

## Onda 4 — Vídeo/GPU definitivo

Fonte: `VIDEO_BACKEND_REWRITE_PLAN.md` (32 abertos), P6.

| # | Item | Estado | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 4.1 | Default do i915 (`INTEL_I915_EXPERIMENTAL_COMMIT=1` hoje) | default atual documentado | default definido e coerente | Makefile + doc | **[!4]** |
| 4.2 | MTRR como fallback opcional de WC | aberto | suporte ou NÃO-OBJETIVO escrito | doc | — |
| 4.3 | Atualizar `hw_validation.md`/`VIDEO_BACKEND_REWRITE_PLAN.md` vs código | drift | docs batem com o código | grep | — |
| 4.4 | Modos widescreen (1366x768/1360x768/1920x1080) | caem para 640x480/1024x768 | modos aplicados | `validate-video` | **HW**/firmware |
| 4.5 | Protocolo de estresse (vidstress/appcycle/dragstress/fill/gradiente) | parcial | matriz rodada e registrada | report | — |
| 4.6 | Campanha de heap no caminho gráfico | faltam coletas | antes/depois boot/startx/jogos | report | parcial **HW** |
| 4.7 | Bench de present (full-frame/WC/SSE2) | faltam medições | números + meta | report | parcial **HW** |
| 4.8 | Portar 1 backend de GPU real (i915/radeon/nouveau) | probe only | modeset real | snapshot/rollback + log | **HW** |

---

## Onda 5 — Runtimes definitivos

Fonte: P7 do `COMPLETION_ACTION_PLAN.md`, `guidelines/java.md`.

| # | Item | Estado | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 5.1 | **Placeholders falsos de js/ruby/python** | ✅ FEITO (`918376735`) — diagnóstico `ENOSYS` honesto | — | boot rescue: `js: NOT IMPLEMENTED (ENOSYS)` + `app: main returned` | — |
| 5.2 | quickjs real | `quickjs.c` vendorado + `js_main_real.c` pronto, **não linkado** (`QUICKJS_SRCS` comentado no Makefile): exige porto libc freestanding | REPL + script real | `validate-modular-apps` cenário js | — |
| 5.3 | mruby (idem: precisa artefatos pré-build) | stub honesto | REPL + script | validador | — |
| 5.4 | micropython (idem) | stub honesto | REPL + script | validador | — |
| 5.5 | lua + sectorc | existem; validar não-trivial | programa real | validador | — |
| 5.6 | Java (`java`/`javac` caseiros) | aberto | declarado experimental honesto OU JVM real | doc + smoke | **[!7]** |
| 5.7 | Heap/stack por classe de runtime (respeitar `MEMORY_ADAPTIVE_RAM_POLICY`) | aberto | medido por runtime | report de memória | — |

**DoD da onda:** `make validate-modular-apps` com **hello + programa não-trivial**
em js/ruby/python/lua/sectorc, sem stub em nenhum.

---

## Onda 6 — Userland/apps definitivos

Fontes: P8, `CRAFT_OPTIMIZATION_PLAN.md` (67 abertos),
`ICON_THEME_IMPLEMENTATION_PLAN.md` (39), `MODULAR_APP_INTEGRATION_PLAN.md` (6),
`COMPAT_GAMES_MENU_PORT_PLAN.md` (7), `NETWORK_AUDIO_PANEL_PLAN.md` (browser).

| # | Item | Estado | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 6.1 | **Shell rescue morre em rc≠0** | `shell.c:402` sai do loop em qualquer rc≠0; um typo mata a sessão | a shell sobrevive a erro de comando (mostra rc, continua) | sessão interativa: comando com erro + próxima linha roda | — |
| 6.2 | Craft = clone funcional (mundo, chunks async, SMP workers, renderer voxel, gameplay, perf) | `upstream/src/world.c` com 77 linhas; sem mundo visível | checklist 7.1–7.6 do CRAFT | sessão real: mundo, andar/destruir/colocar, sem corrupção | dep 2 |
| 6.3 | MicroUI: consumir ou remover | `desktop_new.c` fora do build | decidido e aplicado | build | — |
| 6.4 | Ramo órfão `shell_bsd.c` + `applications/ported/ksh` | FEITO: removidos — pdksh real já ligado ao terminal via `libksh.a` (`ksh_main` em `terminal.elf`); os 2 arquivos eram wrappers mortos | build limpo sem os wrappers | `make` verde + `validate-phase6-boot-smoke` 6/6 (abi-compat-surface segue família-flake pré-Onda 0) | — |
| 6.5 | links2 no catálogo/imagem | fora | entra no `config/app_catalog.tsv` + imagem | boot + execução | dep 1 |
| 6.6 | vibe-browser | Otter Qt5 vendored | status explícito OU engine mínimo | doc | **[!6]** |
| 6.7 | Icon theme: fechar (parser, cache, validação) | 39 abertos | gaps do plano fechados ou NÃO-OBJETIVO | boot + relatório de cobertura | — |
| 6.8 | Fontes: escalável ou multi-tamanho bitmap | 1 bitmap 6x8 | decisão + implementação | UI em várias resoluções | — |
| 6.9 | Jogos `compat/games` no menu: smoke por jogo | sem smoke | cada jogo abre por menu e shell | report por jogo | — |
| 6.10 | Modular app: paths explícitos + assets DOOM/Craft | faltam smokes | todos launchables no AppFS | `validate-modular-apps` | — |

---

## Onda 7 — POSIX/compat definitivo

Fontes: `ACTION_PLAN.md` P6/P9, `posix-graphics-xlike-plan.md` (56 abertos),
`guidelines/COMPAT_PLAN.md` (2), `MODULAR_APP_INTEGRATION_PLAN.md`.

| # | Item | Estado | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- | --- |
| 7.1 | `execve` real na camada compat | `sys_execve` existe no kernel (`syscall.c:2517`); prova end-to-end na camada compat pendente | exec real de binário | teste | — |
| 7.2 | `waitpid` com estados BSD-shaped | aberto | estados corretos | teste | — |
| 7.3 | Sinais completos (sigaction/sigprocmask/sigwait) | parcial | semântica completa | suíte | — |
| 7.4 | `poll`/`select` ligados a backends de espera do microkernel | aberto | espera real, sem busy-loop | teste + latência | — |
| 7.5 | VFS on-disk (além de ramfs) + ELF nativo do VFS | aberto | montagem + exec | teste | — |
| 7.6 | Semântica POSIX real de arquivo (append/trunc/write) | aberto | testes POSIX | suíte | — |
| 7.7 | X-Like: servidor compat isolado + janelas + WM + fontes | cliente shim pronto; Fases 3–8 abertas | fases 3/5/7 do plano X-Like | suíte QEMU gráfica | — |
| 7.8 | X-Like: isolamento de falha (matar cliente ≠ derrubar desktop) | aberto | provado | teste de falha | — |

---

## Onda 8 — Hardware real (T61/T400)

Fonte: `hw_validation.md`, `AUDIO_HARDWARE_VALIDATION_MATRIX.md`, P0, `usb_boot.md`.

| # | Item | DoD | Prova | Bloqueio |
| --- | --- | --- | --- | --- |
| 8.1 | Boot BIOS real com log serial | boot + desktop na máquina | log serial anexado | **HW** |
| 8.2 | USB mass-storage runtime nativo (EHCI/xHCI + BOT) | storage USB monta e lê; UHCI+**EHCI provados em QEMU** (ADENDO 48/49; EHCI q35+ich9-usb-ehci1, read+write+restore) | log | **HW** (xHCI/stick físico) |
| 8.3 | Áudio HDA/AC97 real | playback audível + captura | matriz HW preenchida | **HW** |
| 8.4 | Vídeo i915/radeon real | modeset nativo | log + snapshot | **HW** |
| 8.5 | Rede NIC real (T61/T400) | link + DHCP + ping | log | **HW** + Onda 1 |
| 8.6 | POSIX/timing em PIT/HPET reais | superfície syscall estável | suíte | **HW** |
| 8.7 | SMP real em 1 notebook | 2 CPUs distribuídos | taskmgr + log | **HW** + Onda 2 |
| 8.8 | Classe #5 face-1: hw-watchpoint no T61 | escritor anônimo transiente nomeado | watchpoint + log | **HW** + 2.1 |

> Nada em 8.x é "implementação": é **prova de silício**. Toda linha entra como
> BLOQUEADO-HW até existir a máquina; o código correspondente está nas ondas 0–7.

---

## Onda 9 — Fechamento e honestidade

Fonte: P9 (parcialmente fechado em `8560a0d97`), "Definição Prática de Acabou".

| # | Item | DoD | Prova |
| --- | --- | --- | --- |
| 9.1 | Zero afirmação "funcional/validado" sem linha no report `build/` | varredura de README/docs | grep + reports |
| 9.2 | README reduzido ao honestamente futuro (UEFI, IPv6, 3D accel) | seção final limpa | leitura |
| 9.3 | Planos absorvidos aposentados (pointer stubs) | arquivos com header "consolidado" | grep |
| 9.4 | `validate-smp`, `validate-network-*`, `validate-abi-*`, `validate-audio-*`, `validate-video`, `validate-modular-apps` verdes e repetíveis | matriz completa | reports |

---

## 2. Matriz de prova (comando por onda)

| Onda | Comando de gate | Report |
| --- | --- | --- |
| 0 | `make` + `make validate-phase6` + `make validate-network-surface` | `build/phase6-validation.md`, `build/network-surface-validation.md` |
| 1 | `make validate-network-datapath [NIC=e1000|rtl8139]` + `validate-network-surface` + novo `validate-network-tcp` | `build/network-datapath-validation*.md` |
| 2 | `make validate-smp` (smp2 + smp4) | `build/smp-validation.md` |
| 3 | `make validate-audio-stack`, `validate-audio-hda-*`, `validate-audio-matrix` | `build/audio-*.md` |
| 4 | `make validate-video`, `validate-startx-*`, `validate-gpu-backends` | `build/video-*.md` |
| 5 | `make validate-modular-apps` (+ cenário por runtime) | `build/modular-apps-validation.md` |
| 6 | `make validate-modular-apps`, `validate-craft-*`, `validate-doom-*` | `build/*.md` |
| 7 | suíte compat (a definir no item 7.2/7.4) | `build/compat-*.md` |
| 8 | logs seriais das máquinas físicas | `build/hw-*.md` |
| 9 | varredura + matriz completa | todos acima |

---

## 3. Não-objetivos declarados (para não virarem stub disfarçado)

- **UEFI** — boot legacy 32 bits apenas;
- **IPv6** — sem parsing/config; `supported_families` = UNIX|INET (já honesto);
- **Aceleração 3D** — nenhuma; DRM é modeset/2D;
- **Wayland / XCB / extensões X11 amplas** — X-Like mínimo é o alvo;
- **Suspender/retomar e hotplug** — não implementados;
- **VibeLoader (bootloader novo)** — `VIBELOADER_PLAN.md` se declara
  "funcionalmente fechado"; itens restantes = backlog **opcional** (config file,
  menu, imagem PNG no loader), fora das ondas;
- **Formatos de imagem além de BMP e PNG** no desktop.

---

## 4. Matriz de rastreabilidade (nada foi perdido)

| Plano de origem | Absorvido em |
| --- | --- |
| `COMPLETION_ACTION_PLAN.md` (P3–P9) | Ondas 0,1,2,3,4,5,6,9 + log de evidência |
| `FINALIZATION_EXECUTION_PLAN.md` | Ondas 1,3,8 + DoD global |
| `ACTION_PLAN.md` (P6–P9) | Ondas 6,7 |
| `VIDEO_BACKEND_REWRITE_PLAN.md` | Onda 4 |
| `NETWORK_AUDIO_PANEL_PLAN.md` | Ondas 1,3,6 |
| `CRAFT_OPTIMIZATION_PLAN.md` | Onda 6.2 |
| `ICON_THEME_IMPLEMENTATION_PLAN.md` | Onda 6.7 |
| `posix-graphics-xlike-plan.md` | Onda 7.7/7.8 |
| `usb_boot.md` | Ondas 0.1, 8.2 |
| `VIBELOADER_PLAN.md` | Não-objetivos (backlog opcional) |
| `guidelines/COMPAT_PLAN.md` | Onda 7.5/7.6 |
| `guidelines/MODULAR_APP_INTEGRATION_PLAN.md` | Onda 6.10 |
| `guidelines/COMPAT_GAMES_MENU_PORT_PLAN.md` | Onda 6.9 |
| `guidelines/java.md` | Onda 5.6 |
| `guidelines/MICROKERNEL_MIGRATION.md` (abertos) | Ondas 2,8 |
| `AUDIO_HARDWARE_VALIDATION_MATRIX.md` | Ondas 3.6, 8.3 |
| `KNOWN_BUGS.md` (Abertos: #19, SMP-stage-latent) | Ondas 0.6/0.10/0.11, 2.1 |
| `hw_validation.md` (unverified) | Ondas 4.3, 8 |
| fila `[!]` (0.4) | decisões da mantenedora, não implementação |

---

## 5. Definição de Acabou (global, vale para toda onda)

- [ ] `make` limpo, rc=0, **sem warnings**
- [ ] perfil de boot aplicável chega em desktop/shell sem `hlt`-loop
- [ ] `make validate-*` da área verde, report em `build/`
- [ ] causa raiz corrigida, não sintoma; nenhum stub/verde falso
- [ ] ABI intacta ou nova versão publicada; userland existente não quebrou
- [ ] doc/plano atualizado **no mesmo commit**, com a evidência
- [ ] item marcado FEITO/NÃO-OBJETIVO/BLOQUEADO-HW/DECISÃO — nunca "parcial" eterno

---

## 6. Estado inicial (ponto de partida verificado — `7eb20cc29` + campanha #19 de 01/11)

Já FECHADO e fora do backlog (não reabrir): boot chain real-BIOS (auditoria),
classes de corrupção SMP #1/#2, deadlock de serial, A20, CR0.CD/NW, trampoline
do AP, 58 guards de ponteiro de usuário, classe #3 refutada, guard de privilégio
classe #4, P2 persistência cross-boot, P3 datapath IPv4 (TX/ARP/ICMP/loopback/
roundtrip/cache/TX-ring), e1000/rtl8139 (KNOWN_BUGS #12/#13), `kernel_debug_printf`
width, 0.1 (USB count), P7 placeholders falsos, passagem P9 de honestidade
documental, 0.7/0.8 (`ee09e8b4a`), 0.6 modo-i (`7eb20cc29`), USB mass-storage
BOT+EHCI em QEMU (`b78dc41b7`/`3590c4113`), IOAPIC IRQ1/12 (`adb11cb11`),
P4 #14/#15 e classe #5 face-2.

Aberto hoje, por ordem de entrada nas ondas: **0.2, 0.3, 0.6 (modo-iii/#19),
0.9–0.11**, depois 1.x, 2.x, ... (0.8 FEITO em 30/10; 2.2–2.5 e 1.9 FEITOS em
outubro, ver "Mudou desde a v3").
