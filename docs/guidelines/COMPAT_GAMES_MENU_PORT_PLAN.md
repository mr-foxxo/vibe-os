# Plano de Port dos Jogos de `compat/games` para o Menu Games

Data da auditoria: 2026-03-24

## Objetivo

Portar para o VibeOS todos os jogos viaveis de `compat/games` e fazer com que aparecam no menu de Games do desktop como apps modulares reais, sem stubs. O diretorio `hunt` saiu do escopo deste ciclo porque depende de uma arquitetura cliente+daemon que nao se encaixa no pipeline atual de apps BSD sem abrir um projeto proprio de backend.

## Regras obrigatorias deste port

- [x] O escopo auditado cobriu os 41 diretorios de primeiro nivel em `compat/games`.
- [x] `hunt` foi oficialmente removido do escopo de entrega deste ciclo por custo/complexidade desproporcionais ao restante do port.
- [x] O sistema ja possui manifesto unico de apps em `config/app_catalog.tsv`.
- [x] O desktop ja possui uma aba `Games` no menu iniciar.
- [x] O desktop atual ja expoe jogos nativos no menu (`Snake`, `Tetris`, `Pacman`, `Invaders`, `Pong`, `Donkey Kong`, `Brick Race`, `Flap Birb`, `DOOM`, `Craft`).
- [x] Ja existem implementacoes nativas de `snake` e `tetris` em `userland/applications/games/`.
- [x] Nenhum jogo vindo de `compat/games` publicado ate agora entra como stub temporario.
- [x] Quando houver colisao de nome com jogo ja existente no menu, o port BSD deve usar sufixo `-bsd`.
- [x] O port de `compat/games/tetris` aparece como `tetris-bsd`.
- [x] O port de `compat/games/snake` aparece como `snake-bsd`.

## Estado auditado hoje

- [x] `compat/games` existe e esta versionado no repositório.
- [x] Ja existem entradas de `compat/games` publicadas em `config/app_catalog.tsv`.
- [x] O menu de games atual e alimentado por uma lista fixa em `userland/applications/desktop.c`.
- [x] O desktop passou a abrir os ports BSD publicados por comando no terminal grafico, sem exigir novos `app_type` dedicados.
- [x] O enum de apps graficos continua terminando em `APP_PERSONALIZE`, e os ports BSD publicados convivem via `APP_TERMINAL`.
- [x] `START_MENU_ENTRY_COUNT` ja foi expandido para acomodar os jogos BSD publicados ate aqui.
- [x] Ja existe uma camada compartilhada para portar jogos BSD de `compat/games` para a ABI de apps do desktop.
- [x] Ja existe uma familia `*-bsd` registrada no desktop e no catalogo AppFS (`snake-bsd`, `tetris-bsd`).
- [ ] Ainda nao existe validacao de smoke test para nenhum jogo de `compat/games` dentro do menu do desktop.
- [x] Validacao de build local em 2026-03-24 gerou 41 apps jogaveis via `Build.bsdgames.mk`:
  `adventure`, `arithmetic`, `atc`, `backgammon`, `banner`, `battlestar`, `bcd`, `boggle`, `bs`, `caesar`, `canfield`, `cribbage`, `factor`, `fish`, `fortune`, `gomoku`, `grdc`, `hack`, `hangman`, `mille`, `monop`, `morse`, `number`, `phantasia`, `pig`, `pom`, `ppt`, `primes`, `quiz`, `rain`, `random`, `robots`, `sail`, `snake-bsd`, `teachgammon`, `tetris-bsd`, `trek`, `wargames`, `worm`, `worms`, `wump`.
- [x] Validacao estatica em 2026-03-24 confirmou 41 comandos BSD publicados no catalogo AppFS, 41 entradas BSD na aba `Games` do desktop e os 41 bins correspondentes indexados na `build/data-partition.img`.

## Fase 1 - Infraestrutura comum de port BSD

- [x] Definir diretorio-alvo para os ports dos jogos BSD em `applications/ported/bsdgames/`.
- [x] Criar uma camada comum para entrada, timer, console, persistencia basica e compatibilidade BSD compartilhada.
- [ ] Mapear dependencias por jogo: `libcurses`, `libm`, lexer/parser (`lex`/`yacc`) e arquivos de dados.
- [x] Decidir quais jogos rodam em janela grafica propria e quais rodam via terminal grafico embutido no desktop.
- [x] Garantir que a estrategia escolhida nao use stubs parecidos com `userland/applications/startx_game_stubs.c`.
- [x] Criar padrao unico para assets/importacao de dados em `/games` ou caminho equivalente do AppFS.
  O port agora usa geradores dedicados (`tools/generate_asset_installer.py`, `generate_adventure_data`, `generate_phantasia_assets`, `fortune-strfile`, `monop-initdeck`, `boggle-mkdict`, `boggle-mkindex`) para instalar os dados em AppFS antes da execucao do jogo.

## Fase 2 - Integracao com desktop, shell e catalogo

- [x] Confirmar a rota de abertura dos ports BSD publicados via `APP_TERMINAL`, sem expandir `enum app_type`.
- [x] Adicionar captions e rotas de abertura via terminal para os jogos BSD ja publicados.
- [x] Atualizar `userland/applications/desktop.c` para incluir os jogos BSD ja publicados na aba `Games`.
- [x] Evitar alteracoes em `userland/applications/desktop_app_main.c` usando nomes de comando resolvidos pelo catalogo AppFS no terminal.
- [x] Registrar todos os jogos BSD ja publicados em `config/app_catalog.tsv`.
- [x] Garantir que os nomes finais publicados ate agora respeitam o limite atual do catalogo AppFS.
- [x] Garantir que `snake-bsd` e `tetris-bsd` convivam com os jogos nativos sem sobrescrever entradas existentes.
- [x] Revisar capacidade de `MAX_WINDOWS` e da UI do menu para a nova quantidade de jogos.
  `MAX_WINDOWS` continua em 20 e nao precisou mudar, porque os ports BSD continuam abrindo em instancias de `APP_TERMINAL` em vez de multiplicar tipos/slots dedicados.

## Fase 3 - Validacao obrigatoria

- [x] Validar build de todos os jogos BSD ja publicados sem stubs.
- [x] Validar empacotamento AppFS de todos os jogos BSD ja publicados.
- [ ] Validar abertura pelo menu `Games` para todos os jogos BSD.
- [ ] Validar abertura pela shell para todos os comandos publicados.
- [x] Validar assets/dados para jogos que dependem de arquivos externos.
- [ ] Registrar resultados de smoke test por jogo no proprio documento ou em artefato de build dedicado.

## Checklist por jogo

### Ports diretos de um binario principal

- [x] `adventure` -> publicar `adventure`
- [x] `arithmetic` -> publicar `arithmetic`
- [x] `atc` -> publicar `atc`
- [x] `banner` -> publicar `banner`
- [x] `battlestar` -> publicar `battlestar`
- [x] `bcd` -> publicar `bcd`
- [x] `bs` -> publicar `bs`
- [x] `caesar` -> publicar `caesar`
- [x] `cribbage` -> publicar `cribbage`
- [x] `factor` -> publicar `factor`
- [x] `fish` -> publicar `fish`
- [x] `gomoku` -> publicar `gomoku`
- [x] `grdc` -> publicar `grdc`
- [x] `hack` -> publicar `hack`
- [x] `hangman` -> publicar `hangman`
- [x] `mille` -> publicar `mille`
- [x] `monop` -> publicar `monop`
- [x] `morse` -> publicar `morse`
- [x] `number` -> publicar `number`
- [x] `phantasia` -> publicar `phantasia`
- [x] `pig` -> publicar `pig`
- [x] `pom` -> publicar `pom`
- [x] `ppt` -> publicar `ppt`
- [x] `primes` -> publicar `primes`
- [x] `quiz` -> publicar `quiz`
- [x] `rain` -> publicar `rain`
- [x] `random` -> publicar `random`
- [x] `robots` -> publicar `robots`
- [x] `sail` -> publicar `sail`
- [x] `snake` -> publicar `snake-bsd`
- [x] `tetris` -> publicar `tetris-bsd`
- [x] `trek` -> publicar `trek`
- [x] `worm` -> publicar `worm`
- [x] `worms` -> publicar `worms`
- [x] `wump` -> publicar `wump`

### Ports com multiplos binarios ou auxiliares

- [x] `backgammon` -> publicar `backgammon`
- [x] `backgammon` -> publicar `teachgammon`
- [x] `boggle` -> publicar `boggle`
- [x] `canfield` -> publicar `canfield`
- [x] `fortune` -> publicar `fortune`
- [x] `wargames` -> publicar `wargames`

### Removido do escopo deste ciclo

- [x] `hunt` -> removido da meta de publicacao por exigir acoplamento com `huntd`, protocolo proprio e fluxo cliente/daemon fora do modelo atual dos ports BSD do VibeOS

### Auxiliares que nao devem virar item de menu, mas precisam existir para o port real funcionar

- [x] `backgammon/teachgammon` -> reutilizar codigo compartilhado de `common_source` sem duplicacao desnecessaria
- [x] `boggle` -> tratar `mkdict` e `mkindex` como etapa de build/dados, nao como app de menu
- [x] `canfield` -> tratar `cfscores` como helper, nao como app de menu
- [x] `fortune` -> tratar `strfile` como ferramenta auxiliar do pipeline de dados
- [x] `wargames` -> decidir se o port final reaproveita `wargames.sh` diretamente ou se vira wrapper nativo sem stub

## Assets e casos especiais que precisam ser resolvidos no port

- [x] `adventure` precisa empacotar o arquivo `glorkz`
- [x] `monop` precisa empacotar `cards.pck`; `mon.dat`, `brd.dat`, `prop.dat`, `monop.def` e `monop.ext` continuam sendo incorporados em tempo de compilacao
- [x] `fortune` precisa empacotar os bancos em `datfiles/`
- [x] `atc` usa parser manual em `applications/ported/bsdgames/atc_parser_manual.c`, eliminando a dependencia de `lex` e `yacc` no build do port
- [x] Jogos baseados em `curses` precisam de politica unica de adaptacao para UI do desktop

## Definicao de pronto

- [x] Todos os itens ainda em escopo de `compat/games` estao cobertos por ports funcionais ou por seus executaveis jogaveis correspondentes.
- [x] Todos os jogos jogaveis resultantes ja publicados aparecem na aba `Games` do desktop.
- [x] Nenhum item publicado depende de stub.
- [x] Colisoes de nome foram resolvidas com sufixo `-bsd` quando necessario.
- [ ] Todos os comandos publicados abrem o jogo correto tanto pela shell quanto pelo menu.
- [x] O documento foi atualizado com `[x]` nos itens concluidos durante a implementacao.
- [ ] validar a execução de todos os jogos ao fim do trabalho, corrigir erros de lançamento e testar um a um as funcionalidades
