# Onde paramos — Vulkan renderer / SDL3 port

## Checkpoint 2026-10-02 (parte 18) — achado real: vazamento de memória de 2.5GB no processo do Tiago, build sem os 2 últimos fixes

Enquanto tentava reproduzir o bug sozinho (testes isolados deram falso
alarme de "Error" -- era só o mutex de instância única do motor, "QWCL is
already running on this system", detectando o processo do Tiago ainda vivo,
confirmado via UI Automation lendo o texto real do diálogo), descobri o
processo dele (`ezquake-fsr2test.exe`, PID 40304, rodando desde 00:16:18)
com **2.5 GB de memória privada** (`PrivateMemorySize64`), ~1.1GB de working
set, depois de ~40 minutos de execução. Isso é MUITO acima do normal pra
ezQuake (tipicamente dezenas-centenas de MB) -- confirma vazamento de
memória real, consistente com "FPS caindo, travando" ao longo do tempo que
o Tiago relatou (sistema ficando sem memória / pressão de swap).

**Achado crítico sobre esse dado**: o hash do exe dele (`af755a61676761eb...`)
é da **parte 13** (fix de jitter), rodando SEM os 2 fixes mais importantes
feitos depois: o `g_outputImage` cross-command-buffer race (parte 15) e a
correção de `motionVectorScale` (parte 16). O vazamento observado é
plausivelmente CAUSADO pelo race do `g_outputImage` -- uma imagem Vulkan
única sendo escrita por até 3 command buffers potencialmente concorrentes
sem sincronização correta pode levar a reação em cascata de recursos
(descriptor sets, memória de staging, ou pior: o driver/camada de validação
tentando se recuperar de hazard, acumulando estado a cada frame problemático).

**Não tentei nem vou tentar matar ou mexer no processo do Tiago** -- é o
dele, rodando com os cvars de teste ativos. Só constatei o estado via
`Get-Process` sem interferir. Limpeza feita: removidos os 3 diretórios/exes
de teste isolado que criei durante a investigação (`/c/ezquake_diag2`,
`ezquake-diag.exe`, `ezquake-diag3.exe`) -- não eram o processo dele, eram
tentativas minhas de reproduzir sozinho que bateram no mutex dele.

**Próxima ação real**: quando o Tiago puder fechar o processo antigo
(`ezquake-fsr2test.exe`, build da parte 13) e testar o build mais recente
(`29ed7ad1119c7ce45f11cc5a982237e89ce9e0b1d26179e3a7e89e33b7d58ac9`, parte
16, com os 2 fixes de race+motion vector), isso deve ser verificado de
novo -- hipótese forte, mas ainda não confirmada, de que o vazamento
desaparece com o fix do `g_outputImage` aplicado.

**Correção de interpretação após 100 min de observação**: memória PRIVADA
ficou estável em 2529MB entre duas checagens (00:xx e ~100min depois),
só o working set variou levemente (1118->1214MB, paginação normal do SO,
não alocação nova). Isso muda a categoria do problema: NÃO parece ser
vazamento crescente sem fim por frame -- parece ser uma alocação excessiva
de UMA VEZ que estabiliza num platô alto (2.5GB), compatível com múltiplas
recriações de contexto cada uma deixando resíduo até as recriações pararem,
ou simplesmente a SDK real alocando mais que o esperado de uma vez (13
níveis de mipmap de luminância + histórico + lock status + múltiplos
buffers internos, confirmado em `ffx_fsr2.cpp` -- razoável que some bastante,
ainda que 2.5GB pareça alto demais pra essa resolução). De qualquer forma,
2.5GB é patologicamente alto pra esse motor independente de ser platô ou
vazamento -- a correção do `g_outputImage` continua sendo o candidato mais
forte, só que agora por "evitar recriações repetidas residuais" em vez de
"vazamento por frame".

## Checkpoint 2026-10-02 (parte 17) — Codex bloqueado pelo ambiente, varredura adicional feita sozinho, sem mais achados

Segunda tentativa de delegar ao Codex (`task-muqerlop-8x2wfm`) falhou
diferente da primeira: "Blocked by the environment before any repository
access" -- `CreateProcessAsUserW: Access denied` (erro 5 do Windows) no
shell dele, e `E:\tmp\eqvk-upscaling` fora da raiz gravável configurada pra
ele. Diferente do job anterior (que conseguiu LER via `cmd`, só não
conseguiu escrever), esse nem leu nada -- "I did not read, modify, build,
or commit anything". Não adianta insistir no mesmo padrão de delegação pra
esse worktree; sigo a varredura sozinho.

**Varredura adicional feita sozinho** (os mesmos pontos que pedi ao Codex
investigar, já que ele não conseguiu): `frameTimeDelta` -- confirmado em
milissegundos nos dois lados (`FSR2Sample.cpp:490` divide por 1000 pra virar
segundos, nosso `cls.frametime * 1000.0f` já entrega em ms, bate).
`sharpness` -- sample usa slider `[0,1]` direto sem remapeamento do app
(`UI.cpp:248`), SDK remapeia internamente (`ffx_fsr2.cpp:1004`); nosso
`bound(0.0f, vid_vulkan_sharpness.value, 1.0f)` idêntico. `reactive`/
`transparencyAndComposition`/`exposure`/`enableAutoReactive` não setados --
confirmado que isso é EQUIVALENTE ao padrão do sample: `dispatch` zerado via
`memset` deixa `FfxResource.resource == NULL`, que é exatamente o que
`ffxFsr2ResourceIsNull()` (`ffx_fsr2.cpp:1216`) checa -- não precisa
registrar explicitamente como "vazio" via `ffxGetTextureResourceVK(nullptr,...)`
feito pelo sample, memset já cobre. Formato de depth (`VK_FORMAT_D32_SFLOAT`)
confirmado idêntico ao sample (`Renderer.cpp:72`).

**Nenhuma discrepância nova encontrada nesta passada.** Com motion vector
scale e jitter já corrigidos e confirmados contra o sample real, e todo
resto do dispatch verificado campo a campo sem achar mais nada, a hipótese
de "convenção errada em algum campo do dispatch" está ficando esgotada --
se o tremor/performance persistir no próximo teste, a causa provável muda
de categoria: ou é algo na integração C/C++ em si (barreiras, layout,
timing de submissão) já parcialmente coberto pelas correções de race
anteriores, ou precisa mesmo de profiler/captura real (RenderDoc) em vez de
mais leitura de código.

**Build ainda não deployado** -- arquivo de teste seguiu bloqueado durante
toda esta janela de trabalho autônomo. Hash pronto pra deploy:
`29ed7ad1119c7ce45f11cc5a982237e89ce9e0b1d26179e3a7e89e33b7d58ac9`.

**Achado real sobre a falha de captura de log de sessões anteriores**: `-condebug`
NÃO é uma flag booleana sozinha -- exige um argumento seguinte com o caminho
do arquivo (`sys_win.c:1278-1284`, `s = COM_Argv(i + 1); qconsole_log = fopen(s, "a")`).
Toda tentativa anterior desta sessão usou `-condebug` sem caminho, por isso
nunca criou log algum nos smoke tests passados -- não é limitação do
ambiente, é uso errado do parâmetro. Próximo teste usar
`-condebug caminho\completo\log.txt` (caminho explícito, não relativo
ambíguo) pra garantir captura real do console.

## Checkpoint 2026-10-02 (parte 16) — CORREÇÃO da correção: motionVectorScale da parte 13 estava errado, achado comparando contra o sample oficial real

Enquanto aguardava reteste, fui direto comparar contra o sample Vulkan
REAL da AMD (`external/fsr2/src/VK/UpscaleContext_FSR2_API.cpp`,
vendorizado junto com a API, framework Cauldron completa) em vez de só
ler os headers internos do SDK isoladamente -- e achei que a correção da
parte 13 (`motionVectorScale = (-1,-1)`) estava ERRADA.

**O que o sample oficial realmente faz** (não hipótese, lido direto):
- `libs/cauldron/src/VK/shaders/GLTFMotionVectorsPass-frag.glsl:58-59`:
  `motionVect = CurrPosition.xy/w - PrevPosition.xy/w` -- espaço NDC
  [-1,1], sinal `current - previous`. `CurrPosition = gl_Position`
  (clip space Vulkan nativo, mesma convenção que nosso `vk_motion_vectors.frag`
  já usa via `invViewProj`/`prevViewProj`).
- `src/VK/UpscaleContext_FSR2_API.cpp:231-232`:
  `motionVectorScale = (renderWidth, renderHeight)`, POSITIVO, não negado.
- `ffx_fsr2.cpp:902-903`: o SDK divide esse valor pelo tamanho do alvo de
  motion vectors (`renderWidth`/`Height` aqui) ANTES de guardar como
  `cbFSR2.fMotionVectorScale` -- ou seja a escala EFETIVA interna do sample
  é exatamente `1.0` (sem conversão adicional nenhuma no shader, apesar do
  nome `fUvMotionVector` sugerir uma conversão pra UV que na prática não
  acontece pro sample).

**Conclusão corrigida**: nosso buffer (`vk_motion_vectors.frag`) já usa o
MESMO sinal que o sample (`current - previous`) -- a correção de sinal da
parte 13 estava invertendo algo que já estava certo. A diferença real é só
de MAGNITUDE: nosso buffer está em UV [0,1] (metade da amplitude do NDC
[-1,1] do sample pra o mesmo movimento físico, já que NDC = 2*UV-1). Pra
reproduzir a escala efetiva comprovada do sample (1.0) usando um buffer UV
em vez de NDC, o parâmetro pré-divisão precisa ser o DOBRO:
`motionVectorScale = (2*sceneWidth, 2*sceneHeight)`, sinal inalterado
(positivo, sem negar).

Comentário antigo substituído por um novo citando as 3 fontes exatas
(shader do sample, call site do dispatch, divisão interna do SDK) em vez de
só os headers internos isolados. Build limpo confirmado, hash
`29ed7ad1119c7ce45f11cc5a982237e89ce9e0b1d26179e3a7e89e33b7d58ac9`.

**Nota de processo importante**: esta é a SEGUNDA tentativa de acertar essa
mesma convenção na mesma sessão. A primeira vez (parte 13) citei os headers
internos do SDK (`ffx_fsr2_callbacks_glsl.h`, `ffx_fsr2_reconstruct_...h`)
e errei porque não tinha visto AINDA o sample de referência completo nem a
divisão em `ffx_fsr2.cpp:902-903` que muda tudo. Lição: pra convenção de
unidade/sinal de uma SDK externa, o código de REFERÊNCIA FUNCIONANDO
(sample oficial) é evidência mais forte que ler fragmentos de header
isolados e inferir -- deveria ter procurado o sample primeiro, não depois.
**Ainda não testado ao vivo** -- é a 3ª hipótese de motion vector desta
sessão (sinal normal -> sinal invertido -> sinal normal+escala dobrada),
precisa de confirmação real do Tiago antes de considerar resolvido.

**Varredura sistemática extra contra o mesmo sample** (todos os campos do
`FfxFsr2ContextDescription`/`FfxFsr2DispatchDescription` que o sample
preenche): `FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE` -- sample sempre seta
(Cauldron usa HDR), nós corretamente NÃO setamos (`postProcessColorImages`
usa `physicalDeviceSurfaceFormat.format`, confirmado BGRA8 UNORM, SDR real).
`FFX_FSR2_ENABLE_DEPTH_INFINITE` -- sample seta junto com `DEPTH_INVERTED`,
nós corretamente não setamos (far plane finito, `r_farclip`). `preExposure`/
`exposure` -- ambos `1.0f` igual ao sample. Nenhuma outra discrepância de
convenção encontrada nesta passada.

**Build com o fix de motion vector ainda não deployado** -- `ezquake-fsr2test.exe`
continua bloqueado pelo processo do Tiago em várias tentativas ao longo desta
sessão de trabalho autônomo. Hash do build pronto pra deploy assim que o
arquivo for liberado: `29ed7ad1119c7ce45f11cc5a982237e89ce9e0b1d26179e3a7e89e33b7d58ac9`.

**Jitter re-confirmado contra o mesmo sample** (não só a doc desta vez):
`UpscaleContext.cpp:141-147` confirma exatamente o que a doc já tinha dito e
o que a parte 13 corrigiu -- `jitterOffset` recebe o valor CRU de
`ffxFsr2GetJitterOffset` (sem negar Y), a negação só acontece na hora de
montar a matriz de projeção (`SetProjectionJitter`, `-2*jitterY/height`).
A correção da parte 13 estava certa; diferente do motion vector, não precisou
de correção adicional. Única diferença real: o sample usa a sequência
Halton(2,3) própria da SDK (`ffxFsr2GetJitterOffset`) com fase count calculado
por `ffxFsr2GetJitterPhaseCount(renderWidth, displayWidth)`, enquanto nós
reusamos a tabela Halton-8 fixa do motor -- isso é otimização de qualidade
(pendência já documentada na Fase 2), não bug de convenção/sinal.

## Checkpoint 2026-10-02 (parte 15) — Codex achou o `g_outputImage` sem slot no shim da SDK (mesmo bug, arquivo esquecido)

Resultado do job `task-muqe8fgj-851hug` (investigação de performance):
Codex não conseguiu RODAR build nem commitar (sandbox dele nega escrita fora
de `C:\Projetos Linux\ezquake-sdl3-vulkan-pr`, só investigação read-only),
mas achou por inspeção algo real que eu tinha deixado passar:

> "Concrete correctness concern: `g_outputImage` is a single image shared
> across three frames in flight. It should be per-frame-slot."

**Confirmado real**: `g_outputImage`/`g_outputImageMemory`/`g_outputImageView`
em `vk_fsr2_sdk.cpp` ainda eram instâncias ÚNICAS, sem slot per-frame-in-flight
-- exatamente a MESMA classe de bug que corrigi em `vk_fsr2.c` (8 imagens),
`vk_upscale.c` (history/matrices) e `vk_dlss.c` (output image), mas esqueci
de aplicar no PRÓPRIO shim da SDK oficial que escrevi nesta sessão. Ironia
notada: documentei extensivamente o padrão, corrigi em 3 arquivos, e deixei
o 4º (o mais novo) com o mesmo bug.

**Corrigido**: `g_outputImage[FSR2_SDK_MAX_FRAMES_IN_FLIGHT]` (constante
literal `3`, duplicada localmente já que este arquivo deliberadamente não
inclui headers do motor -- ver comentário do próprio arquivo). `frameSlot`
adicionado como novo parâmetro em `VK_Fsr2SdkComposite` (`vk_fsr2_sdk.cpp`)
e no `extern` correspondente em `vk_fsr2_sdk_bridge.c`, preenchido com
`vk_options.frame.currentFrame` no call site da wrapper. Todos os pontos de
acesso (`CreateOutputImage`/`DestroyOutputImage` em loop, `ffxGetTextureResourceVK`
do output, as 2 barreiras + `vkCmdCopyImage` finais) indexados por
`frameSlot`.

**Removido** o `fprintf` de diagnóstico temporário (não mais necessário --
confirmei junto com o Codex que `CreateContextLocked` NÃO roda toda frame,
`sceneSize`/`displaySize` são estáveis; a hipótese de recriação de contexto
foi descartada por ambos independentemente).

Build limpo confirmado do zero (exit 0, sem warning novo), hash
`ab9b252afff97b72d5c6bf598019de4286b536466c8797081aed8a59b9cfc41e`.

**Hipótese do Codex pra causa de performance** ("custo fixo do FSR2 pode
exceder economia do renderscale 0.66 numa engine leve como Quake") avaliada
e considerada fraca -- FSR2 é desenhado pra rodar em tempo real em GPUs bem
menos potentes que a RX 6800 XT do Tiago, não deveria ser estruturalmente
lento demais. Mais provável que o race agora corrigido (ou outro ainda não
achado) estivesse causando stall de sincronização real via driver, não
custo intrínseco do algoritmo. **Não confirmado ainda -- precisa reteste.**

Sessão total: 12 bugs reais confirmados e corrigidos (6 hand-port + 2 em
vk_upscale.c/vk_dlss.c + 2 achados pelo Codex no shim da SDK + jitter + este).

**Investigação adicional sem achar mais nada por leitura de código**
(verificado e descartado como causa, tudo bate com o hand-port e com a doc
oficial): `g_reset` lifecycle correto (só problema seria tremor no 1º frame
pós-reset, não contínuo); `cameraFovAngleVertical`/`r_refdef.fov_y` conversão
grau->radiano correta; `viewSpaceToMetersFactor` idêntico ao hand-port
(1.7/56); `reversedDepth`/`FFX_FSR2_ENABLE_DEPTH_INVERTED` consistente com
todo o resto do código; `R_FarPlaneZ()` retorna valor finito real (cvar
`r_farclip`, com bound), não precisa de `FFX_FSR2_ENABLE_DEPTH_INFINITE`.

**Build com os 2 fixes desta parte (jitter + output image race) ainda não
testado ao vivo** -- `C:\ezquake\ezquake-fsr2test.exe` continua bloqueado
pelo processo do Tiago (ausente). Esgotei hipóteses razoáveis de bug por
leitura de código sem mais dado real -- próxima ação de verdade precisa ou
do Tiago testando de novo, ou de profiler/log real (RenderDoc, validation
layers com `-dev`, ou captura de frame time), não mais leitura às cegas.

## Checkpoint 2026-10-02 (parte 14) — Tiago retestou: tremor melhorou mas não sumiu, FPS caiu muito (esperado o oposto)

Autorização explícita do Tiago pra trabalhar sem parar até ele voltar amanhã,
junto com o Codex em paralelo (job `task-muqe8fgj-851hug`, investigando a
mesma regressão de performance de forma independente).

**Reteste real** (`vid_vulkan_upscaler 3` + `renderscale 0.66`, build com o
fix de jitter): "deu uma boa melhora mas não parou" (tremor) "e o jogo parece
estar travando com FPS baixo... com uso do FSR2 e DLSS era pro fps triplicar
não ao contrário". Confirma: fix de jitter foi parcialmente correto (melhora
real, não é placebo), mas resta pelo menos 1 causa de tremor residual, e tem
um problema de performance SEPARADO e grave que não foi causado pelo fix de
jitter (Tiago relatou os dois sintomas juntos na mesma mensagem, mais
consistente com bug pré-existente desde o início do caminho SDK, não
regressão nova).

**Diagnóstico temporário adicionado** (`vk_fsr2_sdk.cpp`, `fprintf(stderr,...)`
em `CreateContextLocked`'s trigger condition) pra confirmar/descartar a
hipótese mais perigosa: contexto da SDK sendo recriado TODO frame em vez de
uma vez só (`ffxFsr2ContextCreate` é caro -- compila pipelines, aloca memória
de device -- se rodar toda frame explicaria travamento e FPS baixo
perfeitamente). Build limpo confirmado (precisou de `#include <cstdio>`
adicional), hash `531b016e6b0ffdc85731f77aa1e51bd4ad2cbf181dd4072288b94c6769cd961d`.
**Ainda não deployado** -- `C:\ezquake\ezquake-fsr2test.exe` estava em uso
pelo processo do Tiago (arquivo bloqueado), aguardando ele fechar o jogo pra
copiar o build novo.

**Investigação própria sem achar causa concreta ainda** (só por leitura de
código, sem profiler): descartei several hipóteses -- motion vectors
pipeline/framebuffer já tem cache lazy correto (não recria por frame), o SDK
sempre despacha o luminance pyramid incondicionalmente (comportamento padrão
documentado, não um bug meu), contexto único global (`g_context`, não array
per-frame-in-flight) não deveria ser problema de performance numa única fila
gráfica sequencial. Nenhuma fonte óbvia de stall encontrada por leitura --
precisa mesmo do log/profiler real pra confirmar.

**Codex investigando em paralelo** a mesma regressão, mesmas 5 hipóteses
priorizadas passadas a ele (recriação de contexto, vkDeviceWaitIdle síncrono,
falha silenciosa no dispatch, vazamento de recurso, outro problema óbvio no
hot path) -- lendo o `ffx_fsr2.cpp` real da SDK pra entender o dispatch
interno. Resultado ainda pendente no momento deste checkpoint.

**Próxima ação real**: assim que o Tiago puder fechar o jogo, deployar o
build com diagnóstico, pedir pra ele testar de novo e capturar o
`fprintf(stderr,...)` (ex: rodar com `2> log.txt` ou via terminal visível) --
se aparecer "(re)creating context" repetidamente durante o jogo normal
(não só 1x no início), confirma a hipótese mais perigosa.

## Checkpoint 2026-10-02 (parte 13) — PRIMEIRO TESTE VISUAL REAL: tremor/borrão no modo SDK oficial, causa provável corrigida

**Tiago testou ao vivo**: `vid_vulkan_upscaler 3` + `vid_vulkan_renderscale 0.66`
-- "a tela fica tremendo e meio borrada". Borrão é parcialmente esperado
(upscaling espacial sempre borra algo), mas tremor (shimmering) não é normal --
sintoma clássico de reprojeção temporal com convenção errada.

**Causa provável encontrada e corrigida**: o jitter passado pro campo
`jitterOffset` do dispatch oficial (`vk_main.c`, chamada a
`VK_Fsr2SdkCompositeWrapper`) usava `VK_JitterPixelOffset()` -- a MESMA
função usada pelo hand-port. Mas essa função nega o componente Y
especificamente porque os shaders PRÓPRIOS do hand-port (`vk_fsr2_*.comp`)
consomem o jitter já no espaço de clip Vulkan final (pós-flip de
`vk_flipRemapMatrix`). A documentação oficial da AMD (`ffx_fsr2.h`, seção do
`jitterOffset`) é explícita: o campo espera o valor CRU da sequência Halton,
e a conversão pra espaço de matriz (`jitterY_matrix = -2*jitterY_raw/height`)
é feita INTERNAMENTE pela SDK -- passar um Y já negado nosso causa dupla
inversão.

Confirmado matematicamente: `VK_JitteredProjectionMatrix` (que desenha a cena
de verdade) usa o jitter Y CRU (sem negar) antes do flip de
`vk_flipRemapMatrix`, resultando no mesmo efeito líquido que a fórmula oficial
depois do flip -- ou seja, nossa matriz de projeção real já bate com a
convenção da AMD. O bug era só no valor passado pro dispatch, que deveria
usar o mesmo jitter CRU que a matriz de projeção usa, não o valor já ajustado
de `VK_JitterPixelOffset`.

**Corrigido** (`src/vk_main.c`): a chamada ao `VK_Fsr2SdkCompositeWrapper`
agora lê `vk_jitter_halton8[vk_jitter_frameIndex % 8]` diretamente (mesmos
statics que `VK_JitteredProjectionMatrix` já usa), sem a negação de Y que
`VK_JitterPixelOffset` aplica. Comentário extenso citando a seção exata da
doc oficial. Build limpo confirmado, hash
`af755a61676761eb533eeddce233b726dd4d9c1268670a37325b0b458feeda9a`.

**IMPORTANTE**: esta é uma hipótese bem fundamentada (matemática + doc oficial
citada), não uma confirmação visual ainda -- precisa o Tiago testar de novo
com esse build pra saber se o tremor sumiu ou diminuiu. Se persistir, outras
suspeitas na fila (em ordem): `g_reset` nunca é setado verdadeiramente true
de novo após o primeiro frame real de forma confiável (checar lógica de
invalidate), ou falta mapear `ffxFsr2GetJitterPhaseCount` (ainda usa a tabela
de 8 fases fixa do motor, não a fórmula oficial baseada em
`displayWidth/renderWidth`, que pode mudar quantas fases de Halton são
necessárias para esse fator de upscale específico de 0.66 = ~1.5x).

## Checkpoint 2026-10-01 (parte 12) — Codex achou 2 bugs reais no shim da SDK oficial, verificados e corrigidos

Delegado ao Codex (`codex-companion task`) um segundo-pass de revisão independente
sobre os 16 commits desta sessão. Codex não teve permissão de escrita no
worktree (`E:\tmp\eqvk-upscaling`, fora do diretório autorizado dele), então só
reportou achados sem aplicar -- verificados e corrigidos manualmente aqui.

**Achado 1 (real, confirmado contra o código-fonte vendorizado v2.2.1, CORRIGIDO)**:
`vk_fsr2_sdk.cpp`'s `motionVectorScale` estava `(1.0, 1.0)`. O Codex apontou
sinal errado mas magnitude errada (sugeriu `(-sceneWidth, -sceneHeight)`,
tratando o buffer como se estivesse em pixels). Investigação própria:
- `src/vulkan_shaders/vk_motion_vectors.frag:88`: nosso buffer grava
  `result = texCoord - prevUV` -- já em espaço UV [0,1], NÃO pixels.
- `external/fsr2/.../ffx_fsr2_callbacks_glsl.h:357`: SDK computa
  `fUvMotionVector = fSrcMotionVector * MotionVectorScale()` -- o resultado
  PÓS-escala já se chama `fUvMotionVector`, confirmando que a SDK espera UV
  depois da escala, não pixels (a descrição em pixels do README se aplica ao
  buffer ANTES da escala, não ao resultado).
- `external/fsr2/.../ffx_fsr2_reconstruct_dilated_velocity_and_previous_depth.h:30`:
  SDK usa `fReprojectedUv = fUv + fMotionVector` -- ou seja a SDK espera
  `previousUV - currentUV` (somado à UV atual dá a UV anterior), o OPOSTO do
  que nosso buffer grava (`currentUV - previousUV`).
- Conclusão correta: sinal errado SIM, magnitude não -- fix é
  `motionVectorScale = (-1.0, -1.0)`, não `(-sceneWidth, -sceneHeight)`.
  Comentário extenso adicionado no código citando as 3 fontes exatas.

**Achado 2 (real, confirmado, CORRIGIDO)**: `g_reset = false` rodava ANTES de
checar `ffxFsr2ContextDispatch`'s retorno -- se o dispatch falhasse, o reset
real do próximo frame real seria perdido (flag já zerada), deixando o SDK
tentar blend contra um estado interno que pode não ter avançado direito.
Corrigido: `g_reset = false` só depois de confirmar `FFX_OK`.

Build limpo confirmado do zero (exit 0, sem warning novo), hash
`b14a56438f893f968de6657a98cb8eba5f653fc3f8a41a2793cf3fe866705251`.

**Nota de processo**: delegar ao Codex funcionou bem pra achar um bug real de
convenção (sinal do motion vector) que eu não tinha verificado contra a fonte
antes -- mas o número exato que ele sugeriu (`-sceneWidth`) estava errado,
só o diagnóstico de "sinal invertido" estava certo. Toda alegação de IA
(minha ou do Codex) sobre convenção de SDK externa precisa ser verificada
contra o código-fonte real antes de aplicar, não contra a intuição nem
contra a primeira citação de doc que aparecer -- o README por si só teria
levado a um fix errado aqui.

Ainda nenhuma validação visual. Sessão total: 9 bugs corrigidos antes +
2 agora = 11.

## Checkpoint 2026-10-01 (parte 11) — mesma classe de bug, terceiro arquivo: vk_dlss_outputImage

Varredura final nos 3 arquivos do sistema de upscaling: `vk_dlss.c` tinha
`vk_dlss_outputImage` como instância ÚNICA (não array), escrita pela SDK
Streamline (`slEvaluateFeature`) e lida por `VK_DLSS_CopyOutputTo` dentro do
MESMO frame -- categoria "scratch intra-frame" igual às 6 imagens já
corrigidas em `vk_fsr2.c`, mas com o mesmo risco residual: nada impedia o
command buffer do frame N+1 escrever nela via `slEvaluateFeature` enquanto o
command buffer do frame N ainda executava seu próprio write+copy na GPU.

**Corrigido**: `vk_dlss_outputImage`/`Memory`/`View` viraram arrays
`[VK_MAX_FRAMES_IN_FLIGHT]`, `VK_DLSS_EnsureOutputImage`/`DestroyOutputImage`
em loop por slot, `VK_DLSS_Composite`/`VK_DLSS_CopyOutputTo` indexando por
`vk_options.frame.currentFrame` diretamente (mesmo padrão write-index puro
das 6 imagens scratch intra-frame do `vk_fsr2.c` -- a SDK Streamline mantém
seu próprio estado temporal internamente via `sl::Constants::reset`, esse
buffer é só o destino de saída de CADA frame, não precisa de read-index).

Build limpo confirmado do zero (exit 0, sem warning novo), hash
`d005ea6619c2ca69ddd276538105e0e1f6426e06f775d937b3b296ca154e7ac0`.

**Com isso, os 3 arquivos do sistema de upscaling** (`vk_fsr2.c` hand-port,
`vk_upscale.c` DLSS/espacial, `vk_dlss.c` DLSS real via Streamline) **estão
livres de instâncias únicas sem slot per-frame-in-flight** -- varredura
completa feita, não ficou nenhuma pendência conhecida dessa classe de bug.
Sessão total: 9 bugs reais confirmados e corrigidos.

**Próxima ação real**: parar e aguardar validação visual -- recomendação
repetida, agora com ainda mais peso dado o volume total de mudança.

## Checkpoint 2026-10-01 (parte 10) — mesmo bug de history race corrigido em vk_upscale.c (caminho DLSS/espacial)

Continuação direta da parte 9: a nota registrada lá ("mesmo padrão existe em
`vk_upscale.c`, não corrigido") virou investigação real. `vk_upscale.c` tinha
um comentário existente (de sessão anterior) alegando que `historyImages[2]`/
`matricesBuffers[2]` eram seguros "porque o fence per-imageIndex garante não
sobreposição a cada `VK_MAX_FRAMES_IN_FLIGHT=3` frames". **Essa alegação
estava tecnicamente errada**: `historyIndex` (o índice real usado pra indexar
essas arrays) é um contador livre incrementado 1x por
`VK_UpscaleUpdateHistory`, SEM relação nenhuma com `imageIndex` nem com
`frameSlot` -- período 2, igual ao bug já corrigido em `fsr2History`. O
comentário confundiu dois fences diferentes (`inFlightFences[frameSlot]` e
`imageInFlightFences[imageIndex]`, ambos reais e existentes no motor) com
proteção que nenhum dos dois realmente dava pro contador `historyIndex`.

**Corrigido com o mesmo padrão já provado em `vk_fsr2.c`**:
- `historyImages`/`historyImageMemories`/`historyImageViews` e
  `matricesBuffers`/`matricesBufferMemories` redimensionados de `[2]` pra
  `[VK_MAX_FRAMES_IN_FLIGHT]`.
- `VK_UpscaleHistoryWriteIndex`/`VK_UpscaleHistoryReadIndex` (duplicatas
  locais das mesmas 2 funções já em `vk_fsr2.c` -- pequenas demais pra valer
  compartilhar entre translation units).
- `historyImages`: write-index (`frameSlot`) na escrita
  (`VK_UpscaleUpdateHistory`), read-index (frame anterior) na leitura
  (`VK_UpscaleDescriptorSet`) -- história de cor tem delay real de 1 frame.
- `matricesBuffers`: write-index nos 2 pontos de acesso (`VK_UpscaleUpdateMatrices`
  escreve, `VK_UpscaleDescriptorSet`/`VK_MotionVectorsDescriptorSet` leem) --
  intra-frame, mesma categoria de `fsr2ReconstructedPrevDepth` no `vk_fsr2.c`
  (escrito e lido no MESMO frame, não cross-frame apesar do padrão ping-pong
  antigo sugerir o contrário).
- Removido o toggle `historyIndex = 1 - historyIndex` do fim de
  `VK_UpscaleUpdateHistory` -- não precisa mais, o índice já vem de
  `vk_options.frame.currentFrame`, que avança sozinho a cada frame.
- Todos os comentários que citavam `historyIndex`/"1 - historyIndex"
  atualizados pra refletir o esquema novo.

Build limpo confirmado do zero (exit 0, sem warning novo), hash
`ee777b18d703779c39c5839e5bd570cd58b1e511b2392584c61dfe5136ee7d58`.

**Com isso, os dois arquivos principais do sistema de upscaling (`vk_fsr2.c`
hand-port e `vk_upscale.c` DLSS/espacial) estão livres da classe de bug
"ping-pong período-2 vs frames-em-voo período-3"** que dominou a sessão.
Sessão total: 8 bugs reais confirmados e corrigidos.

**Próxima ação real**: mesma recomendação de sempre -- parar pra validação
visual antes de continuar. O volume de mudança não testada é grande o
suficiente que vale a pena o Tiago confirmar que nada regrediu antes de ir
mais fundo em Fase 3 (entradas temporais) ou qualquer outra coisa nova.

## Checkpoint 2026-10-01 (parte 9) — maior pendência de Fase 1 corrigida: 8 imagens scratch sem slot per-frame-in-flight

Continuação direta da parte 8. As 8 imagens "scratch" de baixa resolução do
hand-port (`fsr2DilatedDepth`, `fsr2DilatedMotion`, `fsr2ReconstructedPrevDepth`,
`fsr2LockInputLuma`, `fsr2PreparedInputColor`, `fsr2DilatedReactiveMasks`,
`fsr2DilatedMotionPrev`, `fsr2NewLocks`) eram instâncias ÚNICAS sem slot
per-frame-in-flight nenhum, só protegidas por barreira INTRA-command-buffer
(`VK_Fsr2Barrier`) -- nada impedia o command buffer do frame N+1 escrever
nelas enquanto o do frame N ainda rodava na GPU (confirmado real lendo
`vk_main.c`'s padrão de 1 CB por frame com fence só por frameSlot).

**Investigação completa feita antes de corrigir** (rastreando cada imagem nos
3 shaders que a tocam, na ordem real de dispatch reconstruct->depthclip->lock->
accumulate->rcas): 2 categorias diferentes, tratamento diferente pra cada:

- **Scratch puramente intra-frame** (`fsr2DilatedDepth`, `fsr2DilatedMotion`,
  `fsr2LockInputLuma`, `fsr2PreparedInputColor`, `fsr2DilatedReactiveMasks`,
  `fsr2NewLocks`): escritas e totalmente consumidas dentro do MESMO dispatch,
  nenhum frame posterior lê o conteúdo. Indexadas só por `frameSlot`, sem
  precisar de "read previous".
- **Estado cross-frame real**: `fsr2DilatedMotionPrev` é genuinamente
  copiado no fim de um frame e lido no início do próximo (igual history) --
  usa `frameSlot` pra escrita, `readIdx` (via `VK_Fsr2HistoryReadIndex`) pra
  leitura. `fsr2ReconstructedPrevDepth` é mais sutil: apesar do nome, é
  MAIORIA intra-frame -- reconstruct escreve via `imageAtomicMax`, depthclip
  lê poucos passes depois NO MESMO dispatch, lock reseta pro far-plane
  sentinel no FIM do mesmo dispatch. O "previous" no nome só significa que o
  reset de um frame prepara o PRÓXIMO USO DO MESMO SLOT (3 frames depois,
  não o frame imediatamente seguinte) -- por isso usa `frameSlot` em TODOS os
  3 pontos de acesso (reconstruct write, depthclip read, lock reset-write),
  não `readIdx`. Essa distinção só ficou clara lendo os 3 shaders na ordem
  real, não seria óbvia só olhando o nome da variável.

**Corrigido**: todas as 8 viraram arrays `[VK_MAX_FRAMES_IN_FLIGHT]`.
`VK_Fsr2EnsureImages`/`VK_Fsr2DestroyImages` unificados num loop único por
slot (10 imagens por iteração: as 8 + `fsr2History`/`fsr2LockStatus`
reaproveitando o mesmo loop). `allImages[]` (lista de clear-na-criação)
recalculada pro tamanho certo (`10 * VK_MAX_FRAMES_IN_FLIGHT + 1`), incluindo
a checagem especial do far-plane sentinel do `fsr2ReconstructedPrevDepth`
ajustada pra comparar contra todos os N slots. `VK_Fsr2UpdateDescriptorSets`
e `VK_Fsr2Composite` (barreiras + dispatch) atualizados em cada um dos ~25
pontos de acesso identificados.

Build limpo confirmado do zero (exit 0, sem warning novo), hash
`d454de1df0c40a92e334bdeefb312c67b6e66081034744cf9c2ffb4cb1ac306d`.

**Com isso, a maior pendência técnica de Fase 1 identificada nesta sessão
está corrigida.** Resumo total da sessão: 7 bugs reais confirmados e
corrigidos (sampler/R32_UINT, usage flags, gate do motion-vector, copy/formato
RGBA16F->BGRA8, falha silenciosa de immediate commands, race de history
2-vs-3, race das 8 imagens scratch), SDK oficial AMD vendorizada e com
dispatch real funcionando como caminho paralelo. **Nenhuma validação visual
ainda** -- toda correção é por leitura cuidadosa de código + build limpo,
sem acesso confiável a teste interativo neste ambiente.

**Próxima ação real**: parar aqui e aguardar validação visual do Tiago antes
de continuar mexendo em código sem feedback -- o volume de mudança não
testada já é grande. Se for continuar sem ele, Fase 3 (entradas temporais:
mapear convenções de jitter/motion/depth, teste matemático executável) é o
próximo item do plano, mas é trabalho que se beneficia MUITO mais de
validação visual intermediária do que leitura de código pura.

## Checkpoint 2026-10-01 (parte 8) — bug de concorrência real corrigido: 2 slots de history vs 3 frames em voo

**Último item de Fase 1 fechado nesta sessão.** `fsr2History[2]`/
`fsr2LockStatus[2]` ping-pongavam via `fsr2HistoryIndex`, um contador global
incrementado 1x por `VK_Fsr2Composite` -- período 2, INDEPENDENTE do
`frameSlot` real (`vk_options.frame.currentFrame`, período 3, já que
`VK_MAX_FRAMES_IN_FLIGHT=3`). Com períodos diferentes, frame N e frame N+2
escrevem o MESMO slot de history (`N mod 2 == (N+2) mod 2`), mas o fence que
`VK_BeginFrame` espera antes de reusar um frameSlot é o fence DAQUELE
frameSlot (frame N+2 espera o fence de frame N-1, que também usa frameSlot
N+2 mod 3 -- não o de frame N). Nada garante que a GPU terminou de escrever
no slot de history do frame N antes do frame N+2 submeter outra escrita no
mesmo slot. Hazard de write-after-write cross-command-buffer real, exatamente
o que a Fase 1 avisava ("ping-pong de 2 imagens não é prova suficiente de
segurança com 3 frames em voo").

**Corrigido** (`src/vk_fsr2.c`): `fsr2History`/`fsr2LockStatus` redimensionados
pra `VK_MAX_FRAMES_IN_FLIGHT` slots (não mais fixo em 2). Índice de escrita
agora é o `frameSlot` direto (`VK_Fsr2HistoryWriteIndex`), índice de leitura é
o slot do frame imediatamente anterior (`VK_Fsr2HistoryReadIndex`,
`(frameSlot + N - 1) % N`) -- mesma garantia de fence que todo outro recurso
per-frame-in-flight deste arquivo já usa (`fsr2DepthParamsBuffer` etc).
Refatorado: criação/destruição em loop (`VK_MAX_FRAMES_IN_FLIGHT` iterações),
`allImages[]` (array de clear-na-criação) construído em runtime com contador
em vez de inicializador estático (C89 não tem loop em inicializador), todo
`fsr2HistoryIndex` removido.

Build limpo confirmado do zero (exit 0, sem warning novo), hash
`5d4f3751916b4bcfbddda7934e8a77dd927b0ac94930faa82bd4665233647aa0`.

**Mesmo padrão de bug existe em `vk_upscale.c`** (`historyIndex`/
`historyImages[2]`/`matricesBuffers[2]`, caminho DLSS/espacial mais antigo) --
NÃO tocado nesta sessão, fora do escopo que a Fase 1 catalogou (só cita
`vk_fsr2.c` explicitamente). Registrar como possível investigação futura se
alguém for mexer no caminho DLSS/espacial.

**Com isso, todos os itens de Fase 1 do plano foram revisados** -- 6 bugs
reais confirmados e corrigidos nesta sessão (sampler/R32_UINT, usage flags,
gate do motion-vector, copy/formato RGBA16F->BGRA8, falha silenciosa de
immediate commands, race de 2-slot vs 3-frame), 2 itens já estavam corretos.
Fase 2 (SDK oficial) também avançou: SDK vendorizada, shim C++/C com dispatch
real, 2 bugs pegos e corrigidos no próprio shim antes de qualquer teste ao
vivo (mesma classe dos bugs do hand-port: formato de copy, agora também
corrigido lá). Nenhuma validação visual ainda -- só leitura de código, build
limpo e um smoke test inconclusivo (sem crash, sem log capturado).

**Investigação adicional (sem fix ainda)**: ao revisar o fix de history,
encontrei um achado maior -- as 8 imagens "scratch" de baixa resolução do
hand-port (`fsr2DilatedDepth`, `fsr2DilatedMotion`, `fsr2ReconstructedPrevDepth`,
`fsr2LockInputLuma`, `fsr2PreparedInputColor`, `fsr2DilatedReactiveMasks`,
`fsr2DilatedMotionPrev`, `fsr2NewLocks`) são instâncias ÚNICAS sem slot
per-frame-in-flight nenhum. Confirmei no `vk_main.c` que o motor realmente
pode ter até 3 command buffers distintos executando CONCORRENTEMENTE na GPU
(`vkQueueSubmit` 1x por frame, cada frameSlot só espera o PRÓPRIO fence antes
de regravar) -- então nada impede o command buffer do frame N+1 escrever
nessas imagens enquanto o do frame N ainda está rodando. Documentado em
`UPSCALING_PLAN.md` como a maior pendência real de Fase 1, NÃO corrigido --
afeta potencialmente mais do que o bug de history já corrigido, mas precisa
de investigação cuidadosa pra separar o que é dependência cross-frame real
(tipo `fsr2ReconstructedPrevDepth`) do que é scratch puramente intra-frame,
decisão melhor tomada com o Tiago podendo validar visualmente em sequência.

**Próxima ação real**: investigar e corrigir a race das 8 imagens scratch
acima (maior pendência técnica aberta), OU parar pra validação visual real
antes de continuar -- a lista de bugs corrigidos sem nenhum teste visual já
é grande (6 bugs confirmados/corrigidos + este achado novo não corrigido)
o suficiente que mais mudança sem feedback real aumenta risco de acumular
erro sobre erro não detectado. Fase 3 (entradas temporais corretas) do plano
continua sem começar.

## Checkpoint 2026-10-01 (parte 7) — bug real corrigido: falha de immediate commands reportava sucesso (hand-port)

`VK_Fsr2EnsureImages` (`src/vk_fsr2.c`) chamava `VK_BeginImmediateCommands()`
e só usava o `cmd` dentro de `if (cmd != VK_NULL_HANDLE)`. Se essa chamada
falhasse (device perdido, memória de command pool esgotada, etc -- condição
rara mas real, é exatamente o tipo de falha que essa API pode retornar), o
bloco inteiro de clear + transição de layout das 13 imagens + `fsr2DefaultBlack`
era silenciosamente pulado, mas a função seguia até `fsr2ResourcesValid = true;
return true;` -- reportando sucesso com toda imagem ainda em `UNDEFINED`,
preparando o próximo `VK_Fsr2Composite` pra amostrar memória de GPU
genuinamente indefinida através de descriptors já escritos como
`VK_IMAGE_LAYOUT_GENERAL`.

**Corrigido**: falha de `VK_BeginImmediateCommands` agora chama
`VK_Fsr2DestroyImages()` (desfaz o que foi criado) e retorna `false`, mesmo
padrão de toda outra falha de criação de recurso nessa função. Build limpo
confirmado, hash `ffa3937d990ec6f4acc7de9702a3bf0ff5dc3c42c9d6bdea82bbcd515397a6a8`.

Também confirmado por leitura completa: nenhum retorno de
`VK_Fsr2CreateImage`/`VK_CreateBufferResource` é ignorado nesta função --
item correspondente da Fase 1 já estava satisfeito, sem ação necessária.

## Checkpoint 2026-10-01 (parte 6) — bug real corrigido: copy RGBA16F->BGRA8 incompatível (hand-port E shim da SDK)

Fase 1 catalogava `vk_fsr2.c:1151`: `vkCmdCopyImage` do `finalImage` (sempre
RGBA16F, necessário internamente pro acumulate/RCAS) direto pro swapchain
(`VK_FORMAT_B8G8R8A8_UNORM`). `vkCmdCopyImage` é cópia crua de bits, exige
formatos compatíveis em TAMANHO DE TEXEL (8 bytes/texel vs 4) -- violação de
spec garantida (`VUID-vkCmdCopyImage-srcImage-01548`), não um "pode dar
errado". Confirmado comparando contra `vk_dlss.c`, que usa o MESMO padrão de
cópia mas cria sua imagem de output já no formato do swapchain
(`vk_options.physicalDeviceSurfaceFormat.format`) -- único motivo de nunca
ter quebrado lá.

**Corrigido no hand-port**: `vkCmdCopyImage` -> `vkCmdBlitImage`
(`src/vk_fsr2.c`), que faz conversão de formato de verdade. Extents src/dst
idênticos (`fsr2DisplaySize` nos dois lados, não há upscale nessa cópia --
o upscale real já aconteceu no pass accumulate antes), então
`VK_FILTER_NEAREST` é exato, não perda de qualidade.

**Mesmo bug encontrado no próprio shim novo desta sessão** (`vk_fsr2_sdk.cpp`):
a imagem de output da SDK oficial também era criada sempre em RGBA16F,
copiada via `vkCmdCopyImage` pro swapchain -- bug recém-introduzido, pego
antes de qualquer teste ao vivo. Corrigido diferente do hand-port: em vez de
trocar pra blit, o formato da imagem de output passou a ser o formato real
do swapchain (`outputFormat`, novo parâmetro threaded de
`vk_fsr2_sdk_bridge.c` -> `vk_fsr2_sdk.cpp`, `CreateContextLocked` e
`ffxGetTextureResourceVK` do registro de output usam esse formato agora) --
`vkCmdCopyImage` continua válido porque os formatos passam a bater de
verdade. Abordagem diferente do hand-port porque aqui a imagem de output é
só um buffer de transporte pro SDK (não precisa ser HDR internamente como
o `fsr2History` do hand-port, que participa do próprio acumulate matemático).

Build limpo confirmado (exit 0, sem warning novo), hash
`c7ce323864fe948403aa79b34ea0e8dcd0f063c2645cb398c7481fee2a2d3805`.
Gamma/contrast/FXAA/HUD preservados -- mudança só no mecanismo de cópia
final, nenhuma etapa de pipeline removida ou reordenada.

Com este fix, TODOS os itens da Fase 1 catalogados no plano foram revisados:
4 bugs reais confirmados e corrigidos (sampler/R32_UINT, usage flags,
gate do motion-vector que travava os dois caminhos FSR2, e este de
copy/formato), 2 itens já estavam corretos (descriptor pool sizing,
`VK_Fsr2DestroyResources` já tinha caller + GPU idle confirmado). Restam da
Fase 1: auditoria de imediate commands/retorno ignorado/resize, e auditoria
de barreiras entre frames -- ambos exigem leitura mais extensa, próximos.

## Checkpoint 2026-10-01 (parte 5) — bug GRAVE corrigido: FSR2 nunca disparava fora do modo DLSS; CORREÇÃO da parte 2 deste checkpoint

**Achado mais sério desta sessão.** `UPSCALING_PLAN.md` Fase 1 já catalogava
isso como suspeita ("vk_main.c:1042 chama VK_MotionVectorsComposite para FSR2,
mas vk_upscale.c:1353 retorna false se DLSS não estiver ativo") -- confirmei
que é real e grave, não um detalhe: `VK_MotionVectorsComposite` (`vk_upscale.c`)
tinha `if (!VK_DLSS_Active()) return false;` como primeira linha. Os 3 pontos
de chamada em `vk_main.c` (hand-port `vid_vulkan_upscaler==1`, SDK oficial `==3`,
DLSS `==2`) todos fazem `vid_vulkan_upscaler.integer==N && VK_MotionVectorsComposite(...)`
-- fora do modo DLSS, essa função sempre retornava false, então **nem o
hand-port nem o caminho da SDK oficial (a integração inteira desta sessão)
jamais chegavam a disparar o dispatch real**, mesmo com `vid_vulkan_upscaler 1`
ou `3` selecionados -- caía direto no fallback antigo de render-pass, sem
nenhum erro ou log visível (falha silenciosa, exatamente o tipo de coisa que
AGENTS.md pede pra nunca acontecer -- "falha não pode se tornar substituição
silenciosa").

**Corrigido** (`src/vk_upscale.c`, `VK_MotionVectorsComposite`): gate trocado
de `VK_DLSS_Active()` para `VK_UpscaleActive()` (mesmo gate que todo outro
ponto de entrada de upscaler já usa neste arquivo). Estritamente mais
permissivo, sem regressão: `VK_DLSS_Active()` já exige `VK_UpscaleActive()`
como primeira condição própria, então o caminho DLSS continua funcionando
exatamente como antes; hand-port e SDK oficial passam a receber o buffer de
motion vectors de verdade. Build limpo confirmado (exit 0, sem warning novo),
hash `c4b97cc8b93747858c685df3fa8c7282ecaf0f87774fee3f8ec4fdbdce529884`.

**CORREÇÃO IMPORTANTE à "parte 2" deste mesmo checkpoint** (smoke test da SDK
oficial, registrado antes deste fix): o teste que relatei como "sobreviveu
~25s sem erro de validação" **não prova nada sobre a SDK oficial** -- com o
bug acima ainda presente naquele momento, `vid_vulkan_upscaler 3` nunca de
fato chamava `VK_Fsr2SdkCompositeWrapper`, caía no fallback antigo o tempo
todo. A ausência de erro era esperada independente de a integração da SDK
estar certa ou errada. O smoke test NÃO valida o dispatch da SDK oficial;
precisa ser refeito agora que o gate está corrigido -- e com um
basedir/config isolado, para não repetir o auto-connect relatado na mesma
parte 2.

**Smoke test refeito com o gate corrigido, em diretório isolado** (`id1` copiado,
sem `ezquake/configs`, sem autoexec -- evitou o auto-connect da parte 2):
`-dev -condebug +set vid_renderer 2 +set vid_vulkan_upscaler 3
+set vid_vulkan_renderscale 0.66 +map dm3`. Processo ficou vivo ~35s, memória
estável, `qw/vulkan/pipeline_cache.bin` escrito (pipelines compilaram),
encerramento limpo via `taskkill` sem `/F` (sem crash, sem dialog de erro).
**Mas**: `-condebug` não gerou `qw/qconsole.log` neste ambiente isolado (sem
config pessoal talvez falte algo que normalmente cria o diretório `logs/` ou
ativa o log) -- não consegui capturar o texto real do console, incluindo se
`vulkan: FSR2 SDK diagnostic` apareceu ou não, nem VUIDs específicos do
dispatch oficial. Evidência indireta (não crashou, não travou, pipeline cache
escrito) é positiva mas não conclusiva -- mesma limitação de automação já
registrada em sessões anteriores (`project_desktop_screen_capture_technique.md`,
notas de sessão 2026-08-05 sobre `SendKeys`/injeção de comando não funcionar
neste ambiente).

**Próxima ação real**: validação visual real precisa do Tiago olhando a tela
ao vivo (mesmo protocolo já estabelecido em sessões anteriores) -- captura de
log automatizada não está resolvida neste ambiente de teste isolado. Enquanto
isso, próximo trabalho seguro sem precisar de teste ao vivo: continuar a
auditoria dos itens restantes de Fase 1 do hand-port por leitura de código
(VK_Fsr2DestroyResources sem caller, immediate commands sem checagem de
retorno, barreiras entre frames).

## Checkpoint 2026-10-01 (parte 4) — bugs reais de usage flags corrigidos (hand-port)

Fase 1 catalogava: "várias imagens sem TRANSFER_DST são limpas"/"motion dilatado
copiado sem TRANSFER_SRC". Recontei real contra `allImages[]`
(`VK_Fsr2EnsureImages`, `src/vk_fsr2.c`) -- confirmado: só 3 das 13 imagens
(`fsr2ReconstructedPrevDepth`, `fsr2DilatedMotionPrev`, `fsr2NewLocks`) tinham
`VK_IMAGE_USAGE_TRANSFER_DST_BIT`, mas TODAS as 13 passam por
`vkCmdClearColorImage` na criação (loop logo após a lista) -- usage flag ausente
nas outras 10. Separadamente, `fsr2DilatedMotion` é source de `vkCmdCopyImage`
no ping-pong de fim de `VK_Fsr2Composite` (copiado pra `fsr2DilatedMotionPrev`)
mas só tinha `STORAGE_BIT|SAMPLED_BIT`, sem `TRANSFER_SRC_BIT`.

**Corrigido**: `TRANSFER_DST_BIT` adicionado às 10 imagens que faltava
(`fsr2DilatedDepth`, `fsr2DilatedMotion`, `fsr2LockInputLuma`,
`fsr2PreparedInputColor`, `fsr2DilatedReactiveMasks`, `fsr2History[0/1]`,
`fsr2LockStatus[0/1]`, `fsr2FinalOutput`); `TRANSFER_SRC_BIT` adicionado a
`fsr2DilatedMotion`. Build limpo do zero confirmado (exit 0, sem warning novo),
hash `dcccfd53912f7e5d5f497e88dade938b329ba89a146e1eb99395263d4c21106f`.

Mesma ressalva das correções anteriores: validação de tipo/uso pelo driver é
real (usage flag ausente numa operação de transferência é erro de validação
garantido, não possibilidade), mas efeito visual (se isso causava os artefatos
relatados) não foi testado ao vivo nesta sessão -- sem acesso seguro a teste
interativo depois do incidente de auto-connect registrado no checkpoint anterior.

Afeta só o hand-port (`vid_vulkan_upscaler==1`).

## Checkpoint 2026-10-01 (parte 3) — bug real corrigido no hand-port: sampler2D/R32_UINT mismatch

Fase 1 do plano catalogava (`vk_fsr2_depthclip.comp:19`, referência de linha já
desatualizada por edições anteriores, mas o bug era real): `reconstructedPrevDepth`
é escrito pelos passes reconstruct/lock como `uimage2D` (r32ui), guardando o bit
pattern cru de um float (`floatBitsToUint`) -- técnica padrão do FSR2 real para
`imageAtomicMax` sobre um valor de profundidade (não há atomic max de float em
GLSL). O pass depthclip lia esse mesmo recurso via `sampler2D`/`texelFetch(...).r`
e usava o resultado DIRETO como float, sem `uintBitsToFloat` -- mismatch de
formato/tipo de sampler real (R32_UINT com sampler float é erro de validação/
comportamento indefinido no Vulkan, não só um warning cosmético).

**Corrigido** (`src/vulkan_shaders/vk_fsr2_depthclip.comp`): binding 4 mudado de
`sampler2D` para `usampler2D`; os 4 sites de leitura (`ComputeDepthClip`'s loop
bilinear, `EvaluateSurface`'s 3 amostras verticais) agora decodificam com
`uintBitsToFloat()` antes de passar para `GetViewSpaceDepth`. Compilação
confirmada limpa (316/316, exit 0, sem warning novo) -- hash do exe
`1d6c30da18faa9126bf307d227a753ccafc4354221bc677110a5ed0adf5cdccb`. **Não
testado visualmente** (mesma limitação de automação de input desta sessão) --
mas o bug era um mismatch de tipo real que o compilador de shader teria
recusado se a sintaxe estivesse errada, e a leitura/escrita agora batem de
verdade com o padrão já comprovado nos outros 2 shaders que tocam esse recurso.

Afeta só o hand-port (`vid_vulkan_upscaler==1`) -- a SDK oficial (`==3`) nunca
usa `vk_fsr2_depthclip.comp`, tem seu próprio shader interno já testado pela AMD.

## Checkpoint 2026-10-01 (parte 2) — shim C++/C do SDK FSR2 oficial, caminho novo e paralelo, compila limpo

Leia `AGENTS.md` e `UPSCALING_PLAN.md` primeiro. Continuação da sessão abaixo
("SDK FSR2 oficial AMD compila limpo, baseline registrada").

**O que foi feito**: dispatch real contra a API oficial do FSR2 (`ffxFsr2ContextCreate`/
`ffxFsr2ContextDispatch`/`ffxFsr2ContextDestroy`), não apenas a SDK vendorizada --
isso é Fase 2 do plano, não Fase 0/1. Dois arquivos novos:

- `src/vk_fsr2_sdk.cpp`: C++ puro, só inclui `<vulkan/vulkan.h>` e os headers da
  SDK FSR2 -- NENHUM header do motor. Motivo real, não estético: `q_shared.h` faz
  `#undef true/false` + `typedef enum {false,true} qbool`, que não compila como
  C++ (testado ao vivo, build quebrou com ~100 erros em cascata na 1ª tentativa
  até isolar a causa). Expõe ABI C pura (`VK_Fsr2SdkInit`, `VK_Fsr2SdkComposite`,
  etc) recebendo só tipos Vulkan/primitivos, nunca tipos do motor.
- `src/vk_fsr2_sdk_bridge.c`: C normal, inclui `quakedef.h`/`vk_local.h` como
  qualquer outro arquivo do backend Vulkan, reúne cvars/câmera/frametime e chama
  as funções do `.cpp` acima. É o único arquivo que conhece os dois mundos.

**Caminho NOVO e PARALELO**, não substitui nada: `vid_vulkan_upscaler==3` ("FSR2
(official SDK, experimental)" no menu), `==1` (hand-port, `vk_fsr2.c`) continua
intacto e é o default recomendado até este caminho passar por Fase 7. Integrado
em `vk_main.c` no mesmo ponto dos outros dois caminhos (fora do render pass
principal, mesmo padrão de invalidação de histórico cruzada entre os 3 caminhos
agora -- hand-port, SDK oficial, DLSS -- cada um invalida os outros dois quando
vence o frame). Teardown ligado em `VK_DestroySwapChainFramebuffers`.

**2 bugs reais corrigidos durante a integração, ambos confirmados por erro real
do compilador** (não suposição):
1. Primeira versão do `.cpp` incluía `quakedef.h`/`vk_local.h` direto (como
   `vk_dlss.c` faz, mas aquele é `.c`, não `.cpp`) -- gerou ~100 erros em cascata
   a partir de `q_shared.h`'s `qbool`. Corrigido isolando o `.cpp` de qualquer
   header do motor (ver acima).
2. `cl.frametime` não existe -- `cl` é `clientState_t` (`client.h:619-847`);
   `frametime` pertence a `clientPersistent_t cls` (`client.h:439-582`, campo na
   linha 452). Erro do MSVC apontou a declaração errada até eu contar chaves
   manualmente linha por linha para achar o struct certo. Corrigido para
   `cls.frametime`.

**Build confirmado limpo do zero** (objetos dos 5 arquivos tocados apagados e
reconstruídos): exit code real 0, sem warning novo. Log em
`C:\Users\Tiago\AppData\Local\Temp\claude\E--Projetos-Linux-ezquake-sdl3-vulkan-pr\e44e1012-7f5e-410c-97a8-3986308e6eb5\tasks\bpb7lhjrg.output`.
Hash do exe: `94f151fe8e5dad4d93395a7abc788e54c072bbef169954a41fdb4c2f90003c65`.

**Verificado contra upstream, não suposto**: `FFX_RESOURCE_STATE_UNORDERED_ACCESS`
mapeia para `VK_IMAGE_LAYOUT_GENERAL` no backend VK oficial (conferido lendo
`external/fsr2/src/ffx-fsr2-api/vk/ffx_fsr2_vk.cpp:357-359`), não assumido por
analogia com o hand-port -- a barreira do output image em `vk_fsr2_sdk.cpp` usa
essa transição confirmada.

**Smoke test real feito, interrompido por risco de segurança, não por bug desta
sessão**: lançado `C:\ezquake\ezquakefsrtest.exe -dev -condebug +set vid_renderer 2
+set vid_vulkan_upscaler 3 +set vid_vulkan_renderscale 0.66 +map dm3`. Log
confirmou `vulkan: upscaler diagnostic -- native=1920x1080 scene=1267x712
upscaleActive=1` (o gate do modo 3 foi alcançado), processo ficou vivo ~25s,
sem diagnóstico de falha do SDK FSR2 (`vulkan: FSR2 SDK diagnostic -- ...` não
apareceu) e sem crash. **Mas** a pasta `C:\ezquake` tem config pessoal do Tiago
(`ezquake/configs/config.cfg`/outros) que auto-conecta a um servidor QW real ao
iniciar -- `connect qw.qlash.com.br:28501` disparou sozinho, log mostrou "tiba
entered the game" num servidor público de verdade. Processo morto imediatamente
(`taskkill /F`) ao ser notado. **Não foi causado por este trabalho** -- é
comportamento pré-existente da pasta de teste, já apontado como risco conhecido
em memória de sessão anterior (`project_incident_2026-09-15_live_match_log.md`).
Nenhum crash/erro de validação apareceu antes da desconexão forçada, mas a
janela de observação foi curta e nenhuma validação visual foi feita.

**Próxima tentativa deve usar basedir/config isolados** (ex. `-basedir` separado
sem os `.cfg` pessoais, ou um perfil QW limpo) para evitar reconectar a um
servidor real antes de poder observar o jogo rodando offline por mais tempo.

**NÃO validar como pronto**: compila, linka, sobrevive aos primeiros ~25s sem
crash/erro de validação visível -- nada além disso foi confirmado.
Pendências reais e específicas (ver `UPSCALING_PLAN.md` Fase 2 atualizada):
- Jitter: reusa a tabela Halton-8 fixa do motor (`VK_JitterPixelOffset`), não
  `ffxFsr2GetJitterPhaseCount`/`GetJitterOffset` do SDK oficial -- o plano pede
  isso explicitamente e não foi feito.
- Capacidades de device (`ffxFsr2GetDeviceCapabilitiesVK`) não auditadas contra
  o que a GPU/driver realmente oferece.
- Sinais/convenções de jitter, motion vector scale, depth params -- só
  verificados por leitura de fórmula, nunca em tela.
- RCAS da SDK oficial habilitado via `enableSharpening`, nunca visto rodando.
- vid_restart, resize, troca de preset em runtime -- não exercitados neste
  caminho ainda (hand-port já passou por isso, este não).

**Próxima ação real**: testar ao vivo com `vid_vulkan_upscaler 3` + `vid_restart`,
mesmo protocolo dm3/giro/paredes/partículas da Fase 7, procurando especificamente
por crash imediato (erro de API mal-formado) antes de qualquer julgamento de
qualidade visual. Se crashar, capturar log com `-dev` (validation layers) antes
de tentar debugar sem evidência.

## Checkpoint 2026-10-01 (parte 1) — SDK FSR2 oficial AMD compila limpo, baseline registrada

Leia `AGENTS.md` e `UPSCALING_PLAN.md` primeiro. Trabalho prévio não commitado (submodule
`external/fsr2` pinned em `v2.2.1`/`1680d1edd5c034f88ebbbb793d8b88f8842cf804`, `cmake/fsr2/CMakeLists.txt`,
gate condicional em `CMakeLists.txt`, `tools/build-upscaling.cmd`, `cmake/GitUtils.cmake` com
submodule update seletivo) estava correto e completo — build real confirma.

**Build baseline real executado**: `tools\build-upscaling.cmd` (preset `msvc-x64`,
config `RelWithDebInfo`, Ninja), exit code real **0** (não filtrado, não `tail`).
316/316 alvos. Log completo salvo em
`C:\Users\Tiago\AppData\Local\Temp\claude\E--Projetos-Linux-ezquake-sdl3-vulkan-pr\e44e1012-7f5e-410c-97a8-3986308e6eb5\tasks\bvcla2nze.output`.

- SDK FSR2 oficial AMD (core `ffx_fsr2_api_x64.lib` + backend Vulkan `ffx_fsr2_api_vk_x64.lib`)
  compilou e linkou sem erro. 316 permutações de shader geradas (GLSL→header, várias com
  dezenas de duplicatas detectadas pelo próprio gerador AMD — comportamento esperado do SDK,
  não bug nosso).
- Warnings presentes são todos pré-existentes, em arquivos não tocados por este trabalho
  (`cmodel.c`, `pr2_cmds.c`, `sv_demo_misc.c`, `vm.c`, `demo_extension.c`, `ez_controls.c`,
  `ez_button.c`, `vid_sdl.c`, `gl_drawcall_wrappers.c` — C4267/C5286/C5287/C4090). Nenhum
  warning novo de ABI/protótipo/formato/ponteiro introduzido pela integração FSR2.
- `src/vk_fsr2.c.obj` ainda compila a partir do shim antigo (reimplementação própria,
  não ligado à API oficial ainda) — confirma que Fase 2 (shim C mínimo chamando
  `ffxFsr2GetInterfaceVK`/`ffxFsr2ContextCreate`/`ffxFsr2ContextDispatch`) é o próximo passo
  real, ainda não feito.
- Exe gerado: `build-msvc-x64/RelWithDebInfo/ezquake.exe`,
  sha256 `1321df985278da198f4861e52d8d1fcb62a60542cdea7654793463f34662b9fb`.
  **Não deployado** em `C:\ezquake\ezquakefsrtest.exe` — não autorizado ainda nesta sessão,
  e o shim ainda não usa o SDK oficial, então não há nada novo pra validar visualmente.
- Fase 0 do checklist: baseline de build marcada `[x]`. Cenário offline reproduzível (dm3
  parado/giro/centro/partículas) e confirmação de qual backend está de fato executando
  continuam pendentes — não tocados nesta etapa.

**Próxima ação real**: Fase 2 — criar o shim C mínimo (`vk_fsr2.c` reescrito) que chama as
interfaces oficiais do SDK agora compilado, substituindo a reimplementação própria descrita
na Fase 1 do plano. Rastrear primeiro os problemas de recurso/layout/pool já catalogados em
`UPSCALING_PLAN.md` Fase 1 antes de religar o dispatch, já que o shim antigo tem bugs
confirmados (gate de motion vectors, pool de sampler, layouts, teardown ausente).

## Checkpoint 2026-09-30 — revisão Codex e plano de recuperação

Leia `AGENTS.md` e `UPSCALING_PLAN.md` antes de continuar. Eles prevalecem sobre
as afirmações históricas abaixo de FSR2 completo. Baseline auditada: `89f8648b`,
worktree `E:\tmp\eqvk-upscaling`, branch `feature/vulkan-upscaling`.
O port FSR2 próprio é incompleto e pode nem executar devido ao gate DLSS do motion pass.
Há erros confirmados de recursos/layouts/formato/pool e teardown ausente.
DLSS SR não foi validado em RTX; suporte ao pedido DLSS5 não foi demonstrado.
Plano: SDK AMD oficial via shim C++, entradas temporais corretas, composição nativa,
revisão Streamline, menu/comandos, validação e comparação upstream. Checklist no plano.
Nesta etapa: documentação persistente e ligação do teardown FSR2 ao teardown swapchain.
Build e validação de execução devem ser registrados abaixo conforme realmente realizados.

- Verificação inicial: `git diff --check` passou.
- Correção inicial: teardown FSR2 chamado por `VK_DestroySwapChainFramebuffers`;
  runtime/restarts ainda não validados. Não marcar lifecycle completo no checklist.
- `cmd /c _build_wip.bat` iniciado; configure disparou reconstrução de dependências
  vcpkg (mudança de compiler hash). Build ainda pendente no checkpoint; nenhum
  executável novo foi implantado. Na retomada, conferir processo/build antes de iniciar outro.

Atualizado em: 2026-09-15 (sessão Claude, Windows, `E:\tmp\eqvk-upscaling` — worktree separado da branch `feature/vulkan-upscaling`) — ver seção "Sessão 2026-09-15 — FSR2/DLSS upscaler" logo abaixo para o estado mais recente. As demais seções continuam válidas como histórico.

## Sessão 2026-09-15 — FSR2/DLSS upscaler (espacial + temporal), enviado para `origin/feature/vulkan-upscaling`

Branch nova `feature/vulkan-upscaling`, criada a partir de `feature/sdl3-vulkan-pr`, empurrada para `origin` (PR ainda não aberta). Trabalho todo em `E:\tmp\eqvk-upscaling` (worktree separado da pasta principal). 9 commits, do zero até um upscaler FSR2 temporal completo.

**Contexto:** Tiago pediu para aplicar FSR2 + DLSS no cliente Vulkan. Sessão anterior tinha um começo (scaffolding de render-scale) perdido numa queda de energia — na verdade não tinha sido perdido, só estava sem commit num worktree diferente (`E:\tmp\eqvk-upscaling`), recuperado no início desta sessão.

- **Recuperação e scaffolding (`76cf5c2b`)**: infraestrutura de render-scale (`vid_vulkan_renderscale`, `sceneSize` menor que `imageSize`) e split do HUD para desenhar em resolução nativa separado da composição do mundo, já existentes de sessão anterior não commitada, commitados como estavam.
- **FSR2 espacial (`cb39ce56`)**: `vk_upscale.c`/`vk_upscale.frag` implementam EASU+RCAS (AMD FidelityFX FSR1, reimplementado à mão em GLSL já que o motor usa SPIR-V pré-compilado, sem pipeline de include de texto pros headers oficiais). Conectado no composite pass real substituindo o blit simples quando o upscaler está ativo. Menu novo em Sistema > Vídeo: "Upscaler (Vulkan)" (off/FSR2/DLSS) e "Render Scale (Vulkan)", mesmo padrão do antilag existente. DLSS ainda não implementado — roda o mesmo shader FSR2 por enquanto (precisa do SDK proprietário da NVIDIA).
- **Dois rounds de code-review automático** corrigiram: descriptor set liberado enquanto ainda em uso (corrompia frame), falta de barreira de sincronização entre passes de outline/normals, alocação por-frame desnecessária, vazamento de memória no resize, overflow silencioso em build release, várias limpezas de código morto/duplicado.
- **Bugs achados ao vivo por Tiago testando**: franja roxa/magenta nas bordas quando upscaler ligado (clamp de cor só olhava luminância, deixando vermelho/azul vazar) — corrigido. Cvars pareciam "não fazer nada" mesmo depois de `vid_restart` — causa raiz real: cvar `CVAR_LATCH_GFX` só aplica o valor pendente quando o C re-registra o cvar, e os dois estavam registrados no lugar errado (`VID_RegisterCvars`, roda só uma vez, não em `VID_RegisterLatchCvars`, que roda a cada `vid_restart`) — corrigido movendo o registro pro lugar certo.
- **FSR2 temporal completo (`c4690488` + `9cc2e5cb` + `51eeba7d`)**: motion vectors reconstruídos via reprojeção de câmera (depth atual + matriz de view-projection do frame atual e anterior, sem precisar de motion vector por-objeto), jitter sub-pixel na projeção (sequência de Halton), buffer de histórico de cor persistente, resolve temporal com clamp de vizinhança anti-ghosting. Autorização explícita do Tiago pra fazer tudo de uma vez sem parar pra perguntar. Um segundo review encontrou 3 bugs reais sérios, todos corrigidos: (1) race de sincronização entre frames da GPU no buffer de histórico/matrizes (resolvido com ping-pong de 2 slots), (2) reentrada em modo multiview/split-screen corrompia o estado temporal (resolvido com guard), (3) checagem de sky/fundo assumia `gl_reverse_z 1` sem checar o cvar real (resolvido lendo `glConfig.reversed_depth`).

**Build verificado limpo em toda etapa** (311/311 alvos), mas **o upscaler temporal (motion vectors) nunca foi testado visualmente**. É matemática 3D real (inversão de matriz, reconstrução de posição via depth) com fallbacks defensivos em cada etapa, mas pode ter erro sutil só visível na tela.

**Pendente pra próxima sessão**: Tiago testar com `vid_vulkan_upscaler 1` + `vid_vulkan_renderscale 0.66` + `vid_restart`, olhando especificamente por rastro/fantasma atrás de objetos em movimento (sintoma de erro na reprojeção). Exe de teste em `C:\ezquake\ezquakefsrtest.exe` (separado do `ezquake.exe` de produção). Depois de validado: abrir PR de `feature/vulkan-upscaling` pra `feature/sdl3-vulkan-pr`, e decidir se investe em DLSS real (SDK Streamline da NVIDIA, links já anotados em `vk_upscale.c`) ou motion vectors por-objeto de verdade (mais precisos que a reprojeção de câmera atual).

## Sessão 2026-08-06/07 (Claude, Windows — drawflat Vulkan corrigido, buffers dinâmicos em heap rápido, VkQueryPool no timerefresh)

Commit `88d916ba` enviado para `origin/feature/sdl3-vulkan-pr`. Build Release limpo gerado (`_build_release.bat`, config `Release`, sem debug info) e deployado em `C:\ezquake\ezquake.exe`.

- **Bug de cor no drawflat (Vulkan) corrigido de verdade.** `r_wallcolor`/`r_floorcolor` não apareciam com `r_drawflat_mode 0` sob Vulkan (funcionavam no GL). Uma sessão anterior tinha marcado isso como "não é bug, era cvar errado" — errado, reaberto e investigado com side-by-side real (mesmos cvars nos dois renderers). Causa real: `vk_world.c` escrevia a flag "esta superfície é drawflat" em `push.causticsEnabled` (offset 172 do push-constant), mas `vk_world_flat.frag` lê esse dado de um campo dele mesmo chamado `drawflatColor`, que fica no offset 124 — campo que na struct C tem esse mesmo nome e nunca era escrito (ficava zerado pelo `memset`). Comentário antigo dizia que os dois offsets coincidiam; estava errado, confirmado calculando os offsets campo a campo (std430: mat4=64B, vec4=16B, float=4B). Fix: trocar para `push.drawflatColor = ...`. Confirmado ao vivo com screenshot lado a lado GL vs Vulkan.
- **World index buffer (e demais buffers `once_per_frame`/`reuse_per_frame`) agora preferem o heap `DEVICE_LOCAL | HOST_VISIBLE`** que GPUs discretas expõem (maior com Resizable BAR, presente na RX 6800 XT desta sessão), com fallback automático para `HOST_VISIBLE | HOST_COHERENT` puro se o device não tiver esse heap. Não usa staging/`vkCmdCopyBuffer` — a doc do Vulkan confirma que isso seria contraproducente para um buffer reescrito todo frame (adicionaria uma cópia GPU extra em vez de eliminar overhead); a abordagem certa pra esse padrão é esse heap combinado, que permite `memcpy` direto como hoje. Novo: `VK_BufferPreferredMemoryType()` (vk_buffers.c) e `VK_CreateBufferResourceWithSelector()` (vk_resources.c). Testado ao vivo, sem crash, sem regressão visual.
- **`VK_TimeRefresh` agora mede tempo de GPU real via `VkQueryPool`** (timestamps), além do wall-clock antigo, porque a métrica antiga incluía acquire/present e não era comparável à do GL (que nunca chama SwapBuffers nas 128 iterações). Reset do query pool tem que rodar fora de qualquer render pass ativo (confirmado na spec) — feito uma única vez via `VK_BeginImmediateCommands` antes do loop, não a cada iteração.
- Também desta sessão: colapso do double-loop de draw do mundo em `vk_world.c` (uma única partição opaco/blended, não duas varreduras com `continue`), e fix de um bug recorrente de corrupção de descriptor set (`VkDescriptorSet was destroyed or updated without UPDATE_AFTER_BIND`) — `VK_WorldFlatSkyDescriptorSet` atualizava o set a cada superfície flat/sky em vez de uma vez por frame.

**Pendente pra próxima sessão: Task #4 — migrar `vk_world.c` para bindless** (textured/lightmapped/alpha_textured/flat/overlay), usando a mesma infraestrutura já provada em `vk_aliasmodel.c`/`vk_texture.c` (`VK_TextureBindlessDescriptorSetLayout`/`VK_TextureBindlessDescriptorSet`). Ainda não iniciada — Tiago pediu pra decidir a estratégia (um pipeline por vez / tudo de uma vez / plano detalhado primeiro) na próxima sessão. Risco real de regressão: é a mesma área de código do bug de corrupção de descriptor set corrigido nesta sessão.

### Parte 6 — Teste ao vivo com Tiago: outline funcionando (bug de ruleset), hang real no vid_restart do preset eyecandy, CORRIGIDO, tudo commitado e enviado

Sessão de teste ao vivo real com Tiago olhando a tela (não automação sozinha). Resultado:

- **Cáusticas, drawflat (todas variações), vsync adaptativo, screenshot**: confirmados funcionando.
- **Outline de mundo**: inicialmente "continua sem funcionar" mesmo com `gl_outline 3`. Causa raiz real, achada e corrigida: `R_DrawWorldOutlines()` (`src/r_brushmodel_surfaces.c`, função COMPARTILHADA pelos 3 backends, bug pré-existente, não introduzido nesta sessão) chamava `RuleSets_DisallowModelOutline(NULL)` — o gate de outline de *modelo*, que para `mod==NULL` exige cheats/demo playback — em vez de `RuleSets_AllowEdgeOutline()`, o gate correto de outline de *mundo/edge* (só restringe ruleset `rs_qcon`, sem exigir cheats). O GLM's `GL_FramebufferStartWorldNormals` já usava a função certa; só essa função compartilhada estava com a função errada. Corrigido — outline de mundo agora funciona com `gl_outline 3` sozinho, sem precisar de `devmap`/cheats, igual ao GL.
- **Mipmap (`gl_texturemode GL_NEAREST` vs default)**: Tiago não conseguiu perceber diferença mesmo testando corretamente (de longe, corredor/distância). Investigação de código não achou bug — lógica de `hasMipmap`/`maxLod` parece correta. Aceito como "sem diferença perceptual relevante que valha mais investigação agora" — pode ser efeito real mas sutil demais pra notar sem comparação A/B por screenshot.
- **FPS baixo notado por Tiago**: era o parâmetro `-dev` (usado em todos os testes de sessão anterior) ativando as validation layers do Vulkan, que têm overhead conhecido — comportamento intencional/pré-existente, não uma regressão. Confirmado: sem `-dev`, FPS normal.
- **Bug real e grave achado por Tiago**: trocar de preset gráfico pra "high eyecandy" (`exec cfg/gfx_gl_higheyecandy.cfg`, que dispara `vid_restart` no final) **travava o processo** (hang, não crash — `Get-Process` mostrava `Responding: False` e nunca mais respondia; log parava logo após "Ping tree has been created", sem nenhum erro). Não acontecia antes do trabalho desta sessão. Reproduzido de forma confiável.
  - **Causa raiz encontrada com certeza** (Opus, leitura de código): os 4 recursos novos de composição do outline em `src/vk_draw.c` (`worldOutlinePipeline`, `worldOutlinePipelineLayout`, `worldOutlineDescriptorSetLayout`, `worldNormalsSampler`) nunca eram destruídos em lugar nenhum — nem em `VK_HudResourcesShutdown()` (que destrói os equivalentes do post-process, `postProcessPipeline`/`postProcessSampler`/etc, mas parava antes de chegar nos do outline). Isso causava dois problemas simultâneos: (a) vazamento de handle a cada `vid_restart` (mesmo pipeline/layout/sampler nunca liberado antes do `VkDevice` morrer), e (b) mais grave — como `worldOutlinePipeline` nunca voltava a `VK_NULL_HANDLE`, o early-out em `VK_WorldOutlineCreatePipeline()` (`if (worldOutlinePipeline != VK_NULL_HANDLE) return true;`) fazia o primeiro frame pós-restart reusar pipeline/layout/sampler/set do **device antigo já destruído**, travando o driver na submissão do command buffer — sem gerar nenhum erro de validação (não estávamos com `-dev` no teste), só hang silencioso. Bate exatamente com o sintoma: log mostra `vid_restart` completando com sucesso, hang no frame seguinte.
  - **Fix aplicado**: adicionado o par de destruição faltante em `VK_HudResourcesShutdown()`, mesmo padrão guarda-`VK_NULL_HANDLE` já usado pros recursos do post-process, logo depois deles.
  - **Testado ao vivo por Tiago depois do fix**: trocou o preset manualmente, funcionou sem travar. Confirmado resolvido.
- **Nota de processo**: durante os testes automatizados de retry (antes do Tiago testar manualmente), sobrou uma tecla "presa" via `keybd_event` que fez o jogo parecer estar "mudando de preset sozinho" por um instante — resolvido encerrando o processo de teste; não era um bug novo, resíduo da própria automação.

**Commitado e enviado** (autorizado explicitamente por Tiago): 2 commits em `feature/sdl3-vulkan-pr` — `9452d70a` (SDL2→SDL3 finalização) e `c05de798` (cáusticas + outline + fixes de cvar + screenshot), mais um terceiro commit com o fix do hang do vid_restart. Push feito para `origin` (atualiza a PR #1145 existente) e sincronizado com o repositório separado `tibazera/ezquakevulkan`.

### Parte 5 — Bug real de sincronização achado ao tentar validar visualmente: screenshot Vulkan sempre gerava erro de validação, CORRIGIDO

Ao tentar validar visualmente os 4 fixes da Parte 4 (Tiago liberou o teclado, "SendKeys"/`AppActivate` confirmados funcionando de verdade quando ninguém mais mexe no teclado ao mesmo tempo — ver nota de método abaixo), todo `screenshot` no Vulkan disparava um erro de validação real: `vkQueueSubmit(): pSubmits[0] performs a layout transition on presentable VkImage ..., but the image has not been acquired from VkSwapchainKHR ... (either never or since the last present operation)`.

**Causa raiz**: `VK_Screenshot` (`vk_main.c`) lia `vk_options.swapChain.images[vk_options.frame.imageIndex]` diretamente e assumia que essa imagem estava em `PRESENT_SRC_KHR`. Mas `frame.imageIndex` só é atualizado dentro de `VK_BeginFrame` (via `vkAcquireNextImageKHR`) e o comando `screenshot` roda fora do ciclo de frame (é um comando de console, disparado do loop de eventos) — segundo o spec Vulkan, uma imagem de swapchain só pertence à aplicação entre ser devolvida por `vkAcquireNextImageKHR` e ser liberada de volta por `vkQueuePresentKHR`; fora dessa janela (que é exatamente onde o comando `screenshot` roda), tocar na imagem é hazard real, não só um warning cosmético.

**Fix aplicado** (`vk_main.c`, `vk_resources.c`, `vk_local.h`): `VK_Screenshot` agora faz seu próprio ciclo dedicado de `vkAcquireNextImageKHR` → copia o conteúdo (é uma leitura do que já estava sendo exibido, então captura exatamente o que um screenshot deveria capturar, sem desenhar nada novo) → `vkQueuePresentKHR` de volta sem modificação (invisível ao usuário, só devolve a imagem pro swapchain como o spec exige). Nova função `VK_EndImmediateCommandsAfter(cmd, waitSemaphore, waitStage)` em `vk_resources.c` (variante de `VK_EndImmediateCommands` que aceita um semáforo de espera no submit, necessário pra garantir que a cópia só rode depois que o acquire sinalizar de verdade).

**Confirmado corrigido**: testado ao vivo, `screenshot` gerando `Wrote ezquakeXXX.jpg` sem nenhum erro de `image has not been acquired` no log (antes do fix, toda captura gerava esse erro).

**Limitação de automação encontrada, ainda não resolvida**: em várias tentativas seguidas (algumas com restart completo do processo, que resolve o problema de foco "preso" que aparece depois de várias trocas de janela via `AppActivate`/`SendKeys`/`ESC` — confirmado que reiniciar do zero é a forma confiável de recuperar isso), a tecla **W** enviada via `keybd_event` pra andar sempre acabou sendo capturada como **texto de chat** (`TIBA: W` aparece no log/HUD) em vez de mover o personagem — mesmo em tentativas onde o mapa foi carregado 100% via linha de comando (`+gl_caustics 1 +map aerowalk`, sem nenhum `~`/comando de console antes), o console aparecia aberto no screenshot seguinte. Não foi possível determinar a causa exata sem ver a tela ao vivo (hipótese não confirmada: o jogo pode abrir o console automaticamente após certos eventos de carregamento de mapa, ou o "W" físico via `keybd_event` — diferente do `SendKeys` de texto — está sendo roteado de forma diferente por alguma razão de foco/hook de teclado). **Resultado**: não consegui validar visualmente (por screenshot real da cena, sem o console cobrindo a tela) se a cáustica aparece corretamente na água, nem o efeito do `gl_texturemode GL_NEAREST`. Ambos os 4 fixes da Parte 4 continuam confirmados via log/estabilidade (compilam, carregam mapa, não crasham, e o de `vid_vsync -1` tem confirmação funcional direta no log), mas a confirmação visual fica pendente — melhor Tiago validar direto olhando a tela, ou uma sessão futura tentar de novo com outra estratégia de automação de input (talvez um `.cfg` de bind que force `wait`-free movement, ou investigar por que o console está reabrindo sozinho).

## Sessão 2026-08-05 (Claude, Windows — port SDL2→SDL3 finalizado + gaps Vulkan de paridade vs. GLC/GLM)

**Contexto**: sessão longa cobrindo 3 frentes em sequência: (1) fechar de vez a migração SDL2→SDL3 (itens que ficaram pendentes de sessões anteriores), (2) criar um repositório novo `tibazera/ezquakevulkan` (privado, GitHub) como espaço isolado pra acompanhar as diferenças do nosso fork Vulkan+SDL3 contra o upstream `QW-Group/ezquake-source`, e (3) trabalho autônomo overnight (autorizado explicitamente por Tiago antes de dormir) implementando e testando ao vivo os 2 gaps de paridade funcional achados na comparação Vulkan vs. GLC/GLM: cáusticas subaquáticas (`gl_caustics`) e outline de mundo (`gl_outline` bit 2).

### Parte 4 — Auditoria de cvars multi-valor (0/1/2/3...) no Vulkan + segunda passada SDL3, 4 gaps reais corrigidos

Pedido explícito do Tiago (após reparar que meus testes anteriores do Gap 1 nunca tinham ativado `gl_caustics 1` de verdade, só rodado com o default desligado): investigar se o port GL→Vulkan e a migração SDL2→SDL3 deixaram passar despercebidos valores específicos de cvars com múltiplos inteiros/enum (não só liga/desliga). Consultado o Opus de novo, achou **4 gaps reais confirmados** (a cvar É lida no Vulkan, mas nem todo valor produz o efeito certo) e 0 gaps na segunda passada de enums SDL3 (área já bem coberta por sessões anteriores). Todos os 4 corrigidos nesta sessão:

1. **`vid_framebuffer_fxaa` (0-17) — CORRIGIDO.** GL mapeia pra 17 presets reais do header FXAA 3.11 da NVIDIA (`GL_FramebufferFxaaPreset`, `gl_framebuffer.c:1009-1017`); o Vulkan colapsava tudo em bool (`push.fxaaEnabled = ... != 0`). Como o post-process Vulkan usa uma implementação própria simplificada (não o header FXAA real — ver comentário já existente em `vk_post_process.frag` explicando por quê), replicar 17 variantes de shader não é viável. Solução: novo campo `fxaaQuality` (float 0-1, derivado de `preset/17.0f`) controla continuamente os dois parâmetros reais do algoritmo simplificado — limiar de detecção de borda (`0.1→0.05`) e força do blend (`0.50→1.00`). Não é bit-a-bit idêntico ao FXAA real por preset, mas agora o valor da cvar produz diferença real e monotônica, em vez de nada. Arquivos: `vk_draw.c` (struct `vk_post_process_push_t` + `VK_PostProcessComposite`), `vulkan_shaders/vk_post_process.frag`.
2. **`gl_texturemode` (6 modos GL) — CORRIGIDO.** Os 2 modos sem mipmap (`GL_NEAREST`/`GL_LINEAR`) não desligavam mipmapping de verdade no Vulkan — `VK_FilterFromMinification` mapeava `nearest` e `nearest_mipmap_nearest` pro mesmo par `(filter, mipmapMode)`, e o sampler sempre usava `maxLod = VK_LOD_CLAMP_NONE` (mipmap completo). Corrigido: `VK_FilterFromMinification` agora também retorna `hasMipmap` (false só para os 2 modos sem sufixo `_mipmap_`), e o cache de sampler (`VK_TextureCachedSampler`/`VK_SamplerCacheIndex`) ganhou essa dimensão extra — quando `!hasMipmap`, `maxLod = 0.25f` (trick padrão pra travar no mip 0). Cache dobrou de tamanho (`VK_SAMPLER_CACHE_SIZE` agora tem um fator `* 2 /* hasMipmap */` a mais). Arquivo: `vk_texture.c`.
3. **`vid_vsync -1` (adaptive) — CORRIGIDO E CONFIRMADO NO LOG.** GL trata `-1` como um terceiro caso (`SDL_GL_SetSwapInterval(-1)`); o Vulkan tratava qualquer valor não-zero (incluindo `-1`) como `r_swapInterval.integer` truthy → sempre `FIFO_KHR` puro, nunca alcançando `FIFO_RELAXED_KHR` (o equivalente Vulkan real de vsync adaptativo, que já estava na lista de presentation modes preferidos mas nunca era alcançado). Corrigido com uma segunda lista de preferência (`preferredModesAdaptive`) só com `FIFO_RELAXED`→`FIFO` como fallback, escolhida quando `r_swapInterval.integer < 0`. **Testado ao vivo e confirmado no log**: `vulkan: selected present mode 3` (FIFO_RELAXED) com `vid_vsync -1` — antes desse fix teria sido mode 2 (FIFO puro). Arquivo: `vk_physical_devices.c`.
4. **`vid_gammacorrection` (0/1/2) — CORRIGIDO.** GL distingue "tentar sRGB, aceitar fallback" (1) de "exigir sRGB, rejeitar o device se não tiver" (2) — ver a escada `vid_options[]` em `vid_sdl.c`. O Vulkan tratava 1 e 2 de forma idêntica (mesmo `req_color_space`, mesmo fallback silencioso pra qualquer colorspace disponível). Corrigido: quando `vid_gammacorrection.integer == 2` e o fallback de formato não encontrar um colorspace sRGB exato, `VK_PhysicalDeviceSwapChainCompatible` agora retorna `false` — o que já faz `VK_SelectPhysicalDevice` rejeitar aquele device específico (`continue` pro próximo, comportamento pré-existente, não modificado) em vez de aceitar silenciosamente um colorspace errado. Testado ao vivo com `vid_gammacorrection 2`: o device AMD RX 6800 XT tem suporte sRGB normal, então não foi rejeitado — não foi possível confirmar visualmente o caminho de rejeição sem um device sem suporte sRGB à mão, mas a lógica foi lida com cuidado e o `continue` do call site já era testado/funcional antes desta mudança. Arquivo: `vk_physical_devices.c`.

**Duas cvars que motivaram a investigação original (`gl_outline`, `r_drawflat`/`r_drawflat_mode`) já estavam corretas e completas** — confirmado lendo o código, o Opus não achou gap novo ali (o outline bit 2/mundo é o trabalho ainda incompleto de integração, não um gap de "valor não tratado", ver Gap 2 acima na Parte 3).

**Descoberta importante sobre o protocolo de teste desta sessão**: `SendKeys`/`AppActivate`/`PostMessage(WM_CHAR)` — testados os 3 — **nenhum consegue injetar texto no console do ezQuake** rodando localmente nesta máquina (confirmado repetidamente com um `echo MARCADOR_UNICO` de verificação que nunca apareceu no log, mesmo com a janela em foco e ninguém mais mexendo no teclado). O jogo deve ler input via SDL3 de baixo nível (raw/DirectInput-like), que ignora eventos sintéticos de janela do Win32. **Método que funciona de verdade**: passar tudo via linha de comando na hora de lançar o processo (`+cvar valor +map nome` encadeados, ou `+exec arquivo.cfg` com os comandos dentro) — confirmado funcionando repetidas vezes (título da janela mostra o mapa carregado, cvars aplicam, log mostra o efeito quando há um). **Limitação real**: sem forma de injetar comando depois que o processo já está rodando, não dá pra tirar `screenshot` depois que o mapa carregou dentro do mesmo processo (testado com `wait`/`cl_maxfps 1` dentro do `.cfg` pra dar tempo real antes do `screenshot` — não funcionou, `wait` é tick de simulação, não tempo de parede, e o screenshot sempre saiu cedo demais, ainda na tela de loading). Validação visual real (a cáustica/mipmap parecendo certos) **precisa de alguém olhando a tela ao vivo** — não é algo que consegui automatizar sozinho nesta sessão.

Todos os 4 fixes compilam limpo e foram testados ao vivo quanto a estabilidade (carrega mapa, não crasha, roda) — 1 deles (`vid_vsync -1`) tem confirmação funcional direta no log (`selected present mode 3`), os outros 3 só têm confirmação de "não quebra", não de "produz o resultado visual esperado". **Nada commitado ainda.**

### Parte 1 — Port SDL2→SDL3 finalizado

Além dos fixes já registrados em sessões anteriores (áudio init check, `refresh_rate` float, IME `SDL_SetTextInputArea`), fechado nesta sessão:

- **Migração completa dos nomes SDL2 legados** (joystick, eventos, GL context, mutex/semáforo, atomic, CPU count) para os nomes nativos SDL3, em todos os arquivos que ainda dependiam do shim `SDL_oldnames.h`. `SDL_ENABLE_OLD_NAMES` **removido** do `CMakeLists.txt` — o build compila/linka limpo sem o shim, confirmando que não sobrou nenhum resíduo.
- **Todos os ~34 includes SDL do projeto (26 arquivos) prefixados com `SDL3/`** (`#include <SDL3/SDL.h>` etc), e o hack `find_path(... PATH_SUFFIXES SDL3)` removido do CMake — o target `SDL3::SDL3-static` do vcpkg já resolve o include path sozinho.
- **Rename de arquivos**: `vid_sdl2.c`→`vid_sdl.c`, `in_sdl2.c`→`in_sdl.c`, `sys_sdl2.c`→`sys_sdl.c` (via `git mv`, preservando detecção de rename), `CMakeLists.txt` e os 2 comentários que citavam o nome antigo atualizados.
- **`build-linux.sh` corrigido**: ainda listava pacotes SDL2 reais (`libsdl2-dev`, `SDL2-devel`, `sdl2`) em 4 distros — trocado pelos equivalentes SDL3.
- **Cvar novo `joy_id`** (`SDL_JoystickID` estável, default `-1` = desativado) adicionado em paralelo ao `joyindex` existente (índice posicional, semântica SDL2, mantida intocada) — decisão explícita do Tiago de não quebrar configs salvos. Quando `joy_id >= 0`, tem prioridade e resolve por ID real (`IN_OpenJoystickId`), resiliente a hot-plug. Documentado em `help_variables.json`.
- Textos "SDL2" corrigidos para "SDL3" em `README.md` e `help_commands.json` (4 descrições de comando visíveis ao jogador).
- `SDL_syswm.h` morto removido de `vid_sdl.c` (nunca existiu no SDL3, confirmado que o `#if SDL_MAJOR_VERSION < 3` nunca disparava e nada dependia dele).
- **Fix de bug real encontrado numa revisão do Opus**: `menu_options.c:533` gravava `refresh_rate` (float) direto no cvar `r_displayRefresh` (int) sem arredondar — mesmo bug já corrigido em `vid_sdl.c` em sessão anterior, só que esse site tinha ficado de fora. Corrigido com o mesmo padrão `(float)(int)(x + 0.5f)`.
- **Fix de robustez**: `text_input_area_set` (flag estática em `IN_UpdateTextInputState`) não sobrevivia a `vid_restart` — removida, a função agora chama `SDL_SetTextInputArea` sempre que necessário em vez de cachear estado.

**Não commitado ainda** — tudo em diff local no worktree.

### Parte 2 — Repositório novo `tibazera/ezquakevulkan`

Criado no GitHub (privado, independente, sem histórico do upstream — snapshot do estado atual deste worktree como primeiro commit). **Cuidado ao reproduzir**: durante a criação, um `git init` acidental quase reafirmou o link de worktree do repo Android por engano (`.git` como arquivo-ponteiro tinha sido copiado junto pelo robocopy do worktree de origem) — identificado e corrigido antes de qualquer push; nenhum dano real aos worktrees existentes. Lição: ao clonar/copiar um worktree linked (não o repo principal) pra virar a base de um repo novo, sempre checar e remover o arquivo `.git` (ponteiro) copiado junto antes de rodar `git init` no destino.

### Parte 3 — Auditoria comparativa Vulkan vs. GLC/GLM (Opus) — 2 gaps reais encontrados

Não é regressão do port SDL3 (código de input/menu/keys confirmado byte-idêntico ao upstream) — são gaps de **escopo** do backend Vulkan, que ainda não implementava 2 coisas que GLC/GLM têm:

1. **Cáusticas subaquáticas (`gl_caustics`)** — `vk_world.c` descartava o parâmetro (`(void)caustics;`).
2. **Outline de mundo (`gl_outline` bit 2)** — nunca implementado no Vulkan (só outline de *modelo* existe, `vk_aliasmodel.c`). Já era gap conhecido de sessões anteriores, confirmado ainda válido.

Consultado o Opus de novo pra projetar solução implementável dos dois (não só identificar) — relatório completo com plano passo a passo pra cada um, resumido abaixo junto do que foi de fato implementado.

### Gap 1 — Cáusticas: IMPLEMENTADO E TESTADO AO VIVO, funcionando

Seguido o plano do Opus quase à risca:

- **`src/vk_world.c`**: campo `padding` (último) de `vk_world_push_t` reaproveitado como `causticsEnabled` (struct continua 176 bytes, sem crescer — só esse slot era pura folga de alinhamento, e `vk_world_flat`'s próprio bloco GLSL local já tratava esse offset como `drawflatColor`, então o C-side que escrevia nele foi renomeado mas continua escrevendo no mesmo byte-offset). Novo par `VK_WorldCausticsTextureReady()`/`VK_WorldCausticsDescriptorSet()` espelhando o padrão já existente de `VK_WorldDetailTextureReady`/`VK_WorldDetailDescriptorSet`, com fallback pra `solidwhite_texture` (nunca `VK_NULL_HANDLE`, layout do pipeline fica fixo). `vk_world_draw_t` ganhou campo `caustics`. **Decisão de design seguindo GLM** (não GLC): a granularidade de "quem recebe cáustica" é 100% decidida no fragment shader pelo bit `EZQ_SURFACE_UNDERWATER` já existente no `inFlags` por-vértice (gravado desde sempre em `vk_main.c:148`, só nunca consumido) — o parâmetro `caustics` por-modelo de `VK_DrawBrushModel` continua ali só pra bater a assinatura do `renderer_api_t` compartilhado, mas é ignorado na composição (documentado com comentário explicando a escolha).
- **Descriptor sets**: adicionado mais um set (cáustica) nos pipelines `worldTextured` (2→3), `worldLightmapped` (3→4, no limite garantido pelo spec Vulkan de `maxBoundDescriptorSets`, sem checagem de device runtime — igual ao padrão já existente pros outros pipelines, nenhum já checava isso) e `worldAlphaTextured` (2→3). `worldAlphaTextured` **não tinha o atributo `inFlags` no vertex input** (só position/texcoord/detail_coords) — adicionado (`VkVertexInputAttributeDescription[4]`, novo location 3 lendo `vbo_world_vert_t.flags`), e o `.vert`/`.frag` correspondentes atualizados pra repassar/ler o flag. Isso importa porque é justamente o pipeline blended (água/lava com `r_wateralpha`) onde a cáustica é mais visível no Quake original.
- **Shaders** (`vk_world_textured.frag`, `vk_world_lightmapped.frag`, `vk_world_alpha_textured.{vert,frag}`): novo `sampler2D causticsTexture[2]` no set seguinte ao de detail; lógica de UV animada + blend multiplicativo idêntica à referência GLM (`draw_world.fragment.glsl`, fator `-0.1234375` = `-3*(0.5/64)`), aplicada **depois** do detail texture (mesma ordem do GLM), gateada por `causticsEnabled > 0.5 && (inFlags & EZQ_SURFACE_UNDERWATER) != 0u`.
- **Asset**: `underwatertexture` (`textures/water_caustic`) já era carregado em código 100% compartilhado (`r_rmisc.c:70`, `R_InitOtherTextures`) — nada novo a carregar, só passou a ser referenciado pelo lado Vulkan.

**Testado ao vivo** (protocolo pedido pelo Tiago: `map <nome>`, esperar ~20s, segurar W ~4-5s pra andar, checar log): ciclo completo em `dm3` → `aerowalk` → `schloss` → `ztndm3` → `dm3` de novo, com `-dev -condebug +set vid_renderer 2`. Build compila limpo em todas as iterações, processo estável e responsivo em todos os mapas, sem crash/TDR. `dm6` não estava disponível no basedir local de teste (só `dm3` do id1, mais `aerowalk`/`schloss`/`ztndm3` de PK3 extra) — pendente testar se algum dia esse mapa for adicionado ao ambiente.

**Achado importante durante o teste, não é bug da cáustica**: apareceu um erro sério em cascata no log (`VkDescriptorSet ... was destroyed or updated without UPDATE_AFTER_BIND`, seguido de `commandBuffer must be in the recording state` repetido ~220-240x por sessão). **Confirmado via teste A/B com `git stash`** (rebuild sem as mudanças de cáustica, mesmo teste em `aerowalk`) que esse hazard é **pré-existente**, idêntico em contagem com e sem a mudança — é o mesmo hazard de sincronização de descriptor-set já documentado extensivamente na seção "Sessão 2026-07-23 (Claude, Windows, parte 2 — bug de modelos pretos/TDR)" mais abaixo (causa raiz #3/#4, upload de textura e free de descriptor set síncronos em frame ativo). **Não é causado nem agravado pela cáustica** — meu código de cáustica não introduziu nenhuma ocorrência nova. Continua sendo a mesma investigação em aberto de sessões anteriores (mipmap NPOT / descriptor set lifetime), não escopo deste trabalho.

**Ainda não commitado** — diff local, pronto pra revisão. Recomendação: revisar visualmente (Tiago vendo a tela de verdade) antes de commitar — o teste automatizado confirma "não quebrou nada e não crasha", mas não confirma que a cáustica está visualmente correta (cor, animação, intensidade) sem alguém olhando a água de um mapa que a tenha.

### Gap 2 — Outline de mundo (`gl_outline` bit 2): PARCIALMENTE IMPLEMENTADO, BLOQUEADO num ponto de integração de fluxo — não testado ao vivo, requer trabalho adicional antes de tentar rodar

**Decisões de arquitetura tomadas e já implementadas** (seguindo o plano do Opus, com pesquisa web adicional confirmando a técnica de normal antes de codar — ver nota sobre `dFdx/dFdy` abaixo):

- **Rejeitada** a réplica literal do MRT do GLM (segundo color attachment no render pass principal) — mexeria na matriz MSAA×post-process já existente (4 combinações). Risco desproporcional.
- **Rejeitada** a alternativa "edge-detect só no depth buffer existente" — a imagem de depth atual não tem `SAMPLED_BIT`, é N-sample com MSAA, perde cantos entre superfícies coplanares.
- **Implementado**: render pass **separado e independente**, `vk_renderpass_worldnormals` (`src/vk_renderpass.c`), single-sample por construção, 2 attachments (color RGBA16F via `VK_WorldNormalsFormat()`, depth próprio single-sample — não reaproveita `vk_options.swapChain.depthImage`, que é N-sample quando MSAA está ativo). `LOAD_OP_CLEAR` obrigatório no color (composição distingue "desenhado aqui" de "nada aqui" pelo canal alfa).
- **Implementado**: recursos de imagem/framebuffer/descriptor pool em `src/vk_swapchain.c` (`VK_CreateWorldNormalsResources`/`VK_DestroyWorldNormalsResources`), um conjunto por imagem de swapchain (mesmo padrão do post-process), alocados **incondicionalmente** junto do resto (`VK_CreateSwapChainFramebuffers`/`VK_DestroySwapChainFramebuffers`) porque `gl_outline` é unlatched. **Testado ao vivo que a alocação em si não quebra nada** (`map dm3`, sem os erros novos de criação de recurso — só o hazard pré-existente de sempre, ver Gap 1).
- **Implementado**: 3 shaders novos —
  - `src/vulkan_shaders/vk_world_normals.vert`/`.frag`: redesenha a geometria de mundo (só `inPosition`, sem textura/lightmap) e escreve `vec4(normal, depth)` no color attachment. A normal é reconstruída no fragment shader via `normalize(cross(dFdx(worldPos), dFdy(worldPos)))` — **pesquisado ativamente na web antes de implementar** (não só seguindo o plano do Opus às cegas): confirmado que essa é a técnica padrão e correta pra flat shading de geometria totalmente plana (que é o caso do BSP do Quake) — dá a normal EXATA do triângulo, não uma aproximação, ao contrário das técnicas de reconstrução de normal a partir de depth buffer (que são pra quando não se tem acesso à geometria original, não é o nosso caso). Fontes: artigo sobre reconstrução de normal via depth (usado só como comparação/descarte) e discussões de fórum Khronos confirmando `cross(dFdx(worldPos), dFdy(worldPos))` como o idiom padrão. `depth` usa `distance(worldPos, cameraPos)/zFar` em vez do `abs(viewZ/zFar)` do GLM (o push constant do Vulkan já é a matriz MVP combinada, sem MV separado disponível no shader pra recuperar Z de view space) — ambos são medidas de profundidade monotônicas ao longo do raio de visão, o que é tudo que o teste de diferença finita do shader de outline precisa; não é bit-a-bit idêntico ao GLM mas deve produzir resultado visual equivalente.
  - `src/vulkan_shaders/vk_world_outline.frag`: porte direto do algoritmo de `src/glsl/fx_world_geometry.fragment.glsl` (mesmo teste de descontinuidade de normal + segunda derivada de depth), lendo o color attachment do pass acima.
- **Implementado**: pipeline de composição (`VK_WorldOutlineCreatePipeline`/`VK_WorldOutlineComposite` em `src/vk_draw.c`), fullscreen triangle (reaproveita o `.vert` do post-process, que já é procedural sem input), blend `r_blendfunc_premultiplied_alpha` (equivalente a alpha-over reto já que o shader só emite alpha 0 ou 1), sampler dedicado `VK_FILTER_NEAREST` (o shader original usa `texelFetch`, então filtragem bilinear borraria exatamente as bordas que o algoritmo tenta medir). Cvars lidas: `gl_outline_color_world`, `gl_outline_world_depth_threshold` (bound 1-16), `gl_outline_world_normal_threshold` (bound 0-0.999), escala por `VID_ScaledWidth3D()/VID_ScaledHeight3D()` igual ao `GLM_DrawWorldOutlines`.
- 3 novos entry points registrados em `src/vk_local.h`: `VK_WorldNormalsRenderPass()`, `VK_CreateWorldNormalsResources()`/`VK_DestroyWorldNormalsResources()`/`VK_WorldNormalsFramebuffer()`, `VK_WorldOutlineActive()`/`VK_WorldOutlineComposite()`/`VK_WorldNormalsTransitionForSampling()` (este último é um no-op documentado — o render pass já deixa o attachment em `SHADER_READ_ONLY_OPTIMAL` como `finalLayout`, sem barreira manual necessária, ao contrário do post-process). 3 shaders novos registrados no `CMakeLists.txt` via `add_vulkan_shader`. **Tudo isso compila e linka limpo, e os 3 shaders compilam pra SPIR-V sem erro** (confirmado, inclusive o uso de `dFdx`/`dFdy` em GLSL 450 core, que o glslang aceitou de primeira).

**Decisão de compatibilidade confirmada com Tiago (importante pra quando for testar/documentar)**: no GLM, `gl_outline 2/3` só funciona de verdade com 4 comandos: `vid_renderer 1`, `vid_framebuffer 1`, `r_drawflat 1`, `gl_picmip 33` (achado real no código: `GL_FramebufferStartWorldNormals`, `gl_framebuffer.c:487/493`, retorna `false` sem um framebuffer FBO alocado, que só existe com `vid_framebuffer 1|2` — `r_drawflat`/`gl_picmip 33` não são requisito técnico do outline em si, são um truque visual à parte do Tiago pra deixar as texturas lisas e os contornos mais visíveis). **Confirmado e decidido**: `gl_outline` já É uma cvar de verdade compartilhada entre os 3 backends (bit 1, outline de modelo, já roda hoje no Vulkan via gate em `cl_ents.c:215`, código comum, não específico de renderer — a doc antiga "Requires vid_renderer 1" em `help_variables.json` estava desatualizada/incompleta). **Não criar `vk_outline` separado.** `vid_framebuffer` é uma cvar específica de GLC/GLM que nunca existiu no Vulkan (o post-process Vulkan é outro sistema, sempre alocado, sem cvar de ativação equivalente) — a arquitetura de render pass separado que foi implementada aqui **não depende dela e não deve criar essa dependência artificial**: quando a integração final estiver pronta, `gl_outline 3` sozinho deve bastar no Vulkan (sem precisar de `vid_framebuffer`/`r_drawflat`/`gl_picmip`). `help_variables.json`'s `gl_outline` já foi atualizado nesta sessão pra documentar isso: "On Vulkan (vid_renderer 2) works standalone, no vid_framebuffer needed."

**Teste ao vivo desta manhã, achado importante sobre o protocolo de teste**: rodei de novo com `gl_caustics 1` ativado explicitamente (sessão anterior só tinha testado com as cvars DESLIGADAS — `gl_caustics`/`gl_outline` são `"0"` por default, então o teste anterior só provava "não quebra nada", nunca exercitou o shader novo de verdade). Confirmado: `textures/water_caustic.png` existe dentro de `ezquake.pk3` (`unzip -l` confirma o path exato batendo com `R_LoadTextureImage("textures/water_caustic", ...)` em `r_rmisc.c:70`), então o asset não é o problema. **Ainda não validei visualmente se a cáustica aparece de verdade** — a tentativa de usar `noclip`+`screenshot` via `SendKeys` não confirmou que os comandos chegaram ao console do jogo (o log mostra só mensagens de location do MVDSV, não uma confirmação de screenshot salvo nem de `noclip` ligado) — o foco de janela via `SendKeys`/`AppActivate` não é 100% confiável neste ambiente. **Pendência real pra próxima verificação**: confirmar que comandos batem no console (ex: usar `echo` de teste antes de comandos reais, ou validar por outro sinal no log) e então validar visualmente (via screenshot de verdade, salvo em `qw/`) que a cáustica aparece nadando na água de `aerowalk` com `gl_caustics 1`.

**BLOQUEIO RESOLVIDO (sessão atual)** — o problema de ORDEM identificado antes era real: `worldDraws[]` só fica populado dentro de `R_DrawWorld()` → `VK_DrawWorld()` → `VK_WorldQueueModel()`, chamado de `R_RenderView()` (`src/r_rmain.c:889`), que roda **depois** de `VK_BeginFrame()` já ter feito `vkCmdBeginRenderPass` do main render pass. Ou seja: no único ponto em que a lista está pronta, o command buffer já está gravando dentro do main pass, e render passes não são aninháveis.

**Solução adotada: rota 1 (intercalar render passes no mesmo command buffer)** — implementada e compilando limpo. Em `VK_RenderView()` (`src/vk_world.c`), logo depois de os buffers de vértice/índice estarem prontos e **antes** de qualquer draw de mundo: se `VK_WorldOutlineActive()` → `vkCmdEndRenderPass` → `VK_DrawWorldNormalsPass()` (abre o pass de normais, redesenha `worldDraws[]` só com position/mvp, pula os `blended`, fecha) → `VK_WorldBeginMainRenderPassNoClear()` (reabre o main pass com a variante `vk_renderpass_main_noclear`, replicando a mesma escolha de framebuffer do `VK_BeginFrame`: offscreen do post-process quando ativo, swapchain caso contrário). O `VK_WorldOutlineComposite()` é chamado depois do batch opaco, no mesmo slot que o `GLM_RenderView` usa (logo após `GLM_DrawWorldModelBatch(opaque_world)`, antes de alias models/sprites/alpha).

**Por que fechar o main pass exatamente aí é lossless**: a variante noclear preserva a cor, mas o depth attachment limpa nas DUAS variantes (`VK_RenderPassCreateVariant`, espelhando `GL_Clear()`). Como nesse ponto o primeiro trecho do main pass não tinha nada além do próprio clear, re-limpar o depth não perde nada. Fazer isso mais tarde (ex: depois do loop opaco) jogaria fora o depth do mundo.

**Validação contra o FTEQW** (pesquisa feita nesta sessão, código real lido de `engine/vk/`): o FTEQW faz exatamente esse mesmo padrão, e ele é idiomático no motor deles, não uma gambiarra. Pontos concretos:
- `engine/vk/vk_backend.c`, `T_Gen_CurrentRender()`: para materializar `$currentrender` no meio do frame ele faz `vkCmdEndRenderPass` → trabalho fora do pass → `vkCmdBeginRenderPass(..., &vk.rendertarg->restartinfo, ...)`, reabrindo o pass no MESMO command buffer.
- `engine/vk/vk_init.c` `VK_GetRenderPass()` + `engine/vk/vkrenderer.h:414`: eles mantêm variantes de render pass indexadas por política de load — `RP_RESUME` (`LOAD_OP_LOAD` em cor e depth), `RP_FULLCLEAR`, `RP_DEPTHCLEAR`, `RP_DEPTHONLY` (shadowmaps) — e `VKBE_RT_Begin()` troca o pass pra `RP_RESUME` depois do primeiro begin justamente pra "future reuse shouldn't clear stuff". É o análogo direto do nosso `vk_renderpass_main_noclear`.
- Diferença arquitetural que vale registrar: o FTEQW **separa** as fases ("build batches" em `BE_GenModelBatches()`, depois `VKBE_SubmitMeshes()` grava), então em tese conseguiria montar o pass auxiliar antes de abrir o principal — mas mesmo tendo essa opção, na prática usa end/begin no meio do frame. Isso confirma que a rota 1 não é um workaround imposto pela nossa arquitetura fundida (ezQuake monta a lista dentro do mesmo loop que grava): é a escolha que o motor mais próximo do nosso também faz. A rota 2 (separar fases no ezQuake, mexendo em código compartilhado entre os 3 backends) fica descartada — muito mais invasiva pro mesmo resultado.

**Diferença deliberada em relação ao GLM**: o GLM escreve as normais via MRT (segundo color attachment nos próprios shaders de mundo, `GL_FramebufferStartWorldNormals`). Replicar isso no Vulkan exigiria dar um segundo color attachment às cinco pipelines de mundo mais uma variante de main render pass pra cada combinação de MSAA/post-process. Redesenhar a geometria position-only num pass dedicado e minúsculo não toca nenhuma pipeline existente — o custo é uma segunda passada sobre os vértices do mundo, que é justamente por isso que tudo é gateado em `VK_WorldOutlineActive()`.

**Arquivos tocados pro Gap 2**: `src/vk_renderpass.c`, `src/vk_swapchain.c`, `src/vk_local.h`, `src/vk_draw.c`, `src/vk_world.c`, `CMakeLists.txt`, `src/vulkan_shaders/vk_world_normals.vert` (novo), `src/vulkan_shaders/vk_world_normals.frag` (novo), `src/vulkan_shaders/vk_world_outline.frag` (novo). **Build limpo confirmado** (MSVC x64 RelWithDebInfo, os 3 shaders novos compilam pra SPIR-V, link OK).

**Pendência**: validação visual ao vivo (`gl_outline 3` sozinho, sem `vid_framebuffer`) — não feita, é escopo de outra etapa. Verificar também o comportamento com MSAA ligado (o pass de normais é sempre single-sample por construção) e com post-process ativo (o reabrir do main pass escolhe o framebuffer offscreen nesse caso, caminho ainda não exercitado em runtime).

**Nada disso foi commitado** — diff local. Como está inacabado e não testado visualmente, considerar isolar esse trabalho (ex: branch separada ou stash) do resto do PR se o Tiago quiser commitar só o Gap 1 (cáusticas, que está completo e testado) antes de terminar o Gap 2.


## Sessão 2026-07-23 (Claude, Windows, parte 2 — bug de modelos pretos/TDR) — EM ANDAMENTO, TDR grave aconteceu, ler antes de continuar

**Gatilho**: Ciscon mandou um tar.xz com sua pasta Quake real (`https://nicotinelounge.com/quake/backups/quake.tar.xz`) pra reproduzir o bug de "modelos pretos/transparentes + itens sem textura" que ele reportou no Vulkan (ver seção anterior, item 4 "Itens pretos"). Baixado e extraído (com autorização do Tiago) em `E:\Projetos Linux\_quake-test-data\quake` (fora deste repositório — dados de jogo, não versionar). Faltavam alguns PK3 (`base.pk3` real, só o `.bak` veio) por causa de symlinks quebrados no tar, mas `id1/pak0.pak`+`pak1.pak` e as demos de duelo dele (`qw/duel/*.qwd`) vieram OK.

**Como reproduzir**: copiar `E:\Projetos Linux\ezquake-sdl3-vulkan-pr\build-msvc-x64\RelWithDebInfo\ezquake.exe` pra DENTRO de `E:\Projetos Linux\_quake-test-data\quake\ezquake.exe` (não usar `-basedir`/`-nohome`, não funcionou — o exe precisa estar fisicamente na pasta, porque `com_basedir` é derivado do path do próprio executável) e rodar de lá: `E:\Projetos Linux\_quake-test-data\quake\ezquake.exe -dev -condebug +set vid_renderer 2`, depois `map dm3` no console. **Confirmado reproduzido pelo Tiago no Windows** (mesma família de driver AMD/RADV que a máquina do Ciscon): armas/modelos pretos e transparentes, itens no chão coloridos sem textura nenhuma — bate exatamente com o relato original. Log fica em `E:\Projetos Linux\_quake-test-data\quake\qw\qconsole.log` (cumulativo entre execuções — sempre `Remove-Item` antes de um teste novo se quiser isolar só a sessão atual).

### Causa raiz #1, CONFIRMADA E CORRIGIDA: pipeline overlay sem o atributo `inFlags`

`VK_WorldCreateOverlayPipeline` (`src/vk_world.c`, usada por `worldLumaPipeline`/`worldFullbrightPipeline`) reutiliza o shader `vk_world_textured_vert_spv`, que ganhou um atributo de vértice `inFlags` (location 3) numa sessão anterior (fix de `r_drawflat_mode` tinted/bright, ver seção de sessão Linux acima). Essa função nunca foi atualizada pra declarar esse atributo — só tinha `attributeDescriptions[3]` (índices 0-2), faltando o 3. Confirmado por erro real de validation layer: `vkCreateGraphicsPipelines(): pCreateInfos[0].pVertexInputState->pVertexAttributeDescriptions does not have a Location 3, but [VK_SHADER_STAGE_VERTEX_BIT] has [Input variable, Location 3, "inFlags"]`. **Corrigido**: array agora `[4]`, com o `attributeDescriptions[3]` (location 3, `VK_FORMAT_R32_UINT`, offset de `flags`) copiado do padrão já usado em `VK_WorldCreateTexturedPipeline`. Confirmado que esse erro específico sumiu do log depois do fix — mas **sozinho não resolveu o bug visual**, só era uma causa concorrente.

### Causa raiz #2, já estava no build (de sessão anterior, não desta): frames-in-flight vs. semáforos

`VK_MAX_FRAMES_IN_FLIGHT` 2→3 em `src/vk_local.h` (pra casar com as 3 imagens de swapchain desde o fix de vsync) + `renderFinishedSemaphores` reindexado por swapchain imageIndex em vez de frame-in-flight, em `src/vk_main.c`. Corrigia um erro de validação separado (`vkQueueSubmit(): pSubmits[0].pSignalSemaphores[0] ... may still be in use by VkSwapchainKHR`), confirmado que sumiu do log — mas também **não resolveu sozinho** o bug visual.

### Causa raiz #3, CONFIRMADA E CORRIGIDA (mas incompleta — ver TDR abaixo): upload de textura síncrono em frame ativo

Achado real: `VK_UploadTexture` (`src/vk_texture.c`) sempre destruía/recriava o descriptor set de uma textura de forma síncrona, mesmo quando chamada NO MEIO da gravação de um command buffer já ativo (`vk_options.frame.active == true`) — por exemplo, ao pegar um item que carrega um ícone de HUD ou skin pela primeira vez. `vkDeviceWaitIdle` (já existente em `VK_TextureDestroyObjects`) protege contra a GPU (trabalho já submetido), mas NÃO desfaz um `vkCmdBindDescriptorSets` já gravado na CPU no command buffer do frame atual, ainda não submetido — daí o erro `VkDescriptorSet ... was destroyed or updated without UPDATE_AFTER_BIND` seguido de cascata de "commandBuffer must be in the recording state", corrompendo o resto do frame.

**Fix aplicado** (`src/vk_texture.c`, `src/vk_main.c`, `src/vk_local.h`): `VK_UploadTexture` original renomeada pra `VK_UploadTextureImmediate` (static). Novo `VK_UploadTexture` público: se `!vk_options.frame.active` → imediato como antes (map load, init, vid_restart — comportamento inalterado); se `frame.active` → enfileira numa fila nova `deferredTextureUploads[]` (cópia própria dos pixels, já que o caller libera o buffer original logo depois). Nova `VK_TextureApplyDeferredUploads()` chamada em `VK_BeginFrame` (`vk_main.c` ~linha 630), ANTES de `vkResetCommandBuffer`/`vkBeginCommandBuffer` — ponto limpo onde nenhum command buffer referencia o descriptor set antigo ainda.

**Confirmado que ISSO SOZINHO NÃO RESOLVEU** — Tiago testou de novo depois desse fix (+ os dois anteriores juntos) e reportou: os modelos/itens já aparecem pretos/sem textura **desde o carregamento inicial do mapa**, ANTES de pegar qualquer item. O log confirma: o erro `UPDATE_AFTER_BIND` aparece logo após "The Abandoned Base"/"ciscon entered the game" (nome do mapa carregado, primeiro ou segundo frame), não em resposta a nenhum evento de gameplay. Ou seja, "pegar item" era só uma coincidência de timing no teste anterior — o hazard real acontece já no primeiro frame pós-load, por um caminho ainda não identificado (suspeitas não confirmadas: outro setter de textura síncrono tipo `VK_TextureWrapModeClamp`/anisotropia rodando durante o load de textura de mapa; ou o load de mapa entrando em `frame.active` de alguma forma inesperada; ver prompt completo passado pro agente de investigação, não repetido aqui).

**Investigação de continuação disparada** (agente Opus, rodando em paralelo, resultado ainda não recebido no momento em que este texto foi escrito) — pedido pra: confirmar se `frame.active` é realmente false durante todo o carregamento de mapa; mapear TODOS os call sites que destroem/atualizam um `vk_texture_t.descriptorSet` (não só os 3 já cobertos); achar a causa raiz real do crash no primeiro frame pós-load; implementar fix; compilar (sem rodar/testar visualmente).

### INCIDENTE GRAVE: TDR/travamento total do Windows

Durante os testes acima (não confirmado em qual etapa exata — pode ter sido ao testar o build com os 3 fixes juntos, rodando `map dm3` repetidamente com validation layers ligadas), **o Windows inteiro travou por ~3 minutos e voltou com um popup de erro "-4 vulkan"** — sintoma de TDR (Timeout Detection and Recovery) do driver AMD, o mesmo tipo de incidente já documentado no `AGENTS.md` como acontecido antes ("vid_restart TDR fix (Vulkan/AMD)", causa raiz na época: command buffer mid-frame + pipeline vazado). **NÃO investigado ainda nesta sessão** — prioridade após recuperar a máquina é: (1) confirmar que a máquina está estável, (2) verificar `qw/qconsole.log` e o Visor de Eventos do Windows por qualquer indício de qual operação especificamente travou o driver, (3) considerar se algum dos 3 fixes desta sessão (especialmente o de frames-in-flight/semáforos, que mexe em sincronização de baixo nível) introduziu um novo risco de TDR, ou se é o próprio bug de descriptor-set-corrompido-em-uso (causa raiz #3) que, sem fix completo, pode estar deixando a GPU num estado inválido grave o suficiente pra travar o driver inteiro, não só corromper o frame visualmente.

**Estado do worktree no momento deste incidente** (preservar, não descartar): `git status --short` mostra modificados `src/vid_sdl2.c` (fix alt-tab, já resumido na seção anterior), `src/vk_local.h`, `src/vk_main.c`, `src/vk_texture.c`, `src/vk_world.c` (os 3 fixes desta seção) — nada commitado ainda, tudo é diff local. Também não-rastreados: `AGENTS.md`, `PR_DESCRIPTION.md`, `_build_wip.bat`, `build_after_rebase.log`, `racat.cfg` (config de teste do racat, não essencial, pode ignorar).

**Regra a seguir ao retomar**: NÃO assumir que os 3 fixes de código estão corretos só porque compilam — o TDR é evidência de que pelo menos um cenário de teste levou a GPU a um estado ruim o suficiente pra precisar reset do driver. Antes de continuar testando ao vivo, esperar o resultado da investigação do agente (causa raiz #4, o crash no primeiro frame), aplicar esse fix também, e só então testar de novo — com cautela (considerar testar sem validation layers primeiro, ou com um timeout curto, pra não travar a máquina de novo caso o problema persista). Não fazer commit/push de nenhum desses fixes até confirmar visualmente que o bug de modelos pretos está resolvido E que não há mais indício de TDR.

**Erro exato confirmado pelo Tiago**: `vkQueueSubmit failed: -4` — código -4 é `VK_ERROR_DEVICE_LOST`. Isso é a causa direta reportada pelo próprio driver antes do TDR/travamento do Windows: a GPU/driver considerou o device perdido (geralmente por um comando inválido/corrompido submetido de verdade à fila, não só um erro de validação que teria sido pego ANTES do submit). Reforça a hipótese de que o command buffer corrompido pela causa raiz #3 (ou a #4 ainda não identificada) não está só gerando avisos de validação — em pelo menos uma execução, algo realmente inválido chegou a ser submetido pra GPU de verdade e travou o driver. Ao retomar: procurar `VK_ERROR_DEVICE_LOST`/`vkQueueSubmit failed` no código (`vk_main.c`) pra ver como isso é tratado hoje (provavelmente só loga e talvez tente continuar, o que seria perigoso se o device já estiver morto) — considerar se precisa de um `Sys_Error` explícito nesse caso em vez de tentar seguir renderizando com um device inválido.

### Causa raiz #4, CONFIRMADA E CORRIGIDA (terceiro caminho de descriptor set, achado só depois de grep exaustivo no repo inteiro)

Depois dos fixes #3 (upload de textura) e do fix dos setters de filtering/anisotropia/clamp (chamado de "causa raiz #3 continuação" na investigação), o bug **ainda persistia** — mesmo padrão, logo após "You got the shells", mesmo descriptor set, 420 ocorrências no log, processo eventualmente crashava (sem travar o Windows dessa vez, mas ainda arriscado).

Grep exaustivo (`vkUpdateDescriptorSets`/`vkFreeDescriptorSets`/`vkAllocateDescriptorSets`/`vkDestroyDescriptorPool`/`vkResetDescriptorPool`) em TODO o `src/` achou o terceiro caminho, nunca coberto pelos fixes anteriores: `R_TextureAllocateSlot()` (`r_texture.c:450`) → `R_DeleteTexture()` → `VK_TextureDelete` → `VK_TextureDestroyObjects` → `vkFreeDescriptorSets`, **síncrono, sem nenhum gate em `frame.active`**. Isso dispara sempre que o jogo recarrega uma textura sobre um slot já existente com tamanho diferente — exatamente o padrão de recarregar ícone de HUD/munição ou skin de jogador/arma ao pegar um item. Roda a partir de código de HUD/gameplay durante `SCR_UpdateScreen`, no meio do frame, depois que os draws de mundo/HUD daquele frame já tinham gravado `vkCmdBindDescriptorSets` contra o set antigo — free ali é exatamente o hazard. Não é coberto pelo deferral do `VK_UploadTexture` porque a exclusão do slot acontece antes/independente de qualquer upload.

**Fix aplicado** (`src/vk_texture.c`, terceira fila seguindo o mesmo padrão): `deferredDescriptorFrees[]`. Em `VK_TextureDestroyObjects`, quando `frame.active`, o handle do descriptor set vai pra fila em vez de ser liberado na hora (o `memset` ainda zera `vktex->descriptorSet`, então o slot pode ser reusado no mesmo frame com um set novo e distinto). Fora de frame ativo (map load/vid_restart/shutdown) ou fila cheia, continua liberando na hora como antes. `VK_TextureApplyDeferredUploads` (mesmo ponto de sempre, início do `VK_BeginFrame`, antes de `vkBeginCommandBuffer`) agora libera essa fila PRIMEIRO, antes dos uploads (recupera capacidade do pool antes de qualquer alocação nova). Fila zerada em `VK_TextureDiscardDeferredUploads`/`VK_TextureInitialiseState` (que já destroem o pool inteiro).

Grep exaustivo também confirmou que os outros 2 `vkUpdateDescriptorSets` do projeto (`vk_draw.c:655`, post-process; `vk_world.c:363`, sky) NÃO são esse hazard — post-process escreve uma vez só por imagem de swapchain (cache lazy, nunca re-escreve um set já usado); sky atualiza um set per-frame-in-flight e faz bind no MESMO frame (update-then-bind, não o padrão "já gravado num frame anterior ainda executando").

**Build limpo, compilado, MAS AINDA NÃO TESTADO AO VIVO no momento em que este texto foi escrito** — o agente que implementou foi instruído a não rodar o jogo (risco de TDR já materializado uma vez nesta sessão). Confiança alta do agente de que esse é o terceiro/último caminho, mas isso precisa ser confirmado testando de verdade antes de considerar o bug resolvido. Se persistir mesmo depois deste fix, o próximo lugar a olhar (sugestão do próprio agente) é se `frame.active` pode ficar `false` numa janela estreita entre `vkBeginCommandBuffer` (`vk_main.c:639`) e `frame.active=true` (`vk_main.c:683`) — ele não acha que é isso, mas não descartou 100%.

**Testado SEM `-dev` (sem validation layers) — bug visual PERSISTE.** Confirmado pelo Tiago: mesmo sem validação (nenhum erro no `qconsole.log`), modelos/itens continuam pretos/sem textura, e apareceu um crackling de áudio novo (provável sintoma de stress/contenção, não necessariamente causa separada). **Conclusão importante**: os 3 fixes de deferral de descriptor set eram reais e corrigiam um hazard genuíno de sincronização (validado por validation layer), mas **não são a causa raiz do bug visual em si** — o "preto"/"sem textura" acontece por outro motivo, independente desse hazard. Não descartar os 3 fixes (continuam corretos e necessários), mas a investigação precisa mudar de eixo: de "sincronização/lifetime de descriptor set" para "conteúdo/pipeline de renderização real desses draws" (sampler, layout de imagem, formato, ou dado de textura em si chegando errado/vazio na GPU).

### Causa raiz #5, EM INVESTIGAÇÃO: conteúdo de mipmap corrompido para texturas NPOT (skins de modelo/itens)

Investigação de pipeline (vertex attributes de `vk_aliasmodel.c`/`vk_sprite3d.c`, descriptor set layout, blend state) comparada contra GLC/GLM não achou nenhum descompasso estrutural — tudo bate. Duas hipóteses restantes: (1) timing/readiness de textura (upload adiado por engano), (2) conteúdo real da textura errado (mipmap malformado).

**Instrumentação de diagnóstico ativada temporariamente**: `VK_AliasDebugLog` (`src/vk_aliasmodel.c` ~linha 93, era no-op) agora imprime de verdade via `Con_Printf`, com uma chamada nova em `VK_AliasQueuePreparedDraw` (~linha 552) logando `texIdx`/`ready`/`weapon`/`player`/`mode` por draw. **Resultado do teste ao vivo (sem `-dev`, só pra isolar o log)**: ~512973 linhas, **100% `ready=1`**, nunca `skippedTexture`. Isso **descarta definitivamente a hipótese #1** (timing) — a textura está sempre marcada pronta quando o draw é enfileirado, tanto pra arma (`texIdx=488 weapon=1`) quanto pra itens (`texIdx=566`/`576`, mode=0). **Confirma a hipótese #2**: textura pronta e bindada certo, mas o CONTEÚDO na GPU está errado.

Suspeita concreta: `VK_BuildMipPyramid` (`src/vk_texture.c` ~linha 715-740) gera a pirâmide via `Image_MipReduce` (`src/image.c` ~linha 376) chamado **in-place** (mesmo buffer como `in` e `out`: `Image_MipReduce(outBuffer + offset, outBuffer + offset, &width, &height, 4)`, linha ~731). Skins de modelo/itens usam `TEX_NOSCALE` com frequência (`src/r_aliasmodel_skins.c` ~linhas 94-96, 158-160), ou seja, dimensões NPOT reais — bem diferente de texturas de mundo do Quake original, que tipicamente são POT (64x64, 128x128), por isso nunca expuseram esse caminho antes. Não confirmado ainda se o in-place quebra matematicamente pra alguma combinação de dimensão NPOT/ímpar — próxima investigação (agente disparado, resultado ainda não recebido) foi instruída a auditar `Image_MipReduce` byte a byte, incluindo os casos de dimensão 1 numa das duas dimensões (função tem mais código depois do trecho já lido), e conferir se os offsets/dimensões de cada `VkBufferImageCopy` batem exatamente com o que foi gerado na CPU.

**Lembrete pra quando isso for resolvido**: a instrumentação de debug (`VK_AliasDebugLog` + a chamada de log em `VK_AliasQueuePreparedDraw`) precisa ser revertida/removida antes de considerar o trabalho pronto — regra geral do projeto, não deixar log de debug temporário na versão final (ver AGENTS.md).

**Efeito colateral do log de diagnóstico, corrigido**: com o log ativo (`Con_Printf` chamado a cada draw de alias model, ~512973 linhas numa sessão curta, `qconsole.log` chegou a 1.68 milhões de linhas), o volume de I/O de console competia por CPU com o thread de áudio e causou crackling audível — não é bug do Windows nem dos fixes de Vulkan, é efeito direto do próprio log de diagnóstico. `VK_AliasDebugLog` já foi revertido pra no-op (`src/vk_aliasmodel.c` ~linha 93-99), mas a chamada em `VK_AliasQueuePreparedDraw` (~linha 559) continua no código, pronta pra reativar rápido se precisar — inofensiva com a função em modo no-op.

**Investigação de continuação (agente Opus) FALHOU por limite de sessão da API** (não é erro de código, resetou 23:30 horário de São Paulo) — não chegou a uma conclusão nem aplicou fix algum. Progresso que ele tinha antes de cair:

- Confirmou (lendo `R_TextureSizeRoundUp`) que quando `r_texture_support_non_power_of_two` está true (Vulkan seta isso), essa função retorna dimensões NPOT sem arredondar — skins de alias model chegam em `VK_UploadTexture` com largura/altura genuinamente NPOT no Vulkan. Confirma que esse é o caminho não-testado (texturas de mundo do Quake original são tipicamente POT).
- Tentou rastrear `Image_MipReduce` (`src/image.c` linha 376+) em vários casos de borda NPOT (largura 3, altura/largura=1) tentando achar corrupção matemática no reduce in-place — não achou o bug concreto, a lógica parecia correta nesses casos específicos.
- Próximo passo mais concreto, ainda não feito: comparar `VK_TextureMipLevelCount` (contagem de níveis esperada) contra a contagem real gerada por `VK_BuildMipPyramid` para uma dimensão NPOT pequena tipo 32x1 — se divergirem, é a causa (nível esperado pela imagem Vulkan nunca populado com dado real, lendo lixo/preto).

**Nada foi commitado nesta sessão inteira**. Todos os 6 fixes/mudanças de código (alt-tab grab, pipeline overlay inFlags, frames-in-flight/semáforo, upload de textura deferred, setters de textura deferred, descriptor-set-free deferred) continuam como diff local no worktree, intactos.

**Sessão pausada aqui por decisão do Tiago** (máquina já sofreu 1 TDR grave nesta sessão; investigação de mipmap não convergiu depois de 3 rodadas de agente + 1 análise direta). Última tentativa de análise direta (sem agente, sessão principal) descartou mais alguns candidatos sem achar a causa:

- `VK_TextureUploadBufferToImageImmediate` (a função real que faz a cópia buffer→imagem pra ambos os casos com/sem mipmap) parece correta: transição de layout, `vkCmdCopyBufferToImage` com as regions calculadas por nível, transição de volta — nada óbvio errado.
- `VK_TextureRecordTransitionBarrier` cobre `levelCount = max(1, vktex->mipLevels)` corretamente — barreira não está limitada só ao nível 0.
- Conversão de paleta→RGBA das skins acontece antes de `R_LoadTexturePixels`/`R_LoadTexture`, que já espera RGBA de 4 bytes — não vi indício de alpha=0/RGB=0 sendo produzido ali (mas não segui esse caminho até a origem real da conversão de paleta, só confirmei que o formato de entrada em `R_LoadTexturePixels` já é RGBA).
- Tentativa de achar uma função tipo `VK_TextureMipLevelCount` mencionada pelo agente anterior — **não existe no código com esse nome**, pode ter sido um nome hipotético/memória falsa do agente, ou uma função que ele pretendia escrever, não uma já existente. Não confundir isso com um call site real ao retomar.

**Candidatos ainda não descartados, pra retomar**: (1) a lógica exata de `Image_MipReduce` pra combinações NPOT muito específicas (o agente tentou e não achou, mas não teve tempo de cobrir todos os casos antes de cair por limite de sessão); (2) se `R_TextureSizeRoundUp`/o caminho de picmip faz alguma suposição de POT em outro lugar que quebra silenciosamente pra dimensões NPOT reais quando picmip/max_size está ativo (não investigado ainda); (3) algo relacionado à origem da conversão paleta→RGBA das skins especificamente (não world textures), ainda não seguido até a fonte.

**Recomendação concreta pra retomar**: antes de mais leitura de código, considerar despejar/inspecionar visualmente os bytes reais de uma skin conhecida (dimensões, alguns pixels de cada nível de mip) via log temporário — é mais rápido confirmar/descartar por dado real do que continuar deduzindo estaticamente. Cuidado: NÃO usar `Con_Printf` em alto volume de novo (causou crackling de áudio nesta sessão, ver nota acima) — se for logar, limitar a um print único por textura carregada, não por draw/frame.

## Regra permanente (Tiago pediu explicitamente, sessão 2026-07-22)

Manter este arquivo (`CONTINUE.md`, maiúsculo — é o mesmo slot de arquivo que `continue.md` em filesystems case-insensitive como Windows, não criar um `continue.md` separado) atualizado sempre que uma sessão avançar ou pausar, tanto aqui quanto em `E:\Projetos Linux\ezquake-source\continue.md` (worktree Android, esse sim minúsculo, filesystem diferente/caso não colide lá). Objetivo: qualquer sessão futura (Claude ou Codex, Windows ou Linux) sabe onde o trabalho parou.

## Sessão 2026-07-23 (Claude, Linux, `/home/tiba/src/ezquake-source`) — 4 bugs reportados pelo Ciscon testando o build da sessão anterior

**Contexto**: Ciscon (outro tester) testou o build Linux gerado na sessão de 2026-07-22 (mesma máquina do Tiago, mesma família de GPU AMD/RADV) e reportou 4 problemas: itens pretos, `r_drawflat_mode` 1/2 sem efeito, outlines não funcionando, `vid_vsync` não respeitando modo imediato. Investigado com o Fable 5 (duas consultas dedicadas, sempre pedindo pra ele ler o código real antes de opinar — ver regra na seção de 2026-07-22 Windows) + verificação ao vivo nesta máquina (AMD RX 6800 XT, Mesa RADV, Wayland, monitor 360Hz).

**Ainda sem commit/push no momento em que este texto foi escrito** — aguardando autorização explícita do Tiago pra commitar as mudanças de código abaixo (só o próprio CONTINUE.md pode já ter sido commitado, conferir `git log` antes de assumir).

### 1. Outline do mundo (`gl_outline 2`/`3`) — NÃO é bug, é gap conhecido

Ciscon tinha testado especificamente outline de *mundo* (confirmado com o Tiago). Bit 2 (`gl_outline & 2`, outline de geometria do mundo via MRT + edge-detect) simplesmente não existe na árvore atual — nenhum `worldNormals*`/`vk_world_normals.*` em lugar nenhum. **Achado importante do Fable**: o CONTINUE.md antigo (seção Windows) descreve esse trabalho como "implementado então desabilitado", mas isso nunca foi commitado neste branch — ficou só numa sessão local/Windows não sincronizada. Tratar CONTINUE.md como log narrativo, não como fonte de verdade do que está na árvore — sempre `grep`/ler o HEAD real antes de assumir que algo existe.

Outline de *modelo* (bit 1, `VK_ALIAS_MODE_OUTLINE` em `vk_aliasmodel.c`) foi auditado pelo Fable via leitura estática (dispatch, gating por ruleset, pipeline sempre criado, ordem de draw) e parece correto — não mexido nesta sessão.

**Decisão explícita do Tiago nesta sessão**: outline de mundo (bit 2) fica pra uma sessão futura de propósito — não é um bug a corrigir, é uma feature inteira a implementar (segundo attachment de cor pra normais + passo de pós-processamento de edge-detect, ver notas de arquitetura na seção "Sessão 2026-07-22 (Claude, Windows)" mais abaixo, incluindo os cuidados com MSAA). Perguntado e adiado deliberadamente — não reabrir como "ainda não corrigido" numa próxima sessão sem checar aqui primeiro.

### 2. `r_drawflat_mode` 1 (tinted) / 2 (bright) não tinha efeito nenhum no Vulkan — CORRIGIDO E CONFIRMADO

Causa raiz (achada pelo Fable, confirmada lendo o código): `r_refdef2.drawFlatFloors`/`drawFlatWalls` (`src/cl_view.c:1024-1025`, compartilhado pelas 3 renderers) só fica `true` quando `r_drawflat_mode == 0` — isso só controla se a superfície vai pro chain "flat puro" (`vk_world_flat.frag`, sem textura) ou pro chain de textura normal. GLC/GLM não dependem desse gate pra tinted/bright: eles reaplicam a cor por cima da textura real dentro do PRÓPRIO shader texturizado (`applyColorTinting()` em `draw_world.fragment.glsl`, gateado só por `r_drawflat.integer`, não pelo mode). Os shaders texturizados do Vulkan (`vk_world_textured.frag`, `vk_world_lightmapped.frag`) não tinham nenhum equivalente — por isso mode 1/2 renderizava 100% textura normal, sem efeito algum.

**Não mexer em `cl_view.c`** (avisado pelo Tiago em tempo real) — isso afetaria GLC/GLM também. O fix ficou inteiramente no lado Vulkan:

- `src/vk_world.c`: `vk_world_push_t` ganhou `floorColor`/`wallColor`/`drawflatMode`/`tintFloors`/`tintWalls` (struct de 144 → 176 bytes — acima do mínimo garantido de 128 do Vulkan mas dentro dos 256 típicos de desktop AMD/NVIDIA/Intel; **não portar pro branch Android** sem reconferir o limite lá). Preenchidos no loop de draw só quando `r_drawflat_mode != 0` (mode 0 continua 100% do pipeline `vk_world_flat` de sempre, intocado).
- Novo atributo de vértice `flags` (location 3/4 conforme o pipeline) adicionado em `VK_WorldCreateTexturedPipeline`/`VK_WorldCreateLightmappedPipeline` — já existia no VBO compartilhado (`vbo_world_vert_t.flags`, com o bit `EZQ_SURFACE_IS_FLOOR` já preenchido em `vk_main.c:147` desde sempre) mas nenhum pipeline texturizado consumia. **Não precisou mudar o VBO/vertex builder** — só passou a ler o que já existia.
- `src/vulkan_shaders/vk_world_textured.{vert,frag}` e `vk_world_lightmapped.{vert,frag}`: recebem o novo atributo, portam `applyDrawflatTint()` (equivalente ao `applyColorTinting()` do GLM: tinted = multiply, bright = recolor por luminância).
- **Testado e confirmado visualmente pelo Tiago** (`r_floorcolor 255 0 0` / `r_wallcolor 0 255 0` com `r_drawflat_mode 1` → chão vermelho / parede verde, textura ainda visível por baixo).
- Achado à parte durante a investigação, não corrigido (fora de escopo, documentado pelo Fable): `R_SetNonPowerOfTwoSupport()` só é chamado do init GL (`vid_common_gl.c`), nunca do Vulkan — `r_texture_support_non_power_of_two` fica sempre `false` no Vulkan, forçando resample de toda textura NPOT. Provavelmente invisível na maioria dos casos (a maioria das skins MDL já é POT) mas é um bug real, renderer-wide, pendente.

### 3. `vid_vsync 0` não usava modo imediato — CORRIGIDO E CONFIRMADO (com pegadinha)

Duas causas, achadas em duas rodadas:

**3a. Ordem de preferência errada** (`src/vk_physical_devices.c`, `VK_PhysicalDeviceBestPresentationMode`): com vsync off, a lista de preferência tentava `MAILBOX_KHR` antes de `IMMEDIATE_KHR`. MAILBOX ainda é sincronizado com a tela (troca de frame em vez de bloquear, mas sem tearing) — diferente do `SDL_GL_SetSwapInterval(0)` real que GLC/GLM usam. Trocada a ordem pra `IMMEDIATE` primeiro quando `r_swapInterval.integer == 0`.

**3b. Só a ordem não bastou** — Ciscon (e depois o próprio Tiago) confirmaram log mostrando `IMMEDIATE` corretamente selecionado, mas o FPS continuava travado no refresh do monitor (360). Causa: `VK_CreateSwapChain` (`src/vk_swapchain.c:402-411`) só pedia um buffer extra (`minImageCount + 1`) para `MAILBOX`, nunca para `IMMEDIATE` — rodando com só 2 imagens. Nesse Wayland/RADV específico, o *release* dos buffers de volta pro app parece ficar pautado pelo próprio ritmo de repaint do compositor a menos que `wp_tearing_control_v1` seja negociado entre driver e compositor (não universal) — com só 2 imagens isso vira um cap efetivo de fps no refresh rate, apesar do present mode certo. Estendida a condição do `+1` pra cobrir `IMMEDIATE` também.

**Confirmado com `timedemo` (`qw/matchinfo/demos/weirdrocket.qwd`, 25581 frames) rodando local, sem servidor/rede no caminho**:
- `vid_vsync 0` + `cl_maxfps 0`: **1727.2 fps** (bem acima do monitor de 360Hz — antes ficava preso em ~360 mesmo com maxfps liberado).
- `vid_vsync 1`: jogo interativo normal (`map dm3`) funciona bem, sem travar.

**Bug novo encontrado, não corrigido, baixa prioridade**: `timedemo` combinado com `vid_vsync 1` (FIFO) trava o processo (CPU cai a ~0%, estado sleeping, nunca termina/imprime relatório). Interativo com `vid_vsync 1` funciona normal — parece específico da combinação timedemo+FIFO, não do gameplay normal. Provável relacionado ao `vkWaitForFences(..., UINT64_MAX)` em `VK_BeginFrame` (`src/vk_main.c`, perto de onde já existe um comentário sobre trocar timeout infinito por finito no `vkAcquireNextImageKHR` por uma razão parecida — mesma área de código, não investigado a fundo ainda). **Não iniciado.**

Diagnóstico temporário deixado no código (`Com_Printf`/`Con_Printf` com prefixo "TEMP diagnostic" em `vk_physical_devices.c` e `vk_swapchain.c`, listando present modes disponíveis e `imageCount` real) — decidir se remove antes de commitar ou deixa (é só log, não afeta comportamento).

### 4. Itens pretos — NÃO REPRODUZIDO nesta máquina, causa raiz não encontrada

Mesma família de GPU/driver (AMD/RADV) nas duas máquinas (Tiago e Ciscon), então não é claramente uma questão de fabricante — pode ser geração de GPU, versão de driver/Mesa, ou uma race condition que só se manifesta em certas condições de timing. O Fable investigou fundo (pipeline de alias models, descriptor sets, upload de textura, mip pyramid, sampler) e eliminou várias hipóteses com evidência, mas não achou a causa raiz por leitura estática — ver relatório completo dele nesta sessão (não resumido aqui por já estar bem detalhado, procurar no transcript se precisar). Sugestão dele: habilitar o log `VK_AliasDebugLog` já existente em `vk_aliasmodel.c` e testar na máquina do Ciscon, ou usar RenderDoc/validation layers lá. **Precisa da máquina do Ciscon pra progredir** — não dá pra reproduzir/depurar daqui.

### Notas técnicas gerais desta sessão

- **`sudo` não funciona de dentro do harness do Claude Code** (sem TTY pra senha) — nem via Bash nem via `!comando` do usuário. Instalação de pacotes precisou ser feita pelo próprio Tiago num terminal separado.
- **Push pro GitHub precisa de token** — sem credencial configurada na máquina por padrão. Token fine-grained do GitHub precisa explicitamente de "Contents: Read and write" nas Permissions (não só "Repository access"), senão dá 403 mesmo autenticando certo (leitura/`git ls-remote` funciona, push não).
- **Cuidado com comandos em cadeia no Bash tool desta sessão**: se um comando no meio de um script multi-linha retorna código de saída != 0 (mesmo um `pkill` sem processo pra matar, que é normal/esperado), os comandos SEGUINTES na mesma chamada não executam. Rodar `pkill`/checks-que-podem-falhar em chamadas separadas dos comandos que realmente importam (`cp`, `chmod`, etc.), não em sequência na mesma call.
- **AppImage é o método padrão agora pra empacotar builds de teste pra compartilhar** (pedido explícito do Tiago) — usar `misc/appimage/appimage-manual_creation.sh` com `EXECUTABLE`/`SKIP_DEPS=1` já setados pro binário já compilado, não o tarball manual com libs soltas usado uma vez no início desta sessão (descartado).
- **`timedemo` é a forma limpa de medir fps sem depender de olho humano/screenshot** — mas só funciona de forma confiável com `qw/autoexec.cfg` renomeado temporariamente pra fora do caminho primeiro (senão o auto-connect do config corrida com o carregamento da demo e derruba o teste no meio). Lembrar de restaurar o nome depois.

## Sessão 2026-07-22 (Claude, Linux, `/home/tiba/src/ezquake-source`, Zorin OS 18.1 / Ubuntu 24.04 "noble")

**Pedido do Tiago**: compilar o branch `feature/sdl3-vulkan-pr` (HEAD `36057234`, o mesmo commit documentado na sessão Windows acima) numa máquina Linux nova (`/home/tiba`, diferente do `/home/tiago` da sessão Codex de 2026-07-05 citada abaixo) e colocar o binário em `/home/tiba/nquake` pra ele testar. **Testado visualmente e confirmado pelo Tiago** ("Sim, abriu normalmente").

Achados relevantes pra quem for reproduzir este build em outra máquina Ubuntu/Debian-based sem `libsdl3-dev` empacotado:

1. **Ubuntu 24.04 não tem `libsdl3-dev` nos repos** (só existe no Debian testing/sid, que é por isso que o job Linux do CI roda dentro de um container `debian:testing`, não direto no runner `ubuntu-latest` — ver `.github/workflows/main.yml`). Sem SDL3 do sistema, `USE_SYSTEM_LIBS=ON` (padrão) falha o `pkg_check_modules(sdl3)`.
2. **Solução usada**: compilar SDL3 3.2.20 a partir do código-fonte (`github.com/libsdl-org/SDL`, tag `release-3.2.20`) e instalar num prefixo local não-privilegiado (`/home/tiba/src/sdl3-install`, sem `sudo`), com Wayland+X11+Vulkan+ALSA+PulseAudio habilitados (todos detectados automaticamente pelo CMake do SDL desde que os `-dev` de X11/Wayland/libdecor/etc. estejam instalados — ver lista de pacotes abaixo). `PKG_CONFIG_PATH=/home/tiba/src/sdl3-install/lib/pkgconfig` faz o `pkg-config --modversion sdl3` do CMake do ezquake achar essa instalação. O binário final carrega `libSDL3.so.0` via `RUNPATH` absoluto que o CMake já embute sozinho (confirmado com `readelf -d`), então não precisa de `LD_LIBRARY_PATH` na hora de rodar.
3. **`libvulkan-dev` do Ubuntu 24.04 (1.3.275) é headers demais antigos** — o código usa `VK_AMD_anti_lag` (`VkAntiLagDataAMD`, `VK_STRUCTURE_TYPE_ANTI_LAG_DATA_AMD` etc. em `src/vk_main.c`), extensão só presente em headers Vulkan mais recentes (~1.3.28x+). Erro de compilação: "unknown type name 'VkAntiLagDataAMD'" / "request for member ... in something not a structure or union". **Solução**: clonar `github.com/KhronosGroup/Vulkan-Headers` (branch default, header version 357 no momento) e passar `-DVulkan_INCLUDE_DIR=/home/tiba/src/Vulkan-Headers/include` no configure — a lib/loader do sistema (`libvulkan.so.1`, ABI estável) continua sendo usada normalmente, só os headers de compilação são mais novos. Não precisou trocar `libvulkan1`/driver do sistema.
4. **Comando de configure completo que funcionou**:
   ```
   export PKG_CONFIG_PATH=/home/tiba/src/sdl3-install/lib/pkgconfig
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRENDERER_VULKAN=ON \
     -DCMAKE_PREFIX_PATH=/home/tiba/src/sdl3-install \
     -DVulkan_INCLUDE_DIR=/home/tiba/src/Vulkan-Headers/include
   cmake --build build --parallel $(nproc)
   ```
5. **Pacotes apt necessários** (além dos já listados em `build-linux.sh`/CI, que ainda faltam alguns pro SDL3 compilar do zero): `cmake ninja-build pkg-config glslang-tools libcurl4-openssl-dev libexpat1-dev libfreetype-dev libjansson-dev libjpeg-dev libminizip-dev libpcre2-dev libpng-dev libsndfile1-dev libspeex-dev libspeexdsp-dev libvulkan-dev libwayland-dev wayland-protocols libxkbcommon-dev libegl1-mesa-dev libgles2-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libpulse-dev libasound2-dev libdrm-dev libgbm-dev libdecor-0-dev libibus-1.0-dev`.
6. Binário final copiado pra `/home/tiba/nquake/ezquake-linux-x86_64` (o binário anterior que estava lá, linkado contra um SDL3 inexistente no sistema — provavelmente de uma tentativa anterior de build/CI download — foi preservado como `ezquake-linux-x86_64.bak-nosdl3`). Testar com `-vulkan` na linha de comando.
7. **Nenhuma mudança de código nesta sessão** — só descoberta de receita de build local. Nada commitado além deste próprio arquivo.
8. Sudo não funciona de dentro do harness do Claude Code neste ambiente (sem TTY pra senha, `sudo -n` falha) — os `apt-get install` precisaram ser rodados pelo próprio Tiago num terminal separado. Por isso a decisão de instalar SDL3/usar Vulkan-Headers em prefixos locais sem privilégio, evitando depender de mais `sudo` pro resto do processo.

## Sessão 2026-07-22 (Claude, Windows, worktree `E:\Projetos Linux\ezquake-sdl3-vulkan-pr`)

**5 commits novos, feitos em cima do trabalho da sessão anterior (Codex/Linux, `e5d4f778`/`48ca9805`/`c87251f5` abaixo), testados visualmente e aprovados pelo Tiago ("funcionou ok sem bugs"). Rebase feito sobre `origin/feature/sdl3-vulkan-pr` sem perda de trabalho de nenhum dos dois lados — só um conflito real em `src/vk_world.c` no branch flat (o commit remoto `48ca9805` mudou esse branch para 2 descriptor sets/lightmap; meu bind-cache foi adaptado a esse formato novo, resolvido manualmente).**

1. `973cfe9d` (era `b3ada541` antes do rebase) — cache de last-bound-pipeline/descriptor-set no loop de draw do mundo (`vk_world.c`, nova função `VK_WorldBindIfChanged`). Pula `vkCmdBindPipeline`/`vkCmdBindDescriptorSets` quando o estado já é igual ao draw anterior (comum, já que draws vêm agrupados por material). Não toca em geometria/contagem/ordem de draws. Cache é invalidado sempre que algo externo (alias models, sprites 3D, overlay luma/fullbright) faz bind no mesmo command buffer entre draws do mundo. **Achado pelo Fable 5** numa investigação dedicada de performance (ver seção logo abaixo).
2. `e3eff48b` (era `00e4ff14`) — fix do drawflat Vulkan (bug do racat): `r_drawflat_mode` só deveria ser um estilo de cor (0=normal/1=tinted/2=bright), não um gate de ativação — bug diferente do que a sessão anterior já tinha corrigido (cores erradas + falta de lightmap shading, `48ca9805`); esse aqui é sobre o `r_drawflat_mode != 0` desligando o efeito inteiro. Tinted/bright continuam lacuna conhecida no Vulkan (só "normal" implementado), documentado, não implementado — nem FTEQW nem vkQuake implementam isso.
3. `281353b3` (era `a389be91`) — fix real de CMake: `string(REGEX REPLACE ...)` sem match retorna a string original, causava `FILEVERSION` inválido no `.rc` do Windows quando o clone não tem tags alcançáveis.
4. `8a7a350a` (era `0493ea03`) — fix de build: preset `msvc-x64` não fixava `RENDERER_VULKAN=ON`, então qualquer reconfiguração do zero silenciosamente gerava um build OpenGL-only, causando "Invalid vid_renderer value detected". Corrigido fixando a flag como `cacheVariable` do preset. **Específico do preset Windows/MSVC** — não afeta Linux (ver pedido de build Linux abaixo, que usa outro preset/toolchain).
5. `1c578ad4` (era `8c801c33`) — buffers estáticos do mundo (`bufferusage_reuse_many_frames`/`bufferusage_constant_data`) agora usam memória `DEVICE_LOCAL` com staging upload, em vez de `HOST_VISIBLE|HOST_COHERENT`.

**Push feito para `origin feature/sdl3-vulkan-pr` nesta sessão** (autorizado explicitamente pelo Tiago).

**Pedido em andamento**: compilar uma build Linux para o Tiago testar no Zorin OS (ele roda a sessão Codex anterior em `/home/tiago/...` segundo o histórico abaixo, então o projeto já tem precedente de build Linux funcional — não é a primeira vez). Ver se há um preset CMake Linux/GCC ou se é `USE_SYSTEM_LIBS=ON` (mencionado no handoff Codex abaixo) + toolchain nativo.

### Investigação de performance desta sessão (Fable 5, 3 consultas)

Tiago pediu para revisar uma proposta de arquitetura de terceiros (RHI, migração por sistema, instancing, command buffers secundários, HUD SDF) e depois focar especificamente em performance real ainda não explorada.

**Veredito sobre a proposta de RHI/arquitetura**: a maior parte já existe no projeto, só com nomes diferentes — `renderer_api_t` (`r_renderer.h`) já é a "RHI" (dispatch `renderer.DrawWorld()` etc. pros 3 backends), migração por sistema já é como os arquivos são organizados (`vk_world.c`, `vk_aliasmodel.c`, etc.), shaders já são um-por-sistema em `vulkan_shaders/`. **Não vale aplicar** — não é modernização, é redescoberta com outro vocabulário.

**Itens genuinamente ausentes, investigados e descartados por ora**:
- Instancing de alias models: infra existe mas vestigial (binding de instância nunca alimentado, `instanceCount=1` hardcoded). Exigiria mover push-constants pra buffer de instância + reescrever shader. Volume de alias models simultâneos em QuakeWorld é baixo — não vale o esforço/risco agora.
- Command buffers secundários: zero threading de render no projeto, nenhum profiling mediu gravação de comandos como gargalo. Não vale.
- HUD batched com SDF: HUD já tem batching parcial (`VK_HudDrawImages`), overhead de draw calls do HUD é trivial frente ao mundo 3D. Não vale.

**Achado real e aplicado**: bind cache de pipeline/descriptor-set no loop do mundo (commit 1 acima) — única coisa encontrada com ganho mensurável em CPU-bound desktop, risco baixo.

**Push constants do mundo**: no momento da investigação, 144 bytes (acima dos 128 garantidos pelo spec Vulkan) — nota: a sessão Codex anterior (`48ca9805`) também mexeu nesse struct (fog + drawflatColor), então esse número pode ter mudado depois do rebase; conferir `sizeof(vk_world_push_t)` de novo antes de assumir. Funciona hoje (AMD/Adreno/Mali topo de linha suportam mais), mas é risco de portabilidade, não performance.

### Trabalho pendente identificado mas não iniciado: outline de mundo no Vulkan

`gl_outline` é bitmask: bit 1 (modelos) já funciona no Vulkan (`VK_ALIAS_MODE_OUTLINE`, `vk_aliasmodel.c`). Bit 2 (mundo) não existe no Vulkan ainda — relacionado mas DIFERENTE do "world outlines" mencionado na auditoria GLM→Vulkan da sessão Codex abaixo (aquela lista trata de outros gaps de paridade visual; conferir se já cobre isso ou se é mais um item da mesma lista antes de duplicar trabalho). Investigado a fundo (dois backends de referência):
- Classic GL (`glc_surf.c`): wireframe simples, `GL_LINE_LOOP` por polígono.
- Modern GL (`glm_rsurf.c`, `gl_framebuffer.c`): post-processing de verdade — segundo color attachment (`fbtex_worldnormals`, RGBA16F) escrito pelo shader do mundo (normal + depth linearizada, com quebra forçada entre tipos de turb), depois edge-detect num passo de pós-processo separado.

Consultado o Fable 5 sobre arquitetura Vulkan para isso. Recomendações principais:
1. Sem `VK_KHR_dynamic_rendering` — ficar em render pass clássico (apiVersion real no código é 1.0).
2. Attachment de normais **sempre alocado**, mesmo com outline desligado — gatear só a escrita via push constant (mesmo padrão do `pc.fxaaEnabled` no post-process), nunca duplicar pipelines.
3. Reusar `push.surfaceType` já existente, mas validar granularidade dos subtipos de turb antes de assumir 1:1 com o GL.
4. Estender `vk_post_process.frag` existente com um segundo binding, em vez de shader separado.
5. **Maior risco técnico**: MSAA + attachment de normais — todos os attachments de um subpass precisam da mesma contagem de samples; normais devem ser sempre single-sample, o que pode forçar mover pra subpass separado se MSAA estiver ativo. Resolver esse ponto de design antes de escrever qualquer código.

Outros cuidados: layout transition write→read do novo attachment (mesma barreira que já existe pra `sceneColor`, cuidando dos 2 frames em voo), formato RGBA16F pode ser caro em GPU mobile (considerar normal octaédrica `R16G16_SFLOAT`), `LOAD_OP_CLEAR` explícito pra não herdar lixo de tile em superfícies que pulam esse pipeline.

**Não iniciado ainda** — Tiago decidiu focar em performance primeiro. Retomar quando ele quiser.

### Regras e lições (sessão 2026-07-22)

- Nunca commitar/push sem autorização explícita do Tiago no momento da conversa atual.
- Build Windows deste worktree: junction `C:\eqspr`, script `_build_wip.bat`. Build gerado em `build-msvc-x64/RelWithDebInfo/ezquake.exe`, copiar para `C:\ezquake\ezquake.exe`. **Antes de copiar, sempre fechar o `ezquake.exe` em execução** (`Stop-Process -Force`) ou o `cp` falha com "Device or resource busy".
- **Cuidado com `continue.md`/`CONTINUE.md` em filesystem case-insensitive (Windows)**: são o MESMO arquivo físico. Um `git rebase`/`checkout` que troca entre um commit com `continue.md` e outro com `CONTINUE.md` pode sobrescrever silenciosamente o conteúdo sem conflito aparente. Sempre usar um só (este, maiúsculo, já que é o que está commitado) e nunca criar a variante minúscula à parte.
- Fable 5 é útil para segunda opinião de arquitetura/performance — mas sempre peça pra ele investigar o código real (Explore/leitura de arquivos), não opinar em abstrato, e sempre valide as conclusões dele contra o código antes de agir.
- Duas branches/worktrees irmãs, mesmo renderer Vulkan (`vk_*.c`): **Desktop** `E:\Projetos Linux\ezquake-sdl3-vulkan-pr` (branch `feature/sdl3-vulkan-pr`, PR #1145, regra inegociável: nada Android pode vazar pra cá) e **Android** `E:\Projetos Linux\ezquake-source` (branch `feature/android-pocket-vulkan`, sem PR ainda). Trabalho de Vulkan validado no desktop deve, quando não quebrar o Android, ser portado para os dois.
- **Este projeto tem pelo menos 3 frentes de trabalho concorrentes**: Codex rodando em Linux (`/home/tiago/...`, ver histórico abaixo), Claude rodando em Windows (`E:\Projetos Linux\...`), e o próprio Tiago. Sempre `git fetch`/checar `git log HEAD..origin/<branch>` antes de push — presumir que o remoto não mudou é o tipo de erro que quase causou perda de trabalho nesta sessão.

## Sessão 2026-07-05 (Codex, Linux) — histórico anterior, mantido como referência

Atualizado em: 2026-07-05 (sessão longa, muita coisa mudou — leia isso todo antes de continuar)

## Contexto geral

- Repo "canônico" limpo: `/home/tiago/ezquake-source` (branch `feature/sdl3-vulkan-pr`,
  sempre em sync com `origin`).
- Repo de trabalho ativo: `/home/tiago/projetoslinux/ezquake-source` (mesma branch) —
  é aqui que ficam as mudanças locais, ainda **não commitadas**.
- PR aberta: https://github.com/QW-Group/ezquake-source/pull/1145
  "Add Vulkan renderer backend and migrate desktop client to SDL3".
- Binário de teste: `/home/tiago/nquake/ezquake-vulkan-test` (copiado manualmente do
  build a cada mudança — não esquecer de recompilar + copiar antes de testar).
- Pra testar: `cd /home/tiago/nquake && DISPLAY=:0 ./ezquake-vulkan-test -condebug`,
  roda visível na tela real do usuário (não é headless). Log vai pra
  `qw/qconsole.log`. **Rodar o jogo via script/automação sem o usuário sentado na
  tela não é confiável** — o jogo parece não renderizar frames reais sem foco de
  janela, os screenshots automáticos saem pretos mesmo sem bug nenhum. Pra
  screenshot de verdade, ou pede pro usuário tirar (`screenshot` no console) ou
  aceita que só serve pra ver o console log, não pra validar visual.

## Bugs corrigidos nesta sessão (commitar em breve — ainda sem commit)

Todos os arquivos abaixo têm diff pendente em `projetoslinux/ezquake-source`,
nada commitado ainda. Revisar e limpar antes de commitar (tem debug logging do
Fable de uma sessão anterior misturado, ver seção seguinte).

1. **`vid_restart` reregistrando cvars/comandos toda vez que reinicia** (spam de
   "Can't register variable X, already defined" no console, achado real, não
   afeta visual mas é trabalho desnecessário a cada restart — candidato a
   contribuir pra instabilidade de FPS reportada). Faltava guard
   `if (!host_initialized)` em:
   - `src/vx_tracker.c` `InitTracker()` — tinha um guard **invertido**
     (`if (!qmb_initialized) return;`), que na real só controlava se
     texturas/coronas deviam recarregar, não se cvars deviam re-registrar.
     Corrigido: `if (host_initialized) return;` logo após o carregamento de
     texturas/coronas (que continuam rodando sempre), antes do bloco de
     `Cvar_Register`.
   - `src/cl_screen.c` `SCR_RegisterDamageIndicatorCvars()` — não tinha guard
     nenhum. Corrigido com early-return.
   - `src/sbar.c` `Sbar_Init()` — só o bloco de cvars/comandos
     (`scr_scoreboard_*`, `+/-showscores`, `+/-showteamscores`) ficou dentro do
     guard; o carregamento dos WAD pics e `CL_LoginImageLoad` continuam sempre.
   - `src/hud_editor.c` `HUD_Editor_Init()` — só o `Cmd_AddCommand("hud_editor")`
     + 4 cvars `hud_editor_allow*` ficaram dentro do guard; carregamento dos
     ícones de cursor continua sempre.
   Verificado: rodando `map dm3` + preset Fastest + `vid_restart` com
   `-condebug`, o spam de "already defined" foi de ~130 linhas pra zero.

2. **Preset "Fastest" com cores erradas no `r_drawflat` (Vulkan)** — CONFIRMADO
   visualmente e CORRIGIDO. Causa raiz: `VK_WorldFlatColorForSurface`
   (`src/vk_world.c`) usava a cor média da textura (`texture->flatcolor3ub`)
   pra superfícies normais de parede/chão, ao invés de `r_wallcolor`/
   `r_floorcolor`. Corrigido pra usar os cvars (igual o
   `GLC_SurfFlatColor` faz em `src/glc_brushmodel.c:195-213`).

3. **`r_drawflat` sem sombreamento/lightmap no Vulkan** (ficava "chapado",
   sem gradiente de luz que o GL mostra) — CONFIRMADO e CORRIGIDO. O pipeline
   `vk_world_flat.vert`/`.frag` nunca tinha textura de lightmap conectada.
   Adicionado:
   - atributo `lightmap_coords` no vertex shader (já existia no VBO
     compartilhado, só faltava expor).
   - sampler de lightmap (set=1, reaproveitando `VK_TextureDescriptorSetLayout()`
     genérico) no fragment shader.
   - novo campo `drawflatColor` no push constant (`vk_world_push_t` e nos
     shaders) pra distinguir "isso é r_drawflat de verdade" de "textura ainda
     não carregou, mostrando fallback" — só multiplica pela lightmap no caso
     verdadeiro.
   - `vk_world.c`: novo campo `drawflatCvar` em `vk_world_draw_t`, setado em
     `VK_WorldQueueSurface`; render loop agora bind a descriptor set da
     lightmap (com fallback pra `solidwhite_texture`) quando usa o pipeline flat.
   Usuário confirmou via screenshots (`ezquake012.png` vulkan vs `ezquake013.png`
   gl) que ficou "idêntico" depois desse fix.

**Ambos os fixes 2 e 3 ainda têm o diff de debug do Fable misturado em
`src/vk_aliasmodel.c`** (reativa `VK_AliasDebugLog`, log extra em draws de
arma) — isso é só instrumentação, não faz nada sozinho. Decidir se remove antes
de commitar ou mantém atrás de uma cvar de debug.

## Bug NÃO resolvido: fillet vermelho da granada/rocket sumindo no Vulkan

Confirmado visualmente várias vezes (`ezquake014/015.png` vulkan vs
`ezquake016.png` gl, e antes `vk_crop.png` vs `gl1_crop.png`/`gl2_crop.png`):
granada (`progs/grenade.mdl`, no chão) e o projétil da rocket launcher perdem
uma faixa vermelha (fullbright) que aparece normal no GL. **Usuário confirmou
que só trocou o `vid_renderer`, nenhum outro cvar** — ou seja, se aparece no
GL com as configs padrão, tem que aparecer no Vulkan também. Isso invalida a
hipótese de "só funciona com gl_program_aliasmodels 0" — o bug é real e size
está mesmo lá.

Investigação extensa (eu + 2 rounds de agente Fable) não achou a causa raiz por
leitura estática. O que já se sabe:
- `src/r_aliasmodel_skins.c` carrega a textura fullbright (`fb_texnum`) via
  caminho compartilhado (`Img_HasFullbrights` + `R_LoadTexture(...,
  TEX_FULLBRIGHT)`), igual pros 3 renderers.
- `src/r_aliasmodel.c:355-359` lê `paliashdr->glc_fb_texturenum[skin][anim]` e
  passa pro `renderer.DrawAliasFrame(...)` igual pros 3 renderers — sem
  bifurcação por renderer nesse ponto.
- `R_OverrideModelTextures` (`r_aliasmodel.c:277-279`) só zera `fb_texture` se
  `ent->full_light || !gl_fb_models.integer` — `gl_fb_models` é `1` por
  default, e o ruleset de multiplayer (`Rulesets_FullbrightModel`) força
  ambientlight/shadelight mas NÃO seta `full_light=true`, então `fb_texture`
  não deveria ser zerado no caso comum.
- `src/vk_aliasmodel.c` `VK_AliasQueueFullbrightDraw`/`VK_DrawAliasFrame`
  replica estruturalmente o que o GL clássico legado
  (`gl_program_aliasmodels 0`, não é o default) faz: pass base + pass extra
  alpha-blend com a fb_texture. Pela leitura, parece correto.
- Tanto Modern GL (`glm_aliasmodel.c:435-449`, `GLM_DrawAliasFrame` descarta o
  parâmetro `fb_texture` completamente) quanto Classic GL com
  `gl_program_aliasmodels 1` (default, `glc_aliasmodel.c:361-415`,
  `GLC_DrawAliasFrameImpl_Program` usa a texture unit 1 pra caustics
  subaquático, não pra fb_texture) **também não desenham esse overlay**. Então
  a teoria de "overlay separado que o Vulkan não desenha direito" ficou capenga
  — se nem o GL default desenha um overlay separado, e mesmo assim a faixa
  aparece lá, então a faixa provavelmente é parte da **textura base** do
  modelo (não um overlay), mostrada "crua"/sem sombra quando
  `Rulesets_FullbrightModel` força brilho total — e o bug real estaria no
  **pass base do Vulkan** (`VK_AliasQueueDraw`, `src/vk_aliasmodel.c:606-648`),
  não no `VK_AliasQueueFullbrightDraw`.

**Próximo passo recomendado** (ainda não feito): parar de ler código e
instrumentar de verdade. Estender `VK_AliasDebugLog` (hoje só cobre
`draw->weapon`, não cobre entidades de mundo tipo granada/rocket) pra logar
`R_AliasModelColor`/textura escolhida no pass base pra esses modelos
especificamente, e comparar com um log equivalente no GL (`Con_Printf`
temporário em `glc_aliasmodel.c`/`glm_aliasmodel.c`) rodando exatamente a
mesma cena. Precisa do usuário sentado na tela pra reproduzir (screenshot
automatizado não presta, ver aviso lá em cima).

## Auditoria completa GLM → Vulkan (Fable, 2026-07-05)

Pedido explícito do usuário: implementar essa lista **uma por uma, da mais
difícil pra mais fácil**. Ordem de trabalho:

1. [x] **IMPLEMENTADO nesta sessão, ainda não testado ao vivo.** Fog inexistente no Vulkan (grande) — nenhum `r_fx_fog*` é lido em
   nenhum shader Vulkan. GLM injeta fog globalmente via `#define DRAW_FOG` em
   `src/gl_program.c:521`, `applyFog`/`applyFogBlend` (`:1578-1636`), uniforms
   `fogDensity`/`fogColor` de `src/glm_misc.c:161`. Usado em
   `draw_world.fragment.glsl:169,205,214,264`, `draw_aliasmodel.fragment.glsl`,
   `draw_sprites.fragment.glsl:22`. Precisa adicionar fog aos push constants +
   cálculo em todos os fragment shaders Vulkan (mundo, alias, sprite, flat).

   **Como foi implementado**: `r_refdef2.fog_*` já é populado todo frame por
   código compartilhado (`R_ConfigureFog`, `src/r_rmain.c:314`) a partir dos
   cvars `r_fx_fog*` — Vulkan só precisava LER, não reimplementar parsing de
   cvar. Adicionado aos push constants de `vk_world_push_t` (+16 bytes:
   fogColor vec4 + fogDensity/fogLinearStart/fogLinearEnd/fogCalculation,
   struct foi de 144→160 bytes), `vk_alias_push_t` (124→160 bytes) e
   `vk_sprite3d_push_t` (160→176 bytes). Fórmulas linear/exp/exp2 replicadas
   em GLSL em cada `.frag` (`vk_world_textured.frag`, `vk_world_lightmapped.frag`,
   `vk_world_alpha_textured.frag`, `vk_world_flat.frag`, `vk_alias_model.frag`,
   `vk_sprite3d.frag`). **Diferença de abordagem vs. GL**: ao invés de
   replicar `gl_FragCoord.z/w` (depende da convenção de clip-space do GL, que
   diverge da do Vulkan), uso distância real até a câmera
   (`length(worldPos - cameraPosition)`) — visualmente equivalente, mais
   simples, não depende de reverter a conversão de depth range que os vertex
   shaders já fazem (`clip.z = clip.z * 0.5 + clip.w * 0.5`). Pra mundo e
   sprites é por-fragmento (varying interpolado); pra alias models é um
   único escalar por entidade calculado na CPU (`VectorDistance(ent->origin,
   r_refdef.vieworg)`, `src/vk_aliasmodel.c` em `VK_AliasQueuePreparedDraw`)
   já que os modelos são pequenos o suficiente pra não fazer diferença visual
   por-pixel. Céu (`vk_world_flat.frag`) usa blend constante com
   `r_fx_fog_sky` (guardado no canal alfa não usado de `fogColor`) ao invés
   de fog por profundidade, já que o céu é "infinitamente longe".

   **NÃO TESTADO AO VIVO AINDA** — compilou limpo, mas preciso confirmar
   visualmente. Pra testar: `r_fx_fog 1` (ou `2` = só debaixo d'água),
   `r_fx_fog_density` (default 0.125), `r_fx_fog_start`/`r_fx_fog_end` (fog
   linear), `r_fx_fog_sky` (quanto o céu pega fog, default 0.3) — ou entrar
   num mapa/servidor que force fog (`cl.map_fog_density` via server, tipo
   alguns mapas customizados). Comparar com GL no mesmo cenário.

2. [ ] **REVERTIDO/DESATIVADO — precisa redesign, não tentar de novo sem
   isso.** Outlines de mundo (`gl_outline & 2`). Testado ao vivo com o
   usuário e causou vários bugs sérios (ver "O que deu errado" abaixo). Todo
   o código da tentativa continua no repo, só desativado via
   `VK_WorldNormalsAttachmentActive()` (`src/vk_renderpass.c`) sempre
   retornando `false` — não apagar essa implementação, só não reativar sem
   redesenhar a parte de sincronização/lifetime dos recursos primeiro.

   **O que deu errado (testado ao vivo, várias rodadas)**:
   1. Tela piscando tipo "night club" ao ligar `gl_outline 2`. Causa #1:
      `worldNormalsImage` era um recurso ÚNICO compartilhado entre todos os
      swapchain images (copiei o padrão de depthImage/msaaColorImage, que
      são só render target), mas esse é lido pelo pós-processo no MESMO
      frame em que é escrito — com múltiplos frames em voo, uma escrita
      pisava em cima da leitura. Corrigido virando array por-imagem.
      Piscar continuou.
   2. Faltava barreira de sincronização explícita entre a passada principal
      (escreve worldNormals) e o pós-processo (lê worldNormals) dentro do
      MESMO frame — adicionada em `VK_PostProcessTransitionForSampling`
      (`vk_draw.c`). Ajudou mas não resolveu tudo.
   3. Instalado `vulkan-validation-layers` (não vinha instalado no sistema)
      pra conseguir diagnóstico real com `-dev`. Achado:
      **`VkDescriptorSet ... was destroyed or updated without
      UPDATE_AFTER_BIND`** — um descriptor set sendo destruído enquanto um
      command buffer que ainda o referencia (mesmo que não tenha sido
      submetido ainda) é invalidado por spec. Rastreado até
      `VK_DestroyPostProcessResources`/`VK_WorldResourcesShutdown`
      destruindo pools de descriptor sem esperar a GPU
      (`vkDeviceWaitIdle` adicionado em `VK_DestroyPostProcessResources`,
      ajudou mas não eliminou 100% — o problema real é que
      `vkDeviceWaitIdle` sozinho não protege um command buffer que JÁ
      COMEÇOU a ser gravado no mesmo frame em que o recurso é destruído;
      precisa garantir que a recriação só acontece numa fronteira de frame
      bem definida, não em qualquer evento tipo vid_restart no meio do
      caminho).
   4. Efeito colateral concreto do bug: texturas com partes pretas
      (normalmente onde teria sombra), HUD "vazando" o mapa por trás ao dar
      tab (blend state corrompido), e um bug LATENTE pré-existente do
      screenshot Vulkan exposto de brinde (a imagem do swapchain nunca teve
      a flag `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` que a cópia do screenshot
      exige — só nunca deu erro porque o driver AMD/RADV é tolerante; não
      tem relação com o outline, só ficou visível com a validação ligada).
   5. Também confirmado (não é bug): outline de mundo só aparece com
      `sv_cheats 1` + ruleset default —
      `RuleSets_DisallowModelOutline(NULL)` (`src/rulesets.c:95-99`) já
      bloqueia isso por design em qualquer renderer, bate com o texto de
      ajuda do próprio `gl_outline`.

   **Lição pra próxima tentativa**: a abordagem "forçar o pós-processo
   sempre ativo + redesenhar o mundo de novo numa pipeline dedicada" tem
   efeitos colaterais grandes demais pra small trial-and-error ao vivo.
   Repensar: (a) recriação de descriptor pools/framebuffers deveria
   acontecer só entre frames, com fence/wait garantido antes de qualquer
   novo command buffer começar a gravar; (b) considerar não forçar
   `VK_PostProcessActive()` e em vez disso ter uma passada de composição
   MENOR e dedicada só pro outline, sem depender do pipeline de
   gamma/contrast/FXAA; (c) testar cada mudança pequena com `-dev` +
   validação ligada desde o início dessa vez (agora já está instalado).

   ---

   Escrita original da tentativa (mantida como referência técnica):

   GLM tem passe de geometria (`DRAW_GEOMETRY`,
   `src/glm_rsurf.c:151,212-214`, saída `normal_texture`) + pós-passe
   `src/glsl/fx_world_geometry.fragment.glsl` (`GLM_DrawWorldOutlines`,
   `src/glm_rmain.c:63,131`).

   **Como foi implementado (bem diferente do GLM, ver histórico da conversa
   pra entender por que)**: o usuário pediu explicitamente pra fazer via MRT
   (múltiplos render targets, igual o GLM), mas o SPIR-V do Vulkan aqui é
   pré-compilado em build-time (não runtime como o GLSL do GLM), então não
   dá pra ter "variantes" de shader condicionais por `#ifdef` fácil. Solução:
   em vez de fazer TODOS os shaders de mundo escreverem num segundo
   attachment condicionalmente, criei um **pipeline dedicado e uma passada
   extra**:
   - `src/vk_local.h` + `src/vk_swapchain.c`: nova imagem
     `worldNormalsImage`/View (`VK_FORMAT_R8G8B8A8_UNORM`, single-sample,
     compartilhada entre swapchain images como o depth buffer). **Só existe
     quando MSAA está desligado** (`VK_WorldNormalsAttachmentActive()` em
     `vk_renderpass.c`) — misturar attachment single-sample num subpass
     multisample não é portável em Vulkan core sem extensão, e não valia a
     complexidade de um resolve extra só pra isso. Ou seja: **outline não
     funciona com MSAA ligado**, cai silenciosamente pra sem-outline (não é
     bug, é limitação documentada).
   - `src/vk_renderpass.c`: `vk_renderpass_main`/`_noclear` agora sempre tem
     3 attachments — com MSAA é (msaaColor, depth, resolve) como antes; sem
     MSAA é (color, depth, worldNormals). `VK_WorldNormalsAttachmentActive()`
     exposta em `vk_local.h` pra todo mundo que precisa saber quantos
     color-blend-attachments declarar.
   - **Todo pipeline que desenha nessa render pass precisou de um 2º
     color-blend-attachment** pra bater com o subpass (senão é erro de
     validação/pipeline inválido): mundo (4 pipelines em `vk_world.c`), alias
     models (`vk_aliasmodel.c`), sprites (`vk_sprite3d.c`), HUD/2D
     (`vk_draw.c`, via `VK_BlendingConfigure` que ganhou um parâmetro
     `mainRenderPass` novo). Em todos esses o 2º attachment é
     **write-disabled** (`colorWriteMask=0`, não escreve nada) — nenhum
     deles escreve normal de verdade.
   - `src/vulkan_shaders/vk_world_normals.vert` + `.frag` (novos, registrados
     em `CMakeLists.txt`): pipeline dedicado (`VK_WorldCreateNormalsPipeline`
     em `vk_world.c`) que **redesenha a geometria opaca do mundo já visível
     uma segunda vez**, só com atributo de posição (reaproveita
     `vbo_world_vert_t`), depth-test igual ao das outras (LEQUAL/GEQUAL) mas
     `depthWriteEnable=false` (não escreve depth de novo, só usa o que já
     foi escrito pra saber o que é frontmost), sem descriptor sets (só push
     constant com o mvp). O fragment shader calcula a normal via
     `dFdx`/`dFdy` da posição no mundo (sem precisar de normal por-vértice
     no VBO) e escreve `vec4(normal*0.5+0.5, gl_FragCoord.z)` no attachment 1.
     Chamado num loop novo em `VK_RenderView` (`vk_world.c`), logo depois do
     loop principal, iterando `worldDraws[]` e pulando os `blended` (só
     opacos contam pro silhouette).
   - Composição final: **não** um passe de render separado — estendi o
     shader de pós-processo que já existe (`vk_post_process.frag`,
     `ApplyWorldOutline`), que agora também amostra `worldNormals` (binding 1
     novo) e compara os 4 vizinhos (`texelFetch`) pra detectar silhueta
     (produto escalar de normais) e crista (2ª derivada de profundidade,
     como o GLC faz). Push constant novo em `vk_post_process_push_t`
     (`vk_draw.c`): `outlineColor` (rgb dos cvars + alpha como flag
     liga/desliga), `outlineDepthThreshold`, `outlineNormalThreshold`,
     `outlineScale`.
   - **IMPORTANTE**: `VK_PostProcessActive()` (`vk_swapchain.c`) precisou
     ganhar um `R_DrawWorldOutlines() && VK_WorldNormalsAttachmentActive()`
     no gate — sem isso, o passe de composição inteiro (onde o outline é
     desenhado) só rodava quando gamma/contraste/FXAA já estavam ativos, e o
     outline ficaria morto em silêncio com configs padrão.
   - **Discrepância conhecida de calibração**: o GLC/GLM usam profundidade
     LINEAR (`r_zFar * diff`) pro teste de crista; aqui uso
     `gl_FragCoord.z` do Vulkan, que é profundidade NÃO-linear (device
     depth). Isso significa `gl_outline_world_depth_threshold` (default "4")
     se comporta numa escala bem diferente — na prática, a detecção de
     crista provavelmente não dispara com o valor default (silhueta via
     normal ainda funciona normal). Precisa recalibrar esse cvar
     especificamente pro Vulkan, ou aceitar que só a silhueta funciona por
     enquanto.

   **BUG achado e corrigido ao vivo**: primeira versão fazia a tela inteira
   piscar tipo "night club" assim que `gl_outline 2` era ligado. Causa:
   `worldNormalsImage`/View eram um recurso ÚNICO compartilhado entre todos
   os swapchain images (copiei o padrão de `depthImage`/`msaaColorImage`,
   que são "nunca amostrados, só render target" — mas o world-normals É
   amostrado pelo post-process no MESMO frame em que é escrito). Com
   `VK_MAX_FRAMES_IN_FLIGHT > 1`, o frame N+1 podia começar a escrever nessa
   imagem enquanto o frame N ainda estava lendo ela no passe de composição —
   race condition clássica de GPU, gerando aquele "strobing". Corrigido
   convertendo pra **um array por swapchain image** (`worldNormalsImages[]`/
   `worldNormalsImageMemory[]`/`worldNormalsImageViews[]` em `vk_local.h`),
   exatamente como `postProcessColorImages[]` já fazia (o comentário
   original dele já explicava esse hazard — eu só não apliquei a mesma
   lição na hora de criar o recurso novo). Mexeu em `vk_swapchain.c`
   (criação/destruição agora em loop, uma por imagem) e `vk_draw.c`
   (descriptor set usa `worldNormalsImageViews[imageIndex]`).

   **Testado**: compilação limpa (build incremental E do zero), roda sem
   crash/sem erro no console com `-condebug` (startup+quit, e
   `map dm3` + `gl_outline 2` + 60 frames + quit) — inclusive depois do fix
   do piscar. **Ainda não confirmado se o outline aparece visualmente
   correto** (o usuário só confirmou que o piscar sumiu, não testou a
   qualidade/calibração do efeito em si ainda).
3. [ ] **Pós-processo incompleto** (grande) — sem tonemap HDR
   (`EZ_POSTPROCESS_TONEMAP`, `src/glm_framebuffer.c:91,104-105`), sem
   framebuffer separado 3D/HUD (`glm_framebuffer.c:90,102`),
   `VK_FramebufferCreate` retorna false, `R_SUPPORT_FRAMEBUFFERS` não é
   anunciado (`vk_main.c:1002-1006,1054`) — `vid_framebuffer*`/supersampling
   não existem. FXAA é aproximação de 4 taps, não o FXAA 3.11 real
   (`vk_post_process.frag:18-23`, comentário já admite).
4. [ ] **Caustics subaquáticos (`gl_caustics`) inexistentes** (médio) — GLM em
   `src/glm_rsurf.c:133,141,167-172,304` (mundo) e
   `src/glm_aliasmodel.c:154,163-168,424,535` (modelos). Vulkan descarta
   explicitamente: `src/vk_world.c:1683` (`(void)caustics;`). Precisa sampler
   extra + mix no fragment dos pipelines lightmapped/textured e alias.
5. [ ] **Modelos alias sem iluminação direcional por vértice** (médio) — GLM
   calcula por normal do vértice (`draw_aliasmodel.vertex.glsl:66-77`). Vulkan
   usa escalar único por modelo (`src/vk_aliasmodel.c:628`). Deixa
   jogadores/itens/armas com brilho uniforme, sem volume. Precisa passar
   `shadelight`/`ambientlight`/`yaw_angle_rad` nos push constants e replicar a
   fórmula no vertex shader.
6. [ ] **`r_drawflat_mode` 1 e 2 (tinted/bright) não implementados** (médio) —
   GLM suporta via `DRAW_DRAWFLAT_TINTED`/`DRAW_DRAWFLAT_BRIGHT`
   (`src/glm_rsurf.c:145-147`, `draw_world.fragment.glsl:56-86`). Vulkan só
   funciona com `r_drawflat_mode 0` (`src/vk_world.c:1639-1640` — número de
   linha pode ter mudado depois do fix do item 2/3 acima, conferir). Com modo
   1/2 o Vulkan mostra o mundo texturizado normal, como se drawflat estivesse
   desligado.
7. [ ] **Luma de mundo não modulada pela lightmap** (médio) — GLM soma luma
   antes de multiplicar pela lightmap quando `gl_fb_bmodels 0`
   (`draw_world.fragment.glsl:241-245`) e faz decal quando `gl_fb_bmodels 1`
   (`:246-254`). Vulkan desenha luma como segundo passe aditivo depois da base
   já iluminada (`src/vk_world.c` `VK_WorldCreateOverlayPipeline` ~linha 956,
   dispatch ~1724-1755) — luma sempre brilha 100% mesmo no escuro, e a
   diferença `gl_fb_bmodels 0/1` desaparece.
8. [ ] **Água iluminada (lit turb) ausente** (pequeno/médio) — GLM marca
   `texture->isLitTurb` (`src/glm_rsurf.c:365`) e multiplica lightmap na turb.
   Vulkan exclui todo `SURF_DRAWTURB` da lightmap
   (`src/vk_world.c:482-484` `VK_WorldLightmapTextureForSurface`).
9. [ ] **Skywind (`r_skywind` + `*_wind.cfg`) ausente** (pequeno/médio) — GLM
   `DRAW_SKYWIND` (`src/glm_rsurf.c:137,153,198-200`;
   `draw_world.fragment.glsl:179-192`; dados em
   `src/r_brushmodel_sky.c:40-49,120`). Nada equivalente em
   `vk_world_flat.frag`.
10. [ ] **`r_lerpmuzzlehack` ignorado** (pequeno) — GLM tem
    `EZQ_ALIASMODEL_MUZZLEHACK` (`src/glm_aliasmodel.c:155,180-181`;
    `draw_aliasmodel.vertex.glsl:54-56`). VBO Vulkan grava o flag
    (`src/vk_renderer_stubs.c:298`) mas `vk_alias_model.vert` não tem atributo
    de flags e lerpa tudo sempre — viewmodel "estica" durante muzzleflash.
11. [ ] **Skybox com clamp fixo em 512px** (pequeno) — `vk_world_flat.frag:84`
    assume face 512×512. GLM usa `samplerCube` real. Skyboxes com resolução
    diferente têm costuras.
12. [ ] **Outline de jogador sem separar cor de cima/baixo** (pequeno) — GLM
    pinta pernas com `bottomcolor` por-fragmento
    (`draw_aliasmodel.fragment.glsl:30-56`). Vulkan só escolhe `topcolor` na
    CPU (`src/vk_aliasmodel.c:452-478`).
13. [ ] **`gl_textureless` vaza pra brush models** (pequeno) — GLM restringe
    ao mundo (`Flags & EZQ_SURFACE_WORLD`,
    `draw_world.vertex.glsl:110-113`). Vulkan seta pra todo draw
    (`src/vk_world.c:1916`, número de linha pode ter mudado).
14. [ ] **`r_dynamic 2` (compute shader) sem equivalente** — SEM IMPACTO
    VISUAL (cai pro caminho software que já funciona). Fable marcou como
    "aceitável documentar", não precisa implementar de verdade — grande
    esforço pra zero ganho visual. Deixar de fora da lista de trabalho, só
    documentado aqui.

Itens verificados como OK (não são gaps, não mexer): powerup shells, r_shadows
(Vulkan até implementa melhor que GLM), dynamic lights em lightmaps,
polyblend/cshifts, fastturb/fastsky com cores dos cvars, detail textures, HUD,
wateralpha.

## Lembrete de processo

- Sempre recompilar (`cmake --build build -j$(nproc)` dentro de
  `projetoslinux/ezquake-source`) E copiar o binário
  (`cp build/ezquake-linux-x86_64 /home/tiago/nquake/ezquake-vulkan-test`)
  antes de pedir pro usuário testar — fácil esquecer o `cp` e o usuário testar
  o binário velho.
- Matar o processo antigo antes de subir um novo
  (`pgrep -fa ezquake-vulkan-test`, `kill -TERM`/`-KILL` se não morrer).
- Não editei/commitei nada via `git commit` ainda nesta sessão — tudo é diff
  local em `projetoslinux/ezquake-source`. Perguntar antes de commitar.
- `vulkan-validation-layers` foi instalado no sistema nesta sessão (Arch/
  CachyOS, pacote `vulkan-validation-layers`). Sempre testar com `-dev
  -condebug` e checar `qw/qconsole.log` por `VUID`/"invalid state"/
  "destroyed" antes de considerar uma mudança Vulkan pronta — não só
  compilar, rodar de verdade com validação.

## REVERT TOTAL desta sessão (2026-07-05)

Depois de uma tentativa de implementar outlines de mundo via MRT causar
piscamento de tela, texturas pretas e corrupção de HUD ao vivo (ver histórico
completo do item 2 na auditoria acima), o usuário pediu revert total pro
estado de antes de qualquer trabalho do Fable (fog + outlines). Feito via
`git checkout -- <arquivo>` nos arquivos que só tinham fog/outline, e reescrita
manual em `vk_world.c`/`vk_world_flat.frag` (que misturavam fog+outline com o
fix de drawflat/lightmap pré-Fable) partindo do `git show HEAD:...` original.
Estado atual = só os fixes pré-Fable (cvars, drawflat cor/lightmap), fog e
outline **zerados**, precisam ser refeitos do zero quando retomar a auditoria.
Confirmado pelo usuário: "parece ter corrigido, esta igual gl e vulkan".

**Bug pré-existente descoberto durante o revert, NÃO introduzido nesta sessão
nem pelo Fable** — continua acontecendo mesmo no estado 100% revertido:
- `VkDescriptorSet ... was destroyed or updated without UPDATE_AFTER_BIND` +
- `vkQueueSubmit(): pSubmits[0].pSignalSemaphores[0] ... may still be in use
  by VkSwapchainKHR` (semáforo reusado antes de ser reapresentado).

Causa provável raiz de ambos: em `src/vk_main.c`, `imageAvailableSemaphores`/
`renderFinishedSemaphores` são arrays indexados por **frame-in-flight**
(`frameIndex`, cicla 0..VK_MAX_FRAMES_IN_FLIGHT-1), mas deveriam ser
indexados pela **imagem do swapchain** (`imageIndex`) — prática recomendada
pela própria doc do Vulkan (linkada no erro:
https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html). Quando
`VK_MAX_FRAMES_IN_FLIGHT != imageCount`, os dois contadores dessincronizam
com o tempo, o semáforo pode ser reaproveitado enquanto ainda em uso pela
apresentação de uma imagem diferente — e a CPU pode achar que pode reusar
recursos (descriptor sets) de um frame antes da GPU/apresentação realmente
terminar, explicando o outro erro também.

**Usuário decidiu NÃO corrigir agora** (perguntei explicitamente, ver
[[feedback-vulkan-incremental-testing]] na memória) — deixar documentado pra
uma sessão futura dedicada a isso, separada da auditoria do Fable. Fix
esperado: trocar os arrays de semáforo pra serem indexados por `imageCount`
(um semáforo por imagem do swapchain) em vez de por frame-in-flight, seguindo
o padrão recomendado.
