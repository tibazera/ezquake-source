# Continuidade — validação CSQC / ezQuake

## Branches e commits

- **Patch limpo para revisão/PR:** `tiago/csqc-bind-security`
  - commit `73311f9ee4a0b6450ad377f50412d15cbf45e2cf`
  - base: `c0da2b1e` (`QW-Group/ezquake-source` PR #1163 work)
- **Esta branch:** `tiago/csqc-bind-security-handoff`
  - contém o patch acima e este arquivo; use-a para retomar o contexto.

O patch de produção toca somente:

- `src/csqc_builtins.c`
- `src/csqc_client.c`
- `src/csqc_client.h`

## O que foi corrigido

O builtin CSQC `setkeybind` (#630) gravava o bind de módulo diretamente com
`Key_SetBindingModule`. Assim, um `csprogs` não confiável podia substituir uma
tecla por `quit` ou por uma cadeia como `+ws2cprobe;quit`, sem passar pela
política existente de comandos remotos.

`CSQC_BindAllowed` agora:

1. permite limpar o bind;
2. rejeita `;`, CR e LF;
3. permite verbos de entrada (`+` e `-`);
4. permite comandos registrados pelo próprio módulo ou liberados por
   `Cmd_RemoteAllowed`;
5. permite nomes desconhecidos (podem ser comandos do servidor);
6. recusa comandos locais conhecidos, aliases e cvars não autorizados.

O helper `CSQC_Client_IsModuleCommand` consulta a lista já mantida por
`registercommand`, sem introduzir tabela nova.

## Evidência de teste manual

Ambiente TF2003 v6, servidor local mvdsv na porta 27501 e cliente diagnóstico
`client/ezq-pr1vmdiag.exe`:

- antes: `setkeybind(K_ESCAPE, "quit")` e
  `setkeybind(K_ESCAPE, "+ws2cprobe;quit")` falhavam no harness WS2-C;
- depois: `csqc_selftest core` registrou
  `SUMMARY group=1 pass=243 fail=0 skip=2` em `client/qw/qconsole.log`;
- o módulo testado era `csprogsvers/b57cd18.dat`.

O teste está no checkout local separado
`E:\Projetos Linux\estudoantilag\tf2003-csqc` (branch `csqc`). Ele é apenas
harness de validação e **não** deve ser incluído no PR do cliente sem pedido
explícito.

## Escopo acordado

O objetivo pedido por nano é uma baseline segura para carregar `csprogs` não
confiável. Não misturar neste patch:

- antilag/EZCSQC do dusty;
- v7 ou alterações de protocolo;
- extensões FTE faltantes;
- falhas do `csqc_selftest all` que não são segurança.

O teste `all` teve falhas de extensões FTE ainda ausentes (hash, recursos,
efeitos e buffers); elas são conhecidas e fora deste escopo. A suspeita GE504
foi investigada e descartada: depois do spawn de classe, o modelo chegou como
índice 13 e não houve falha GE504.

## Próximos passos recomendados

1. Faça revisão do diff de `73311f9e` contra a política do FTE/ezQuake;
   especialmente, confirme que permitir um nome desconhecido é compatível com
   binds destinados ao servidor.
2. Se houver aprovação para upstream, abra um PR **somente** da branch
   `tiago/csqc-bind-security`, sem este arquivo de handoff.
3. Antes de PR, rode novamente o TF2003 core self-test e anexe apenas a
   evidência de segurança (não a suíte `all`).

## Regras locais importantes

- Não tocar em `C:\unezquake`: é o cliente pessoal do usuário.
- O cliente de teste é `client\ezq-pr1vmdiag.exe`.
- O usuário usa NoPing/ExitLag; deve desligá-lo para conectar via UDP ao WSL.
- O histórico detalhado das sessões está em
  `E:\Projetos Linux\estudoantilag\REGISTRO.md` (itens 79--81).
