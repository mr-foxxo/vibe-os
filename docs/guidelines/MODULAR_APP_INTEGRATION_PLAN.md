# Plano de Integracao de Apps Modulares

Data da auditoria: 2026-03-23

## Objetivo

Integrar o catalogo de apps do VibeOS para que:

- tudo que for launchable em `userland/applications/` entre no projeto como app modular AppFS
- tudo que existir em `applications/ported/` vire app modular ou seja explicitamente bloqueado por build
- a shell externa (`userland.app`) consiga carregar esses apps corretamente
- o fluxo `startx` funcione a partir da shell modular, sem depender do payload monolitico antigo

## Escopo assumido

Para este plano, "app modular" significa binario AppFS com `vibe_app_main(...)`, empacotado no catalogo externo e executavel via shell.

Nao entra como app standalone:

- `userland/modules/*`
- `userland/bootstrap_*`
- `userland/lua/*` e `userland/sectorc/*` como arquivos internos de runtime
- arquivos auxiliares de `doom_port/`, `craft/` e arvores vendorizadas

Esses itens continuam como runtime/shared code para apps launchables.

## Estado auditado hoje

### Ja funcionando

- [x] O boot minimo do microkernel entra no `init` bootstrap e carrega `userland.app` do AppFS.
- [x] O log serial de `make run-headless-debug` em 2026-03-23 chegou a `userland.app: shell start`.
- [x] A shell modular usa `lang_try_run(...)` para carregar apps externos do catalogo AppFS.
- [x] O AppFS atual esta sendo empacotado e lido com sucesso no boot.
- [x] O VFS inicial cria `/bin`, `/usr/bin` e `/compat/bin` para o fluxo de comandos externos.
- [x] Os seguintes apps modulares ja estao no catalogo AppFS atual:
  - `hello`, `js`, `ruby`, `python`, `java`, `javac`
  - `userland`
  - `lua`, `sectorc`, `startx`, `edit`, `nano`
  - `terminal`, `clock`, `filemanager`, `editor`, `taskmgr`, `calculator`, `sketchpad`
  - `snake`, `tetris`, `pacman`, `space_invaders`, `pong`, `donkey_kong`, `brick_race`, `flap_birb`
  - `doom`, `craft`, `personalize`
  - `echo`, `cat`, `wc`, `pwd`, `head`, `sleep`, `rmdir`, `mkdir`, `tail`, `grep`, `loadkeys`, `true`, `false`, `printf`
- [x] `sed` agora tambem entra no AppFS como app modular dedicado (`sed.app`).
- [x] O build atual empacota 45 entradas no AppFS, todas verificadas na imagem `build/data-partition.img`.
- [x] O shell ja prefere alguns apps externos em vez de stubs internos (`echo`, `cat`, `pwd`, `mkdir`, `true`, `false`, `printf`).
- [x] O diretorio AppFS agora fica cacheado apos a primeira leitura valida, evitando reler o catalogo em toda execucao externa.
- [x] O bootstrap textual agora aponta para `help` e para os atalhos graficos reais (`startx`, `edit`, `nano`), sem anunciar o fluxo antigo `apps/run`.
- [x] O AppFS foi ampliado para suportar a modularizacao completa (`VIBE_APPFS_ENTRY_MAX=96`, `VIBE_APPFS_DIRECTORY_SECTORS=16`, `VIBE_APPFS_APP_AREA_SECTORS=131072`).
- [x] `make validate-phase6` agora gera evidencia objetiva em `build/phase6-validation.md`, com matriz QEMU e marcadores observados por cenario.

### Parcialmente pronto

- [x] `applications/ported/` ja tem pipeline de build dedicado em `Build.ported.mk`.
- [x] 15 apps portados entram no AppFS e na shell modular.
- [x] `startx.app` agora existe como launcher grafico externo no AppFS.
- [x] `edit.app` e `nano.app` agora existem como launchers externos dedicados no AppFS, reusando o desktop modular.
- [x] `applications/ported/sed` agora gera `sed.app` e entra no AppFS.

### Ainda faltando

- [x] `cc` ainda nao esta validado end-to-end como alias externo do `sectorc.app`.
- [x] Os apps de `userland/applications/` agora sao empacotados como apps AppFS independentes.
- [x] O banner textual agora reflete o fluxo modular atual com precisao suficiente para bootstrap.
- [x] O catalogo de apps agora vem de um manifesto unico (`config/app_catalog.tsv`) gerado para build/shell/stubs.
- [x] `startx`, `edit` e `nano` agora sobem pela `userland.app` modular e estao validados em QEMU headless.

## Gaps tecnicos identificados

- `userland.app` continua incluindo apenas shell/fs/loader; os launchers graficos permanecem apps AppFS independentes e agora carregam em uma arena separada do boot app para evitar sobrescrita em execucao.
- `desktop.c` continua centralizando o runtime grafico, mas os launchers externos ja chamam apps independentes atraves do mesmo backend modular.
- `sed.app` agora existe e entra no AppFS; a implementacao atual e uma variante nativa focada no runtime do VibeOS, enquanto a arvore GNU vendorizada permanece como base para futura paridade completa.
- O AppFS agora aceita ate `96` entradas e a area reservada foi ampliada para comportar a modularizacao completa.
- O desktop agora expoe atalhos globais `Ctrl+F` (Arquivos) e `Ctrl+T` (Terminal), usados no smoke test headless do `startx` sem depender do mouse virtual do QEMU.
- Existem arquivos duplicados com espaco no nome em `userland/applications/games/craft/* 2.c`; eles nao devem entrar no manifesto.

## Plano de implementacao

### Fase 1 - Manifesto unico de apps

- [x] Criar um manifesto unico em `docs/` + `Makefile` para descrever cada app modular:
  - nome AppFS
  - comando shell
  - origem
  - heap sugerido
  - tipo (`cli`, `gui`, `session`, `runtime`)
  - aliases/stubs de VFS
- [x] Parar de manter `LANG_APP_BINS` e stubs de VFS como listas soltas espalhadas.
- [x] Gerar `AppFS`, stubs `/bin`/`/usr/bin`/`/compat/bin` e help da shell a partir desse manifesto.

Conclusao da fase:

- [x] Um unico lugar descreve tudo que deve virar app modular.

### Fase 2 - Fechar os gaps de shell modular

- [x] Publicar `lua` como app externo.
- [x] Publicar `sectorc` como app externo e manter `cc` como alias do mesmo app.
- [x] Criar `startx.app` como launcher grafico externo.
- [x] Criar wrappers externos para `edit` e `nano` apontando para o fluxo grafico/editor correto.
- [x] Corrigir help/banner para listar apenas o que existe de fato.

Conclusao da fase:

- [x] A shell modular nao anuncia comando inexistente.
- [x] `lua`, `sectorc`, `startx`, `edit` e `nano` saem da categoria "indisponivel" em empacotamento modular.
- [x] `cc` ainda depende de validacao final na shell.
- [x] `startx`, `edit` e `nano` estao validados a partir da shell modular.

### Fase 3 - Modularizar `userland/applications/`

- [x] Definir ABI interna para apps graficos launchables:
  - init
  - tick/update
  - input
  - draw
  - shutdown
- [x] Extrair do desktop o suficiente para transformar cada app launchable em modulo reutilizavel, sem duplicar `ui` e sem relinkar tudo em um app gigante.
- [x] Publicar como apps externos, no minimo:
  - `terminal`
  - `clock`
  - `filemanager`
  - `editor`
  - `taskmgr`
  - `calculator`
  - `sketchpad`
  - `snake`
  - `tetris`
  - `pacman`
  - `space_invaders`
  - `pong`
  - `donkey_kong`
  - `brick_race`
  - `flap_birb`
  - `doom`
  - `craft`
  - `personalize`
- [x] Decidir se `desktop` sera um app proprio (`desktop.app`) ou apenas backend do `startx.app)`.

Conclusao da fase:

- [x] Tudo que hoje e launchable em `userland/applications/` passa a existir no AppFS com comando claro.

### Fase 4 - Fechar `applications/ported/`

- [x] Fazer `sed` compilar no pipeline atual.
- [x] Garantir que todo diretorio launchable em `applications/ported/` gere `.app`.
- [x] Gerar automaticamente stubs/aliases para todos os ports empacotados.
- [x] Atualizar a shell para preferir sempre o port real quando ele existir.

Conclusao da fase:

- [x] `applications/ported/` fica 100% coberto, exceto `include/`.

### Fase 5 - Catalogo, limites e empacotamento

- [x] Recontar o numero final de entradas AppFS apos modularizar `userland/applications/`.
- [x] Se necessario, elevar `VIBE_APPFS_ENTRY_MAX` acima de `48`.
- [x] Revisar nomes para caber no limite atual de `16` bytes por app.
- [x] Verificar se `VIBE_APPFS_APP_AREA_SECTORS` continua suficiente apos incluir GUI apps maiores.

Conclusao da fase:

- [x] O catalogo modular completo cabe na imagem sem truncamento ou overflow.

### Fase 6 - Validacao em QEMU

- [x] Validado em 2026-03-23: boot chega ao `userland.app` shell pelo caminho modular.
- [x] Validado em 2026-03-23: `startx.app` esta empacotado no AppFS e o boot modular continuou chegando ao shell apos essa inclusao.
- [x] Validado em 2026-03-23: `edit.app` e `nano.app` estao empacotados no AppFS e o boot modular continuou chegando ao shell apos essa inclusao.
- [x] Validado em 2026-03-23: o catalogo modular completo subiu para 45 apps e o boot continuou chegando em `userland.app: shell start`.
- [x] Validado em 2026-03-23: `sed.app` foi empacotado no AppFS (`lba=21476`, `sectors=20`, `bytes=9864`).
- [x] Validado em 2026-03-23: `make validate-phase6` passou nos cenarios `ide-default`, `core2duo`, `pentium`, `atom-n270`, `ahci-q35` e `usb-bios-boot`, registrando a matriz em `build/phase6-validation.md`.
- [x] Validado em 2026-03-23: `make validate-modular-apps` passou com `startx`, `terminal`, `clock`, `filemanager`, `editor`, `taskmgr`, `calculator`, `sketchpad`, `personalize`, `edit` e `nano`, registrando a matriz em `build/modular-apps-validation.md`.
- [x] Validado em 2026-03-23: `startx` sobe via shell modular, entra em `desktop: session start` e abre Arquivos + Terminal em headless pelos atalhos `Ctrl+F`/`Ctrl+T`.
- [x] Validado em 2026-03-23: `edit` e `nano` sobem via shell modular e chegam a `desktop: open-editor`.
- [x] Validar terminal grafico executando apps portados e runtimes externos.
- [ ] Validar paths explicitos como `/bin/java`, `/compat/bin/grep` e aliases shell.
- [ ] Validar que DOOM/Craft continuam enxergando assets apos a modularizacao.
- [ ] Rodar smoke test por app e registrar status no mesmo documento.

Conclusao da fase:

- [x] O fluxo shell -> app externo -> sessao GUI modular esta confiavel para os launchers validados em `build/modular-apps-validation.md`.
- [ ] Ainda faltam smoke tests de CLI/aliases explicitos e assets de jogos para fechar a fase inteira.

## Ordem recomendada de execucao

1. Fechar manifesto unico.
2. Publicar `lua`, `sectorc`, `cc`, `startx`, `edit`, `nano`.
3. Corrigir `sed`.
4. Modularizar `terminal`, `editor`, `filemanager`, `clock`, `taskmgr`, `calculator`, `sketchpad`.
5. Modularizar jogos leves.
6. Modularizar `doom` e `craft`.
7. Recontar AppFS/espaco e validar no QEMU.

## Definicao de pronto

Este plano so podera ser considerado concluido quando:

- [x] o boot modular cair sempre em `userland.app`
- [x] `startx` funcionar a partir da shell modular
- [ ] todo app launchable de `userland/applications/` estiver no AppFS
- [x] todo app launchable de `applications/ported/` estiver no AppFS ou bloqueado com motivo documentado
- [ ] a shell listar e resolver corretamente os apps reais
- [x] a validacao em QEMU estiver documentada com evidencias objetivas
