# Plano de recuperação e integração oficial FSR2 / DLSS

Atualizado: 2026-09-30. Documento compartilhado para Codex e Claude.

## Objetivo e estado verdadeiro

Integrar FSR2 oficial AMD e DLSS Super Resolution oficial NVIDIA ao renderer Vulkan,
com seleção em Options/Video, HUD nativo, histórico estável e fallback explícito.
O pedido DLSS5 exige investigação de produto/API separada; não equivale automaticamente a SR.

Worktree: `E:\tmp\eqvk-upscaling`; branch: `feature/vulkan-upscaling`.
Baseline auditada: `89f8648be4d892cf80b30e850f2113286d668394`, igual ao remoto em 2026-09-30.
A pasta `E:\Projetos Linux\ezquake-sdl3-vulkan-pr` é outro checkout, HEAD `91859a99`.
GPU disponível segundo histórico: RX 6800 XT; confirmar por diagnóstico antes de testar.
O código atual tem uma reimplementação incompleta de FSR2 e integração DLSS sem teste RTX.
Tremulação e artefatos relatados continuam abertos; nenhuma causa única foi comprovada.
As afirmações históricas de “FSR2 real completo” e “DLSS5 pronto” não são critérios de aceitação.

## Fontes e decisão de implementação

- AMD: https://github.com/GPUOpen-Effects/FidelityFX-FSR2
- Baseline candidata FSR2 2.2.1: tag `v2.2.1`, commit `1680d1edd5c034f88ebbbb793d8b88f8842cf804` (consultado via git ls-remote).
- Manual AMD: https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-temporal/
- NVIDIA: https://github.com/NVIDIA-RTX/Streamline
- Guia geral: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md
- Guia SR: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md
- SDK DLSS: https://github.com/NVIDIA/DLSS

Usar core e backend Vulkan oficiais AMD via shim C++ pequeno com ABI C para o motor C.
Não reescrever o algoritmo. Confirmar viabilidade do build oficial (shader compiler,
SPIR-V/permutations, bibliotecas, licença, runtime MSVC) antes de fixar a versão final.
Preservar avisos legais e registrar origem/hash de fontes e binários.
Usar headers C++ oficiais Streamline via shim, substituindo mirrors C manuais onde necessário.
Não atualizar SDKs automaticamente para master: documentar revisão e justificativa.

## Fase 0 — checkpoint e reprodução

- [x] Identificar worktree correto e comparar hash remoto da baseline.
- [x] Registrar regras persistentes em AGENTS.md e este plano.
- [x] Registrar build baseline com exit code, warnings, hash do exe e configuração. Ver `CONTINUE.md`, sessão 2026-10-01.
- [ ] Registrar cenário offline reproduzível: dm3, câmera parada, giro, centro/paredes, partículas.
- [ ] Confirmar backend realmente executado; hoje modo FSR2 pode cair no código antigo.
- [ ] Registrar evidência visual/log fresca dos artefatos sem atribuir causa por suposição.

## Fase 1 — problemas confirmados no código atual

Tratar o port próprio como transição. Corrigir infraestrutura reaproveitável; problemas
dos shaders próprios podem ser eliminados pela substituição oficial, sem retrabalhá-los.

- [x] CONFIRMADO E CORRIGIDO -- bug real e grave, não suspeita. `VK_MotionVectorsComposite`
  (`vk_upscale.c`) tinha `if (!VK_DLSS_Active()) return false;` como primeira linha.
  Os 3 call sites em `vk_main.c` (hand-port `==1`, SDK oficial `==3`, DLSS `==2`)
  fazem `vid_vulkan_upscaler.integer == N && VK_MotionVectorsComposite(...)` --
  ou seja, FORA do modo DLSS, a função sempre retornava false e **nem o hand-port
  nem o caminho da SDK oficial jamais disparavam o dispatch real**, mesmo com
  `vid_vulkan_upscaler 1` ou `3` selecionado -- caíam direto no fallback antigo
  de render-pass sem nenhum erro visível (silêncio, não crash). Corrigido:
  gate trocado para `VK_UpscaleActive()` (mesmo gate que todo outro ponto de
  entrada de upscaler já usa), estritamente mais permissivo que o antigo --
  `VK_DLSS_Active()` já exige `VK_UpscaleActive()` como primeira condição, então
  DLSS continua funcionando, hand-port e SDK oficial passam a disparar de verdade.
  **Isso invalida a conclusão anterior desta sessão de que o smoke test da SDK
  oficial "sobreviveu sem erro"** -- o dispatch real nunca rodou naquele teste;
  ver CONTINUE.md para a nota de correção.
- [x] `vk_fsr2.c:726`: recontado -- reconstruct(3)+depthclip(8)+lock(1)+accumulate(6)+rcas(1)=19,
  bate exatamente com `poolSizes[0].descriptorCount = framesInFlight * 19`. Claim do
  plano (20 usados) não reproduz no código atual; corrigido em sessão anterior ou
  nunca foi real. Sem ação necessária.
- [x] `vk_fsr2.c:483–523`: confirmado real -- 10 de 13 imagens em `allImages[]`
  (limpas via `vkCmdClearColorImage` logo na criação) não tinham
  `VK_IMAGE_USAGE_TRANSFER_DST_BIT`; `fsr2DilatedMotion` é source de
  `vkCmdCopyImage` no ping-pong de fim de frame e não tinha `TRANSFER_SRC_BIT`.
  Corrigido: `TRANSFER_DST_BIT` adicionado a todas (`fsr2DilatedDepth`,
  `fsr2LockInputLuma`, `fsr2PreparedInputColor`, `fsr2DilatedReactiveMasks`,
  `fsr2History[0/1]`, `fsr2LockStatus[0/1]`, `fsr2FinalOutput`), `TRANSFER_SRC_BIT`
  adicionado a `fsr2DilatedMotion`. Compila limpo (ver CONTINUE.md).
- [x] `vk_fsr2_depthclip.comp` (hand-port): `reconstructedPrevDepth` lia R32_UINT
  via `sampler2D`/`texelFetch(...).r` tratado direto como float -- format/sampler
  mismatch real (reconstruct/lock escrevem via `uimage2D r32ui` com bits crus,
  depthclip lia como se já fosse float decodificado). Corrigido: binding 4 agora
  `usampler2D`, 4 sites de leitura (`ComputeDepthClip`, `EvaluateSurface`)
  envolvidos em `uintBitsToFloat()`. Compila limpo (ver CONTINUE.md 2026-10-01).
  Só afeta o hand-port (`==1`); caminho da SDK oficial (`==3`) não usa este shader.
- [ ] `VK_Fsr2SamplerInfo` usa GENERAL para todas as imagens; confrontar layouts reais
  de scene color, depth, motion e imagem preta (esta é SHADER_READ_ONLY).
- [ ] `vk_fsr2.c:1151`: copy RGBA16F para swapchain BGRA8 é incompatível;
  compor por shader/conversão validada, preservando gamma/contrast/FXAA e HUD.
- [ ] `VK_Fsr2DestroyResources` não tem caller. Ligar ao teardown do swapchain
  com GPU sincronizada e conferir init parcial/recriação/device antigo.
- [ ] Verificar falha de immediate commands, recursos parcialmente criados, retorno
  ignorado, tamanho zero, resize e descriptor sets referenciando recursos substituídos.
- [ ] Auditar barreiras entre frames e acessos a intermediários compartilhados.
  Ping-pong de 2 imagens não é prova suficiente de segurança com 3 frames em voo.

## Fase 2 — SDK oficial AMD

- [x] Incorporar dependência fixada com licença, instrução de build e hashes.
  Ver `CONTINUE.md` sessão 2026-10-01: submodule `external/fsr2` em v2.2.1.
- [x] Compilar core/backend Vulkan e shaders oficiais; habilitar C++ só onde necessário.
- [x] Criar shim C mínimo: create, dispatch, destroy e acesso ao jitter oficial.
  `src/vk_fsr2_sdk.cpp` (C++ puro, sem headers do motor -- ver comentário do
  próprio arquivo) + `src/vk_fsr2_sdk_bridge.c` (ABI C, reúne estado do motor).
  Caminho novo e PARALELO ao hand-port (`vid_vulkan_upscaler==3`), hand-port
  original (`==1`) intacto -- nenhuma substituição silenciosa.
- [x] Criar contexto com flags de depth invertido/infinito, HDR e motion vectors
  coerentes com os recursos reais. `FFX_FSR2_ENABLE_DEPTH_INVERTED` condicional a
  `glConfig.reversed_depth`; `FFX_FSR2_ENABLE_AUTO_EXPOSURE` sempre ligado (motor
  não fornece exposure externo); sem `DEPTH_INFINITE` (far plane sempre finito,
  `R_FarPlaneZ`). Features/extensões de device NÃO auditadas ainda contra
  `ffxFsr2GetDeviceCapabilitiesVK` -- pendência real.
- [x] Chamar ffxFsr2GetInterfaceVK e gerir scratch/context conforme headers da revisão.
- [x] Dispatch via ffxFsr2ContextDispatch com color, depth, motion, output,
  renderSize, jitter, motionVectorScale, cameraNear/Far/FOV, frameTimeDelta em ms,
  preExposure/exposure, reset e sharpness corretos. Compilação confirmada
  (ver checkpoint); valores corretos (sinais, convenções) NÃO confirmados
  visualmente -- só verificados contra comentários/fórmulas já usadas pelo
  hand-port (vk_fsr2.c) e vk_dlss.c, não testados ao vivo.
- [ ] Usar ffxFsr2GetJitterPhaseCount/GetJitterOffset; remover tabela fixa de 8 fases
  e garantir mesma amostra/signo no desenho e no dispatch. NÃO feito -- este
  caminho ainda reusa `VK_JitterPixelOffset` (tabela Halton-8 fixa do motor,
  mesma do hand-port), não a sequência própria do SDK oficial.
- [ ] Usar luminance pyramid/locks/luma history do SDK, sem substituir por luma direta.
  Delegado inteiramente ao SDK (dispatch único, sem acesso a passes internos) --
  não há substituição por luma direta neste caminho, mas também não auditado.
- [ ] Remover cinco shaders próprios e referências de CMake após caminho oficial compilar.
  NÃO remover ainda -- caminho oficial compila mas não foi validado visualmente;
  remover o hand-port agora seria a "substituição silenciosa" que AGENTS.md proíbe.
- [ ] Remover fallback EASU/TAA ou identificá-lo explicitamente como fallback espacial.

## Fase 3 — entradas temporais corretas

- [ ] Mapear convenções: coluna/linha, ordem MVP, Vulkan Y, depth range, unidades,
  jitter em pixels, motion current-to-previous e UV/pixels; registrar em comentário/teste.
- [ ] Teste matemático executável: câmera parada => motion zero sem jitter;
  deslocamento conhecido => signo/magnitude esperados; depth => distância conhecida.
- [ ] Capturar matriz anterior no momento 3D correto, antes de HUD alterar matrizes;
  distinguir projeção jittered usada na profundidade e matrizes unjittered.
- [ ] Gerar motion para objetos/entidades animados, não apenas reprojeção da câmera.
  Cobrir viewmodel, sprites/partículas ou documentar tratamento oficial específico.
- [ ] Fornecer máscara reactive para transparência/partículas e composition mask
  quando exigida; comparar gerador oficial com entradas do motor.
- [ ] Reset em primeiro frame, mapa, teleport/camera cut, seek demo, vid_restart,
  resize, mudança de escala/qualidade/backend e multiview.
- [ ] Definir MSAA: resolve depth/motion corretamente ou bloquear combinação
  com mensagem explícita; não usar depth multisample como sampler normal.
- [ ] Ajustar mip bias conforme guia, evitando afetar HUD/texturas indevidas.

## Fase 4 — composição e exclusividade

- [ ] Um backend efetivo por frame: off, FSR2 oficial ou DLSS SR.
- [ ] Falha deve restaurar layouts/estado e invalidar histórico antes de fallback;
  não executar segundo temporal após dispatch parcialmente gravado sem plano válido.
- [ ] Output do backend -> composição nativa -> gamma/contrast/FXAA definidos -> HUD.
- [ ] Garantir pós-processamento uma vez e UI/texto nativos, sem histórico temporal.
- [ ] Log/diagnóstico: solicitado, ativo, tamanho render/output, reset e motivo de falha.

## Fase 5 — NVIDIA

- [ ] Conferir SDK vendorizado, licença, versão de cada DLL e binários NGX necessários.
- [ ] Integrar interfaces oficiais C++ e eliminar fragilidade de mirror ABI C.
- [ ] Seguir ordem init/requirements/support/device/info/hooking definida na revisão;
  validar extensões de instance/device, features Vulkan e queues, sem truncar listas.
- [ ] Preencher matrizes de câmera, jitter pixel-space real, motion scales,
  depth flags, near/far/FOV, reset e tags/resources/lifetimes conforme guia.
- [ ] Selecionar modo Quality/Balanced/Performance, consultar tamanho ótimo,
  gerenciar frame tokens/contextos e recriação sem handles do device anterior.
- [ ] Confirmar manual hooking e avaliação realmente funcionam; observar código de retorno.
- [ ] Investigar pedido DLSS5 em fonte oficial e registrar API/produto/requisitos.
  Se distinto de SR, criar checklist separado antes de afirmar atendimento.
- [ ] RX AMD: somente suporte/fallback. RTX compatível: validação real obrigatória,
  com evidência de slEvaluateFeature bem-sucedido e imagens/performance.

## Fase 6 — Options/Video e comandos

- [ ] Seleção única Off/FSR2/DLSS SR com disponibilidade e motivo de indisponibilidade.
- [ ] Quality/Balanced/Performance; render scale custom só se backend suportar.
- [ ] Sharpness com faixa validada; comportamento consistente entre backends.
- [ ] Apply/vid_restart e cvars latched registrados uma vez; preservar seleção em config.
- [ ] Diagnóstico por comando: mostrar seleção e execução reais. Documentar comandos
  existentes; não sugerir r_speeds sem confirmar que existe.

## Fase 7 — verificação e aceite

- [ ] Build RelWithDebInfo e Release com exit code zero, sem warnings novos relevantes.
- [ ] Validation layers/sync validation: zero erros introduzidos, inclusive teardown.
- [ ] Offline dm3: parada >=10 segundos reais; giro, caminhada, paredes finas,
  centro, partículas, água/alpha, arma, HUD e console; sem branco/magenta/tremulação anormal.
- [ ] Transições off/FSR2/DLSS, escala, resize, janela/fullscreen, alt-tab,
  restart repetido, troca mapa, demo seek/multiview; resultado por cenário.
- [ ] Esperar tempo real medido: `wait 300` não significa 300 frames neste motor.
- [ ] Logs/capturas identificados por timestamp/build/backend. Screenshot preto
  exige verificar capture; não concluir que jogo está preto sem evidência.
- [ ] Performance sem validation, mesmo mapa/câmera/limites/vsync, baseline nativa
  versus modos; GPU timestamp e frame time. Não prometer ganho num cenário CPU-bound.
- [ ] Revisão 1 -> corrigir -> testes afetados -> revisão 2 -> conferência upstream
  (campos, flags, etapas, resources, lifecycle). Registrar achados e evidências.
- [ ] RTX real completa fase NVIDIA. Ausência de hardware permanece pendência explícita.
- [ ] Commit/push e comparação remota final; pacote teste com hash e instrução;
  checkpoint claro para outra sessão. Não abrir PR sem pedido/necessidade autorizada.

## Protocolo de continuidade

Na retomada, ler AGENTS.md, este arquivo e topo de CONTINUE.md; executar git status,
git log e git worktree list. Não confiar em memória antiga para declarar completude.
Atualizar este checklist só com evidência. Se surgir bug novo, registrar cenário,
causa confirmada/hipótese, correção e validação. Nenhum agente deve declarar tudo pronto
enquanto os critérios correspondentes estiverem pendentes.

Próxima ação: corrigir lifecycle confirmado e preparar build baseline; em seguida
obter SDK AMD fixado e provar build core/backend antes de substituir render dispatch.
