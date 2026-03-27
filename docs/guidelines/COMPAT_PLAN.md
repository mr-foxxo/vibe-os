Você está trabalhando no sistema operacional VibeOS.

O repositório já contém uma árvore de compatibilidade baseada no código-fonte do OpenBSD dentro do diretório:

compat/

Essa árvore contém diretórios importados de:

OpenBSD src

exemplo:

compat/
  bin/
  sbin/
  usr.bin/
  usr.sbin/
  lib/
  libexec/
  gnu/
  share/
  sys/
  include/
  etc/

IMPORTANTE:

Esse código já foi importado.
Sua tarefa NÃO é clonar novamente o repositório.

Sua tarefa é IMPLEMENTAR a integração dessa árvore com o sistema atual.

Mas isso deve ser feito de forma incremental e segura.

--------------------------------------------------------------------

OBJETIVO PRINCIPAL

Transformar a árvore compat/ em uma base funcional para portar utilitários BSD reais.

Esses utilitários devem:

• compilar usando a toolchain atual
• usar a camada compat do sistema
• ser instalados no VFS durante o boot
• ser executáveis diretamente pela shell

Mas sem quebrar:

• boot
• kernel
• shell
• desktop
• sistema de build

--------------------------------------------------------------------

REGRA CRÍTICA

É PROIBIDO tentar compilar toda a árvore compat de uma vez.

Isso quebraria o sistema.

Você deve implementar suporte incremental.

--------------------------------------------------------------------

FASE 1 — INDEXAR A ÁRVORE compat

Primeiro:

1. percorrer todo o diretório compat/
2. identificar cada subdiretório
3. classificar:

Tipo:
- utilitário userland
- biblioteca
- kernel code
- documentação
- share
- scripts

Criar um inventário em:

compat/metadata/compat_inventory.txt

Cada entrada deve conter:

diretório
tipo
dependências prováveis
viabilidade de port

--------------------------------------------------------------------

FASE 2 — CLASSIFICAR POR NÍVEL DE DIFICULDADE

Classificar utilitários em níveis:

Tier 1 (muito fáceis)
echo
cat
wc
true
printf

Tier 2 (fáceis)
head
tail
grep
cut
tr

Tier 3 (médios)
sed
sort
find
uniq

Tier 4 (complexos)
less
vi
nano-like
awk

Tier 5 (não suportados ainda)
coisas que dependem de:
fork
exec
signals
tty complexo
mmap

--------------------------------------------------------------------

FASE 3 — CRIAR CAMADA COMPAT BSD

Implementar uma camada de compatibilidade que permita compilar utilitários BSD.

Criar diretórios se necessário:

compat_runtime/
compat/libc/
compat/bsd/
compat/posix/
compat/term/

Implementar funções mínimas necessárias:

MEMORY
malloc
free
realloc
calloc

STRING
strlen
strcmp
strncmp
strchr
strcpy
memcpy
memset
memmove
memcmp

STDIO
printf
snprintf
vsnprintf
puts
putchar
getchar

POSIX I/O
open
read
write
close
lseek
stat
fstat
isatty

PROCESS
exit
abort

UTIL
atoi
strtol
qsort

errno

TERMINAL
funções básicas de terminal texto

Essa camada deve usar:

• VFS do sistema
• console atual
• driver de teclado atual

--------------------------------------------------------------------

FASE 4 — BUILD DE UTILITÁRIOS

Criar um sistema de build para utilitários compat.

Exemplo:

build/compat/bin/echo.app
build/compat/bin/cat.app
build/compat/bin/wc.app

Esses apps NÃO podem ser linkados dentro do kernel.

Eles devem ser executáveis separados.

--------------------------------------------------------------------

FASE 5 — INSTALAÇÃO NO VFS

Durante o boot do sistema:

utilitários compat que foram compilados com sucesso devem ser instalados no VFS.

Exemplo de caminhos:

/bin
/usr/bin
/compat/bin

Escolher um layout consistente.

A shell deve conseguir encontrar esses executáveis automaticamente.

--------------------------------------------------------------------

FASE 6 — INTEGRAÇÃO COM SHELL

A shell deve:

1. procurar comandos internos
2. procurar executáveis no VFS
3. executar utilitários compat automaticamente

Exemplo esperado:

echo hello
cat arquivo.txt
wc arquivo.txt
grep foo arquivo.txt

Sem hardcode para cada utilitário.

--------------------------------------------------------------------
AUDITORIA (2026-03-18)
--------------------------------------------------------------------

Status geral: PARCIALMENTE IMPLEMENTADO

Implementado:
- árvore `compat/` importada e presente
- camada de compat mínima compilável (`Build.compat.mk` -> `build/libcompat.a`)
- build incremental de apps portados (`Build.ported.mk`)
- apps portados presentes: `echo`, `cat`, `wc`, `head`, `tail`, `grep`, `sed`, `loadkeys`
- shell tenta executar apps externos de forma genérica via loader de appfs (`lang_try_run`)
- inventário exigido criado em `compat/metadata/compat_inventory.txt`
- API POSIX mínima ampliada em `compat/src/posix/unistd.c`:
  `open/read/write/close/lseek/stat/fstat/lstat/dup/dup2/fcntl/isatty`
- integração VFS de executáveis compat no boot (`/compat/bin`, `/usr/bin` e stubs em `/bin`)
- shell agora consulta VFS (`/bin`, `/usr/bin`, `/compat/bin`) antes do fallback externo

Inconsistências plano vs implementação:
- execução final ainda é carregada via appfs (não há formato ELF/VFS nativo completo);
  os nós no VFS hoje funcionam como integração de descoberta/roteamento
- fase de integração shell/VFS ainda simplificada (sem PATH configurável/permissões)
- parte da API POSIX ainda parcial para casos avançados (escrita em arquivo, truncamento, append, etc.)
- existem fontes duplicados com sufixo `" 2.c"` em `compat/src/libc/` fora do build principal

Classificação atual dos itens que estavam “planejados”:
- Fase 1: PARCIAL (há inventário, mas formato/caminho divergem)
- Fase 2: PARCIAL (tiers documentados no inventário, não no formato do plano)
- Fase 3: PARCIAL (POSIX mínimo ampliado; ainda sem cobertura total de semântica)
- Fase 4: IMPLEMENTADO PARCIAL (build incremental existe e funciona por alvo)
- Fase 5: PARCIAL (estrutura e stubs no VFS implementados; execução real continua via appfs)
- Fase 6: PARCIAL (shell agora busca no VFS e executa externo; PATH/exec ainda simplificados)

Checklist incremental:
- [x] inventário em `compat/metadata/compat_inventory.txt`
- [x] camada POSIX mínima com `open/read/write/close/lseek/stat/fstat`
- [x] build incremental de utilitários compat
- [x] estrutura `/compat/bin` criada no VFS no boot
- [x] shell consulta `/bin`, `/usr/bin` e `/compat/bin`
- [ ] semântica POSIX completa (append/trunc/write em arquivo real)
- [ ] execução nativa de binários a partir do VFS (atualmente roteada via appfs)

--------------------------------------------------------------------

FASE 7 — PORT INCREMENTAL

Implementar utilitários nessa ordem:

1
echo
cat
wc
printf

2
head
tail
grep
cut
tr

3
sed
sort
uniq
find

4
less

Não tentar portar editores complexos antes de estabilizar o terminal.

--------------------------------------------------------------------

DIRETÓRIOS QUE NÃO DEVEM SER INTEGRADOS AO SISTEMA

compat/sys/

O código dentro de compat/sys deve permanecer apenas como referência.

Ele NÃO deve ser integrado ao kernel do VibeOS.

--------------------------------------------------------------------

VALIDAÇÃO OBRIGATÓRIA

Após cada fase verificar:

BOOT
sistema ainda inicia

SHELL
shell continua funcional

DESKTOP
startx continua funcionando

TAMANHO
kernel.bin não aumentou por causa de compat

EXECUÇÃO

testar:

echo hello
cat arquivo.txt
wc arquivo.txt
head arquivo.txt
tail arquivo.txt
grep palavra arquivo.txt

--------------------------------------------------------------------

PROVA DE FUNCIONAMENTO

A resposta final deve mostrar:

estrutura compat final
camada compat implementada
utilitários compilados
onde são instalados no VFS
prova de execução na shell
prova de que boot e interface continuam intactos

--------------------------------------------------------------------

IMPORTANTE

Não tentar portar tudo.

Port incremental e seguro.

Objetivo:

transformar compat/ em base real para utilitários BSD no VibeOS.

--------------------------------------------------------------------

ATUALIZAÇÃO DE IMPLEMENTAÇÃO (2026-03-18)

Compat/bin ciclo incremental concluído:
- [x] `echo` portado a partir de `compat/bin/echo/echo.c`
- [x] `cat` portado a partir de `compat/bin/cat/cat.c`
- [x] `pwd` portado a partir de `compat/bin/pwd/pwd.c`
- [x] `sleep` portado a partir de `compat/bin/sleep/sleep.c`
- [x] `rmdir` portado a partir de `compat/bin/rmdir/rmdir.c`
- [x] integração no build (`Build.ported.mk` + `Makefile`)
- [x] integração no VFS (`/bin/pwd`, `/usr/bin/pwd`, `/compat/bin/pwd`)
- [x] shell prioriza execução externa para `echo`, `cat`, `pwd` com fallback interno
- [x] runtime externo ganhou API mínima de `getcwd` para suportar `pwd`
- [x] runtime externo ganhou API mínima para remoção de diretório (`rmdir`)

Próximos alvos recomendados:
- [x] `mkdir` (integrado como app externa)
- [ ] `sync` de `compat/bin`
- [ ] utilitários restantes de `compat/usr.bin` (`uname`)
