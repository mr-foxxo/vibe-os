# VibeLoader BIOS Debug Handoff

Data da ultima atualizacao: 2026-03-24

## Objetivo deste arquivo

Preservar o contexto do bug atual do `VibeLoader` em hardware real BIOS legacy 32-bit, para que o trabalho continue mesmo se o chat acabar.

## Resumo curto do estado atual

- O bug original de `A20 FAIL` deixou de ser o unico bloqueio, porque o loader agora chega ao menu grafico em hardware real.
- O plano em `docs/VIBELOADER_PLAN.md` foi rechecado e havia um desvio importante de arquitetura:
  - o `stage2` estava carregando `KERNEL.BIN` antes do menu
  - isso contradizia a meta de simplicidade estilo `LILO`
- A imagem mais nova corrige esse ponto:
  - o `stage2` nao pre-carrega mais o kernel na entrada
  - o kernel agora so deve ser lido depois da selecao de boot
  - o caminho de boot volta para real mode, carrega `KERNEL.BIN`, revalida A20 e so entao volta para protected mode
- No estado mais recente, a maquina:
  - tenta exibir o background do `VibeLoader`
  - mostra as opcoes `VIBEOS` e `SAFE MODE`
  - trava e reinicia em loop
- Isso muda o foco do diagnostico:
  - antes: A20 / BIOS legacy
  - agora: handoff do menu para o kernel, polling de teclado, ou algum problema no caminho grafico apos entrar em protected mode

## Sintomas observados em ordem cronologica

### Fase 1: boot falhando cedo

- Em maquina real apareceu `VIBELOADER A20 FAIL`.
- Logs anteriores mostraram o mesmo padrao em BIOS de eras diferentes:

```text
VIBELOADER A20 FAIL
LAST A
TRACE 2FKVBabcdA
A20 P0 B0 C0 D0
BIOS 0E0E CF0 P92 0E KBC R
P=PRE B=INT15 C=92 D=8042
```

### Fase 2: log mais rico mostrou contradicao importante

- Novo dump em hardware real:

```text
VIBELOADER A20 FAIL
LAST A
TRACE 2FKUBabcdA
A20 P0 B0 C0 D0
BIOS 0E0E CF0 P92 0E>0E KBC R
2402 1 AX0E0E CF0
2403 AX0E0E BX0E0E CF0
KDBG G RCE WCE
P=PRE B=INT15 C=92 D=B042
```

- Leitura principal desse dump:
  - `2402 1` dizia que a BIOS considerava o A20 habilitado.
  - Ao mesmo tempo, nosso teste de memoria continuava dizendo que o A20 estava desligado.
  - Isso sugeriu falso negativo no teste/log, nao necessariamente falha real do A20.

### Fase 3: estado mais recente

- Em hardware real, depois das ultimas mudancas:
  - apareceu tela azul
  - o loader tentou carregar a imagem do `VibeLoader`
  - entrou em bootloop
  - as opcoes `VibeOS` e `SafeMode` aparecem
  - fica travado e reinicia

### Fase 4: regressao mais nova

- Em rodada posterior, o sintoma mudou de novo:
  - nao aparece mais a tela normal do `VibeLoader`
  - fica so um cursor em texto
  - as vezes acontece um flash ciano rapido
  - depois a maquina reinicia
- Leitura provisoria:
  - isso pode significar que o menu grafico esta sendo pulado e o boot esta indo direto para o kernel
  - o flash ciano pode ser marcador muito inicial do kernel
  - ou o loader esta entrando no caminho de fallback antes de conseguir desenhar o menu

### Fase 5: travamento durante a propria renderizacao

- Em rodada mais nova no hardware real:
  - ha boots em que a moldura azul do `VibeLoader` aparece parcialmente
  - o titulo aparece
  - a area interna ainda fica vazia, sem as `boot entries`
  - e a maquina pode travar ou reiniciar enquanto a tela ainda esta sendo desenhada
- Leitura provisoria:
  - isso empurra a suspeita para dentro do primeiro `menu_render`
  - especialmente entre `draw_text` do titulo e o primeiro `draw_menu_entry`
  - portanto o bug ja nao parece depender de `Enter`, timeout ou handoff para o kernel

## O que ja foi tentado

### 1. Compatibilidade BIOS / A20

- Reordenacao do fluxo de A20 no `stage2`.
- Tentativa por `INT 15h AX=2401`.
- Tentativa por porta `0x92` com cuidado para nao acionar reset.
- Fallback por `8042`.
- Ajuste do caminho `8042` para:
  - limpar output buffer
  - ler output port
  - escrever com bit de A20 ligado
  - fallback direto com `0xDF`
- Logs na tela para:
  - `LAST`
  - `TRACE`
  - estado por metodo `P/B/C/D`
  - retorno BIOS `2401`
  - estado BIOS `2402`
  - suporte BIOS `2403`
  - valores da porta `0x92`
  - valores lidos/escritos no `8042`

### 2. Validacao mais robusta do A20

- Foi adicionado um segundo teste alternativo de A20.
- O boot inicial agora aceita continuar se:
  - o teste principal passar, ou
  - o teste alternativo passar, ou
  - a BIOS reportar A20 ativo via `2402`
- A mesma logica foi levada para `kernel_asm/vesa_runtime.asm`, para evitar falso negativo no retorno da troca de modo grafico.

### 3. VESA / modos graficos / fallback

- O loader passou a catalogar mais modos VESA.
- O fallback padrao continua `640x480`.
- O catalogo passou a aceitar ate 16 modos.
- Foi corrigido bug de `DS/ES` na enumeracao de modos VESA que podia corromper `mode ids`.
- Foi endurecida a filtragem de modos:
  - `8bpp`
  - LFB valido
  - pitch coerente
  - resolucoes dentro de limites seguros
- `1366x768` passou a ser aceito na cadeia `boot -> kernel -> userland`.

### 4. Runtime de troca de resolucao

- O caminho de troca de modo em `kernel_asm/vesa_runtime.asm` foi sincronizado com as correcoes de:
  - `DS/ES`
  - A20
  - `8042`
- Isso foi feito porque o bug "troca resolucao e volta para o bootloader" podia ser o mesmo falso negativo do A20 acontecendo mais tarde.

### 5. Build e pipeline

- O `Makefile` foi ajustado para Linux/macOS.
- Foi criado `tools/patch_boot_sectors.py` para loop de baixo desgaste.
- O usuario preferiu, por enquanto, continuar regravando a imagem inteira.

### 6. Rodada de debug mais recente

- O `stage2` agora inicializa o `bootinfo` inteiro antes do resto do boot.
- `bootinfo.magic` e `bootinfo.version` agora sao preenchidos no `stage2`.
- O autoboot do menu foi pausado propositalmente para diagnostico.
- O menu passou a mostrar HUD de debug em tempo real:
  - `DBG`
  - `KEY`
  - `EXT`
  - `ACT`
- O kernel ganhou marcas visuais maiores no inicio de `kernel_entry()`, para ficar claro em hardware real se o handoff chegou ao kernel.

### 7. Rodada de debug apos a regressao do cursor

- O caminho `vibeloader_menu_default_boot` foi alterado temporariamente.
- Em vez de pular direto para o kernel quando o menu grafico nao puder ser mostrado, o `stage2` agora para numa tela de texto com:
  - `LAST`
  - `FLAGS`
  - `MODE`
  - `BPP`
  - `W`
  - `H`
- Objetivo:
  - diferenciar claramente `menu pulado por VESA/bootinfo` de `salto para kernel seguido de reset`

### 8. Rodada de debug para reboot antes do menu

- Foi adicionado um rastro persistente em RAM baixa durante o boot.
- O `stage1` agora:
  - mostra `PREV BOOT TRACE ... LAST ...` se o boot anterior morreu antes do kernel estabilizar
  - espera uma tecla antes de tentar novamente
  - registra os marcos:
    - `S` = entrada no `stage1`
    - `R` = leitura do `stage2`
    - `L` = `stage2` carregado
    - `E` = erro de disco no `stage1`
- O `stage2` passou a anexar seus `DEBUG_BOOT_CHAR` ao mesmo rastro persistente.
- O kernel tambem passou a anexar marcos muito iniciais (`0`, `1`, `2`...) antes de marcar o boot como estavel.
- Objetivo:
  - transformar `cursor + reboot` em diagnostico legivel na proxima inicializacao
  - separar `morreu antes do stage2`, `morreu dentro do stage2` e `chegou no kernel e caiu logo em seguida`

### 9. Rodada de alinhamento com o plano do VibeLoader

- O arquivo `docs/VIBELOADER_PLAN.md` foi rechecado antes de continuar a investigacao.
- Foi confirmado um desvio entre implementacao e arquitetura alvo:
  - o `stage2` fazia `find_root_entry + load_file_to_memory` de `KERNEL.BIN` antes de mostrar o menu
  - isso era o oposto do comportamento mais simples e previsivel pedido para BIOS real
- O `stage2` foi ajustado para:
  - nao carregar mais `KERNEL.BIN` na entrada do loader
  - mostrar o menu primeiro
  - ao confirmar a selecao, voltar para real mode
  - carregar o kernel apenas nesse momento
  - reabilitar/revalidar A20 e entrar novamente em protected mode antes do salto final
- Consequencia pratica para o proximo teste:
  - se a maquina reiniciar antes de `Enter`, com essa imagem nova, o problema ainda e do loader/menu e nao do kernel carregado

### 10. Rodada para fechar o ponto do crash dentro do render

- Foi encontrado um problema no proprio mecanismo de trace persistente:
  - `BOOTDEBUG_ADDR` estava em `0x0700`
  - `SCRATCH_BUFFER` usa `0x0600`
  - leituras FAT em `SCRATCH_BUFFER` sobrescreviam a faixa `0x0700-0x07FF`
- Consequencia:
  - o `PREV BOOT TRACE` podia sumir ou vir corrompido justo nos boots em que mais precisava ser confiavel
- Correcao aplicada:
  - `BOOTDEBUG_ADDR` foi primeiro movido para `0x0E00`
  - numa rodada seguinte foi movido de novo para `0x1000`, por ser uma faixa mais conservadora e longe do bloco `0x0600-0x0DFF` usado por scratch/VBE
- Tambem foram adicionados novos marcos persistentes ao redor do primeiro render do menu:
  - `M` = entrou em `vibeloader_menu`
  - `C` = palette/layout do menu passou
  - `m` = entrou no primeiro `menu_render`
  - `n` = titulo ja foi desenhado
  - `o` = prestes a chamar o primeiro `draw_menu_entry`
  - `p` = primeiro `draw_menu_entry` retornou
  - `q` = prestes a chamar o segundo `draw_menu_entry`
  - `r` = segundo `draw_menu_entry` retornou
  - `s` = prestes a chamar o terceiro `draw_menu_entry`
  - `t` = terceiro `draw_menu_entry` retornou
  - `u` = primeiro frame inteiro terminou
  - `I` = o menu entrou no loop estavel apos o primeiro frame
- Leitura esperada do proximo boot:
  - morrer em `...n` ou `...o` aponta para a primeira entry
  - morrer em `...q` ou `...r` aponta para a segunda
  - morrer em `...s` ou `...t` aponta para a terceira
  - chegar em `...uI` significa que o primeiro frame completou e o problema ja esta depois do render inicial

### 11. Rodada para transformar tela preta em diagnostico visivel

- Depois da rodada acima apareceu uma regressao em hardware real:
  - em vez de menu parcial, a maquina passou a ficar em tela preta
- Hipotese principal dessa regressao:
  - se o `stage2` estivesse caindo no caminho `no vesa menu`, o fallback estava tentando desenhar texto diretamente em `0xB8000` ainda a partir de protected mode
  - se a BIOS/loader ja tivesse entrado em modo grafico, isso podia virar "tela preta" em vez de texto legivel
- Correcao aplicada:
  - o fallback `vibeloader_menu_default_boot` agora volta primeiro para real mode
  - força `INT 10h` modo texto `03h`
  - e so entao imprime `LAST`, `TRACE`, `FLAGS`, `MODE`, `BPP`, `W`, `H`
- Consequencia pratica para o proximo teste:
  - se o problema atual for realmente fallback de menu/VESA, a tela preta deve virar uma tela de texto explicita
  - se continuar preta mesmo com essa imagem, o travamento esta acontecendo antes desse fallback ou antes mesmo de chegar ao `stage2`

### 12. Rodada de breadcrumbs visiveis antes do caminho grafico

- Como a tela continuou preta mesmo apos o fallback em real mode:
  - a suspeita principal passou a ser travamento antes do fallback, provavelmente ainda no `stage2` real mode
- Correcao/instrumentacao aplicada:
  - o `stage2` agora força `INT 10h` modo texto `03h` logo na entrada
  - e imprime breadcrumbs visiveis por BIOS teletype durante o caminho inicial:
    - `2` = entrou no `stage2`
    - `F` = passou do `parse_bpb`
    - `V` = terminou `load_optional_background`
    - `B` = terminou `detect_vesa_modes`
    - `P` = passou da revalidacao de A20 e esta prestes a entrar em protected mode
- Leitura esperada do proximo teste:
  - se aparecer algo como `2F` e depois travar, o problema esta entre FAT/background e VESA
  - se aparecer `2FVB` e depois preto, o problema ja esta depois da troca/validacao de video
  - se nao aparecer nem `2`, o problema esta antes do `stage2` ou o texto BIOS nao esta sendo exibido nesse hardware

## Leitura atual mais provavel do bug

### O que parece menos provavel agora

- A20 puro como causa primaria do reset atual.
- Falha em entrar no menu VESA.

### O que parece mais provavel agora

- Alguma etapa entre leitura do `stage2`, carga do kernel, VESA, A20 ou entrada no modo protegido esta derrubando o bootloader antes do diagnostico visual ficar estavel.
- O flash ciano ocasional pode significar:
  - entrada parcial no caminho grafico do `stage2`
  - ou entrada muito curta no kernel antes do reset
- O novo rastro persistente foi introduzido exatamente para decidir entre essas duas hipoteses.
- Com a imagem reconstruida apos o alinhamento com o plano:
  - reboot antes da confirmacao de boot passa a implicar problema no loader
  - reboot so depois de `Enter` volta a colocar o kernel na linha de suspeita
- Com o relato mais novo de travamento durante a propria renderizacao:
  - o suspeito principal passa a ser o caminho de desenho do menu inicial
  - sobretudo a transicao `titulo -> primeira entry`
  - o handoff para o kernel fica ainda menos provavel enquanto o trace morrer antes de `u` ou `I`

## Arquivos-chave ja mexidos nesta investigacao

- `boot/stage2.asm`
- `kernel_asm/vesa_runtime.asm`
- `kernel/entry.c`
- `kernel/drivers/video/video.c`
- `headers/kernel/bootinfo.h`
- `headers/include/userland_api.h`
- `userland/modules/ui.c`
- `Makefile`
- `tools/build_partitioned_image.py`
- `tools/patch_boot_sectors.py`
- `docs/QUICK_BUILD.md`

## Ponto mais importante para a proxima rodada

Nao voltar a focar em A20 por reflexo.

Como o menu grafico do `VibeLoader` ja aparece em hardware real, o bug atual precisa ser tratado primeiro como:

- teclado / polling no menu
- transicao menu -> carga tardia do kernel
- ou travamento apos o salto para o kernel

## Checklist operacional do que ainda falta fazer

## Root cause fechado no QEMU em 2026-03-24

- O reboot preto principal nao era mais A20 nem o salto para protected mode.
- O problema real estava no logger persistente `DEBUG_BOOT_CHAR` quando usado dentro do trecho `BITS 32` do `stage2`.
- `DEBUG_BOOT_CHAR` chamava `debug_log_char`, mas `debug_log_char` estava montado no bloco `BITS 16`.
- Em real mode isso funcionava.
- Em protected mode isso virava codigo com enderecamento/semantica errada e derrubava a maquina no primeiro breadcrumb do menu.
- O sintoma no QEMU ficou bem claro:
  - trace limpo antigo: `MIJSL2FVBabcdqPQMMIJ`
  - leitura:
    - `MIJSL2FVBabcdqPQ` = MBR + stage1 + stage2 ate entrar em protected mode
    - primeiro `M` = entrou em `vibeloader_menu`
    - depois vinha reboot imediato antes de qualquer `C` ou `menu_render`
    - `MIJ` final = segundo boot, travando no prompt `PREV BOOT`
- Correcao aplicada:
  - todos os breadcrumbs de protected mode em `boot/stage2.asm` foram trocados de `DEBUG_BOOT_CHAR` para `DEBUG_CHAR`
  - os breadcrumbs de real mode continuam podendo usar o trace persistente
  - o loader passa a evitar chamar `debug_log_char` de dentro do codigo `BITS 32`
- Marcadores extras `1` e `2` foram deixados logo no inicio de `vibeloader_menu` para separar:
  - flags VESA ok
  - bpp 8 ok

## Validacao fechada no QEMU em 2026-03-24

- Com a correcao acima, o trace de debug ficou:

```text
MIJSL2FVBabcdqPQM12CRmnopqrsturI
```

- Leitura:
  - `M12C` = entrou no menu, validou `BOOTINFO`, passou por `bpp == 8`, voltou do setup inicial
  - `Rmnopqrstur` = primeiro `menu_render` inteiro terminou
  - `I` = loop do menu estabilizou depois do primeiro frame
- Screenshot do QEMU confirmou o menu completo desenhado com:
  - `VIBEOS`
  - `SAFE MODE`
  - `RESCUE SHELL`
  - `VIDEO 640X480`
- Depois de `sendkey ret` no QEMU, o trace virou:

```text
MIJSL2FVBabcdqPQM12CRmnopqrsturIgghKkabcdqPG
```

- Leitura:
  - `gghKkabcdqPG` = caminho de boot pelo menu, volta para real mode, carga tardia do kernel, nova revalidacao de A20 e retorno para `pmode_kernel_resume`
- Screenshot do QEMU depois do `Enter` mostrou `VIBEOS BOOTSTRAP IN...`, entao o fluxo `menu -> kernel` deixou de reiniciar dentro do loader.

## Rodada seguinte para hardware real

- Correcao de contexto:
  - o screenshot com `...Ivv LAST g` foi do QEMU, nao do notebook real
  - portanto esse frame nao prova input fantasma no hardware
  - no hardware real, o sintoma reportado e:
    - renderiza o `VibeLoader` com os botoes visiveis
    - reinicia logo em seguida
    - sem selecao manual do usuario
- O relato novo de hardware real foi:
  - aparecem `2/F/V`
  - o loader chega a mostrar o background / arte do `VibeLoader`
  - depois reinicia
- Isso e compativel com queda ja dentro do caminho grafico em protected mode, depois de `V` e antes de estabilizar o menu.
- A imagem mais nova deixa isso muito mais observavel:
  - agora existe um logger persistente proprio para `BITS 32`
  - o buffer persistente de `PREV BOOT TRACE` foi ampliado para 48+ chars uteis no caminho completo
  - portanto o `PREV BOOT TRACE` do boot seguinte ja pode mostrar marcadores como `M`, `1`, `2`, `C`, `R`, `m...u`, `I`, `g`, `d`, `W`, `G`
  - isso evita voltar ao falso negativo antigo em que o trace persistente morria em `q` porque o logger de PM nao era seguro
- O smoke test no QEMU com essa versao traceada continuou ok:

```text
MIJSL2FVBabcdqPQM12CRmnopqrsturI
```

## Rodada de endurecimento para falha no meio do render em 2026-03-24

- O relato mais novo do hardware real ficou mais especifico:
  - o notebook agora pode travar no meio da propria renderizacao do menu
  - em seguida reinicia sozinho
- Isso reforcou duas hipoteses praticas:
  - excecao em protected mode durante o caminho grafico, que sem IDT valida virava reboot imediato
  - lixo pendente no buffer do teclado, levando o menu a agir como se tivesse recebido entrada logo ao nascer
- Correcao aplicada no `stage2`:
  - agora o loader instala uma IDT minima assim que entra em protected mode
  - se ocorrer excecao no menu, ele deixa de rebootar direto e cai no fallback de debug em texto
  - esse fallback agora mostra titulo generico `VIBELOADER DEBUG HALT`
  - e imprime `EXC xx` quando houve excecao capturada
- Acoes complementares:
  - o buffer do teclado agora e drenado uma vez antes do primeiro loop do menu
  - o `BOOTDEBUG_TRACE_MAX` do `stage2` foi alinhado de volta para `48`, igual ao `stage1`
  - o primeiro `menu_render` ganhou marcos extras:
    - `3` = background terminou
    - `4` = borda externa terminou
    - `5` = painel interno terminou
    - `6` = barra do titulo terminou
    - `7` = texto do titulo terminou
  - as acoes do teclado tambem ganharam marcos:
    - `<` = video anterior
    - `>` = proximo video
    - `!` = confirmacao / boot
- Consequencia pratica esperada no hardware real:
  - se o reboot vinha de excecao do CPU, o comportamento deve mudar de "reinicia" para "para numa tela de debug"
  - se o reboot vinha de tecla fantasma, o menu deve ficar estavel por mais tempo ou parar de saltar sozinho
  - se ainda reiniciar, o `PREV BOOT TRACE` do boot seguinte deve ficar mais limpo e mais consistente
- Validacao local:
  - `make -B build/stage2.bin` OK
  - `make build/boot.img` OK
  - `build/stage2.bin` ficou com `9760` bytes, ou `20` setores
  - o `stage1` final esta carregando `0x14` setores do `stage2`
  - smoke headless no QEMU com `-no-reboot` ficou vivo por varios segundos, sem reset extra no log alem do bootstrap do emulador

## Rodada de alinhamento do trace persistente do kernel em 2026-03-24

- Foi encontrada uma inconsistência entre bootloader e kernel:
  - `stage1` e `stage2` usam `BOOTDEBUG_ADDR = 0x1000`
  - `kernel/entry.c` ainda estava usando `BOOTDEBUG_ADDR = 0x0700`
  - o kernel tambem estava com `BOOTDEBUG_TRACE_MAX = 32`, enquanto o loader ja estava em `48`
- Impacto provavel:
  - o kernel podia marcar o boot como estavel no lugar errado
  - o `PREV BOOT TRACE` podia continuar aparecendo como "sujo" no boot seguinte mesmo quando o kernel ja tivesse avancado
  - isso tambem podia confundir a leitura dos marcadores iniciais do kernel
- Correcao aplicada:
  - `kernel/entry.c` agora usa `BOOTDEBUG_ADDR = 0x1000`
  - `kernel/entry.c` agora usa `BOOTDEBUG_TRACE_MAX = 48`
- Validacao local:
  - `make build/kernel/entry.o` OK
  - `make build/boot.img` OK
- Consequencia pratica para o proximo teste em hardware real:
  - se o kernel chegar a executar e estabilizar memoria, o dirty flag deve ser limpo na mesma estrutura usada por `stage1/stage2`
  - o `PREV BOOT TRACE` do boot seguinte deve ficar mais confiavel para diferenciar "morreu no loader" de "chegou no kernel"

### Check imediato em hardware real com a imagem atual

- [ ] Regravar a imagem completa nova.
- [ ] Se aparecer tela de texto `VIBELOADER DEBUG HALT`, fotografar inteira.
- [ ] Se houver `EXC xx`, anotar exatamente esse valor hexadecimal.
- [ ] Lembrar que, nesta imagem nova, o kernel ainda nao deve ter sido carregado enquanto o menu esta na tela.
- [ ] Dar boot e observar se ainda ocorre `cursor + reboot`.
- [ ] Na inicializacao seguinte, verificar se aparece `PREV BOOT TRACE ... LAST ...`.
- [ ] Fotografar exatamente a linha `TRACE` e o `LAST`.
- [ ] Se travar/reiniciar durante a renderizacao parcial do menu, anotar se o ultimo frame visivel parou antes das entries ou no meio delas.
- [ ] Se aparecer a tela `VIBELOADER NO VESA MENU`, fotografar tambem `FLAGS`, `MODE`, `BPP`, `W`, `H`.
- [ ] Se o menu grafico voltar a aparecer, confirmar se ainda mostra `AUTO BOOT PAUSED`.
- [ ] Se o menu grafico voltar, confirmar se a linha `DBG ... KEY ... EXT ... ACT ...` reage ao teclado.
- [ ] No boot seguinte ao crash, fotografar `PREV BOOT TRACE` para mapear o ultimo marco entre `M/C/m/n/o/p/q/r/s/t/u/I`.
- [ ] Se o salto para o kernel acontecer, observar se surgem blocos coloridos/barra de progresso no topo da tela.
- [ ] Observar se aparece texto `VIBE OS Booting...`.

### Se o HUD do menu nao reagir ao teclado

- [ ] Instrumentar ainda mais `menu_poll_keyboard`.
- [ ] Verificar se o controlador de teclado esta entregando scancodes diferentes do esperado.
- [ ] Considerar adicionar diagnostico visivel do status da porta `0x64`.

### Se o HUD reagir, mas `Enter` reiniciar sem marcas do kernel

- [ ] Instrumentar o caminho `vibeloader_menu_boot_now -> menu_apply_selection -> pmode_to_real_for_kernel_boot -> load_kernel_file -> enable_a20 -> pmode_kernel_resume -> jmp CODE_SEG:0x10000`.
- [ ] Adicionar marcador visual imediatamente antes da volta para real mode e imediatamente antes do `jmp CODE_SEG:0x10000`.
- [ ] Confirmar se o problema esta na carga tardia do kernel, na volta para protected mode ou no proprio binario carregado.

### Se as marcas do kernel aparecerem, mas reiniciar depois

- [ ] Mapear exatamente ate qual marcador de `kernel_entry()` o boot chega.
- [ ] Se necessario, transformar os marcadores do kernel em legenda numerada visivel na tela.
- [ ] Isolar qual subsistema dispara o reset:
  - HAL
  - CPU init
  - GDT
  - video
  - PIC/IDT
  - timer/input
  - memoria
  - storage

### Depois que o boot principal estabilizar

- [ ] Retomar o bug de troca de resolucao no menu do loader.
- [ ] Retomar o bug de troca de resolucao no `Personalizar`.
- [ ] Confirmar que `1366x768` funciona em BIOSes que realmente exponham esse modo em VESA 8bpp LFB.

## Checklist de continuidade se o chat acabar

- [ ] Reabrir este arquivo primeiro.
- [ ] Conferir `git diff` dos arquivos de boot/video.
- [ ] Regerar a imagem com `make build/boot.img`.
- [ ] Fazer o proximo teste fisico seguindo o checklist acima.
- [ ] Atualizar este documento antes de iniciar outra rodada grande de mudancas.

## Ultimo estado do build

- `make -B build/stage2.bin` OK
- `make build/boot.bin` OK
- `make build/kernel/entry.o` OK
- `make build/boot.img` OK

## Observacao final

O trabalho ja saiu da fase "boot nem chega no menu" e entrou na fase "menu aparece, mas a transicao seguinte quebra". Isso e progresso real. A prioridade agora e transformar a proxima rodada em um experimento binario:

- ou o teclado/menu esta morto
- ou o salto para o kernel esta quebrando
- ou o kernel entrou e esta quebrando cedo

Este arquivo existe justamente para manter essa linha de raciocinio sem perder contexto entre conversas.
