# Adaptive RAM Policy

## Objetivo

O objetivo deste documento e assumir uma diretriz clara para o VibeOS:

- app nao deve ser "economico por medo" quando ha RAM sobrando
- app deve usar agressivamente a RAM disponivel para reduzir IO, pop-in e recomputacao
- esse uso precisa ser adaptativo, previsivel e nao bloqueante
- quando a memoria apertar, o sistema deve degradar qualidade e cache antes de travar

Em resumo:

- usar bastante RAM e permitido
- travar por usar RAM de forma burra nao e permitido

## Premissa Atual

Ja existe base para isso.

O bootstrap do kernel ja conhece a memoria utilizavel via:

- `physmem_usable_base()`
- `physmem_usable_end()`

e ja faz reserva explicita de:

- kernel
- framebuffer
- arenas de app

Isso significa que o sistema ja sabe, cedo, qual e a janela real de RAM disponivel.

O que ainda falta e transformar essa informacao em uma politica universal de orcamento dinamico para userland e caches pesados.

## Problema Atual

Hoje a gestao de memoria ainda e muito fixa.

Sintomas:

- stacks e arenas ainda sao escolhidas mais por constante do que por carga real
- apps grandes nao sobem com um orcamento derivado da RAM real da maquina
- jogos, desktop, viewers e runtimes repetem IO e recomputacao mesmo quando sobra memoria
- quando algo aperta, a degradacao nao segue uma politica global

Na pratica, isso produz dois defeitos opostos:

- maquinas com bastante RAM ficam subutilizadas
- maquinas menores podem sofrer regressao por falta de prioridades claras

## Diretriz Global

Todo app deve operar com tres niveis de memoria:

1. memoria obrigatoria
2. memoria de trabalho
3. memoria oportunista

### 1. Memoria obrigatoria

E a memoria sem a qual o app nao pode funcionar.

Exemplos:

- stack
- heap basal
- estruturas principais de estado
- frame atual
- buffers minimos de IPC e IO

Essa parte precisa ser pequena, deterministica e sempre reservavel.

### 2. Memoria de trabalho

E a memoria que sustenta boa responsividade.

Exemplos:

- chunks proximos do player
- surfaces do desktop
- cache de icones quentes
- buffers de decode de imagem/audio
- indices de filesystem e diretorio

Essa parte deve crescer de acordo com a RAM disponivel.

### 3. Memoria oportunista

E tudo que melhora experiencia, mas pode ser descartado sem quebrar a sessao.

Exemplos:

- prefetch agressivo
- caches de textura fora da vista
- thumbnails frios
- paginas de script/bytecode reciclaveis
- scan results historicos

Essa parte deve consumir o que sobrar e ser a primeira a encolher.

## Regra Principal

Apps nao devem mais perguntar:

- "qual e a constante de heap?"

Apps devem perguntar:

- "qual e meu orcamento atual?"
- "qual parte dele e obrigatoria?"
- "qual parte posso usar para cache quente?"
- "qual parte posso usar para prefetch?"

## Politica de Orcamento

Cada app deve receber um orcamento derivado de:

- RAM total utilizavel
- RAM atualmente livre ou reservavel
- classe do app
- visibilidade da sessao
- presenca de outros apps pesados

### Classes sugeridas

- `foreground-interactive`
- `desktop-core`
- `background-helper`
- `streaming-media`
- `viewer-cacheable`
- `game-world-streaming`
- `runtime-compiler`

### Exemplo de comportamento

`desktop-core`

- pode manter caches amplos de wallpaper, icones, fontes e estado visual
- nao deve monopolizar memoria que impeça jogo/app foreground de crescer

`game-world-streaming`

- deve usar RAM agressivamente para chunks, geometria pronta e assets locais
- deve preferir descartar distante/frio antes de tocar no hot set visivel

`background-helper`

- deve ter teto duro e pequeno
- nao deve expandir cache so porque ha RAM livre

## API Proposta

Precisamos de uma interface universal de memoria para userland.

Exemplo conceitual:

- `sys_memory_status(struct mk_memory_status *out)`
- `sys_memory_budget_query(enum mk_task_class, struct mk_memory_budget *out)`
- `sys_memory_budget_hint(struct mk_memory_budget_hint *hint)`

### `mk_memory_status`

Campos desejados:

- `usable_total_bytes`
- `kernel_reserved_bytes`
- `app_reserved_bytes`
- `heap_total_bytes`
- `heap_free_bytes`
- `pressure_level`
- `recommended_cache_bytes`

### `mk_memory_budget`

Campos desejados:

- `required_floor_bytes`
- `working_set_target_bytes`
- `opportunistic_ceiling_bytes`
- `stack_target_bytes`
- `shrink_grace_ticks`

### `mk_memory_budget_hint`

Campos desejados:

- `task_class`
- `latency_sensitive`
- `streaming_workload`
- `visible_foreground`
- `can_drop_cache_fast`

## Pressao de Memoria

Precisamos formalizar niveis de pressao.

### Nivel 0: folga ampla

- permitir prefetch e cache largo
- aumentar buffers de leitura
- manter assets quentes residentes

### Nivel 1: normal

- manter working set cheio
- limitar crescimento oportunista

### Nivel 2: pressao moderada

- parar prefetch
- reduzir caches frios
- comprimir estruturas descartaveis quando fizer sentido

### Nivel 3: pressao alta

- liberar memoria oportunista imediatamente
- reduzir working set ao minimo bom
- adiar tarefas de rebuild pesado

### Nivel 4: critica

- shed agressivo de cache
- negar expansoes nao essenciais
- priorizar foreground

## Politica de Eviction

O sistema inteiro deve seguir uma regra simples:

- descartar longe antes de perto
- descartar frio antes de quente
- descartar recomputavel antes de irreproduzivel
- descartar invisivel antes de visivel
- degradar qualidade antes de bloquear frame

### Ordem sugerida

1. cache oportunista frio
2. prefetch ainda nao usado
3. derivados recomputaveis
4. working set morno
5. working set quente so em ultimo caso

## Aplicacao por Dominio

### Desktop

O desktop deve usar RAM sem vergonha para:

- cache de wallpaper em formatos prontos para blit
- atlas de icones frequentes
- fontes rasterizadas
- thumbnails de janelas recentes
- estado parseado de arquivos runtime

Mas deve:

- derrubar thumbnail fria antes de derrubar interatividade
- nunca competir com app foreground pesado em memoria sem reavaliar budget

### Craft e jogos streaming

Jogos devem usar memoria para:

- chunks visiveis
- chunks adjacentes
- geometria pronta
- buffers de mesh rebuild
- assets quentes

Politica:

- chunk visivel tem prioridade maxima
- chunk adjacente tem prioridade alta
- chunk atras do player vira candidato natural a descarte
- rebuild deve usar buffer temporario amplo quando houver RAM
- sob pressao, reduzir raio frio antes de reduzir hot set

### File manager / image viewer

Devem usar RAM para:

- thumbnails
- decode cache
- previews
- leitura antecipada de diretorios

Mas com invalidacao clara por:

- mudanca de pasta
- pressao de memoria
- entrada em background

### Audio / network helpers

Devem ter memoria previsivel e baixa:

- buffers fixos
- sem cache oportunista amplo
- sem crescimento descontrolado

## Arena Fixa vs Orcamento Dinamico

A arena fixa ainda pode continuar existindo como barreira de seguranca e isolamento.

Mas dentro dela, o uso nao pode continuar cego.

Proposta:

- manter arenas virtuais por classe de app
- permitir que a arena desktop seja grande
- gerir stack, heap basal e cache dentro dela com budget dinamico
- no futuro, considerar arenas elasticas ou submapeamento dinamico por demanda

Ou seja:

- arena fixa e cerca
- budget dinamico e politica

## Heuristica Inicial "Lanca a Braba"

Como diretriz pratica inicial:

- foreground interativo pode consumir uma fatia agressiva da RAM livre
- desktop pode consumir forte cache quando nao houver app foreground pesado
- jogos podem crescer ate o teto do budget enquanto o frame continuar estavel

Heuristica inicial por disponibilidade apos reservas duras:

- ate 256 MiB livres: modo conservador
- 256 MiB a 512 MiB: modo normal
- 512 MiB a 1 GiB: modo agressivo
- acima de 1 GiB: modo sem timidez

### Modo sem timidez

Permite:

- stacks largas para apps grandes
- caches de geometria persistentes
- textura e asset cache quentes
- prefetch maior
- menos IO repetido

Desde que:

- o app mantenha latencia boa
- o reclaim seja rapido
- o frame nao trave por GC improvisado ou liberacao monolitica

## Regras de Implementacao

- nunca fazer free massivo no meio do frame foreground
- preferir reclaim incremental
- preferir listas LRU/LFU simples a estruturas "espertas" demais
- medir bytes por classe de cache
- medir tempo de reclaim
- medir quanto IO foi evitado pelo cache
- tornar pressao de memoria observavel em debug/status

## Telemetria Necessaria

O sistema deve expor pelo menos:

- RAM utilizavel total
- RAM reservada pelo kernel
- RAM reservada para arenas de app
- heap livre
- nivel de pressao atual
- budget recomendado por task class

E cada app pesado deve expor:

- bytes de working set
- bytes de cache oportunista
- bytes temporarios
- tempo medio de reclaim
- eventos de shrink por pressao

## Fases de Implementacao

### Fase 1: Contrato global

- [X] Criar `mk_memory_status`
- [X] Criar `mk_memory_budget`
- [X] Expor syscall de status/orcamento
- [X] Definir `pressure_level`

### Fase 2: Loader e runtime

- [X] Trocar validacoes cegas por `lang_app_arena_size()` por classe
- [X] Separar stack minima, stack alvo e heap basal
- [X] Registrar budget inicial por app no launch

### Fase 3: Desktop

- [X] Aplicar budget dinamico a caches de wallpaper, icones e thumbnails
- [X] Parar de reler/parsing de estado quando cache quente couber
- [X] Implementar reclaim incremental quando app foreground pedir espaco

### Fase 4: Craft e apps streaming

- [X] Budget de chunk hot/warm/cold
- [X] Cache de mesh e buffers temporarios por pressao
- [X] Prefetch agressivo quando houver RAM
- [X] Shrink rapido quando houver pressao

### Fase 5: Helpers e servicos

- [X] Teto duro pequeno para helpers
- [X] Sem crescimento oportunista por default
- [X] Buffer sizing guiado por latencia real

## Criterios de Sucesso

- apps grandes usam mais RAM quando a maquina tem RAM
- apps pequenos nao monopolizam memoria sem motivo
- foreground nao sofre stutter por reclaim grosseiro
- caches reduzem IO e recomputacao perceptivelmente
- sob pressao, sistema degrada elegancia antes de degradar responsividade

## Regra Final

No VibeOS, RAM livre sem funcao e oportunidade desperdicada.

Mas RAM usada sem politica vira travamento.

A politica correta e:

- ocupar agressivamente
- medir continuamente
- encolher cedo
- nunca travar por surpresa
