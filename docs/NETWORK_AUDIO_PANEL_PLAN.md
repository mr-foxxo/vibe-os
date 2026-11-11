# Plano de Rede + Som no Painel

Este e um plano ativo de produto/desktop. Para a visao arquitetural canônica da migracao de servicos, use [guidelines/MICROKERNEL_MIGRATION.md](guidelines/MICROKERNEL_MIGRATION.md).

Data da proposta: 2026-03-25

## Objetivo

Adicionar ao desktop do VibeOS dois applets no lado direito do painel:

- um applet de rede, antes do som, para listar redes Wi-Fi, pedir senha, conectar e mostrar estado da interface
- um applet de som, na extrema direita, para listar entradas/saidas de audio e controlar o volume de cada uma
- o `vibe-browser` continua importado no repo, mas ainda nao esta integrado ao desktop/AppFS
- deve mostrar na aba desempenho do gerenciador de tarefas o driver de som e o driver de rede detectados assim como ja e feito com a gpu
- deve se criar sprites para os applets de som e network no painel ao inves de usar letras
O plano prioriza reaproveitar ao maximo o que ja existe em `compat/` e no microkernel atual, em vez de criar APIs totalmente novas.
- usar o novo sistema de som para tentar reproduzir assets/vibe_os_boot.wav no bootloader uma unica vez, caso falhe na execução isso não deve impactar o boot.
- o mesmo vale para assets/vibe_os_desktop.wav que deve reproduzir uma unica vez ao carregar o startx. caso falhe tambem não deve travar a area de trabalho
## Resumo executivo

Conseguimos planejar isso de forma realista e com bastante reuso, mas os dois itens nao estao no mesmo estagio.
Hoje a verdade dura e simples e esta: a rede segue em MVP sintético de control-plane e o navegador ainda nao virou app desktop/AppFS de verdade.

- Som:
  - ha um caminho claro e incremental
  - a ABI BSD de audio/mixer ja esta importada
  - o servico `audio` ja existe no microkernel, embora ainda seja `query-only`
  - o applet de painel e o painel de volume sao viaveis cedo, mesmo com backend inicial simples

- Rede:
  - o servico `network` tambem existe, mas ainda esta em `query-only` e segue sustentado por estado sintético, nao por driver compat real
  - para DHCP e DNS ha muito reuso pronto em `compat`
  - para Wi-Fi real ainda faltam partes de stack e driver/control-plane 802.11
  - o equivalente a "NetworkManager" no VibeOS deve nascer como um daemon proprio e fino, reaproveitando `ifconfig`, `dhcpleased` e `unwind`, nao como um port literal do NetworkManager do Linux

- Navegador:
  - o `vibe-browser` ja esta no repo, mas ainda nao esta integrado ao desktop/AppFS do VibeOS
  - ele vira uma fase propria do projeto, dependente da stack de rede, DNS, downloads e UX de desktop

## Checklist de progresso

### Painel / Desktop

- [X] existe area de tray/applets no lado direito do painel
- [X] applet de rede foi adicionado antes do som
- [X] applet de som foi adicionado na extrema direita
- [X] sprites/icone dos applets substituíram as letras no painel
- [X] popup de som abre a partir do applet
- [X] popup de rede abre a partir do applet
- [X] popup de rede reutiliza perfis salvos e tenta auto-conexao no desktop
- [X] popup de rede exibe redes salvas na propria UI
- [X] popup de rede permite alternar auto-conexao e esquecer perfis salvos
- [X] popups de rede e som aceitam navegacao por teclado
- [X] desktop delega reconciliacao inicial de rede ao `netmgrd` MVP
- [X] popup de rede usa `netmgrd` MVP para conectar, desconectar e persistir perfis
- [X] a aba de desempenho do task manager mostra driver/backend de audio e rede
- [X] a aba de desempenho do task manager tambem mostra estado real de link/interface/IP/DNS
- [X] boot/startx tentam tocar os WAVs novos em modo best-effort
- [X] fallback de wallpaper foi endurecido para nao sumir em caso de config/decodificacao invalida

### Audio

- [X] servico `audio` responde a `GETINFO`
- [X] servico `audio` responde a `GET_STATUS`
- [X] servico `audio` responde a `GET_PARAMS`
- [X] servico `audio` responde a `MIXER_READ`
- [X] servico `audio` responde a `MIXER_WRITE`
- [X] syscalls/runtime de `START`, `STOP` e `WRITE` estao expostas para userland
- [X] existe playback one-shot de WAV por helper reaproveitavel
- [X] `soundctl list` existe
- [X] `soundctl status` existe
- [X] `soundctl set` existe
- [X] `soundctl mute` e `soundctl unmute` existem
- [X] `soundctl tone` existe para smoke test rapido
- [X] `soundctl play` existe para testar WAVs reais
- [X] `doom_port/i_sound_vibe.c` saiu do stub e usa o backend novo
- [X] backend `compat-auich` detecta hardware PCI suportado em familias AC97 compativeis alem de Intel
- [X] backend `compat-auich` faz bring-up basico de AC97/mixer/sample-rate
- [X] backend `compat-auich` possui caminho MVP de PCM out via DMA
- [X] telemetria de runtime de audio foi exposta para status/task manager
- [X] existe matriz explicita de backends de audio (`compat-azalia`, `compat-auich`, `pcspkr`)
- [X] fallback universal audivel via `pcspkr`/buzzer existe quando nenhum driver PCI utilizavel sobe
- [X] backend `compat-uaudio` para USB Audio Class foi portado
- [X] port honesto dos drivers necessarios presentes em `compat`
- [X] audio audivel real foi validado em runtime no QEMU com AC97
- [X] backend de audio tem IRQ/underrun handling robusto o suficiente para considerar o driver fechado
- [X] enumeracao de endpoints fisicos reais acima do mixer/driver esta pronta
- [X] captura real de audio esta pronta
- [X] HDA/`azalia` foi portada como segundo backend `compat`
- [X] existe matriz de validacao em hardware real para as ~20 maquinas alvo
- [X] no T61 real o audio ainda sai com cara de filme de terror / TV com interferencia; antes de nova rodada seria de validacao auditiva ainda falta acertar clock, formato PCM e/ou DMA do backend

### Rede

- [X] servico `network` responde a `GETINFO`
- [X] servico `network` responde a `GET_STATUS`
- [X] servico `network` expoe scan basico e operacoes de conectar/desconectar
- [X] `netctl` existe com `status`, `scan`, `connect` e `disconnect`
- [X] `netctl` tambem gerencia perfis salvos e auto-conexao
- [X] popup de rede mostra status/interface/IP/gateway/DNS
- [X] popup de rede tambem lista perfis salvos
- [X] task manager mostra backend/estado de rede e lease exportado pelo `netmgrd`
- [X] task manager tambem consome o snapshot exportado por `netmgrd`
- [X] o snapshot/status de rede agora tambem explicita a origem do lease aplicado em `em0`
- [X] o servico `network` agora sonda NIC PCI presente e parou de assumir sempre `em0` no estado exportado
- [X] socket/connect/send/recv MVP deixou de ser puro stub com loopback local
- [X] socket ABI MVP agora cobre `listen`/`accept` no loopback local
- [X] existe `netmgrd` MVP como app dedicado para reconciliacao/estado/perfis
- [X] ponte para aplicar lease IPv4 de `compat` em `em0` via `netmgrd`/runtime foi preparada
- [X] `netmgrd import-lease` já entende o texto do `dhcpleased` e normaliza isso para o runtime
- [X] `netmgrd reconcile` e `connect em0` já procuram automaticamente `/var/db/dhcpleased/em0`
- [X] port honesto dos drivers necessarios presentes em `compat`
- [X] existe `netmgrd` dedicado
- [X] ethernet com DHCP funcional — via the in-tree kernel stack (network.c virtio + DHCP client), NOT via compat reuse
- [ ] DNS: the live resolver is a minimal in-tree A-record client (compat/src/posix/netdb.c) with a HARDCODED server 10.0.2.3; the BSD `unwind` base is not integrated. Feed DHCP-provided DNS instead (COMPLETION_ACTION_PLAN P3)
- [ ] Wi-Fi: NOT functional. Scan results are hardcoded SSIDs (kernel/microkernel/network.c) and "connect" fabricates an IP; no 802.11 auth/assoc frames, no CCMP data path. wifi_wpa2.c crypto is real but orphaned. See COMPLETION_ACTION_PLAN P3
- [ ] socket ABI still needs work beyond the MVP (backlog=1, 8 sockets, no IPv6)
- [ ] ifconfig/route/netstat print snapshots (no real route table); curl supports file:// only; wget does not exist; `ping` uses the real ICMP client and `ping localhost` now goes through the kernel 127/8 loopback (socket → enqueue → recv) instead of a fabricated print — proven by `make validate-network-datapath` (loopback smoke marker). Track in COMPLETION_ACTION_PLAN P3
- [ ] DNS default/fallback servers configurable (currently hardcoded 10.0.2.3)

### Navegador

- [ ] `vibe-browser` esta integrado ao desktop como navegador padrao
- [ ] runtime/rede/downloads necessarios para o navegador estao prontos

## O que ja existe e pode ser reaproveitado

### Audio

- `headers/sys/audioio.h`
  - ABI BSD de audio e mixer
- `headers/kernel/microkernel/audio.h`
  - formato Vibe-native para o servico de audio
- `kernel/microkernel/audio.c`
  - servico `audio` ja registrado
- `userland/applications/games/doom_port/i_sound_vibe.c`
  - exemplo de consumidor de audio em userland
- `userland/applications/games/DOOM/sndserv/*`
  - referencia util para um servidor de audio simples baseado em mensagens/fila
- `compat/usr.bin/mixerctl/*`
  - referencia direta para modelo de mixer/class/devinfo
- `compat/usr.bin/sndioctl/*`
  - referencia direta para volume, mute e nomes de controles
- `compat/usr.bin/sndiod/*`
  - base para um servidor de audio mais completo depois do MVP
- drivers e infraestrutura de audio em `compat/sys/dev/*`
  - fonte de extracao para drivers reais

Estado atual:

- [X] o servico `audio` responde a `GETINFO`, `GET_STATUS`, `GET_PARAMS`, `MIXER_READ` e `MIXER_WRITE`
- [X] ja existe enumeracao compacta de controles no estilo BSD para `output/input` e `volume/mute`
- [X] o applet de som do painel ja le/escreve o mixer real
- [X] `soundctl` ja existe como CLI de diagnostico e controle
- [X] `START`, `STOP` e `WRITE` de audio estao implementados para playback
- [X] existe helper de WAV best-effort para boot/startx
- [X] existe backend `compat-auich` MVP com AC97 + DMA PCM out, agora com deteccao ampliada para AMD/ATI/NVIDIA/SiS/ALI/VIA compativeis
- [X] validacao final de audio audivel real no QEMU foi concluida
- [X] o driver `compat-auich` ja usa IRQ real, contabiliza progresso DMA por descritor e roda `soundctl tone` sem stubs
- [X] a deteccao PCI do backend AC97 compat agora cobre Intel ICH/ESB/440MX, AMD 768/8111, ATI SB200/SB300/SB400/SB600, NVIDIA nForce/MCP, SiS 7012, ALI M5455 e VIA VT82C686/VT8233
- [X] o backend `compat-auich` agora tambem puxa quirks reais de `compat/auich` para BAR nativo em memoria nos Intel ICH4+ e unmute especifico do SiS 7012
- [X] o backend `compat-auich` agora tambem tolera o quirk de Intel ICH4/5/6/7 que nao levanta `PCR`, seguindo o caminho `ignore_codecready` inspirado no `compat/auich`
- [X] o seletor `input default` agora programa `AC97_REG_RECORD_SELECT` para `mic`/`line`
- [X] `READ` de audio agora existe na ABI (`syscall`, runtime e `soundctl record`)
- [X] existe `audiosvc` MVP para exportar endpoints logicos, backend e defaults acima do mixer atual
- [X] `audiosvc` e `soundctl` agora exportam/listam endpoints conforme a topologia realmente detectada, em vez de anunciar sempre `main/headphones` e `mic/line`
- [X] a enumeracao de saidas agora considera tambem DACs AC97 extras (`surround` e `center-lfe`) quando o codec anuncia esses caminhos, em vez de assumir no maximo 2 saidas
- [X] o backend AC97 agora tambem ativa/programa os DACs extras (`surround` e `center-lfe`) via `EXT_AUDIO_CTRL` e registra os volumes/mutes correspondentes quando esses caminhos existem
- [X] o popup de som do desktop agora respeita a topologia detectada e nao desenha entradas inexistentes
- [X] a telemetria exportada pelo backend agora inclui flags de IRQ/captura e contagem real de endpoints para diagnostico
- [X] a telemetria de runtime agora diferencia IRQ observada, backend sem IRQ valida, starvation e underrun no status/export
- [X] a telemetria de diagnostico agora tambem expõe transporte `io/mmio`, quirk `ignore_codecready` e presenca de caminhos multicanal para `soundctl`/`audiosvc`/task manager
- [X] a telemetria de captura agora tambem expõe `capture-dma`, `capture-data` e `capture-xrun`, deixando a prontidao/atividade da captura menos implicita
- [X] a validacao automatizada em QEMU (`validate-audio-stack`) agora cobre `audiosvc status` + `soundctl status` com a telemetria nova
- [X] a validacao automatizada em QEMU agora tambem exige os marcadores de `transport`, `codecready-quirk` e `multichannel` exportados por `audiosvc`/`soundctl`
- [X] a validacao automatizada em QEMU agora tambem exige os marcadores de `capture-dma` exportados por `audiosvc`/`soundctl` quando a captura esta em teste
- [X] o roundtrip automatizado de captura+playback em QEMU (`validate-audio-stack-roundtrip`) passou em execucao sequencial; a falha vista antes era colisao de artefato ao rodar `make` concorrente no mesmo `build/`
- [X] `compat-auich` agora captura por DMA real no `PCMI` e ja retorna dados no QEMU com `AC97`; `soundctl record` ja grava `/capture.wav` no runtime em smoke curto e tambem em captura longa de 3s, mas ainda falta validacao em maquina fisica
- [X] existe smoke test separado no QEMU para `intel-hda`, agora confirmando o backend bootstrap `compat-azalia` em vez do fallback antigo para `softmix`
- [X] o endurecimento recente do validador tambem foi revalidado nos cenarios `validate-audio-stack-roundtrip` e `validate-audio-hda-smoke`
- [X] a frente de hardware real virou item explicito do plano: priorizar os chipsets presentes nas ~20 maquinas alvo e validar por PCI ID/codec/controladora, em vez de continuar so no QEMU
- [~] existe agora um backend `compat-azalia` bootstrap: detecta controladora HDA, faz reset MMIO basico, sobe CORB/RIRB, responde a probe minima de codec/AFG, registra IRQ/telemetria e passa a aparecer como backend proprio em vez de cair diretamente em `softmix`; o bootstrap agora tambem aproveita verbos/constantes de conexao do `compat/azalia` para ficar menos "stub disfarçado"
- [~] o `compat-azalia` agora tambem descobre widgets basicos de saida (DAC/pin), classifica pins fisicos por `config default`, tenta casar pin->DAC via `connection list`/`selector`, programa verbs iniciais de caminho de playback e arma stream descriptor + BDL no controlador; os cenarios `validate-audio-hda-smoke`, `validate-audio-hda-playback` e `validate-audio-hda-startup` passam no QEMU `intel-hda`, o codec sobe com `codec-probe=1`, `widget-probe=1` e agora tambem preserva `path-programmed=1` apos playback; o playback explícito ja recicla defensivamente o stream anterior entre chunks e o handler de IRQ HDA agora reconhece/limpa `BCIS`/`FIFOE`/`DESE`; os WAVs automáticos de `boot` e `desktop` agora completam no HDA no fluxo automatizado de startup; ainda falta revalidar isso em hardware real
- [~] mais uma aproximacao do `azalia_codec.c` entrou no caminho de widgets multi-connection: quando a rota ativa atravessa `AUDIO_MIXER`, o backend agora desmuta a entrada selecionada e muta explicitamente as paralelas, em vez de deixar o mixer inteiro aberto; isso aproxima os defaults praticos de `azalia_mixer_default()` sem puxar o framework completo do BSD
- [~] outra fatia estrutural do `compat/azalia` entrou no `jack sense`: pins de saida com presenca e suporte a `UNSOL` agora recebem `SET_UNSOLICITED_RESPONSE`, eventos assincronos invalidam imediatamente o cache de presenca/rota para o proximo `START/WRITE`, e a politica de saida passou a aplicar mute do `speaker` interno quando existe jack externo realmente presente, alem de desligar explicitamente jacks ausentes em vez de continuar "primando" rota morta; `validate-audio-hda-smoke` e `validate-audio-hda-playback` seguiram verdes depois desse lote
- [~] o `compat-azalia` agora tambem materializa uma trilha canonica `pin -> ... -> dac` antes de programar playback: `connection select`, `power state`, `input/output amp` e `pin ctl` passaram a andar pela mesma rota resolvida, em vez de cada etapa recalcular o ramo por conta propria; junto disso, a validacao de `widget_has_output_path()` deixou de aceitar `PIN/AUD_IN` como terminal valido no meio do grafo de playback. `validate-audio-hda-smoke` e `validate-audio-hda-playback` voltaram a passar com esse endurecimento
- [~] outra aproximacao direta do `azalia` do BSD entrou no probe: widgets `AUDIO_MIXER`/`AUDIO_SELECTOR` sem cadeia de conexao plausivel agora sao podados cedo no scan, no mesmo espirito de `widget_check_conn()`, e o rebalance dos outputs passa a consolidar a rota escolhida no proprio codec em vez de ficar so no cache local; alem disso, o mute automatico do `speaker` interno ganhou metodo mais proximo do BSD (`pin outamp`, `pin ctl` ou `dac outamp`, conforme a topologia). Esse lote ainda precisa revalidacao limpa porque o ambiente de teste esbarrou em falhas paralelas de build/AppFS
- [~] o tratamento de `UNSOL` no `compat-azalia` tambem ficou menos passivo: quando um evento de jack chega para uma saida observada e a rota atual segue valida, o backend agora refresca a presenca e reaplica imediatamente a politica de `speaker`/pins em vez de apenas zerar cache e esperar o proximo `START`; isso aproxima melhor o mute de speaker guiado por jack do comportamento do BSD em notebook real
- [~] a poda de widgets do `compat-azalia` deixou de ser so diagnostico de probe e passou a virar estado persistente de topologia: mixers/selectors descartados no scan agora ficam realmente desabilitados para `find_output_dac()`, `resolve_output_path()`, `rebind` e validacao da rota corrente, no espirito do `w->enable` do BSD; `validate-audio-hda-smoke` e `validate-audio-hda-playback` voltaram a passar com esse endurecimento
- [~] outra aproximacao do `select_spkrdac()` do BSD entrou no rebalance de saidas: quando o `speaker` interno ainda divide DAC com saidas externas depois do scan, o backend agora tenta primeiro dar um DAC proprio ao speaker e, se nao houver rota alternativa para ele, passa a empurrar cada saida externa conflitante para outro DAC compativel ate quebrar o conflito; `validate-audio-hda-smoke` e `validate-audio-hda-playback` seguiram verdes
- [~] outra nuance do `compat` entrou na programacao da rota: `AUDIO_SELECTOR` com conexao unica e `outamp` proprio agora deixa de ser tratado sempre como mero `input amp`; quando o widget filho nao anuncia `input amp` ou `output amp`, o backend passa a usar o `outamp` do proprio selector, no mesmo espirito do caso especial do OpenBSD para widgets de conexao unica; `validate-audio-hda-smoke` e `validate-audio-hda-playback` seguiram verdes
- [~] mais uma aproximacao do caminho de notebook do BSD entrou no `compat-azalia`: quando o codec anuncia mais de um pin fixo de `speaker`, o backend agora preserva um `speaker2` enxuto em vez de colapsar tudo no primeiro pin; esse segundo speaker participa do `commit` de rota e da politica de mute/pin para acompanhar o `speaker` principal, o que aproxima o comportamento de `speaker/speaker2` do OpenBSD sem puxar o mixer framework inteiro. `validate-audio-hda-smoke` e `validate-audio-hda-playback` seguiram verdes
- [X] o `compat-azalia` agora registra rotas por endpoint fisico (`speaker`, `headphones`, `line-out`, `digital`) em vez de depender de um unico pin "melhor"; o `default output` do mixer passa a escolher o pin/DAC correspondente ao endpoint HDA detectado, e `audiosvc`/`soundctl`/popup exportam esses nomes fisicos quando o backend ativo e `compat-azalia`
- [X] o playback WAV em HDA foi endurecido tambem para os sons automáticos: `soundctl play` fatia o WAV em bursts HDA e aguarda o backend ficar ocioso entre chunks; para autoplay, o `desktop` agora dispara uma unica vez, com atraso, depois do inicio da sessao, e o `boot` deixou de cair em `defer`; o `desktop-session` passou a ser reproduzido em modo cooperativo no loop do desktop, sem monopolizar a UI enquanto o HDA esvazia cada chunk; os alvos `validate-audio-hda-startup` e `validate-audio-hda-playback` voltaram a passar juntos no QEMU `intel-hda`
- [X] o alvo `validate-audio-hda-playback` agora exige `path-programmed=1` apos playback, endurecendo a validacao do `compat-azalia` no proprio fluxo padrao do repo
- [X] a selecao inicial do audio agora tenta multiplos candidatos PCI HDA/AC97 antes de aceitar `softmix`; quando o fallback ainda acontece, `device.config` diferencia `no-pci-audio` de `no-usable-hw-backend`
- [X] a telemetria e o UX de diagnostico do audio ficaram mais acionaveis em hardware real: `audiosvc`, `soundctl`, task manager e o `audioplayer` agora mostram hints para estados como `hda-no-output-stream`, `hda-reset-failed` e `no-usable-hw-backend`, em vez de deixar isso implicito
- [X] a telemetria de hardware do audio ficou mais util para a matriz de maquinas reais: `GETINFO` agora carrega PCI ID/localizacao da controladora, codec HDA detectado e a rota de saida `pin/dac`; `audiosvc`, `soundctl` e o task manager passaram a exibir isso diretamente
- [X] a validacao HDA ficou endurecida tambem no diagnostico de hardware: `validate-audio-hda-playback` agora exige no serial os markers `pci=`, `codec=` e `route=` emitidos por `audiosvc`/`soundctl`, e o fluxo voltou a passar no QEMU `intel-hda`
- [X] o diagnostico do `audioplayer` ficou mais acionavel em maquina real: o detalhe da ultima falha agora tambem inclui PCI ID da controladora, codec HDA e rota `pin/dac`, e a linha de status do app foi ampliada para caber esse contexto
- [X] o `compat-azalia` agora nao programa so pin/EAPD: durante a selecao da rota de playback ele tambem tenta desmutar e aplicar ganho basico nos amplificadores de saida/entrada ao longo do caminho HDA escolhido (pin/mixer/selector/DAC), ativa defensivamente os demais pins fisicos de saida detectados, sobe o function group e os widgets da rota para power state `D0` e passa a logar a rota `pin/dac` escolhida; `validate-audio-hda-playback` continuou verde depois desse endurecimento
- [X] o `compat-azalia` agora tambem replica quirks basicos do `azalia` de `compat` no lado PCI/controlador: habilita back-to-back no comando PCI, limpa `HDTCSEL`, desabilita `no-snoop` nos Intel HDA e preserva os bits nao relacionados de `SD_CTL2` ao programar o stream; alem disso, o start do stream agora valida se o bit `RUN` realmente armou e tenta rotacionar para outro output stream quando o primeiro descriptor aceita reset mas nao entra em execucao, cobrindo melhor chipsets Intel `ICH8/82801H` como `8086:284b`
- [X] o `compat-azalia` agora tambem importa uma primeira leva de quirks de codec do `azalia` de `compat`: aplica `GPIO unmute/polarity` em codecs que dependem disso, fecha o bypass de `PC beep` em alguns Realtek e corrige `config default` de pinos dock em familias ThinkPad, reduzindo os casos em que o codec responde mas o path real do notebook continua mudo
- [~] a compatibilidade HDA em notebook real ainda esta em fechamento: o `compat-azalia` deixou de depender de um unico output stream fixo e agora varre/rota candidatos de playback a partir de `ISS`/`OSS`/`BSS` do `GCAP`, reprogride o codec para o novo stream quando o stream inicial falha e tolera topologias reais em que a selecao formal de rota pin->DAC nao fecha exatamente como no QEMU; ainda falta revalidacao fora do QEMU para confirmar que o erro de playback no player foi resolvido em hardware Intel HDA real
- [~] o foco atual voltou para fechar `compat-azalia` em notebook real: o probe HDA agora refresca `STATESTS` de forma menos fragil, nao aceita mais como sucesso o primeiro codec que responde sem `Audio Function Group`, reporta `hda-no-audio-fg`/`hda-no-usable-output` de forma mais honesta e reproba codec/widgets no `START` quando a primeira programacao do caminho falha; alem disso, quando a topologia ja aponta para o DAC certo por selecao default do codec, o backend passa a reconhecer isso como `route already-selected` em vez de tratar tudo como falha formal de rota
- [~] a selecao de saida do `compat-azalia` ficou mais "notebook-aware": quando o codec anuncia `pin sense`, o backend agora marca quais saidas fisicas realmente aparecem como presentes e passa a preferi-las na escolha do caminho HDA; ainda preserva speaker/line-out/digital como fallback, mas deixa de insistir cegamente num unico pin "melhor" quando outro jack/endpoint esta claramente ativo
- [~] o `pin sense` HDA deixou de rodar no `probe_widgets()` do boot e ficou deferido para o `START`; isso preserva a heuristica de saida em runtime, mas reduz o risco de travar a maquina cedo durante a fase ainda coberta por "Initializing VFS..."
- [~] o `START` do `compat-azalia` ficou menos destrutivo em maquina real: se um reprobe falhar durante a reprogramacao do caminho HDA, o backend agora preserva a topologia previamente descoberta em vez de zerar `codec/route`, e tambem tenta mais de um candidato `pin/dac` antes de desistir do playback
- [~] o `compat-azalia` agora tambem refresca `pin sense` de saida no proprio `START` e evita "primar" jacks ausentes como se estivessem ativos; isso reduz o risco de um headphone desconectado ou stale state do probe contaminar a rota efetiva em notebook real
- [~] o fallback de selecao HDA no `START` ficou menos teimoso: candidatos `jack/both` marcados como ausentes deixam de entrar na rodada secundaria de `pin/dac`, reduzindo as tentativas em rotas claramente desconectadas antes de cair em speaker/line-out fixo
- [~] o fallback de programacao HDA no `START` agora tambem tenta `dac-only` por candidato, em vez de depender de um unico estado global; isso cobre melhor codecs reais em que o conversor certo ja esta selecionado/ligado, mas a selecao formal do caminho `pin->dac` continua falhando
- [~] o `stream_start_buffer` do `compat-azalia` agora tambem faz um reprobe leve de `codec/widgets` quando a programacao do caminho HDA falha ou quando o bit `RUN` nao entra em latch; isso reduz o numero de falhas "definitivas" causadas por topologia stale no momento exato do `START`
- [~] a programacao de candidato HDA ficou mais honesta: um `pin/dac` so e aceito como sucesso quando `path-programmed=1`; se a rota formal falha e o backend nao consegue provar `route already-selected`/`dac-only`, o candidato agora e rejeitado explicitamente e o `START` segue para o proximo fallback em vez de mascarar uma rota quebrada
- [~] o diagnostico de falha do `START` HDA ficou mais especifico em runtime: `device.config` agora diferencia `hda-stream-reset-failed`, `hda-codec-connect-failed` e `hda-run-not-latched`, e volta para a localizacao PCI normal quando o stream arma; isso deve tornar a proxima linha de erro em hardware real bem mais acionavel
- [~] o `stream_start_buffer` do `compat-azalia` deixou de reprograr a rota HDA inteira em todo chunk quando `path-programmed` ja esta valido; em playback continuo isso reduz bastante o custo do `write()` no kernel e deve aliviar travamentos perceptiveis da UI enquanto o desktop toca audio
- [~] o `START` HDA agora tambem evita refresh agressivo de `pin sense` quando a mesma rota `pin/dac` segue valida e o cache recente ainda esta fresco; durante playback continuo, o backend alonga esse cache de presenca e cai direto em `rebind` do stream, reduzindo consultas de jack no caminho quente sem perder a capacidade de reavaliar a saida depois que o stream para
- [~] o `stream_start_buffer` do `compat-azalia` tambem deixou de recalcular a mesma validade de rota varias vezes no mesmo ciclo de `START`; o backend agora carrega esse estado uma vez por tentativa e o reaproveita nas decisoes de fast-path, `rebind` e fallback, cortando trabalho redundante no playback continuo
- [~] o fallback de `hda-run-not-latched` ficou mais barato e mais honesto: o backend agora tenta primeiro rotacionar/rearmar o output stream e so cai em reprobe completo de `codec/widgets` quando a rota atual ja nao parece valida, evitando gastar topologia inteira em falhas que tem cara de controladora/stream
- [~] o caminho `hda-codec-connect-failed` tambem ficou menos destrutivo quando a rota HDA corrente ainda parece boa: antes de reprobar codec/widgets, o backend agora tenta primeiro rotacionar e rearmar o output stream, separando melhor falha de stream/controladora de falha real de topologia
- [~] o fallback de `hda-stream-reset-failed` tambem ficou mais barato quando a topologia ja estava valida: apos rotacionar o output stream, o backend agora tenta `rebind` leve no caminho HDA atual antes de cair de novo em `program_output_path()`, reduzindo custo no caso em que so o stream descriptor ficou ruim
- [~] a propria selecao/rotacao de output stream do `compat-azalia` tambem ficou um pouco mais leve: o helper de candidato deixou de salvar/restaurar estado que ja nao era necessario e passou a apenas fixar `index/number/regbase`, reduzindo overhead nas tentativas de stream durante `START` e fallback
- [~] a progressao de fallback entre output streams tambem ficou mais coerente: dentro do mesmo `START`, o backend agora avanca o ponto de partida de rotacao conforme o stream corrente muda, evitando reinsistir no mesmo descriptor inicial quando ja houve uma troca anterior
- [~] o bring-up do controlador HDA tambem ficou mais conservador para hardware Intel antigo: o reset global agora aplica pequenos settles apos `CRST`, o init das command rings passa a habilitar `RINTCTL` no `RIRB` e so prossegue quando `CORBRUN` e `RIRB DMAEN` realmente latam; isso reduz os falsos positivos em que o codec aparece no probe mas o controlador nao entra num estado confiavel para comandar playback
- [~] teste real no ThinkPad T61 (`8086:284b`): o bootloader subiu sem som, o desktop demorou minutos para aparecer, o mouse ficou congelado por minutos, a aba de desempenho mostrou `INTEL-HDA COMPAT_AZALIA HDA-NO-AUDIO-FG MIXER PLAY OUT`, e clicar em `play` no player voltou a congelar a UI por minutos antes do log `falha ao iniciar compat azalia cfg=hda-no-audio-fg act=0 pend=0 path=0 irq=1/0 pci=8086:284b`; a correcao imediata em andamento eh fazer o servico parar de tratar esse HDA sem `Audio Function Group` como backend usavel e falhar rapido em `START/WRITE` em vez de insistir em reprobe pesado
- [~] comparacao direta com `compat/sys/dev/pci/azalia.c` ja rendeu mais duas aproximacoes no bring-up HDA nativo: o probe do codec agora faz uma varredura heuristica de NIDs baixos quando o codec responde mas o root nao entrega `Audio Function Group` utilizavel, e ao encontrar o AFG ja o sobe para `D0` imediatamente antes de continuar; o smoke `validate_audio_hda_smoke` seguiu verde em QEMU depois disso (`build/audio-hda-smoke-after-compat-compare.md`)
- [~] o fallback final tambem ficou mais honesto para casos como o T61: quando o servico abandona um HDA detectado por cair em `hda-no-audio-fg` ou `hda-no-usable-output`, o `pcspkr` agora preserva essa causa no `device.config` (`pcspkr-fallback-hda-no-audio-fg` / `pcspkr-fallback-hda-no-usable-output`) em vez de esconder tudo sob um erro generico de "no usable hw backend"
- [~] nova rodada focada no ThinkPad T61 (`8086:284b`): o bring-up HDA agora espelha melhor o `compat` em dois pontos que importam para controladoras/codificadores antigos: o reset de `CORB/RIRB` ficou mais fiel ao driver maduro (incluindo limpeza/poll do `CORBRP` reset bit), e o probe de widgets passou a subir `widgets`/`pins` para `D0`, inicializar `PIN_WIDGET_CONTROL` de forma mais cedo e cair num `legacy nid scan` (`2..0x3f`) quando os `subnodes` do `Audio Function Group` vierem quebrados; o smoke `compat-azalia` no QEMU continuou verde depois dessa rodada (`build/audio-hda-smoke-after-t61-hardening.md`)
- [~] mais uma aproximacao direta com o `compat/azalia` entrou no caminho de verbos HDA: o leitor de `RIRB` agora anda entrada por entrada e ignora respostas `unsolicited`, como o driver maduro faz, em vez de aceitar cegamente a primeira resposta que aparecer; isso mira exatamente o risco de o probe do codec no T61 estar lendo um evento assíncrono como se fosse a resposta do verbo e por isso concluir falsamente `hda-no-audio-fg`
- [~] o probe do codec no `compat-azalia` tambem ficou menos fragil quando a metadata do root vem quebrada: se o codec responde ao `VENDOR_ID` mas o `SUB_NODE_COUNT` do root falha ou nao lista function groups uteis, o backend agora registra isso no log e ainda cai no `legacy fg scan` em vez de abortar cedo; essa aproximacao tenta cobrir exatamente o tipo de HDA antigo em que o codec esta vivo, mas o root node nao se comporta de forma totalmente confiavel
- [~] outra fatia pequena do `azalia_codec_init()` do BSD entrou no lado Vibe: depois de descobrir o `Audio Function Group`, o backend agora sincroniza e guarda no estado os parametros-base do function group (`stream formats`, `PCM`, `input/output amp caps`) e reaproveita esses amp caps nos widgets que nao anunciam override proprio; isso deixa o bring-up e a programacao de ganho menos dependentes de respostas por-widget potencialmente capadas em codecs HDA antigos
- [~] a descoberta de widgets de audio tambem ficou mais proxima do `azalia_widget_init_audio()` do BSD: widgets `AUD_OUT/AUD_IN` agora so entram como candidatos se passarem pela checagem de `stream formats`/`PCM`, com fallback para as capacidades do function group quando o widget nao anuncia override ou devolve zero; isso reduz o risco de o backend escolher um conversor "morto" ou sem PCM real como DAC de playback
- [~] o caminho de verbos HDA ficou mais fiel ao `compat` tambem no tratamento de `RIRB`: o kernel agora limpa `RINTFL/RIRBOIS` explicitamente, drena esses bits no IRQ e rearma `CORB/RIRB` uma vez quando um comando pega timeout/overflow antes de cair no verbo imediato; isso tenta cobrir o caso de controladora Intel antiga em que o ring "parece vivo", mas entra em estado stale bem no probe do codec/AFG
- [~] mais uma heuristica foi trocada por comportamento do `compat`: ao procurar um DAC de playback, mixers e selectors agora so continuam elegiveis se realmente houver um caminho recursivo ate um `AUD_OUT` valido, em vez de o backend seguir qualquer connection list existente; isso aproxima o Vibe do `widget_check_conn()` do BSD e reduz o risco de topologia "fantasma" em codecs HDA de notebook
- [~] a enumeracao de widgets/pins/connections HDA tambem ficou menos fragil em hardware velho: leituras de `AUDIO_WIDGET_CAP`, `PIN_CAP`, `STREAM_FORMATS`, `PCM`, `CONNECTION_LIST_LENGTH`, `CONNECTION_LIST_ENTRY`, `CONFIG_DEFAULT` e `GET_CONNECTION_SELECT` agora fazem retries curtos antes de desistir; isso mira especificamente o caso do T61 em que uma unica resposta perdida no meio do scan pode derrubar o codec inteiro em `hda-no-audio-fg`/rota vazia
- [~] a aceitacao do `Audio Function Group` tambem ficou mais literal ao `compat`: quando o backend encontra um AFG por `function group type`, ele agora valida cedo se o proprio AFG anuncia uma faixa de widgets plausivel (`subnodes` com `first nid >= 2`) antes de promovelo; isso reduz falso positivo de FG "fantasma" que responde a verbos mas nao entrega uma arvore de widgets coerente
- [~] a descoberta de widgets tambem ficou menos dependente da metadata "bonita" do AFG: se a faixa anunciada por `SUB_NODE_COUNT` parece valida mas nao rende nenhum widget real no scan, o backend agora refaz a busca em `legacy nid scan` (`2..0x3f`) antes de desistir; isso cobre melhor codecs antigos que respondem ao AFG mas anunciam `widget range` incompleto ou enganoso
- [~] a transicao do `Audio Function Group` para o scan de widgets tambem ficou mais robusta: depois de subir o AFG para `D0`, o backend agora espera um settle curto e faz retries em `SUB_NODE_COUNT` do proprio AFG antes de cair no `legacy nid scan`; isso aproxima mais o timing tolerante do `compat` e ajuda quando o codec acorda "meio tarde" em HDA antigo
- [~] a coleta dos parametros-base do `Audio Function Group` tambem ficou menos fragil: `STREAM_FORMATS`, `PCM` e `INPUT/OUTPUT_AMP_CAP` agora sao lidos so depois de repor o AFG em `D0` com um settle curto e retries curtos por parametro; isso reduz o risco de o estado-base do codec nascer zerado ou parcial logo apos o wake no T61
- [~] nova rodada de endurecimento do `compat-azalia`: antes de cada verbo via `CORB/RIRB`, o backend agora descarta respostas `RIRB` solicitadas atrasadas para nao casar o comando atual com reply stale; o power-up de widgets ficou mais fiel ao `compat` com `D0` + settle curto apenas quando o widget realmente anuncia power management; e o scan de widgets passou a cair no `legacy nid scan` tambem quando a faixa oficial do `AFG` ate responde, mas nao produz nenhuma saida utilizavel. A rodada seguinte tambem levou `START`/`WRITE` a falhar mais rapido e a chamar failover quando o HDA prova que nao sobe. O kernel recompilou, mas o smoke `validate-audio-hda-smoke` desta rodada ainda ficou `FAIL` em QEMU por timeout antes de `startx`/`audiosvc` (`build/audio-hda-validation.md`), entao a validacao funcional ainda nao voltou ao estado verde
- [X] nova rodada focada em reduzir o risco de congelamento em HDA antigo sem regredir o caminho verde do QEMU: o `compat-azalia` agora usa timeouts de `stream reset`/`CORB-RIRB` alinhados ao `compat`, trocou o polling bruto mais quente por espera cooperativa e passou a enfileirar respostas `unsolicited` antes de processa-las, em vez de misturar isso inline no caminho do verbo atual; com isso os tres cenarios `validate-audio-hda-smoke`, `validate-audio-hda-playback` e `validate-audio-hda-startup` voltaram a passar no QEMU `intel-hda`, e o backend ficou menos propenso a travar minutos quando a controladora antiga entra em estado ruim
- [X] mais uma rodada para estabilizar o lote anterior sem mascarar falha real: a politica de `speaker`/`pin sense` agora bloqueia reentrada enquanto reaplica `apply_output_pin_policy()`, o power-up de widgets passa a reutilizar cache `D0` por `nid` e o settle por widget caiu para um valor curto; com isso o probe/runtime quente do `compat-azalia` ficou mais leve, `build/kernel/microkernel/audio.o` voltou a compilar limpo e os tres validadores HDA (`validate-audio-hda-smoke`, `validate-audio-hda-playback` e `validate-audio-hda-startup`) seguiram verdes em sequencia no QEMU `intel-hda`
- [X] o fallback final de audio deixou de ser sempre silencioso: quando HDA/AC97 nao ficam utilizaveis, o servico agora sobe um backend `pcspkr` baseado no PIT/channel 2 e no speaker legacy (`porta 0x61`), aparecendo em `audiosvc`/`soundctl` como backend proprio em vez de ficar so em `softmix`
- [~] o backend `pcspkr` ja garante um fallback audivel praticamente universal em desktops/notebooks x86, inclusive para `soundctl tone` e para WAV/PCM em modo degradado; a qualidade ainda e propositalmente rudimentar, com reducao do PCM para blocos curtos de tom no buzzer
- [~] o gargalo atual do `compat-azalia` deixou de ser bring-up de codec/topologia e ficou concentrado no playback PCM do kernel: ainda ha chiado, artefato visual e sinais de acoplamento entre audio, compositor e input. A leitura mais provavel neste ponto e dupla: (1) paginas DMA do HDA ainda podem nascer com lixo/stale tail, e (2) o backend continua muito perto de um modelo `start/stop por write`, em vez de um stream persistente com refill assíncrono
- [ ] o terceiro backend amplo planejado agora e `compat-uaudio` para USB Audio Class; o repo ja tem a base `compat/sys/dev/usb/uaudio.c`, mas o VibeOS ainda precisa hospedar/controlar uma stack USB nativa suficiente para enumerar controladoras/dispositivos e dar substrate real para esse port
- [~] o groundwork de USB para destravar `compat-uaudio` ja comecou no kernel: o VibeOS agora descobre host controllers USB PCI (`UHCI`/`OHCI`/`EHCI`/`XHCI`) durante o boot, habilita o dispositivo PCI, inventaria BAR/IRQ/tipo/portas estimadas, le o estado bruto das portas do root hub, infere speed hint (`low/full/high/super`) para portas ocupadas e loga um sumario proprio; ainda falta attach de bus/root hub e enumeracao real de dispositivos/interfaces/endpoints
- [X] o fallback de audio agora tambem diferencia o caso "sem audio PCI, mas com host USB presente": quando isso acontece, `device.config` passa a indicar `pcspkr-fallback-usb-host-present`, deixando explicito no diagnostico que o proximo backend natural ali e `compat-uaudio`
- [X] o fallback de audio agora tambem diferencia o caso "ha algo plugado em USB": quando nenhuma placa de audio PCI sobe e o root hub ja reporta porta ocupada, `device.config` passa a indicar `pcspkr-fallback-usb-device-present`, deixando explicito no diagnostico que falta exatamente o backend/enum de `uaudio`
- [X] o fallback de audio agora tambem diferencia um candidato USB mais plausivel para A/V: quando nao existe audio PCI e o root hub ve porta ocupada em `high/super speed`, `device.config` passa a indicar `pcspkr-fallback-usb-av-candidate`, ajudando a priorizar a frente `compat-uaudio`
- [X] o groundwork USB agora tambem materializa um inventario minimo de dispositivos vistos diretamente no root hub, com `controller/port/speed/flags`, e contabiliza `audio-candidates` plausiveis para o diagnostico do audio antes mesmo de existir enumeracao completa de descritores
- [X] o fallback de audio agora tambem diferencia o caso "ja existe candidato plausivel para audio USB": quando nenhuma placa PCI sobe e o inventario do root hub ve dispositivo em velocidade/estado compativeis com a proxima fase do `uaudio`, `device.config` passa a indicar `pcspkr-fallback-usb-audio-candidate`
- [X] o substrate USB agora tambem cria `device slots` minimos acima do root hub, com `address/controller/port/speed/state/flags`, para deixar a proxima fase de enumeracao partir de "dispositivo anexado" em vez de apenas "porta ocupada"
- [X] o fallback de audio agora tambem diferencia quando ja existe dispositivo USB pronto para a proxima fase de enumeracao: `pcspkr-fallback-usb-enum-ready` e `pcspkr-fallback-usb-enum-ready-audio`
- [X] o substrate USB agora tambem separa melhor os estados do primeiro passo de enumeracao: `ready-for-control` quando o host/porta ja parecem aptos ao futuro control path minimo, e `needs-companion` quando um device atras de `EHCI` ainda depende de handoff para companion controller
- [X] o fallback de audio agora tambem diferencia esses dois estados mais proximos do `GET_DESCRIPTOR`: `pcspkr-fallback-usb-control-ready`, `pcspkr-fallback-usb-control-ready-audio` e `pcspkr-fallback-usb-needs-companion-audio`
- [X] o substrate USB agora tambem tenta resolver companions provaveis de `EHCI` para `UHCI/OHCI` no mesmo slot PCI e propaga isso para os `device slots`, aproximando o diagnostico do primeiro handoff real
- [X] o fallback de audio agora tambem diferencia quando o problema ja nao e "faltou companion", e sim "falta implementar o handoff/control path": `pcspkr-fallback-usb-companion-available-audio`

## Plano operacional detalhado para fechar `compat-azalia` no T61

### Plano curto de estabilizacao do DMA HDA sem regredir desktop/I/O

Objetivo imediato:

- atacar chiado, artefato e acoplamento `audio <-> mouse/video` no kernel antes de abrir nova frente grande
- preservar o estado atual que ja ficou aceitavel no QEMU: bootloader sound no `stage2`, startup sound do desktop e compositor subindo sem depender de input

Fase 1. Higiene de DMA e cauda de buffer

- zerar todas as paginas fisicas usadas pelo playback HDA logo apos `alloc_phys_page()`
- zerar explicitamente a sobra do buffer DMA alem de `bytes` em cada write
- revisar `LVI/CBL/FIFOW/LPIB` para garantir que o hardware nao leia cauda stale por arredondamento/prefetch
- nao mudar a orquestracao de desktop/bootstrap nesta fase

Risco:

- baixo
- esta fase deve mexer quase so em `kernel/microkernel/audio.c`
- a chance de travar desktop/I/O aqui e pequena se o contrato de `sys_audio_write(_async)` for mantido

Criterio de saida:

- `validate-audio-hda-startup` continua verde
- `validate-audio-hda-playback` continua verde
- reducao perceptivel de chiado/artefato no QEMU

Fase 2. Stream persistente em vez de `restart por write`

- parar de tratar cada write como mini-sessao HDA independente
- manter stream/BDL ativos enquanto houver playback em andamento
- trocar `halt/restart` frequente por refill cooperativo do buffer
- usar IRQ/BDL recycle como fonte principal de progresso e polling apenas como fallback

Risco:

- medio
- aqui ja existe chance real de regressao em input, compositor ou scheduler cooperativo se a fila ficar mal drenada

Criterio de saida:

- sumir o sintoma "audio so anda quando mexe o mouse"
- sumir congelamento de cursor durante playback continuo
- `audioplayer` e autoplay do desktop usarem o mesmo miolo HDA sem telas pretas nem texto cru no topo

Fase 3. Limpeza final de acoplamento com UI

- remover qualquer debug/status de audio que ainda vaze para a tela grafica
- garantir que wallpaper e compositor nao dependam do loop de audio para terminar bootstrap
- manter `audiosvc`/`soundctl` como control-plane e o datapath o mais direto possivel no backend `compat`

Risco:

- baixo a medio
- esta fase e mais de acabamento e isolamento do hot path

Sequencia pratica recomendada:

1. fechar Fase 1 inteira e revalidar QEMU
2. so depois abrir a Fase 2
3. deixar a Fase 3 por ultimo, quando o som ja estiver limpo

Regra de seguranca:

- nao mexer em rede, VFS ou compositor enquanto a Fase 2 do HDA estiver aberta
- manter a cada rodada um estado que ainda boota desktop e passa os validadores HDA do QEMU
- so chamar novo teste manual em hardware real quando entrar um lote estrutural completo, nunca no meio de um refactor de stream

Estado real de referencia:

- maquina-alvo critica: ThinkPad T61
- controladora observada: `8086:284b`
- sintoma de hardware real mais grave visto ate aqui: desktop/mouse travando por minutos enquanto o backend insiste em bring-up HDA sem rota utilizavel
- regra pratica da frente atual: so manter no branch lotes que continuem verdes em `validate-audio-hda-smoke`

Backlog priorizado em lotes:

1. Topologia e selecao de conexao
- endurecer `connection select`/selector defaults no probe
- garantir que `route select`, `power`, `amp` e `pin ctl` andem exatamente pela mesma trilha ate o `AUD_OUT`
- preservar e restaurar esse estado em reprobe/fallback

2. Politica de saida fisica
- manter preferencia por jack externo presente sem descartar `speaker` interno quando o pin sense do notebook for ruim
- desligar speaker interno quando a rota ativa for headphone/line-out presente
- evitar priming de jacks ausentes e reduzir consultas agressivas de `pin sense` no caminho quente

3. Defaults e coerencia de widgets
- corrigir selectors apontando para entradas invalidas ou sem caminho ate `AUD_OUT`
- revisar widgets com connection list multipla que nascem em estado incoerente no codec
- aproximar os defaults praticos de `azalia_mixer_default()` sem puxar o mixer framework inteiro do BSD

4. Controladora e stream bring-up
- continuar separando falha de stream/controladora de falha real de topologia
- manter rotacao de output stream, `rebind` leve e reprobe so quando necessario
- preservar diagnostico acionavel em `device.config`

5. Validacao e criterio para gravar imagem
- compilar `build/kernel/microkernel/audio.o`
- rodar `validate-audio-hda-smoke`
- so pedir teste no T61 quando entrar um lote estrutural com chance concreta de alterar o comportamento observado em hardware real

Fases operacionais para continuar sem perder contexto:

Fase A. Coerencia de topologia
- objetivo: garantir que o estado detectado no probe continue coerente ate o `START`
- checkpoints:
  - `connection select` valido para selectors
  - cache/restaure de selecao funcionando em reprobe/fallback
  - `route select`, `power`, `amp` e `pin ctl` andando pela mesma trilha
- criterio de saida:
  - smoke HDA continua verde
  - nenhuma regressao em `path-programmed=1`
- comandos minimos:
  - `make -j2 build/kernel/microkernel/audio.o`
  - `make -j2 validate-audio-hda-smoke`

Fase B. Politica de saida fisica
- objetivo: aproximar notebook real do comportamento esperado para `speaker`/`headphone`
- checkpoints:
  - preferencia por jack externo presente
  - `speaker` interno nao some por `pin sense` ruim
  - `speaker` interno e desativado quando a rota ativa e externa/presente
- criterio de saida:
  - sem piora no QEMU
  - logs de rota continuam acionaveis
- pontos de observacao no serial:
  - `audio: hda choose-output`
  - `audio: hda route out=`

Fase C. Defaults de widget e mixer pratico
- objetivo: absorver a parte mais valiosa de `azalia_codec.c` sem portar o framework inteiro
- checkpoints:
  - selectors incoerentes corrigidos cedo
  - widgets multi-connection nao nascem presos a entrada morta
  - defaults praticos aproximados de `azalia_mixer_default()`
- criterio de saida:
  - probe continua encontrando a mesma rota valida no smoke
  - nenhuma heuristica nova pode mascarar falha real de topologia
- prioridade interna:
  - primeiro `AUDIO_SELECTOR`
  - depois widgets multi-connection sem framework de mixer completo

Fase D. Controladora e stream
- objetivo: separar melhor falha de codec/topologia de falha de stream/controladora
- checkpoints:
  - `rebind` leve quando a rota atual ainda e valida
  - rotacao de output stream antes de reprobe completo
  - `device.config` continua especifico para diagnostico
- criterio de saida:
  - `RUN`/reset regressam de forma detectavel no smoke
  - sem travar o backend em reprobe pesado desnecessario

Fase E. Gate para hardware real
- objetivo: so gastar imagem quando houver chance concreta de evolucao no T61
- checkpoints:
  - lote estrutural novo e validado no smoke
  - muda uma causa plausivel do sintoma real observado
  - estado atual e proximo passo ficam registrados no plano
- criterio de saida:
  - se nao houver esse conjunto, continuar iterando localmente
- formato do proximo handoff:
  - o que mudou no codigo
  - onde validar no codigo
  - se o smoke passou
  - se ja vale imagem ou nao

Pontos de entrada principais no codigo para o proximo prompt:

- probe/descoberta: `mk_audio_azalia_probe_codec()`, `mk_audio_azalia_probe_widgets()`
- selecao/rota: `mk_audio_azalia_choose_output_path()`, `mk_audio_azalia_select_output_route()`
- fast-path/runtime: `mk_audio_azalia_current_output_path_valid()`, `mk_audio_azalia_rebind_output_stream()`, `mk_audio_azalia_program_output_path()`
- bring-up de stream: `mk_audio_azalia_stream_start_buffer()`

Regras de regressao para qualquer proxima rodada:

- nao manter heuristica que derrube `validate-audio-hda-smoke`
- nao deixar `pcspkr` esconder a causa real quando o HDA falha
- nao aceitar “melhora” que so aumente retries/reprobe sem mudar a coerencia do caminho
- quando o smoke acusar ausencia de marcadores de status, conferir alias de app (`audiosvc:` vs `soundctl:`) antes de tratar como regressao real do HDA

Proximos lotes concretos ainda abertos:

- portar mais comportamento util de `azalia_codec.c` em torno de defaults de selector/mixer
- estudar enable minimo de unsolicited/jack-driven speaker mute sem trazer dependencias grandes demais
- seguir fechando discrepancias entre topologia detectada e caminho efetivamente programado no codec
- [X] o substrate USB agora tambem calcula um `effective controller` por `device slot` e marca o estado `handoff-ready` quando ja existe caminho plausivel de migracao de `EHCI` para companion
- [X] o fallback de audio agora tambem diferencia esse estagio intermediario mais proximo da enumeracao real: `pcspkr-fallback-usb-handoff-ready-audio`
- [X] o substrate USB agora tambem agrega os casos `control-ready` e `handoff-ready` em um estado unico de `control-path-ready`, deixando a proxima etapa de descriptor/control transfer partir de um inventario ja resolvido por device
- [X] o fallback de audio agora tambem diferencia quando ja existe caminho plausivel completo ate o primeiro control path: `pcspkr-fallback-usb-control-path-audio`
- [X] o substrate USB agora tambem seleciona `probe targets` a partir dos `device slots` `control-path-ready`, inclusive com filtro para candidatos de audio, preparando a futura leitura de descriptors por ordem estavel
- [X] o substrate USB agora tambem materializa um `probe plan` minimo por alvo, começando pelo primeiro `GET_DESCRIPTOR` curto do `device descriptor` (8 bytes), para a futura etapa de control request nao precisar inventar esse encaixe depois
- [X] o fallback de audio agora tambem diferencia quando ja existe alvo direto para essa primeira probe USB de audio: `pcspkr-fallback-usb-descriptor-probe-audio`
- [X] o substrate USB agora tambem materializa uma fila/snapshot estavel dessas probes minimas no boot, com status `planned`, para a futura etapa de transfer/control request consumir entradas prontas
- [X] o fallback de audio agora tambem diferencia quando essa fila de probe para audio ja existe: `pcspkr-fallback-usb-probe-queue-audio`
- [X] o substrate USB agora tambem classifica quais entradas dessa fila ja estao `dispatch-ready` para uma futura execucao imediata e quais ainda ficariam `deferred-no-transport`
- [X] o fallback de audio agora tambem diferencia quando ja existe probe USB de audio pronta para despacho: `pcspkr-fallback-usb-probe-dispatch-audio`
- [X] o substrate USB agora tambem tem um pequeno despachante para selecionar a proxima probe `dispatch-ready`, avancando um cursor estavel sem ter de recalcular a fila toda quando a etapa de transfer for ligada
- [X] o despachante agora tambem monta um `dispatch context` completo por probe, carregando snapshot + controlador efetivo + status da porta efetiva, para a futura etapa de transfer/control request consumir isso direto
- [X] o substrate USB agora tambem tem a primeira API de "tentativa de execucao" dessa probe; por enquanto ela ainda termina honestamente em `no transport`, mas ja fixa o fluxo `dispatch -> execution result` e atualiza o status da probe para `exec-no-transport`
- [X] o `dispatch context` agora tambem resolve qual backend de transporte deveria executar a probe (`UHCI`/`OHCI`/`EHCI`/`XHCI`) e a tentativa de execucao ja retorna um resultado especifico por transporte indisponivel, em vez de um `no transport` totalmente generico
- [X] o substrate USB agora tambem executa um primeiro preflight real para probes em `UHCI` e `OHCI`: valida o transporte efetivo, limpa/mascara status/interrupts do host controller de forma minima e marca a probe como `exec-ready` quando esse caminho basico esta vivo; `EHCI` e `XHCI` continuam honestamente como indisponiveis nessa fase
- [X] o fallback de audio agora tambem diferencia quando esse primeiro degrau do transporte USB ja ficou realmente pronto para um futuro control transfer de audio: `pcspkr-fallback-usb-probe-exec-audio`
- [X] o substrate USB agora faz a leitura curta e tambem a leitura completa do `configuration descriptor` em `UHCI` e `OHCI`: depois do `device descriptor` completo e do primeiro `SET_ADDRESS`, a probe ja busca os 9 bytes iniciais da configuracao, usa `wTotalLength` para puxar o blob inteiro e guarda `bConfigurationValue`/`wTotalLength` no snapshot
- [X] a promocao para `descriptor-ready` agora ficou defensiva tambem no descriptor completo: o snapshot so sobe quando o prefixo e o `device descriptor` inteiro fazem sentido, incluindo `bLength`, `bDescriptorType`, `bMaxPacketSize0` e `bNumConfigurations`
- [X] o fallback de audio agora tambem diferencia quando essa primeira leitura curta do `device descriptor` ja aconteceu em um candidato USB de audio: `pcspkr-fallback-usb-descriptor-read-audio`
- [X] o substrate USB agora tambem varre `configuration/interface/endpoint` no descriptor completo e ja marca probes onde a classe USB de audio (`AudioControl`/`AudioStreaming`) foi detectada
- [X] o fallback de audio agora tambem diferencia quando a enumeracao USB ja encontrou uma probe com `USB Audio Class`: `pcspkr-fallback-usb-audio-class-detected`
- [X] o substrate USB agora tambem envia o primeiro `SET_CONFIGURATION` real em `UHCI` e `OHCI` depois da leitura completa dos descriptors, promovendo a probe para `configured-ready`
- [X] o fallback de audio agora tambem diferencia quando um candidato USB de audio ja passou por `SET_CONFIGURATION`: `pcspkr-fallback-usb-configured-audio`
- [X] o microkernel de audio agora tambem captura o primeiro candidato `compat-uaudio` em estado `configured-ready`, expondo attach readiness e metadados basicos (`addr/cfg/interfaces/endpoints`) via snapshot/telemetria
- [X] o scan do `configuration descriptor` agora tambem identifica as primeiras interfaces `AudioControl` e `AudioStreaming`, e o microkernel de audio ja carrega esses numeros como base do attach minimo do `compat-uaudio`
- [X] o attach minimo de `compat-uaudio` ficou menos superficial: o scan USB agora tambem identifica o primeiro altsetting `AudioStreaming` com endpoint isocronico de playback (`OUT`) e o microkernel de audio so marca `attach-ready` quando ja existem `AC interface + AS interface + altsetting + endpoint + max packet`, em vez de tratar qualquer `configured-ready` como se ja fosse um device de audio utilizavel
- [X] o diagnostico do audio tambem ficou mais honesto para USB Audio Class: quando o fallback ainda cai em `pcspkr`, `device.config` agora diferencia `usb-audio-attach-ready`, e `soundctl`/`audiosvc`/task manager passam a renderizar a rota USB como `as/alt/ep/cfg` em vez de fingir o mesmo formato `pin/dac` do HDA
- [X] o attach minimo do `compat-uaudio` agora tambem executa o primeiro `SET_INTERFACE` real sobre o `AudioStreaming altsetting` detectado, promovendo a probe USB para `attached-ready`; o microkernel exporta esse estado novo, o fallback diferencia `usb-audio-attached` e a telemetria nativa passa a expor `usb-attached-ready` separadamente de `usb-attach-ready`
- [~] o `compat-uaudio` deixou de ser so telemetria e ganhou o primeiro backend de playback MVP: quando nao existe audio PCI utilizavel e ja ha um device USB Audio Class em `attached-ready`, o servico agora pode selecionar `compat-uaudio` como backend real e enviar `write()` direto para o endpoint de playback em `UHCI` e tambem em `OHCI`; ainda e um caminho minimo, sem agendamento isocronico completo e sem cobertura `EHCI/XHCI`, mas ja tira a stack do estado "attach-only"
- [~] regra de liberacao para a rodada de validacao nos 22 notebooks: so disparar a matriz grande quando `compat-azalia` estiver fechado em hardware real, `compat-uaudio` tiver saido do MVP atual, captura estiver validada em maquina fisica e a matriz de backends reais (`compat-azalia`/`compat-auich`/`pcspkr`/`compat-uaudio`) estiver pronta para execucao coordenada
- [~] o `compat-uaudio` agora tambem carrega no backend o transporte USB real do device anexado (`uhci`/`ohci`/`ehci`/`xhci`) e passa a expor isso em `device.name`/`device.config`; isso nao amplia sozinho o playback alem de `UHCI`, mas deixa a validacao em hardware real e a proxima fase de expansao do backend bem menos opacas
- [~] quando existe `USB Audio Class` anexado mas o transporte ainda nao suporta playback real, o fallback agora diferencia isso com o transporte especifico (`pcspkr-fallback-usb-audio-ohci-unsupported`, por exemplo), evitando que a matriz de maquinas trate todo caso `attached` como se fosse o mesmo gargalo
- [~] o `compat-uaudio` agora tambem herda no snapshot do audio o PCI/localizacao da controladora USB efetiva usada pelo device anexado; isso deixa `pci=`/localizacao do backend USB tao acionaveis quanto ja estavam em `compat-azalia`
- [~] a ordem real de fallback do servico agora foi alinhada ao plano: quando HDA/AC97 sao detectados mas nao ficam utilizaveis, `compat-uaudio` passa a entrar antes de `pcspkr` se houver `USB Audio Class` anexado com playback disponivel, em vez de ficar preso ao caso "sem hardware PCI detectado"
- [~] o runtime do `compat-uaudio` tambem ficou mais acionavel para hardware real: `START` e `WRITE` agora deixam `device.config` especifico por transporte em falhas como `-unsupported`, `-write-failed` e `-short-write`, reduzindo o numero de erros USB que antes apareciam so como retorno generico
- [~] o runtime do `compat-uaudio` tambem passou a limpar estados stale de diagnostico quando o backend volta a funcionar: em `select/start/stop` e em `write()` completo ele restaura a identidade normal `usb-audio-<transport>-attached`, em vez de deixar o ultimo `-unsupported`/`-write-failed` preso no snapshot
- [~] o runtime de fallback ficou mais robusto quando um backend some na hora errada: se `compat-uaudio` perder suporte efetivo em `START`/`WRITE`, ou se `compat-auich` falhar no `START`/`WRITE`, o servico agora degrada automaticamente para `pcspkr`/`softmix` e ainda tenta priorizar `compat-uaudio` antes do buzzer quando houver `USB Audio Class` anexado e pronto
- [~] os parametros default do `compat-uaudio` tambem ficaram menos conservadores: o backend agora calcula `round` a partir de multiplos do `max packet` do endpoint USB e sobe `nblks` para reduzir overhead de syscalls no playback continuo, em vez de empurrar bursts quase do tamanho de um unico pacote isocronico
- [~] o `compat-uaudio` agora tambem aproveita melhor o descritor `AudioStreaming`: o scan USB passa a carregar `channels/subframe/bits/sample-rate` do primeiro `FORMAT_TYPE I` encontrado no altsetting de playback, e o backend usa esses metadados para montar parametros menos fixos que o antigo estereo 48 kHz hardcoded
- [~] o `compat-uaudio` agora tambem evita quebrar frames PCM no meio ao fatiar `write()` por `max packet`: o backend usa `subframe/channels/bits` do descritor para alinhar chunks ao tamanho real do frame USB antes de enviar pacotes, reduzindo o risco de short writes que ainda so seriam "validos" byte a byte
- [~] o `compat-uaudio` agora tambem alinha `round` ao tamanho real do frame PCM USB quando deriva os parametros do backend; isso evita que o proprio bloco default do servico force writes continuamente desalinhados com `channels * subframe`
- [~] o parser do `compat-uaudio` tambem ficou correto para a ordem real dos descritores `AudioStreaming`: o `FORMAT_TYPE I` agora fica pendente por altsetting e so e aplicado quando o endpoint isocronico `OUT` daquele bloco aparece, em vez de depender da ordem impossivel "endpoint antes do format"
- [~] a escolha de sample rate do `compat-uaudio` tambem ficou menos arbitraria: quando o `FORMAT_TYPE I` anuncia multiplas frequencias discretas ou um range continuo, o parser agora prioriza `48000`, depois `44100`, e so entao a frequencia mais proxima do caminho padrao do sistema, em vez de escolher sempre a maior
- [~] o `compat-uaudio` agora tambem preserva a coerencia entre probe USB e parametros do servico: em vez de passar pela normalizacao generica que achatava tudo para `16-bit stereo`, o backend ganhou uma normalizacao propria que fixa `rate/bits/bps/channels` no formato realmente detectado e deixa ajustavel principalmente `round/nblks`
- [~] o runtime do `compat-uaudio` agora tambem recusa `write()` desalinhado com o frame PCM do device USB e diferencia isso no diagnostico (`-unaligned-write` / `-unaligned-short-write`), em vez de tratar qualquer tamanho de buffer como se fosse igualmente valido so porque cabe em bytes
- [~] a stack USB de audio agora tambem rearmou probes de `USB Audio Class` sob demanda e permite promocao tardia de `pcspkr`/`softmix` para `compat-uaudio` quando o attach fechar depois do boot; isso melhora bastante o comportamento incremental, mas o smoke atual no QEMU com `piix3-usb-uhci + usb-audio` ainda para no primeiro degrau de enumeracao UHCI, antes de `SET_CONFIGURATION`, entao a regra segue sendo: nao gastar rodada de hardware real ate esse cenario emulado sair do fallback

### Matriz de backends de audio

- `compat-azalia`
  - alvo: HDA/Intel HD Audio, principal caminho moderno para notebooks e desktops PCI/PCIe
  - status: playback real no QEMU, telemetria rica, fallback de rota e rotacao de stream implementados; revalidacao em hardware real ainda em andamento
- `compat-auich`
  - alvo: AC97/PCI legado e familias compativeis Intel/AMD/ATI/NVIDIA/SiS/ALI/VIA
  - status: playback real no QEMU e captura DMA MVP; cobertura de IDs/quirks ja esta ampla
- `pcspkr`
  - alvo: fallback universal quando nenhum backend PCI sobe
  - status: implementado; usa PIT + speaker legacy/buzzer e garante audio rudimentar em praticamente qualquer notebook/desktop x86
- `compat-uaudio`
  - alvo: USB Audio Class, equivalente mais forte de "funciona em muito hardware" no mundo de audio atual
  - status: MVP implemented; playback over UHCI/OHCI, format detection, USB Audio Class attach, fallback integration with event stream

### Proxima fase do audio universal

Prioridades imediatas:

1. plugar um `compat-uaudio` minimo para attach/probe usando o device USB ja configurado, a interface `AudioControl` e o primeiro altsetting/endpoint `AudioStreaming` de playback ja descobertos
2. ligar o attach basico das interfaces de audio antes de tentar playback
3. expandir o transporte real para `EHCI` e depois `XHCI`
4. validar em hardware real com headset/adaptador USB audio class simples

- manter a ordem de preferencia:
  - `compat-azalia` -> `compat-auich` -> `compat-uaudio` -> `pcspkr` -> `softmix`
- fechar a matriz de hardware real:
  - testar pelo menos HDA Intel real, AC97 legado real/emulado, e notebook sem driver PCI usando `pcspkr`
- preparar substrate USB minimo para `uaudio`:
  - enumeracao de host controllers suportados
  - attach basico de bus/root hub
  - inventario minimo de dispositivos conectados ao root hub, para deixar de depender so de contadores agregados
  - slots/logica de device attach com estado "ready for enum" por porta
  - distinguir ports/devices que ja parecem aptos a control path minimo dos que ainda dependem de companion handoff
  - mapear companions provaveis de `EHCI` para `UHCI/OHCI` e usar isso para preparar o futuro handoff
  - fixar o `effective controller` por device e usar isso como entrada do primeiro control path
  - consolidar um inventario `control-path-ready` para alimentar a futura leitura de descriptors
  - escolher `probe targets` estaveis para a primeira leitura de descriptor/control request
  - materializar o primeiro `probe plan` (`GET_DESCRIPTOR` curto do device descriptor) por alvo
  - materializar uma fila/snapshot dessas probes para a futura execucao sequencial
  - classificar a fila em `dispatch-ready` vs `deferred-no-transport` para a futura etapa de transferencia real
  - despachar a proxima probe elegivel a partir dessa fila, com cursor estavel
  - montar um `dispatch context` completo para a futura transferencia USB nao precisar remontar controlador/porta
  - ligar attach minimo no device ja configurado
  - sair do `write()` MVP de `UHCI` para um agendamento isocronico mais correto e depois cobrir `OHCI`
  - so depois encaixar `compat/sys/dev/usb/uaudio.c` como backend proprio do `audio`
- [~] compat plan: pronto para smoke test em hardware real do substrate USB (`UHCI`/`OHCI` + `device/config descriptor` + deteccao de `USB Audio Class`); ainda nao pronto para playback USB real

### Rede

- `headers/sys/socket.h`
  - vocabulario BSD de sockets
- `headers/kernel/microkernel/network.h`
  - formato Vibe-native para o servico de rede
- `kernel/microkernel/network.c`
  - servico `network` ja registrado
- `compat/sbin/ifconfig/*`
  - referencia fortissima para configuracao de interfaces, incluindo Wi-Fi
- `compat/sbin/dhcpleased/*`
  - base para cliente DHCP IPv4
- `compat/sbin/dhcp6leased/*`
  - base para DHCPv6 futuro
- `compat/sbin/unwind/*`
  - base para resolucao DNS local
- `compat/sbin/ping/*`
  - bom smoke test posterior
- drivers e stack em `compat/sys/dev/*`, `compat/sys/net*` e afins
  - fonte de extracao para ethernet, Wi-Fi e 802.11

Estado atual:

- [X] o servico `network` responde a `GETINFO`
- [X] o servico `network` responde a `GET_STATUS`
- [X] existe control-plane MVP para scan/conectar/desconectar
- [X] existe camada MVP de socket/bind/connect/send/recv/listen/accept para loopback local
- [X] existe `netmgrd` MVP com `status`, `scan`, `reconcile` e exportacao de estado
- [X] o snapshot exportado por `netmgrd` ja inclui backend, DNS, manager e lease para preparar o encaixe de DHCP real
- [X] `netmgrd`, `netctl status` e task manager agora mostram se o lease veio de `runtime`, `compat-default` ou fallback MVP
- [X] `netmgrd` ja consegue consumir um lease texto de compat e aplicá-lo ao backend de `em0`
- [X] o formato de lease do `compat/dhcpleased` já é aceito diretamente pelo `netmgrd`
- [X] o lease padrão de `compat` já é descoberto automaticamente sem import manual prévio
- [X] o desktop ja usa `netmgrd reconcile` no boot da sessao e exporta estado em eventos de conexao
- [X] o desktop ja usa `netmgrd connect` e `netmgrd disconnect` no fluxo principal do applet
- [X] o task manager ja le `/runtime/netmgrd-status.txt` para mostrar auto-conexao e perfis salvos
- [X] ja existe `netmgrd` como network manager do VibeOS

### Painel / Desktop

- `userland/modules/ui.c`
  - ja desenha taskbar/painel
- `userland/applications/desktop.c`
  - ja concentra hit-test, janelas, menu iniciar, personalize e logica de clique

Estado atual:

- [X] o painel ja desenha area de bandeja/applet a direita
- [X] rede e som ja ocupam essa area com popups proprios
- [X] menu iniciar ganhou atalhos diretos para `netctl status` e `soundctl status`

## Arquitetura proposta

## 1. Audio: `audiosvc` + `soundctl` + applet de painel

### 1.1 Servico de audio

Criar um daemon/app de servico de audio em userland, hospedado pelo servico `audio` do microkernel.

Responsabilidades:

- enumerar endpoints logicos:
  - saida principal
  - fones
  - entrada de linha
  - microfone
- manter estado:
  - volume por endpoint
  - mute
  - endpoint padrao
- expor operacoes mixer-like:
  - listar controles
  - ler volume
  - escrever volume
  - trocar endpoint padrao
- no backend inicial:
  - mesmo que ainda sem DMA real completa, manter a API pronta e um mixer funcional sobre o backend existente
- no backend final:
  - plugar drivers reais extraidos de `compat`
  - reaproveitar o maximo possivel da semantica `sndio`

### 1.2 ABI a preservar

Preservar a forma BSD/audioio sempre que possivel:

- `audio_device_t`
- `audio_status`
- `audio_swpar`
- `mixer_devinfo_t`
- `mixer_ctrl_t`

Onde a ABI BSD for ampla demais para o estagio atual, criar um wrapper Vibe pequeno em cima dela, sem quebrar compatibilidade futura.

### 1.3 Ferramentas de userland

Criar:

- `soundctl`
  - CLI para listar dispositivos/controles
  - ajustar volume
  - mutar/desmutar
  - escolher entrada/saida padrao

Exemplos esperados:

- `soundctl list`
- `soundctl set output-main 70`
- `soundctl mute mic`
- `soundctl default output headphones`

Estado atual do MVP:

- `soundctl list`
- `soundctl status`
- `soundctl set output-main 70`
- `soundctl mute mic`
- `soundctl unmute output`

### 1.5 Drivers e `compat`

Depois do MVP do mixer:

- extrair drivers PCI/AC97/HDA mais realistas para o alvo i386/QEMU/hardware real
- aproveitar `sndiod` como referencia para arbitragem e nomes de portas
- usar `mixerctl`/`sndioctl` como base de compatibilidade e diagnostico

### 1.6 Estrategia de implementacao do backend de som

Para cumprir os novos requisitos de reproducao de `assets/vibe_os_boot.wav` e
`assets/vibe_os_desktop.wav` sem travar o sistema, a implementacao deve seguir
em fases, preservando a ABI atual do servico `audio`.

Fase 1: playback one-shot funcional com backend software

- manter o mixer atual e abrir o caminho de playback real no servico `audio`
- expor `START`, `STOP` e `WRITE` por syscall/runtime para userland e apps Lang
- implementar uma fila/ring buffer PCM simples dentro do backend atual
- aceitar primeiro o formato mais facil para o MVP:
  - PCM assinado
  - little-endian
  - 16 bits
  - 44100/48000 Hz
  - mono ou stereo
- se a reproducao falhar:
  - o boot continua normalmente
  - o `startx` e o desktop continuam normalmente

Fase 2: player de WAV pequeno e reaproveitavel

- criar um helper em userland para:
  - abrir arquivo via `fs_read_file_bytes`
  - validar cabecalho RIFF/WAVE
  - localizar o chunk `fmt `
  - localizar o chunk `data`
  - alimentar o backend de audio em blocos pequenos
- usar esse helper em dois pontos:
  - boot: tocar `assets/vibe_os_boot.wav` uma unica vez
  - `startx`/desktop: tocar `assets/vibe_os_desktop.wav` uma unica vez
- a reproducao deve ser best-effort:
  - sem dependencia para o fluxo principal
  - sem busy-loop infinito
  - com timeout/log de falha curto

Fase 3: backend real via `compat`

- manter a ABI do servico `audio` e trocar apenas a implementacao interna
- criar uma camada de backend com dois modos:
  - `softmix`/fallback
  - `compat`/driver real
- o primeiro alvo real deve ser AC97 Intel ICH usando:
  - `compat/sys/dev/pci/auich.c`
  - `compat/sys/dev/ic/ac97.h`
- motivo da escolha:
  - encaixa melhor no alvo i386/QEMU do projeto
  - menor risco do que tentar HDA/`azalia` de inicio
  - permite validar DMA, mixer e interrupcao com uma base menor

Fase 4: consolidacao para apps e jogos

- fazer `doom_port/i_sound_vibe.c` sair do stub e consumir o backend novo
- depois disso, ampliar suporte para:
  - stereo fixo
  - mixing de multiplas fontes
  - notificacao de underrun
  - captura real
  - HDA/`azalia` como proximo alvo de compat

### 1.4 Applet do painel

Adicionar um applet de som na extrema direita do painel com:

- icone de alto-falante
- estado visual:
  - mute
  - volume baixo/medio/alto
- clique:
  - abre popup de som

Popup de som:

- lista de saidas
- lista de entradas
- slider de volume por endpoint
- botao/toggle de mute
- marcador de dispositivo padrao

## 2. Rede: `netmgrd` + `netctl` + applet de painel

### 2.1 Daemon de gerencia

Criar um daemon proprio do VibeOS, chamado por exemplo `netmgrd`.

Ele sera o equivalente funcional ao "NetworkManager" no projeto, mas fino e orientado a reuso de `compat`.

Responsabilidades:

- enumerar interfaces
- classificar:
  - ethernet
  - loopback
  - wifi
- manter perfis de rede
- acionar DHCP
- publicar DNS local
- expor estado para o painel

### 2.2 Reuso de `compat`

Usar `compat` em camadas:

- `ifconfig`
  - referencia para controle de interface
  - reutilizar parsing e semantica de configuracao onde fizer sentido
- `dhcpleased`
  - base para obter lease IPv4
- `unwind`
  - base para DNS local/caching resolver

Importante:

- nao tentar portar os daemons de OpenBSD integralmente logo de inicio
- extrair primeiro as partes uteis:
  - modelos de estado
  - parsers
  - controle de interface
  - fluxo de lease
  - publicacao de resolvers

### 2.2.1 Drivers e `compat`

Antes de Wi-Fi completo, precisamos puxar de `compat`:

- drivers ethernet que encaixem no alvo atual
- stack 802.11 suficiente para scan e associacao
- controle de autenticacao para WPA/WPA2
- glue minimo para expor isso ao `mk_network`

### 2.3 Wi-Fi

Para Wi-Fi real, o gerenciador precisa destas capacidades:

- listar redes vistas no scan
- expor SSID, seguranca, intensidade
- conectar com senha
- lembrar perfil salvo
- indicar conectado/conectando/falha

Isso exige:

- control-plane 802.11 no driver/kernel
- scan
- associacao
- WPA/WPA2 pelo menos

Conclusao honesta:

- o menu de rede pode ser projetado agora
- DHCP e DNS podem avancar antes
- Wi-Fi completo depende de trabalho real de stack/driver; nao e so colar `ifconfig`

### 2.4 Ferramentas de userland

Criar:

- `netmgrd`
  - reconciliar auto-conexao a partir dos perfis salvos
  - exportar estado sintetizado para arquivos/runtime
  - concentrar a futura transicao do painel para um manager dedicado
- `netctl`
  - listar interfaces
  - status da conexao
  - scan Wi-Fi
  - conectar
  - desconectar
  - mostrar lease e DNS

Exemplos esperados:

- `netctl status`
- `netctl scan wlan0`
- `netctl connect em0`
- `netctl connect wlan0 "MinhaRede"`
- `netctl connect wlan0 "MinhaRede" --psk "senha"`
- `netctl disconnect wlan0`
- `netctl profiles`
- `netctl remember wlan0 "MinhaRede" --psk "senha"`
- `netctl autoconnect "MinhaRede"`

### 2.5 Applet do painel

Adicionar applet de rede no lado direito do painel, antes do som.

Estado visual:

- sem link
- ethernet conectada
- wifi desconectado
- wifi conectando
- wifi conectado com barras de sinal

Clique:

- abre popup de rede

Popup de rede:

- interface ativa
- IP atual
- gateway
- DNS
- lista de redes visiveis
- campo de senha
- botao conectar/desconectar
- lista de redes salvas

## Fases propostas

## Fase 1 - Infra de painel/tray

Objetivo:

- criar uma area de applets no lado direito da taskbar

Entregas:

- reservar area fixa para applets
- hit-test dos applets
- popup ancorado ao applet
- base generica para applets do painel

Critério de pronto:

- [X] painel suporta pelo menos 2 applets sem conflitar com botoes de janela

## Fase 2 - Audio control-plane

Objetivo:

- sair do `query-only` no servico `audio`

Entregas:

- suporte real a `MIXER_READ` e `MIXER_WRITE`
- enumeracao basica de endpoints e controles
- `soundctl`
- popup de som no painel

Critério de pronto:

- alterar volume no painel muda o estado do mixer
- `soundctl list` e `soundctl set` funcionam
- o applet deixa de usar estado local fake para volume/mute

Estado atual:

- [X] pronto em modo MVP
- [X] ainda falta plugar/estabilizar driver real e enumeracao de endpoints fisicos vindos de `compat`

## Fase 3 - Network control-plane minimo

Objetivo:

- construir o `netmgrd` e tirar valor de `compat` antes de Wi-Fi completo

Entregas:

- estado de interfaces
- suporte a ethernet
- DHCPv4 via base `dhcpleased`
- DNS local via base `unwind`
- `netctl status`
- popup de rede com status e detalhes de link

Critério de pronto:

- [X] interface cabeada sobe, recebe lease e resolve DNS

## Fase 4 - Wi-Fi scan + conexao

Objetivo:

- adicionar controle de Wi-Fi no gerenciador

Entregas:

- scan de redes
- lista SSID/sinal/seguranca
- conexao com senha
- perfis salvos

Critério de pronto:

- [X] usuario escolhe rede, informa senha e conecta pelo popup

## Fase 6 - `vibe-browser` como app de desktop

Objetivo:

- transformar `userland/applications/network/vibe-browser` no navegador oficial do ambiente grafico

Entregas:

- mapa de dependencias de build e runtime
- integracao com launcher/menu/taskbar
- integracao com sockets, DNS, downloads e URLs
- estrategia de port para Qt/WebEngine ou camada de compatibilidade equivalente
- associacao como navegador padrao

Critério de pronto:

- [ ] o navegador abre pelo desktop, navega com a rede real do sistema e consegue baixar/renderizar paginas no ambiente suportado

## Fase 5 - Refinamento de UX

Objetivo:

- fazer painel e popups ficarem de uso diario

Entregas:

- indicadores de erro
- reconnect automatico
- mute rapido
- scroll/teclas no popup
- persistencia de volume por dispositivo
- persistencia de redes salvas

## Ordem recomendada

1. Criar infraestrutura de tray/applet no painel.
2. Fechar mixer/control-plane de audio.
3. Entregar applet de som.
4. Criar `netmgrd` para ethernet primeiro.
5. Integrar DHCPv4.
6. Integrar DNS local.
7. Entregar applet de rede com status cabeado.
8. Evoluir scan/conexao Wi-Fi.
9. Portar e integrar o `vibe-browser`.

## Riscos e bloqueios reais

### Audio

- o backend atual de audio ainda pode ser mais "estado" do que "driver real"
- pode ser necessario introduzir uma camada de dispositivos/endpoints sinteticos antes de ter hardware completo

### Rede

- `mk_network` ainda nao implementa operacoes reais
- Wi-Fi nao depende so de UI; depende de stack/driver e seguranca
- portar `ifconfig`/`dhcpleased`/`unwind` literalmente pode ser caro demais; a estrategia certa e extrair partes e manter compatibilidade de semantica

## Recomendacao de implementacao

### Reuso forte de `compat`

Usar diretamente como referencia e fonte de extracao:

- `compat/sbin/ifconfig`
- `compat/sbin/dhcpleased`
- `compat/sbin/unwind`

### Evitar

- portar NetworkManager do Linux
- tentar portar o userspace BSD inteiro antes de fechar os contratos Vibe
- misturar UI de painel com a logica de DHCP/Wi-Fi
- tentar encaixar o `vibe-browser` antes de rede, DNS e downloads estarem minimamente funcionais

## Definicao de pronto

O projeto so deve ser considerado concluido quando:

- [X] existir area de applets no lado direito do painel
- [X] o applet de som listar entradas/saidas e controlar volume
- [X] existir `soundctl`
- [X] existir `netmgrd` e `netctl`
- [~] ethernet com DHCP funcional; DNS funciona mas com servidor hardcoded 10.0.2.3 (ver P3)
- [X] o applet de rede mostrar estado real da conexao
- [X] Wi-Fi puder listar redes, pedir senha e conectar
- [ ] o `vibe-browser` estiver integrado ao desktop como navegador de internet

## Meta de MVP

Se quisermos um MVP pragmatico antes do pacote completo:

1. [X] tray/applet no painel
2. [X] applet de som com mixer funcional
3. [ ] applet de rede com ethernet + DHCP + DNS

## Proximos passos imediatos

1. [X] subir o `audiosvc` MVP para mediar endpoints logicos e nomes de portas acima do mixer atual
2. [~] endurecer e validar em runtime o backend de som `compat-auich`
3. [ ] puxar DHCP/DNS reais de `compat` para o caminho de rede
4. [X] subir o esqueleto do `netmgrd`
5. [ ] com rede funcional, iniciar o port completo do `vibe-browser`
6. [ ] Wi-Fi real entra na iteracao seguinte

Esse MVP ja entrega muito valor e deixa o caminho do Wi-Fi isolado para quando a stack 802.11 estiver pronta.

## Atualizacao tecnica 2026-03-26

### Audio: estado apos endurecimento do backend

- o backend `compat-auich` agora ja tem:
  - probe PCI real do AC97 Intel ICH
  - acesso por I/O ou MMIO conforme o BAR real exposto pelo chipset
  - codec reset + mixer AC97
  - fila DMA de playback
  - progresso por polling e tambem por IRQ PCI registrada no kernel
  - quirks de compat para ICH4+/BAR nativo e SiS 7012
  - normalizacao explicita dos parametros aceitos pela ABI atual:
    - PCM signed little-endian
    - 16 bits
    - mono ou stereo
    - 11025 / 22050 / 44100 / 48000 Hz
- isso fecha melhor o caminho para:
  - `soundctl tone`
  - `soundctl play`
  - WAVs de boot/startx
  - `doom_port/i_sound_vibe.c`

### Audio: o que ainda falta para considerar fechado de verdade

- validacao audivel real em pelo menos uma maquina fisica alvo
- validacao funcional real em QEMU com `intel-hda` so deve mudar de fallback `softmix` para backend nativo quando o port de `azalia` existir
- telemetria final para diferenciar:
  - IRQ recebida
  - starvation
  - underrun
  - backend sem IRQ valida
- enumeracao/exportacao de endpoints fisicos acima do mixer/driver ja cobre os backends atuais:
  - AC97: `main`, `headphones`, `surround`, `center-lfe`, `mic`, `line`
  - HDA: `speaker`, `headphones`, `line-out`, `digital`, `mic`, `line-in`
- captura real de audio validada em maquina fisica
- segundo backend `compat` para HDA/`azalia`

### Wi-Fi: plano complementar realista para sair dos stubs

Conclusao objetiva:

- hoje `mk_network` ainda e um service/control-plane sintetico
- portanto nao basta plugar `if_iwm`, `if_iwn`, `athn` ou `rtwn`
- antes do Wi-Fi precisamos criar a base minima de rede do kernel

### Etapa A - Base de NIC real no kernel

Implementar uma camada Vibe minima para interface de rede:

- registro de interfaces (`lo0`, `em0`, depois `wlan0`)
- estado de link
- endereco MAC
- TX/RX de frames
- filas RX/TX
- interrupcoes de NIC
- attach/detach de driver PCI
- exportacao de MTU/capabilities

Meta:

- conseguir trazer primeiro `if_em` como backend cabeado real

### Etapa B - Ethernet real antes do Wi-Fi

Trazer primeiro um caminho cabeado completo:

- portar o attach minimo de `compat/sys/dev/pci/if_em.c`
- mapear BAR/IRQ/DMA/descriptor ring para o runtime do kernel
- entregar `link up/down` real
- integrar lease IPv4 real via `dhcpleased`
- publicar DNS real via `unwind` ou arquivo/runtime equivalente

Critério de pronto:

- `netctl connect em0`
- lease IPv4 real
- DNS real
- socket IPv4 deixando de ser loopback-only

### Etapa C - Socket/runtime real

Antes do navegador e antes do Wi-Fi completo:

- evoluir socket ABI para usar a interface real
- suportar:
  - ARP
  - IPv4
  - UDP
  - TCP MVP
- manter o loopback existente como fallback/teste

Critério de pronto:

- `connect/send/recv/listen/accept` funcionando em IPv4 real sobre `em0`

### Etapa D - 802.11 minimo

So depois da Etapa A/B/C:

- escolher um primeiro alvo de Wi-Fi PCI:
  - `if_iwn`
  - ou `athn`
  - ou `rtwn`
- portar:
  - attach PCI
  - firmware loading
  - scan
  - associacao
  - RSSI/link state
- expor isso no `mk_network`

Critério de pronto:

- `netctl scan wlan0`
- listar SSID/sinal/seguranca reais

### Etapa E - WPA/WPA2 PSK

Depois do scan/associacao aberta:

- portar o glue necessario de 802.11/WPA do `compat`
- suportar pelo menos:
  - open
  - WPA2-PSK
- integrar o fluxo de senha/perfil salvo do `netmgrd`

Critério de pronto:

- conectar em rede WPA2 real
- manter perfil salvo
- auto-conectar no boot da sessao

### Etapa F - Fechamento de UX e browser

Com rede real entregue:

- popup de rede deixa de mostrar scan sintetico
- task manager passa a mostrar driver/firmware/link reais
- `vibe-browser` deixa de depender de stubs de rede

## Ordem recomendada revisada

1. endurecer e validar o `compat-auich` ate sair som audivel real
2. fechar `if_em` + DHCP + DNS reais
3. tirar sockets do modo loopback-only
4. integrar o navegador apenas depois da rede cabeada real
5. portar um primeiro driver Wi-Fi PCI
6. fechar scan + associacao aberta
7. fechar WPA2-PSK e perfis salvos

Pode testar no T61. Nesse ponto não parece desperdício de ciclo do HD.

O motivo é simples: os dois bloqueios que estavam mais feios no QEMU já foram resolvidos. O `compat-azalia` parou de travar o bootstrap e chegou até `desktop-session` com playback no `intel-hda`, e o bug do `compat-ac97` que te mostrou “Falha ao enviar audio” também foi reproduzido e corrigido no `AC97` do QEMU.

Minha leitura honesta é:
- vale teste real no T61 se o alvo é validar HDA/ThinkPad de verdade
- ainda existe risco de ajuste fino de rota/pin quirk específico do codec do notebook
- mas agora já estamos no estágio em que hardware real pode revelar coisa nova útil, não só repetir falha besta já conhecida

Se quiser economizar tentativas, eu sugiro esse critério:
- vale gravar se a sua meta é “ver se sobe som algum no T61 e coletar diagnóstico real”
- não vale gravar se você só queria confirmar que “o básico nem trava mais”, porque isso eu já confirmei no QEMU

Se rodar no T61 e der ruim, me manda exatamente a linha de erro do player ou do `soundctl status` que eu continuo em cima sem te fazer queimar tentativas à toa.

--smoke test no t61: som não foi reproduzido, startx deu falha ao iniciar app externo catalog, após reboot em fallback 480p deu o mesmo sintoma. regressão confirmada em drivers ahci, um dos logs deu erro ao carregar storage vfs. em modo compatibilidade do sata controller eu repeti o teste, chegamoa a um bootloop sem som, em fallback 480p congelamento no startx... nada carrega e após minutos nada de novidades 

- causa provável isolada nesta rodada: o `compat-azalia` estava mascarando a linha PIC do HDA já na seleção do backend; no T61 isso pode bloquear uma IRQ PCI INTx compartilhada com SATA/USB e derrubar `AHCI`, `AppFS/catalog` e `startx` mesmo antes do primeiro playback. Corrigido para manter a linha desmascarada e deixar o gating só no `INTCTL` do próprio HDA.
- endurecimento adicional aplicado logo depois: como o PIC atual ainda só aceita um handler por IRQ, o `compat-azalia` deixou de registrar INTx no modo legado e passou a operar em polling no runtime HDA. Isso evita que o áudio roube uma linha compartilhada do SATA/USB no T61 até existir multiplexação real de IRQ no kernel.
