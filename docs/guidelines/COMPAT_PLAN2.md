Você está trabalhando no projeto VibeOS.

O diretório:

~/Documentos/vibe-os/compat

contém código-fonte importado do OpenBSD que deve ser PORTADO para rodar no VibeOS.

Esse diretório NÃO é funcionalidade pronta.
Ele contém apenas código de referência.

Seu trabalho é:

PORTAR UTILITÁRIOS REAIS desse diretório e integrá-los ao sistema.

====================================================================
OBJETIVO PRINCIPAL
====================================================================

Transformar o conteúdo de `compat/` em utilitários funcionais do sistema VibeOS.

Cada utilitário portado deve:

1. vir realmente de `compat/`
2. compilar
3. ser integrado ao build
4. ser instalado no sistema (VFS/appfs)
5. ser invocável pela shell
6. substituir implementações stub existentes quando aplicável

====================================================================
AUTORIZAÇÃO PARA SUBAGENTS
====================================================================

Você tem permissão para criar subagents especializados para analisar e portar diretórios específicos.

Você DEVE usar subagents para paralelizar análise de diretórios grandes.

Exemplos válidos:

spawn_agent("compat-bin-porter")
spawn_agent("compat-usrbin-porter")
spawn_agent("compat-libc-adapter")

Cada subagent deve receber:

- o caminho do diretório
- objetivo claro
- regras de port

O agente principal coordena e integra resultados.

====================================================================
ESTRUTURA DO DIRETÓRIO COMPAT
====================================================================

compat/

bin
etc
games
gnu
include
lib
libc
libexec
metadata
posix
regress
sbin
share
src
sys
unistd
usr.bin
usr.sbin

====================================================================
REGRAS ABSOLUTAS
====================================================================

1. NÃO quebrar boot.
2. NÃO quebrar kernel.
3. NÃO quebrar shell.
4. NÃO quebrar startx.
5. NÃO modificar bootloader.
6. NÃO mover código de compat/sys para o kernel.
7. NÃO declarar algo implementado sem código real.
8. NÃO marcar como concluído no plano sem integração real.
9. NÃO tentar portar todo diretório de uma vez.

====================================================================
REGRA SOBRE STUBS EXISTENTES
====================================================================

No VibeOS existem comandos simplificados (stubs).

Exemplos comuns:

echo
cat
uname
pwd
ls

Esses NÃO contam como port de compat.

Quando um port real ficar pronto:

1. integrar o utilitário real ao build
2. alterar shell/busybox para usar o port real
3. remover ou desativar stub antigo

====================================================================
ORDEM DE PORT
====================================================================

Você DEVE trabalhar nesta ordem.

FASE 1 (essencial)

compat/bin

FASE 2

compat/usr.bin

FASE 3

compat/sbin
compat/usr.sbin

FASE 4

compat/lib
compat/libc
compat/posix
compat/unistd
compat/include

FASE 5

compat/share
compat/etc
compat/games
compat/gnu
compat/libexec

FASE 6

compat/src
compat/regress

====================================================================
NÃO PORTAR
====================================================================

compat/sys

Esse diretório é apenas referência.

NÃO integrar ao kernel.

====================================================================
COMANDOS PRIORITÁRIOS
====================================================================

Portar primeiro os utilitários pequenos.

PRIORIDADE ALTA

echo
cat
true
false
printf
uname
pwd

PRIORIDADE MÉDIA

wc
head
tail
grep
cut
tr

PRIORIDADE AVANÇADA

sort
uniq
sed
find
less

====================================================================
CAMADA DE COMPATIBILIDADE
====================================================================

Você pode adaptar código de:

compat/lib
compat/libc
compat/posix
compat/unistd

MAS apenas conforme necessário.

Não implementar POSIX completo.

Somente o mínimo necessário para destravar utilitários.

Possíveis áreas:

string
memory
stdio
file I/O
errno
isatty
stat
fstat

====================================================================
INTEGRAÇÃO COM SHELL
====================================================================

Todo comando portado deve poder ser invocado pela shell.

Isso pode acontecer de duas formas:

1. registro explícito no dispatcher da shell
2. resolução automática de executáveis no VFS/appfs

Resultado final esperado:

usuario digita:

echo hello
cat arquivo.txt
grep foo arquivo.txt

e o comando portado é executado.

====================================================================
INTEGRAÇÃO COM BUILD
====================================================================

Cada utilitário portado deve:

1. ser compilado
2. gerar artefato executável
3. ser instalado no sistema
4. não inflar kernel indevidamente

Use o sistema atual de apps externos do VibeOS.

====================================================================
PROCESSO DE EXECUÇÃO
====================================================================

Trabalhe em ciclos pequenos.

Para cada ciclo:

1. escolher diretório alvo
2. listar subpastas e arquivos
3. identificar utilitários portáveis
4. escolher 1-3 alvos
5. portar código
6. adaptar dependências
7. integrar build
8. integrar shell
9. substituir stub se aplicável
10. atualizar plano

====================================================================
ATUALIZAÇÃO DOS PLANOS
====================================================================

Arquivos de controle:

COMPAT_PLAN.md
compat/metadata/*

Atualizar apenas quando implementação real existir.

Auditoria não conta como implementação.

====================================================================
FORMATO DE RELATÓRIO DE CADA CICLO
====================================================================

Sempre responder com:

Diretório analisado
Subpastas relevantes
Utilitários encontrados
Alvos escolhidos
Arquivos modificados
Código portado
Integração no build
Integração na shell
Stubs substituídos
Atualização do plano
Próximos alvos

====================================================================
PROVA DE IMPLEMENTAÇÃO
====================================================================

Um utilitário só conta como portado se:

1. código veio de compat/
2. compila
3. build integrado
4. shell consegue executar
5. stub antigo não é mais caminho principal
6. sistema continua bootando

====================================================================
TAREFA INICIAL
====================================================================

Agora execute o primeiro ciclo:

1. spawn_agent para analisar compat/bin
2. identificar utilitários pequenos
3. escolher até 3 alvos
4. portar de verdade
5. integrar ao sistema
6. substituir stubs se necessário
7. atualizar COMPAT_PLAN.md
8. continuar para o próximo ciclo automaticamente

====================================================================
STATUS DE IMPLEMENTAÇÃO (ATUALIZADO EM 2026-03-23)
====================================================================

FASE 1 — compat/bin (ciclo inicial)

- [x] Análise de `compat/bin` com subagente
- [x] Port de `echo` baseado em `compat/bin/echo/echo.c`
- [x] Port de `cat` baseado em `compat/bin/cat/cat.c`
- [x] Port de `pwd` baseado em `compat/bin/pwd/pwd.c`
- [x] Port de `sleep` baseado em `compat/bin/sleep/sleep.c`
- [x] Port de `rmdir` baseado em `compat/bin/rmdir/rmdir.c`
- [x] Integração no build (`Build.ported.mk` e `Makefile`)
- [x] Instalação no VFS (`/bin`, `/usr/bin`, `/compat/bin`) com stubs de descoberta
- [x] Shell ajustada para preferir apps externos para `echo`, `cat`, `pwd` (stub interno como fallback)
- [x] Build validado com `make clean && make run-debug`
- [x] API runtime expandida para remoção de diretório (`rmdir`)

Pendências imediatas da fase 1

- [x] Port de `mkdir` baseado em `compat/bin/mkdir/mkdir.c`
- [x] Port de `true` e `false` integrado como apps externas
- [x] Port de `printf` integrado como app externa
- [ ] Expandir próximos alvos de `compat/bin` (`sync`) conforme APIs necessárias

====================================================================
HANDOFF TÉCNICO (PARA PRÓXIMO AGENTE)
====================================================================

Resumo objetivo do estado atual

- Build completo segue passando com `make clean && make run-debug`.
- Apps externos são gerados e empacotados no appfs (confirmado: `echo`, `cat`, `wc`, `pwd`, `head`, `sleep`, `rmdir`, `mkdir`, `tail`, `grep`, `loadkeys`, `true`, `false`, `printf`).
- Shell encontra comandos externos (não é falha de build/empacotamento).
- Em runtime, usuário reporta travamento/loop com cursor piscando para comandos portados.

Implementações concluídas nesta rodada

- [x] Toolchain/build cross-platform (macOS/Linux) com fallback de compilador.
- [x] Integração de ports no build (`Build.ported.mk`, `Makefile`) e appfs (`tools/build_appfs.py` via target padrão).
- [x] Ports adicionados/ajustados: `pwd`, `sleep`, `rmdir`.
- [x] `loadkeys` migrado para API host (sem syscall variádica direta no app).
- [x] `busybox` com validação de existência no VFS antes de chamar loader externo para comandos desconhecidos.
- [x] `lang_loader` com debug VGA silenciado (remoção de ruído visual).
- [x] `compat` POSIX mínimo expandido (`getcwd`, `rmdir`, `ENOTEMPTY`).
- [x] API host/runtime expandida para:
  - `getcwd`
  - `remove_dir`
  - `keyboard_set_layout`
  - `keyboard_get_layout`
  - `keyboard_get_available_layouts`

Bugs tratados

- [x] `loadkeys` chamando syscall variádica com assinatura insegura.
- [x] Shell tentando loader para comando inexistente (reduzido).
- [x] Conflito de headers `video_mode` (guardas de definição aplicadas).
- [x] Possível conflito de `stdio`/`stdlib` duplicado da camada compat:
  - `compat/src/libc/stdio.c` esvaziado para não redefinir `stdin/stdout/stderr`.
  - `compat/src/libc/stdlib.c` passou a delegar `malloc/free/realloc` para `vibe_app_runtime`.
- [x] `lang_loader` alterado para chamada C direta de entrypoint (remoção da ponte asm manual).
- [x] VFS robustecido contra árvore persistida corrompida/loop:
  - validação da árvore carregada.
  - guardas anti-loop em travessias de filhos.
  - fallback de `fs_build_path` para evitar loop.

Empecilhos ainda abertos (prioridade alta)

- [X] Sintoma persistente no runtime: comandos portados “reconhecidos” porém sem retorno ao prompt (cursor piscando).
- [X] Falta prova conclusiva de onde o fluxo fica preso entre:
  - `busybox_main`
  - `try_run_external`
  - `lang_try_run`
  - retorno de `vibe_app_entry`
- [X] Necessário instrumentar trilha de execução com logs curtos e controlados (serial/debug), sem spam em tela.

Hipóteses técnicas pendentes para investigação imediata

- [X] Estado persistido antigo da FS ainda gerando cadeia inválida em alguns ambientes.
- [X] Algum app externo entrando em loop de `read(STDIN)`/`poll_key` por caminho inesperado.
- [X] Retorno do app não propagado corretamente para shell em cenário específico.

Plano recomendado para próximo agente (ordem)

1. Reproduzir com logs mínimos no caminho:
   - `busybox_main` entrada/saída
   - `try_run_external` decisão
   - `lang_try_run` (encontrou entry, chamou app, retornou)
2. Testar sequência curta:
   - comando inexistente (`babuska`)
   - `pwd`
   - `echo teste`
   - `loadkeys us`
3. Se travar, coletar último ponto logado e corrigir no ponto exato (evitar mudanças amplas).
4. Só após estabilizar execução dos ports, continuar novos ports da fase 2.

Arquivos-chave alterados nesta fase (referência rápida)

- Build:
  - `Makefile`
  - `Build.compat.mk`
  - `Build.ported.mk`
- Shell/VFS/Loader:
  - `userland/modules/busybox.c`
  - `userland/modules/fs.c`
  - `userland/modules/lang_loader.c`
  - `headers/userland/modules/include/syscalls.h`
  - `userland/modules/syscalls.c`
- Runtime SDK:
  - `lang/include/vibe_app.h`
  - `lang/include/vibe_app_runtime.h`
  - `lang/sdk/app_runtime.c`
  - `lang/sdk/app_entry.c`
- Compat POSIX/libc:
  - `compat/include/compat/posix/unistd.h`
  - `compat/include/compat/posix/errno.h`
  - `compat/src/posix/unistd.c`
  - `compat/src/libc/stdio.c`
  - `compat/src/libc/stdlib.c`
- Ports:
  - `applications/ported/loadkeys/loadkeys.c`
  - `applications/ported/pwd/pwd.c`
  - `applications/ported/sleep/sleep.c`
  - `applications/ported/rmdir/rmdir.c`
