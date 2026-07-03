# Continue.md — Download automático de dados do Quake (Android) + debug do crash no tablet

Última atualização: 2026-07-03

## Onde estamos

Implementada e já validada no Xiaomi (rothko) uma feature nova no branch `feature/android-pocket-vulkan` (`E:\Projetos Linux\ezquake-source`): download automático dos dados do jogo (Quake shareware + assets QuakeWorld do nQuake) no primeiro launch do app Android, em vez de exigir cópia manual de arquivos.

**Não commitado ainda** — pendente de validação completa (funcionou no celular, falhou no tablet) antes de pedir autorização pra commitar.

## Arquivos da feature (todos novos/modificados, não commitados)

- `app/src/main/java/org/ezquake/android/GameDataManifest.java` (novo) — lista de 399 arquivos essenciais (~124.3MB) + 5 arquivos de texturas HD opcionais (~385.4MB), cada entrada com `remotePath`/`localPath`/`approxSize` reais, verificados um a um contra a árvore do GitHub `nQuake/distfiles` (via `gh api repos/nQuake/distfiles/git/trees/master?recursive=1`) e cruzados com uma instalação real do instalador nQuake em `C:\nquakezero` nesta máquina.
- `app/src/main/java/org/ezquake/android/GameDataInstaller.java` (novo) — baixa via `HttpURLConnection` puro (sem libs novas), grava em `<arquivo>.part` e só renomeia pro nome final após completar (nunca deixa dado parcial passar na checagem `isEssentialDataPresent`), pula arquivos já baixados (retomável), limpa `.part` órfãos no início.
- `app/src/main/res/layout/activity_launcher_progress.xml` (novo) — overlay simples (TextView + ProgressBar) sobre o splash já existente, só inflado quando entra no fluxo de download.
- `app/src/main/java/org/ezquake/android/EzQuakeLauncherActivity.java` (modificado) — `launchGame()` agora checa `GameDataInstaller.isEssentialDataPresent(baseDir)` antes de abrir o jogo; se faltar, baixa em `ExecutorService` background com UI de progresso; trata falha de rede (AlertDialog retry/sair) e espaço em disco insuficiente (`StatFs` prévio); depois do essencial, oferece as texturas HD como prompt opcional uma vez só (`SharedPreferences`: `hd_textures_prompted`, `hd_textures_installed`).
- `src/sys_posix.c` — **não foi tocado**, continua como fallback de segurança (`Android_HasUsableBaseData`/`Android_PreflightDataFolder`), mesmos candidatos de arquivo (`id1/pak0.pak`, `id1/PAK0.PAK`, `id1/gfx.wad`, `id1/gfx/palette.lmp`).

Plano completo salvo em `C:\Users\Tiago\.claude\plans\luminous-spinning-yao.md` (mesma máquina Windows onde a sessão anterior rodou) — não acessível do tablet, mas o resumo acima cobre o essencial.

## Fonte dos dados

Repositório `https://github.com/nQuake/distfiles` (shareware Quake 1.06 + assets QuakeWorld livres de licença problemática, distribuídos publicamente há ~20 anos). Download via `https://raw.githubusercontent.com/nQuake/distfiles/master/<caminho>`, sem autenticação. Excluído deliberadamente: Team Fortress (`addon-fortress/`), Clan Arena (`addon-clanarena/` e `non-gpl/qw/sound/` inteira, que só contém sons de CA), demos de exemplo (`matchinfo/`).

## Estado do teste

- **Celular Xiaomi (rothko, 2407FPN8EG, serial ADB `LJN7LFRKXG79P7J7`)**: ✅ funcionou de ponta a ponta. Download completou, `EzQuakeActivity` abriu e ficou rodando normalmente (~120fps confirmado no logcat via `BufferQueueProducer`), sem `FATAL EXCEPTION`/crash no log.
- **Tablet do Tiago (USB-A, conectado via adaptador/cabo USB-A→USB-C na porta USB-C do PC)**: ❌ reportado por Tiago que "apareceu, baixou os arquivos, mas depois fecha a tela" — ou seja, o download em si funcionou (rede ok), mas o app morre em algum ponto depois disso (possivelmente ao tentar abrir `EzQuakeActivity`, ou durante/logo após o prompt de texturas HD, ou um crash nativo no motor SDL/Vulkan específico daquele hardware).
- **Ainda não foi possível capturar o log do crash no tablet**: o ADB no PC não detectou o tablet (`adb devices` veio vazio mesmo após `kill-server`/`start-server`). Motivo mais provável: falta autorizar o popup "Permitir depuração USB?" na tela do tablet (não confirmado se apareceu), ou "Depuração USB" não está ativada nas Opções do desenvolvedor do tablet, ou o cabo/adaptador USB-A→USB-C usado é só de carga (não transfere dados).

## Por que o crash provavelmente é específico do tablet

Nenhuma mudança de código foi feita entre o teste do celular (passou) e o do tablet (falhou) — é o mesmo APK. Hipóteses a investigar com o log real do tablet:
1. Fabricante/Android do tablet mata o processo de forma mais agressiva durante o download em background (gerenciamento de memória/bateria diferente do MIUI do Xiaomi).
2. O tablet pode ter uma GPU/driver Vulkan diferente que falha ao inicializar `EzQuakeActivity` (já vimos antes, no trabalho de Vulkan deste mesmo projeto, que bugs de driver/GPU são comuns e específicos de aparelho — ver AGENTS.md, seção de correções Vulkan).
3. Pode ser um crash Java real na nova lógica (`GameDataInstaller`/`EzQuakeLauncherActivity`) que só se manifesta com timing/quantidade de memória diferente no tablet — precisa do stacktrace real pra confirmar ou descartar.

## Próximo passo imediato

1. No tablet: `Configurações → Sobre o tablet → tocar 7x em "Número da versão"` pra habilitar Opções do desenvolvedor (se ainda não estiver), depois `Configurações → Opções do desenvolvedor → ativar "Depuração USB"`.
2. Reconectar o cabo USB-A→USB-C e aceitar o popup de autorização que deve aparecer na tela do tablet.
3. Rodar `"C:\Android\Sdk\platform-tools\adb.exe" devices -l` pra confirmar que o tablet aparece.
4. Se aparecer: instalar o APK (`app/build/outputs/apk/debug/app-debug.apk`, já compilado e presente no worktree — ou recompilar se o Claude no tablet estiver rodando de outra máquina/sessão sem esse build), limpar dados antigos (`adb shell pm clear org.ezquake.android` + `adb shell rm -rf //storage/emulated/0/Documents/ezQuake` — atenção ao `//` duplo no início do path se rodar via Git Bash/MSYS, senão o path é reinterpretado incorretamente), iniciar captura de log (`adb logcat -c` seguido de `adb logcat -v time` rodando em background) ANTES de abrir o app, então abrir o app e reproduzir o fechamento.
5. Procurar no log por `FATAL EXCEPTION`, `AndroidRuntime`, `libc.*Fatal signal`, ou qualquer menção a `org.ezquake.android`/`EzQuakeLauncherActivity`/`EzQuakeActivity`/`GameDataInstaller` por perto do momento em que a tela fecha.
6. Se for crash Java: corrigir no código Java listado acima. Se for crash nativo (SDL/Vulkan) específico daquele hardware: pode ser um problema pré-existente do renderer Vulkan neste device, não relacionado à feature de download — nesse caso avaliar se faz sentido também tentar `vid_renderer` diferente ou investigar separadamente.
7. **Não commitar nada da feature de download ainda** — só depois que o crash do tablet for entendido e (se for bug real) corrigido, com autorização explícita do Tiago pra commit/push, seguindo a regra do AGENTS.md do projeto.

## Regras do projeto (lembrar)

- Ler `AGENTS.md` (raiz do repo) e `docs/android-pocket-vulkan-handoff.md` antes de mexer em qualquer coisa — worktree local é fonte de verdade, não confiar em branch remoto/PR.
- Não commitar/push sem autorização explícita do Tiago no momento.
- Build Android: variáveis de ambiente e comando completo estão documentados no `AGENTS.md`, seção "Build validado".
- Ao usar `adb shell` com paths absolutos tipo `/storage/...` a partir do Git Bash/MSYS no Windows, usar `//storage/...` (barra dupla) pra evitar que o MSYS reinterprete o path como um caminho de host.
