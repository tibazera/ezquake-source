# Continuidade FSR2 / DLSS

Leia `UPSCALING_PLAN.md` e o início de `CONTINUE.md` antes de modificar o upscaling.
Esses arquivos são a memória compartilhada entre Codex, Claude e o usuário.

## Regras obrigatórias

- Trabalhar neste worktree, branch `feature/vulkan-upscaling`; conferir caminho, branch e status antes de editar. Não confundir com a pasta principal do port SDL3.
- Usar FSR2 oficial AMD, com backend Vulkan oficial e revisão fixada. Não apresentar shaders próprios, EASU, TAA ou fallback como FSR2 oficial.
- Usar interfaces oficiais NVIDIA. Distinguir DLSS Super Resolution, Frame Generation, Ray Reconstruction e qualquer outro produto: integrar um não comprova os demais. Não prometer DLSS5 sem identificar API, SDK, disponibilidade e requisitos oficiais.
- FSR2 e DLSS são alternativas mutuamente exclusivas por frame. Expor backend solicitado, backend efetivamente utilizado e motivo de fallback. Falha não pode se tornar substituição silenciosa.
- Corrigir causas comprovadas. Antes de editar, rastrear produtores, consumidores, chamadas, layouts, recursos e lifecycle. Não corrigir tremulação com constantes arbitrárias.
- Preservar arquivos e alterações do usuário. Não sobrescrever `racat.cfg`, autoexec, demos, screenshots, saves ou cliente de produção.
- Testar offline em diretório/configuração isolados. Não conectar servidores, enviar estatísticas/chat nem carregar autoexec pessoal. Não encerrar processos do usuário: acompanhar somente o PID iniciado pelo teste.
- Deploy autorizado apenas do executável de teste `C:\ezquake\ezquakefsrtest.exe`, depois de verificar que não está em uso e registrar hash/build. Não abrir o jogo automaticamente sem conferir as instruções mais recentes do usuário.
- Não chamar compilação de validação visual. Não declarar DLSS validado em RTX quando só houve fallback AMD. Registrar comando, exit code, GPU, configuração, log novo e resultado real.
- Cada mudança lógica precisa de uma verificação executável adequada. Repetir revisão após correções e conferir novamente o contrato upstream antes de concluir. Sem repetir testes sem mudança ou dúvida concreta.
- Compilar conservando o exit code real; não usar o sucesso de `tail`/filtro como sucesso de build. Warnings novos de ABI, protótipos, formatos e ponteiros bloqueiam entrega.
- Commits pequenos e frequentes no branch do projeto, com arquivos explicitamente selecionados. Não adicionar atribuição de assistente aos commits. Push conforme autorização já existente; informar falha sem afirmar sincronização.
- Atualizar checklist e checkpoint em cada etapa: feito, evidência, pendência, próximo comando. Não marcar tarefa completa por existir código ou compilar.
- Continuar trabalho autorizado sem pedir repetidamente permissão. Respeitar pedidos posteriores de pausa. Não criar agendamentos ou intervalos artificiais de 30 minutos.
- `/graphify`: ler a skill instalada antes de agir, conforme instrução do usuário.

## Critério de entrega

Somente chamar a integração de pronta após cumprir os critérios de `UPSCALING_PLAN.md`.
Ausência de RTX é limitação de validação: registrar explicitamente, mantendo o item RTX pendente.
