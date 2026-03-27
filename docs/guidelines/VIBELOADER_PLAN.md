# Plano do Refactor do Bootloader para VibeLoader e da Pipeline de Imagens do Desktop

Data da auditoria: 2026-03-24

## Objetivo

Evoluir o boot atual do vibeOS para um bootloader com identidade propria, chamado `VibeLoader`, inspirado na combinacao de simplicidade previsivel do LILO com a experiencia de menu e configuracao do GRUB. No mesmo ciclo, consolidar a pipeline de imagens do desktop para que o `mdesktop` aceite `png` como wallpaper, use `assets/wallpaper.png` como fundo padrao e permita escolher uma imagem real do filesystem pelo menu `Personalizar`.

## Regras obrigatorias deste refactor

- [x] O nome oficial do refactor sera `VibeLoader`.
- [x] O bootloader precisa exibir `boot entries` navegaveis por teclado.
- [x] A referencia visual obrigatoria do boot sera `assets/bootloader_background.png`.
- [x] O boot deve continuar funcional em BIOS legado com a cadeia atual `MBR -> VBR/stage1 -> loader`.
- [x] O design deve misturar filosofia `LILO` nos primeiros estagios pequenos e deterministas com filosofia `GRUB` no menu, timeout e configuracao.
- [x] O desktop deve passar a aceitar `png` alem de `bmp` para wallpaper.
- [x] O menu `Personalizar` deve ganhar uma forma de escolher imagem do filesystem, e nao apenas listar wallpapers detectados automaticamente.
- [x] `assets/wallpaper.png` deve virar o wallpaper padrao do desktop quando nao houver wallpaper salvo pelo usuario.
- [x] O suporte atual a `bmp` nao pode regredir.

## Estado auditado hoje

- [x] `boot/mbr.asm` inicializa `BOOTINFO`, tenta fixar `VBE_MODE_640X480X8` e transfere o boot para a VBR ativa.
- [x] `boot/stage1.asm` so carrega `boot/stage2.asm` por setores fixos.
- [x] `boot/stage2.asm` parseia FAT32, encontra apenas `KERNEL.BIN`, carrega o kernel e entra em protected mode.
- [x] O build reserva `BOOT_STAGE2_START_SECTOR=8` e `BOOT_KERNEL_START_SECTOR=32`, logo o loader em setores fixos nao deve crescer sem controle.
- [x] O binario atual `build/stage2.bin` mede 1115 bytes, entao existe folga, mas nao o bastante para transformar o stage atual em um GRUB inteiro sem planejamento.
- [x] A FAT do boot hoje recebe `KERNEL.BIN`, `STAGE2.BIN`, `LAYOUT.TXT`, `BOOTPOLICY.TXT` e `DATAINFO.TXT`.
- [x] `stage2/` e `linker/stage2.ld` existem no repo, mas nao estao ligados ao build real do boot neste momento.
- [x] `userland/modules/ui.c` persiste o caminho do wallpaper em `/config/ui.cfg`.
- [x] `userland/modules/ui.c` ainda decodifica wallpaper apenas por `bmp_decode_to_palette(...)`.
- [x] `headers/userland/modules/include/bmp.h` limita o wallpaper a `BMP_MAX_TARGET_W=80` e `BMP_MAX_TARGET_H=60`.
- [x] `userland/applications/desktop.c` lista wallpapers so por `find_bmp_nodes(...)`.
- [x] `find_bmp_nodes(...)` limita a UI a no maximo 4 imagens encontradas.
- [x] O menu de contexto do file manager so habilita `Definir plano` para arquivos `.bmp`.
- [x] `userland/modules/fs.c` ja sabe detectar e registrar assets `png` em LBAs fixos da particao de dados.
- [x] `assets/bootloader_background.png` e `assets/wallpaper.png` ja existem no repositorio.

## Principios de design

- LILO: manter `MBR` e `stage1` minimos, previsiveis e baratos de depurar.
- GRUB: mover configuracao de menu, timeout, entries e tema para arquivos legiveis e para um loader menos apertado.
- Compatibilidade: manter boot direto do kernel como fallback se `VibeLoader` falhar ou se a configuracao estiver ausente.
- Pipeline unica: `bmp` e `png` devem convergir para a mesma representacao indexada do wallpaper no desktop.
- Fallback claro: se a imagem falhar, o sistema volta para cor solida em vez de quebrar o boot ou o desktop.

## Arquitetura alvo do boot

### Camadas

- `boot/mbr.asm`
  Continua minimo, responsavel por geometria, `BOOTINFO` e salto para a particao bootavel.
- `boot/stage1.asm`
  Continua como carregador pequeno por setores fixos.
- `boot/stage2.asm`
  Deve virar um shim de descoberta/carregamento do `VibeLoader` real, e nao mais o lugar final da logica de menu.
- `VibeLoader core`
  Novo binario principal carregado a partir da FAT do boot, idealmente como arquivo dedicado como `/VIBELOAD.BIN` ou nome equivalente.

### Motivacao desta quebra

- [x] O stage atual vive numa janela curta antes do kernel e nao e o lugar ideal para parser de config, menu, timeout, renderizacao e asset grafico.
- [x] O build ja sabe copiar arquivos arbitrarios para a FAT do boot via `--boot-file`, entao existe caminho natural para adicionar `VibeLoader`, config e assets.
- [x] Essa divisao reproduz a ideia de "primeiros estagios enxutos" do LILO e "core configuravel" do GRUB.

## Escopo funcional do VibeLoader

- [ ] Ler um arquivo de configuracao simples, por exemplo `/VIBELOADER.CFG`.
- [x] Mostrar um menu grafico em `640x480x8`, alinhado com o modo que o boot atual ja tenta habilitar.
- [x] Usar `assets/bootloader_background.png` como fonte do background do boot.
- [x] Exibir pelo menos 3 entradas no primeiro ciclo: `VibeOS`, `Safe Mode` e `Rescue Shell`.
- [x] Suportar `default`, contador/timeout de `5s`, setas para cima/baixo e `Enter`.
- [x] Permitir fallback para boot direto de `KERNEL.BIN` quando config, asset ou loader avancado nao estiverem disponiveis.
- [ ] Mostrar status curto de erro quando houver falha de leitura.

## Estrategia de asset do boot

- [ ] Nao decodificar `png` bruto dentro do estagio mais apertado do boot.
- [ ] Tratar `assets/bootloader_background.png` como fonte autoritativa de design, mas converter em build para um blob de boot amigavel a BIOS.
- [x] Criar um conversor em `tools/` para gerar um asset indexado 8-bit, com dimensao alvo compacta (`80x60`) escalada em boot para `640x480`.
- [x] Copiar o asset convertido para a FAT do boot junto com o loader.
- [x] Se o asset falhar, o menu deve renderizar sobre um fundo solido e continuar bootando.

## Formato inicial sugerido para configuracao

```ini
timeout=3
default=0
background=/VIBEBOOT.BIN

entry=VibeOS|/KERNEL.BIN|
entry=VibeOS Safe Mode|/KERNEL.BIN|safe=1
entry=VibeOS Rescue Shell|/KERNEL.BIN|rescue=1
```

Obs.: o formato pode continuar propositalmente simples neste ciclo, sem script, sem modulos dinamicos e sem submenu.

## Plano de implementacao

### Fase 1 - Preparar o bootstrap do VibeLoader

- [ ] Extrair de `boot/stage2.asm` somente o necessario para FAT32, leitura de arquivo e handoff.
- [ ] Definir endereco de carga e contrato de entrada do `VibeLoader core`.
- [ ] Decidir se o `VibeLoader core` sera escrito em ASM, em C reaproveitando `stage2/`, ou em formato hibrido.
- [ ] Ligar o build do novo loader ao `Makefile` sem romper `KERNEL.BIN` e `STAGE2.BIN`.
- [ ] Publicar `VIBELOAD.BIN`, `VIBELOADER.CFG` e o background convertido na FAT do boot.

### Fase 2 - Menu, entries e UX do boot

- [ ] Implementar parser do arquivo de configuracao do boot.
- [ ] Renderizar frame de menu, titulo, countdown e estado da entrada selecionada.
- [ ] Suportar navegacao por teclado antes do salto para o kernel.
- [ ] Padronizar nomes e argumentos das entradas iniciais.
- [ ] Adicionar tela/fallback de erro amigavel para `kernel nao encontrado`, `config invalida` e `asset ausente`.

### Fase 3 - Pipeline generica de imagem no desktop

- [ ] Criar uma API de imagem generica, por exemplo `image_decode_to_palette(...)`, em vez de acoplar wallpaper a `bmp_decode_to_palette(...)`.
- [ ] Manter o backend atual de `bmp`.
- [ ] Adicionar backend de `png` para userland.
- [ ] Renomear o limite de wallpaper para algo neutro ao formato, em vez de reaproveitar `BMP_MAX_TARGET_*` como nome definitivo.
- [ ] Garantir que o wallpaper continue sendo reduzido para uma grade pequena indexada, como hoje, para nao inflar custo de renderizacao do desktop.

### Fase 4 - Default wallpaper e registro de asset do desktop

- [ ] Incluir `assets/wallpaper.png` no build da particao de dados, seguindo o mesmo modelo dos outros assets raw registrados por LBA.
- [ ] Registrar esse asset como arquivo visivel em runtime, por exemplo `/wallpaper.png`.
- [ ] Ensinar `ui_init()` ou `ui_load_settings()` a usar `/wallpaper.png` quando `wallpaper=` estiver vazio ou invalido.
- [ ] Preservar a ordem de preferencia: wallpaper salvo pelo usuario -> wallpaper padrao `/wallpaper.png` -> fundo solido pela cor do tema.

### Fase 5 - Melhoria do menu Personalizar e do file manager

- [ ] Trocar `find_bmp_nodes(...)` por uma busca generica de wallpapers (`.bmp` e `.png`).
- [ ] Manter alguns atalhos rapidos no painel atual, mas adicionar um botao explicito `Escolher arquivo`.
- [ ] Criar um modo novo no dialogo de arquivo ou um picker dedicado para selecionar imagem do filesystem.
- [ ] Permitir aplicar wallpaper tanto pelo `Personalizar` quanto pelo menu de contexto do file manager para `.bmp` e `.png`.
- [ ] Atualizar mensagens de UI para nao falarem apenas em `.bmp`.
- [ ] Remover o gargalo artificial de no maximo 4 wallpapers descobertos como unica forma de selecao.

### Fase 6 - Compatibilidade, fallback e validacao

- [ ] Validar boot normal com config presente.
- [ ] Validar boot com `VIBELOADER.CFG` ausente, confirmando fallback para `KERNEL.BIN`.
- [ ] Validar boot com background ausente ou corrompido.
- [ ] Validar aplicacao de wallpaper `.bmp`.
- [ ] Validar aplicacao de wallpaper `.png`.
- [ ] Validar persistencia do wallpaper escolhido em `/config/ui.cfg`.
- [ ] Validar que `assets/wallpaper.png` aparece como default em instalacao limpa.
- [ ] Validar que o menu `Personalizar` consegue escolher um arquivo real do filesystem.

## Riscos que precisam ser tratados cedo

- [ ] Crescimento indevido do loader nos setores fixos entre `BOOT_STAGE2_START_SECTOR` e `BOOT_KERNEL_START_SECTOR`.
- [ ] Falta de memoria convencional se o background do boot for grande demais ou descompactado cedo demais.
- [ ] Reuso acidental de decoder pesado de `png` dentro do boot, quando ele deveria ficar no pipeline de build ou no userland.
- [ ] Regressao no suporte atual a wallpapers `bmp`.
- [ ] Persistencia de caminho invalido em `/config/ui.cfg`, deixando o desktop sem fallback visivel.

## Fora de escopo deste ciclo

- [ ] UEFI nativo. não quero suporte a urfi, apenas boot legacy 32 bits
- [ ] Script de boot estilo shell.
- [ ] Temas dinamicos multiplos do boot.
- [ ] Animacoes complexas no boot.
- [ ] Suporte a formatos alem de `bmp` e `png` no desktop.

## Definicao de pronto

- [ ] O boot ainda funciona no caminho BIOS atual.
- [ ] `VibeLoader` sobe com menu e `boot entries`.
- [ ] O background do boot vem de `assets/bootloader_background.png`, ainda que convertido em build para um formato de boot proprio.
- [ ] Existe pelo menos uma entrada padrao de boot e duas entradas auxiliares.
- [ ] O desktop aceita `bmp` e `png` como wallpaper.
- [ ] O menu `Personalizar` consegue escolher imagem do filesystem sem depender de descoberta automatica limitada.
- [ ] `assets/wallpaper.png` aparece como wallpaper padrao em ambiente sem configuracao previa.
- [ ] O documento pode ser atualizado com `[x]` conforme cada fase for implementada.
