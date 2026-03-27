# SMP Plan

Objetivo: evoluir o VibeOS de kernel single-core cooperativo para kernel SMP real, sem perder compatibilidade com o boot atual e mantendo fallback seguro para hardware sem APIC/SMP.

## Fase 0 - Fundacao

- [x] Detectar topologia de CPU no boot via `CPUID`
- [x] Detectar tabelas Intel MP em low memory quando disponiveis
- [x] Expor API de topologia (`cpu_count`, `boot_cpu_id`, `smp_capable`)
- [x] Logar no boot quando hardware multiprocessado for detectado
- [x] Criar estado per-CPU do kernel
- [x] Criar spinlocks reutilizaveis no kernel

## Fase 1 - Local APIC

- [x] Adicionar helpers `rdmsr/wrmsr` e acesso seguro ao MSR `IA32_APIC_BASE`
- [x] Implementar descoberta do endereco base do Local APIC
- [x] Implementar leitura/escrita MMIO do LAPIC
- [x] Implementar enable do Local APIC no BSP
- [ ] Implementar helpers de EOI/IPI do LAPIC
- [ ] Validar fallback limpo para maquinas sem APIC

## Fase 2 - AP Startup

- [ ] Reservar area baixa para trampoline dos APs
- [x] Adicionar codigo de trampoline 16/32-bit para AP
- [x] Enviar `INIT` + `SIPI` pelo LAPIC
- [x] Handshake BSP/AP com contador de CPUs iniciadas
- [ ] Inicializar GDT/IDT basicas nos APs
- [ ] Colocar APs em idle seguro ate scheduler SMP entrar

## Fase 3 - Scheduler SMP

- [x] Mudar `scheduler_current()` para estado per-CPU
- [x] Proteger run queue com spinlock
- [ ] Separar contexto atual por CPU
- [ ] Fazer `yield/schedule` funcionar corretamente em SMP
- [ ] Garantir que interrupcoes e syscalls usam o estado da CPU atual
- [ ] Manter fallback single-core como default seguro

## Fase 4 - Robustez

- [ ] Revisar heap para concorrencia
- [ ] Revisar timers/EOI/IRQ para APIC
- [ ] Revisar drivers de entrada/video/storage para reentrancia minima
- [ ] Adicionar testes de boot em hardware/QEMU com 2+ CPUs
- [ ] Documentar riscos e knobs de ativacao

## Principios

- Ativar SMP gradualmente.
- Nao trocar o caminho principal de boot ate o bootstrap dos APs estar estavel.
- Manter o sistema buildavel e bootavel ao fim de cada fase.
- Usar PIC/single-core como fallback padrao ate o scheduler SMP estar correto.
