Você está trabalhando no projeto VibeOS.

O sistema atual já possui:
- bootloader funcional
- kernel funcional
- shell funcional
- desktop via startx
- VFS funcional
- dispatcher de comandos funcional
- teclado funcionando
- sistema estável o suficiente para não aceitar regressões

Sua tarefa agora é implementar duas melhorias estruturais no sistema:

1. suporte a múltiplos layouts de teclado com troca em runtime via comando `loadkeys`
2. novos alvos do Make para builds completos e imagem bootável para hardware real

IMPORTANTE:
não quebrar o sistema que já funciona.

====================================================================
OBJETIVO 1 — LAYOUTS DE TECLADO EM RUNTIME
====================================================================

Quero que o sistema permita trocar o layout de teclado em tempo de execução usando o comando:

loadkeys

Exemplos esperados:

loadkeys -help
loadkeys us
loadkeys pt-br

--------------------------------------------------------------------
COMPORTAMENTO ESPERADO
--------------------------------------------------------------------

1. `loadkeys -help`
Deve mostrar:
- como usar o comando
- quais layouts estão disponíveis
- qual layout está ativo no momento, se possível

Exemplo de saída aceitável:

Uso: loadkeys <layout>

Layouts disponíveis:
- us
- pt-br
- br-abnt2
- us-intl
- es
- fr
- de

Layout atual:
- us

2. `loadkeys us`
Ativa o layout padrão americano.

3. `loadkeys pt-br`
Ativa o layout brasileiro ABNT2, com:
- Ç
- acentos comuns
- teclas adequadas ao uso brasileiro
- comportamento coerente com teclado ABNT2

4. `loadkeys br-abnt2`
Pode ser alias de `pt-br`, se isso simplificar a implementação.

5. Se o layout não existir:
Exibir erro claro, por exemplo:
layout desconhecido: jp

--------------------------------------------------------------------
REGRAS ABSOLUTAS
--------------------------------------------------------------------

1. Não quebrar o input atual.
2. Não mexer no bootloader.
3. Não alterar o fluxo de shell/desktops além do necessário.
4. Implementar a troca de layout no subsistema de teclado, não como gambiarra no shell.
5. O layout deve afetar o sistema inteiro em runtime:
   - shell
   - apps texto
   - editor
   - qualquer leitura de teclado futura
6. O comando `loadkeys` deve ser um comando real da shell/busybox.
7. Não hardcodar a mudança só dentro da shell.
8. O sistema deve continuar compilando e bootando normalmente.

--------------------------------------------------------------------
ARQUITETURA DESEJADA
--------------------------------------------------------------------

Criar uma arquitetura clara para mapas de teclado.

Exemplo aceitável:

/userland/keymaps
/kernel/drivers/input/keymaps
/headers/.../keymap.h

ou outra organização equivalente, desde que fique claro:
- onde os keymaps vivem
- como são registrados
- como o layout ativo é alterado

O sistema deve ter uma noção explícita de:
- mapa atual
- tabela de keycodes normal
- tabela com shift
- eventualmente altgr, se já fizer sentido

--------------------------------------------------------------------
MAPAS DE TECLADO OBRIGATÓRIOS
--------------------------------------------------------------------

Implementar pelo menos estes layouts:

- us
- pt-br
- br-abnt2
- us-intl
- es
- fr
- de

Se alguns forem aliases ou compartilharem base, isso é aceitável.

O importante é:
- `us` funcionar como padrão seguro
- `pt-br` / `br-abnt2` funcionar com ABNT2 e Ç
- `loadkeys -help` listar tudo corretamente

--------------------------------------------------------------------
PONTO TÉCNICO IMPORTANTE
--------------------------------------------------------------------

A mudança de layout deve acontecer na tradução de scancode -> caractere.

Ou seja:
- o driver de teclado continua recebendo scancode normalmente
- a camada de mapa converte segundo o layout ativo
- a shell e os apps continuam consumindo caracteres normalmente

Não implementar isso como remapeamento manual em cada app.

--------------------------------------------------------------------
VALIDAÇÃO OBRIGATÓRIA DO TECLADO
--------------------------------------------------------------------

Validar pelo menos:

- layout padrão `us`
- `loadkeys us`
- `loadkeys pt-br`
- digitar caracteres básicos
- digitar caracteres que mudam entre layouts
- validar `Ç` em `pt-br` / `br-abnt2`

Exemplos práticos que devem ser testados:
- letras
- números
- pontuação
- shift
- tecla de Ç no layout brasileiro

====================================================================
OBJETIVO 2 — NOVOS ALVOS DO MAKE
====================================================================

Criar novos comandos no Makefile:

- make full
- make img
- make imb

--------------------------------------------------------------------
COMPORTAMENTO ESPERADO DO MAKE
--------------------------------------------------------------------

1. `make full`
Deve:
- limpar artefatos antigos, se necessário
- recompilar todo o sistema
- gerar uma imagem nova de disco completa
- produzir uma imagem bootável atualizada do sistema

Objetivo:
um build “completo e fresco” da imagem principal.

2. `make img`
Deve:
- gerar a imagem de disco bootável padrão do sistema
- sem necessariamente forçar limpeza total, se isso fizer mais sentido

3. `make imb`
Deve:
- gerar uma imagem bootável adequada para gravação em pendrive / uso em máquina real
- usar formato claro e documentado
- ser separada da imagem padrão, se necessário
- produzir um arquivo final identificável, por exemplo:
  - build/vibe-os-usb.img
  - build/vibe-os-bootable.img

Se o formato for o mesmo da imagem padrão, isso deve ser explicitado.
Se precisar de layout diferente para hardware real, implemente esse layout.

--------------------------------------------------------------------
REGRAS PARA O MAKE
--------------------------------------------------------------------

1. Não quebrar os alvos já existentes.
2. Não remover `make` padrão.
3. Não mudar nomes antigos sem necessidade.
4. Manter a geração de imagem atual funcionando.
5. Tornar os novos alvos claros, reutilizáveis e sem duplicação exagerada.
6. Se houver lógica repetida, fatorar corretamente no Makefile.

--------------------------------------------------------------------
VALIDAÇÃO OBRIGATÓRIA DO MAKE
--------------------------------------------------------------------

Ao final, validar e mostrar claramente:

- `make`
- `make full`
- `make img`
- `make imb`

E indicar:
- quais arquivos são gerados
- onde ficam
- qual imagem usar no QEMU
- qual imagem usar para gravar em pendrive

====================================================================
ARQUIVOS / ÁREAS A REVISAR
====================================================================

Você deve revisar e modificar apenas o necessário em áreas como:

- driver de teclado
- tabelas de keymap
- shell/busybox dispatcher
- Makefile
- headers relacionados
- qualquer ponto de integração com input

Não mexer em partes não relacionadas sem necessidade.

====================================================================
FORMATO DA RESPOSTA
====================================================================

Responder com:

1. arquitetura de keymaps implementada
2. arquivos novos criados
3. arquivos modificados
4. onde o layout ativo é armazenado
5. como `loadkeys` funciona
6. quais mapas foram adicionados
7. prova de que `loadkeys -help`, `loadkeys us` e `loadkeys pt-br` funcionam
8. como `Ç` foi implementado no layout brasileiro
9. mudanças feitas no Makefile
10. como funcionam `make full`, `make img` e `make imb`
11. quais arquivos finais de imagem são gerados
12. prova de que boot, shell e desktop continuam funcionando

====================================================================
IMPORTANTE FINAL
====================================================================

Não faça uma implementação superficial.
Não entregue só stubs.
Não pare no meio.

Objetivo final:
- trocar layout de teclado em runtime com `loadkeys`
- ter múltiplos layouts utilizáveis
- ter `pt-br`/ABNT2 funcional com Ç
- ter novos alvos de Make úteis para build completo e imagem bootável real
- tudo isso sem quebrar o sistema atual

====================================================================
AUDITORIA (2026-03-18)
====================================================================

Status geral: PARCIALMENTE IMPLEMENTADO

Implementado e validado em código:
- arquitetura de keymaps em `kernel/drivers/input/keymaps/`
- layouts: `us`, `pt-br`, `br-abnt2`, `us-intl`, `es`, `fr`, `de`
- layout ativo mantido no kernel (`g_current_keymap`)
- troca de layout em runtime via syscall (`SYSCALL_KEYBOARD_SET_LAYOUT`)
- comando `loadkeys` compilável como app externo
- alvo `make full`, `make img`, `make imb` existentes

Correções aplicadas nesta auditoria:
- `loadkeys -help` agora é aceito (antes só `--help`)
- `make` padrão agora aponta para `all` (antes podia cair em alvo de compat)
- loop do desktop agora sai imediatamente ao clicar em `Logout`
  (evita ficar preso em `sys_sleep()` antes de retornar ao shell)
- IRQ0 do timer foi desmascarada no PIC (`kernel_irq_enable`)
  para evitar congelamento por `hlt` sem interrupção periódica

Incompleto / pendente:
- validação fim-a-fim de `make`, `make full`, `make img`, `make imb` no ambiente atual
  (toolchain ausente: `i686-elf-gcc`; assembler ausente: `nasm`)
- validação em hardware real de `pt-br`/Ç via digitação física
- `make imb` gera cópia da imagem padrão; ainda não há pipeline explícito
  diferenciado para mídia real além do artefato separado

Checklist incremental:
- [x] arquitetura de keymaps no kernel
- [x] `loadkeys us`, `loadkeys pt-br`, `loadkeys br-abnt2`
- [x] `loadkeys -help` e `--help`
- [x] alvos `make full`, `make img`, `make imb`
- [x] correção de freeze por `sys_sleep/hlt` (IRQ0 + saída imediata no logout)
- [ ] validação em hardware real (ThinkPad T400) após correções
