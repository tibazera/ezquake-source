# Agente do projeto ezQuake Pocket Vulkan

Este arquivo é a memória operacional compartilhada entre Codex, Claude e futuros colaboradores. Leia-o por inteiro antes de alterar qualquer arquivo. Leia também `docs/android-pocket-vulkan-handoff.md`.

## Regra mais importante

O diretório local `E:\Projetos Linux\ezquake-source` é a fonte de verdade mais fiel do projeto. O worktree pode conter avanços ainda não commitados e, nesse caso, ele é mais atual que o branch remoto, o histórico Git ou qualquer PR.

Antes de trabalhar:

1. Confirme que o diretório atual é `E:\Projetos Linux\ezquake-source`.
2. Execute `git status --short`, `git branch --show-current` e examine os diffs locais.
3. Preserve toda mudança local que não pertença claramente à tarefa atual.
4. Nunca use `git reset --hard`, `git checkout --`, `git clean` ou equivalentes para apagar o worktree.
5. Não faça commit, push, force-push nem abra/atualize PR automaticamente. Faça isso somente quando Tiago pedir naquele momento.

`E:\Projetos Linux\unezquakepocket` não é o código-fonte. Essa pasta é usada apenas para receber APKs de teste quando solicitado. Não implemente mudanças nela.

## Objetivo e limites atuais

O projeto usa o ezQuake/uNezQuake como base e busca um cliente QuakeWorld Android arm64 moderno com SDL3, Vulkan, controles utilizáveis e desempenho adequado em smartphones topo de linha.

O branch de desenvolvimento é `feature/android-pocket-vulkan` no repositório `tibazera/ezquake-source`. Os commits servem para preparar, mais tarde, um PR completo e limpo para o grupo `qw-group`. Esse PR upstream ainda não é o foco. Não ofereça, publique ou envie mudanças ao upstream sem autorização explícita.

Não substitua a base por FTEQW. FTEQW e vkQuake são referências para soluções pontuais de Android, Vulkan e desempenho. Importe ideias apenas depois de medir o gargalo e adaptar a solução à arquitetura do ezQuake.

## Estado técnico alcançado

- Build Android arm64 com SDL 3.4, Vulkan e Oboe.
- Bootstrap Java atualizado para os arquivos oficiais do SDL 3.4.
- `EzQuakeActivity.getMainFunction()` retorna `main` e o `main` nativo Android possui visibilidade exportada.
- APIs de janela, eventos, entrada e superfície Vulkan foram adaptadas para SDL3.
- Retornos booleanos do SDL3 foram corrigidos em inicialização, mutexes, semáforos e criação de superfície.
- Captura VOIP foi desativada no Android; reprodução continua pelo backend Oboe.
- Atualizações dinâmicas de lightmaps são enfileiradas e copiadas por um staging buffer persistente no command buffer do frame seguinte.
- O caminho normal de atualização de lightmaps não cria vários command buffers imediatos nem executa `vkQueueWaitIdle` por atualização.
- O loop Vulkan permite dois frames em voo. Cada frame possui semáforos, fence e staging de lightmap próprios; cada imagem do swapchain rastreia seu último fence.
- A superfície de render Android é limitada ao envelope 1920x1080, preservando a proporção física e sem fazer upscale. No Xiaomi 2712x1220, o buffer validado é 1920x864 e o SurfaceView é escalado pelo compositor para preencher a tela.
- `SDLActivity.mLayout.addView(mSurface, ...)` agora passa `RelativeLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)` explícito. Sem isso, o `RelativeLayout` usava `WRAP_CONTENT` por padrão e, depois do `setFixedSize()` do item acima, o `SurfaceView` media seu próprio tamanho preferido como o buffer fixo (1920x864) em vez de preencher a tela — confirmado via `dumpsys SurfaceFlinger` (bounds da layer do SurfaceView = 1920x864 contra os 2712x1220 da Activity) e visualmente (área preta cobrindo o resto da tela). Corrigido; confirmado visualmente no Xiaomi que a tela volta a preencher por completo.
- dm3, dm4 e dm6 voltaram a carregar com texturas depois de isolar um conflito de conteúdo PK3 no aparelho.

Commits de referência no momento em que esta memória foi criada:

- `675fb197` — migração do cliente Android/Vulkan para SDL3.
- `cea4a636` — batching das atualizações dinâmicas de lightmap.
- `691a5959` — dois frames Vulkan em voo com sincronização e staging por frame.
- `53fdaa54` — superfície Android limitada proporcionalmente ao envelope 1920x1080.
- `e33e1597` — handoff Android/Vulkan inicial.
- `eff9b8f2` — estratégia documentada para o conflito de PK3.

Esses hashes são marcos, não substitutos do worktree local.

## Build validado

Ambiente usado:

- JDK 17: `C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot`
- Android SDK: `C:\Android\Sdk`
- NDK: `C:\Android\Sdk\ndk\28.2.13676358`
- glslang host tools: `C:\eqs\vcpkg\installed\x64-windows\tools\glslang`

No PowerShell, configure as variáveis e execute:

```powershell
$env:JAVA_HOME='C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot'
$env:ANDROID_HOME='C:\Android\Sdk'
$env:ANDROID_SDK_ROOT='C:\Android\Sdk'
$env:ANDROID_NDK_HOME='C:\Android\Sdk\ndk\28.2.13676358'
$env:Path='C:\eqs\vcpkg\installed\x64-windows\tools\glslang;' + $env:Path
.\gradlew.bat :app:assembleDebug
```

APK gerado: `app\build\outputs\apk\debug\app-debug.apk`.

Dispositivo usado nos testes: Xiaomi 2407FPN8EG, codinome `rothko`, Android 16, serial ADB `LJN7LFRKXG79P7J7`. O serial pode mudar ou o aparelho pode estar desconectado; sempre confirme com `C:\Android\Sdk\platform-tools\adb.exe devices`.

## Erros cometidos e o que aprendemos

### Diretório errado

No início, houve trabalho e investigação apontando para `unezquakepocket`. O fonte real passou a ser `E:\Projetos Linux\ezquake-source`; `unezquakepocket` é somente entrega de APK. Sempre confirme o diretório antes de editar ou executar Git.

### Texturas tratadas inicialmente como defeito Vulkan

dm3, dm4 e dm6 carregavam lava, portais e itens, mas paredes e chão apareciam sem as texturas esperadas. e1m2 e aerowalk funcionavam. Isso levou a várias hipóteses no upload Vulkan, BSP e sobreposição de texturas.

A causa confirmada no aparelho foi `id1/gpl_maps.pk3`: ele continha `maps/dm3.bsp`, `maps/dm4.bsp` e `maps/dm6.bsp` em variantes simple-texture e tinha prioridade sobre os mapas originais de `pak1.pak`. Remover somente essas três entradas restaurou imediatamente os mapas texturizados.

Não transforme isso em hack de renderer, regra especial por nome de mapa ou reescrita automática de arquivo. A correção planejada é criar ou obter outro PK3 limpo, sem esses BSPs substitutos, e validar sua prioridade de carregamento. Não versione dados do Quake neste repositório. No aparelho foi mantido um backup diagnóstico chamado `gpl_maps.pk3.simpletextures-backup`; isso não é parte do projeto.

### Migração SDL2 para SDL3 feita além da troca de dependência

Trocar `sdl2` por `sdl3` no vcpkg/CMake não bastou. SDL3 mudou assinaturas, nomes de eventos, estruturas de teclado, APIs Vulkan, retorno de inicialização, mutexes e semáforos. O projeto usa temporariamente `SDL_ENABLE_OLD_NAMES`, mas toda chamada alterada deve respeitar a semântica SDL3 real.

O erro mais grave foi manter a expressão SDL2 `SDL_TryLockMutex(mutex) == 0`. No SDL2, sucesso era zero; no SDL3, a função retorna `bool`. A chamada adquiria o mutex, interpretava sucesso como falha e não o liberava. Isso causou silêncio e a assertion em `SDL_TryLockMutex`/`SDL_sysmutex.c:116`. A forma correta atual é testar diretamente o retorno booleano.

Ao portar outras chamadas, confira a documentação/headers da versão SDL3 instalada; não deduza a semântica apenas pelo nome antigo.

### Oboe acusado quando o scrcpy desviava o áudio

O backend Oboe do APK do Lele já funcionava e é essencialmente o mesmo. Instrumentação temporária confirmou PCM não zero no callback, AAudio iniciado e player sem mute. O silêncio restante ocorria porque o scrcpy captura áudio por padrão e roteava a saída para `AUDIO_DEVICE_OUT_REMOTE_SUBMIX`, retirando-a do alto-falante do telefone.

Para manter áudio no celular:

```powershell
scrcpy --serial LJN7LFRKXG79P7J7 --keyboard=uhid --mouse=uhid --no-audio
```

Não abra instâncias duplicadas do scrcpy. Verifique processos existentes antes de iniciar outra. Se o objetivo for ouvir no computador, remova `--no-audio` e confira o dispositivo de saída do Windows.

Durante o diagnóstico, o callback Oboe foi temporariamente alterado para bloquear no mutex e foram adicionados logs de amostras. Essas experiências foram revertidas. O callback final permanece não bloqueante como no port do Lele; a correção real foi a semântica SDL3 do mutex.

### Tela preta depois de limitar a resolução de render

Depois de introduzir `getHolder().setFixedSize()` em `SDLSurface` para limitar o buffer ao envelope 1920x1080, a tela passou a mostrar o jogo renderizado num retângulo no canto superior esquerdo, com o resto preto. A causa não foi o compositor: `SDLActivity` adicionava o `SurfaceView` ao `RelativeLayout` sem `LayoutParams` explícito (`mLayout.addView(mSurface)`), então o layout usava `WRAP_CONTENT`. Sem tamanho fixo de buffer isso nunca importava; com o buffer fixo, o `onMeasure()` do `SurfaceView` passou a reportar o tamanho fixo como preferido, e a View ficou do tamanho do buffer (1920x864) em vez de preencher a Activity (2712x1220). Confirmado via `dumpsys SurfaceFlinger` comparando `bounds`/`geomLayerBounds` da layer do SurfaceView com a layer da Activity. Corrigido passando `RelativeLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)` no `addView`. Ao tocar em `setFixedSize`/dimensionamento de buffer em qualquer SurfaceView, confirme sempre que o `LayoutParams` do pai força preenchimento total — não assuma que o compositor escala automaticamente sem isso.

### Entrada remota no Xiaomi

`adb shell input` e a injeção normal do scrcpy falharam com `INJECT_EVENTS`. O modo UHID de teclado e mouse funciona sem essa permissão adicional. Não conclua que o SDL3 perdeu entrada antes de separar restrição do Android/Xiaomi de falha no engine.

### Aplicativo que não iniciava

Após a migração, o APK chegou a não abrir. Foram necessários o bootstrap Java SDL3 correto, `getMainFunction()` retornando `main`, símbolo nativo exportado e correções de ciclo de vida/retornos booleanos. Evite desfazer esses pontos ao sincronizar arquivos Java do SDL.

## Decisões de desempenho e riscos conhecidos

O primeiro gargalo atacado foi a atualização dinâmica de lightmaps, que usava command buffers imediatos e esperas de fila repetidas. O staging atual reduz essas sincronizações e introduz uma latência de até um frame para uploads enfileirados.

O renderer originalmente usava um único fence global e esperava no começo de todo frame. Isso anulava o benefício das várias imagens do swapchain e podia provocar degraus de frame pacing em FIFO. A implementação atual usa dois frames em voo, synchronization objects por frame, fence por imagem e staging de upload separado por frame. Se o número de frames em voo mudar, mantenha todas essas estruturas dimensionadas em conjunto.

No Xiaomi de teste, o surface em paisagem é 2712x1220 e o display físico foi observado a 120 Hz. O build com dois frames em voo iniciou e permaneceu estável, sem fatal ou erro Vulkan no logcat. O ganho real de FPS em dm3/dm4/dm6 ainda não foi medido; não o trate como resultado confirmado.

Para evitar custo de fill-rate desnecessário, `SDLSurface` chama `SurfaceHolder.setFixedSize()` com um tamanho que cabe em 1920x1080 e mantém o aspect ratio do aparelho. Exemplos: 1920x1080 permanece 1920x1080; 2712x1220 vira 1920x864; aparelhos menores mantêm sua resolução nativa. O tamanho físico do `View`, e não o tamanho reduzido do buffer, deve ser usado para normalizar eventos de toque.

`dumpsys gfxinfo` mede principalmente a UI Java e não representa os frames Vulkan do `SurfaceView`. Para observar apresentação real, obtenha o nome exato da layer BLAST no `dumpsys SurfaceFlinger` e use `dumpsys SurfaceFlinger --latency '<nome da layer>'`. A primeira linha é o período do display em nanossegundos e as seguintes contêm timestamps por frame. No menu desconectado foram observados intervalos próximos de 33–40 ms, mas isso não é benchmark de gameplay: `CL_MinFrameTime()` aplica `cl_maxfps_menu` ou a frequência detectada quando `cls.state == ca_disconnected`. O config ativo tinha `cl_maxfps 308`, `cl_maxfps_menu 0` e `vid_vsync 0`.

Não importe grandes blocos de vkQuake/FTEQW por intuição. Primeiro meça:

- tempo de CPU e GPU por frame;
- draw calls do mundo;
- trocas de pipeline e descriptor sets;
- custo de lightmaps dinâmicos;
- resolução real do swapchain e escalonamento;
- pacing, present mode e sincronização;
- comportamento térmico depois de alguns minutos.

Use dm3, dm4 e dm6 com dados conhecidos e limpos para comparação. e1m2 e aerowalk são controles úteis porque nunca apresentaram o mesmo conflito.

## Fluxo obrigatório de trabalho

1. Leia o pedido mais recente e este arquivo.
2. Inspecione worktree, branch e diffs antes de editar.
3. Faça mudanças somente em `E:\Projetos Linux\ezquake-source`, salvo pedido explícito de copiar o APK pronto.
4. Preserve alterações locais, arquivos de usuário e dados PK3/PAK.
5. Para bugs, obtenha evidência com logs, diffs e testes pequenos antes de mudar arquitetura.
6. Remova toda instrumentação temporária antes de considerar a alteração concluída.
7. Execute pelo menos o build Android debug após alterações nativas, Java, CMake ou dependências.
8. Se houver aparelho conectado e o pedido incluir teste, instale com `adb install -r`, relance e confirme que o processo permanece vivo. Tiago navega e valida visualmente; não tire screenshots automaticamente.
9. Relate exatamente o que foi comprovado e o que ainda é hipótese.
10. Não faça commit/push/PR sem autorização atual. Quando autorizado, prefira commits pequenos por assunto e mantenha o PR preparado para revisão futura, não para envio imediato ao `qw-group`.

## Próximas prioridades

1. Confirmar áudio no aparelho com scrcpy em `--no-audio` durante jogo real.
2. Medir FPS e frame time em dm3/dm4/dm6 após o batching de lightmaps e os dois frames em voo.
3. Preparar e validar um PK3 substituto limpo fora do repositório de código.
4. Perfilar CPU/GPU antes da próxima otimização Vulkan.
5. Só depois comparar implementações específicas de vkQuake e FTEQW para o gargalo medido.

## Arquivos centrais

- `CMakeLists.txt` e `vcpkg.json`: SDL3, Oboe e dependências Android.
- `app/src/main/java/org/ezquake/android/EzQuakeActivity.java`: entrada Android específica do app.
- `app/src/main/java/org/libsdl/app/`: bootstrap Java SDL3.
- `app/src/main/java/org/libsdl/app/SDLSurface.java`: limite proporcional da superfície Android e normalização física do toque.
- `src/vid_sdl2.c` e `src/in_sdl2.c`: plataforma SDL; os nomes históricos dos arquivos permanecem apesar do SDL3.
- `src/snd_main.c`: mutex e mixer.
- `src/snd_backend_oboe.cpp`: saída Android Oboe.
- `src/snd_voip.c`: captura de voz, atualmente desativada no Android.
- `src/vk_texture.c`: upload e batching de texturas/lightmaps.
- `src/vk_main.c`: início/fim do frame Vulkan e flush de uploads.
- `docs/android-pocket-vulkan-handoff.md`: resumo técnico curto para handoff.

Atualize este arquivo quando uma decisão importante, regressão, correção confirmada ou mudança de fluxo tornar alguma seção obsoleta. Não registre senhas, tokens, chaves de assinatura privadas ou dados proprietários.
