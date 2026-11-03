# Recarga, corrida, rifle inicial e contador

Pedido em 03/11/2026.

## Kit básico e kit personalizado

Na base, acima do equipamento, as abas KIT BÁSICO e KIT PERSONALIZADO (clique ou K) escolhem como entrar na próxima partida. Personalizado é o fluxo anterior: entra com o equipamento preparado. Básico entra com um kit novo criado pelo backend em `/raids/enter` (`"kit":"basic"`): rifle 20/40 a 100%, duas caixas de munição e um kit médico. Com o kit básico selecionado, a coluna de equipamento mostra esse kit esmaecido e com cadeado, e não aceita arrastar para dentro nem para fora (cliente e servidor recusam); o stash continua editável. O equipamento personalizado não é apagado: volta a aparecer ao trocar para KIT PERSONALIZADO. Ao extrair, o que voltar ocupa o próprio slot se estiver livre, senão o primeiro slot livre do stash. Não há mais rifle gratuito ao entrar desarmado no kit personalizado.

## Contador de munição

Canto inferior direito: `carregador / capacidade` (ex.: 12 / 20), vermelho com carregador vazio; abaixo, menor, toda a munição carregada (reserva do rifle + caixas de 20 na mochila). "RECARREGANDO" aparece acima durante a recarga.

## Recarga direto da mochila

R abre as caixas de munição da mochila (e do safe pocket) direto na reserva, 20 balas por caixa, até cobrir o que falta no carregador, e então recarrega. Usa o mesmo caminho durável de arrastar a caixa sobre o rifle, então o backend continua validando 20 balas por caixa consumida. Arrastar continua funcionando.

## Sons da recarga

Gravação real recortada em três (ver docs/som-disparo.md), no ritmo da recarga encenada (frações dos 2 s): carregador saindo (0,24), carregador entrando (0,53), ferrolho (0,70). Para o dono sem posição; para os outros nos pés do personagem, com a mesma atenuação dos passos. Recarga cancelada interrompe os sons pendentes.

## Animações

- Primeira pessoa: os braços não tocam clipes; `UpdateViewModel` os move em código e o control rig prende as duas mãos no rifle. Por isso a corrida em primeira pessoa é um balanço no ritmo dos passos (1,6 m por passo a 5 m/s), com rolagem e mergulho do rifle (andando, 30% menor que a primeira versão; correndo com Shift, igual), e a recarga é encenada no view model: inclina e desce, tira o carregador, coloca, puxa o ferrolho e sobe, nos 2 s da recarga.
- Terceira pessoa, corrida: `BS_Marau6_Locomotion` é uma cópia de `BS_Idle_Walk_Run` com o jog em 5 m/s (o template só chegava ao jog em 6 m/s; a 5 m/s parecia caminhada). Mirando (3 m/s) ainda caminha; a corrida com Shift continua no jog.
- Terceira pessoa, recarga: `MM_Rifle_Reload` toca no ramo superior do corpo (`DefaultSlot`), acelerada para caber nos 2 s; as pernas continuam a locomoção. `bReloadPose` replica para todos, pois o estado de munição só vai ao dono. A primeira pessoa copia a pose do corpo, então a mesma montagem aparece nas duas visões. O carregador do lasgun não se move.

As duas mudanças de asset são feitas por `Marau6.SetupLocomotion`, com o editor fechado e depois de compilar:

`UnrealEditor-Cmd.exe Marau6.uproject -ExecCmds="Marau6.SetupLocomotion,QUIT_EDITOR" -unattended -nullrhi`

O log deve mostrar `MARAU6_ANIM body saved=1 locomotion=1 slots=1`. Assets do template não são alterados. Teste: `Marau6.Combat.LocomotionAndReload` (falha até o comando rodar).

## Loot no chão

Kit médico, caixa de munição e sucata usam os modelos de `SourceArt/Loot` (glb, via LFS), importados por `scripts/import_loot_meshes.py` para `/Game/Marau6/Items`. O ator de loot centraliza cada modelo na caixa de coleta e ajusta a escala (1,4; 0,012; 0,1), porque os arquivos vêm em unidades diferentes. Sem o asset, volta o cubo.

## Inspeção

`Marau6.PreviewReload [others]` recarrega todos os personagens (others: só os que não são jogadores); `Marau6.PreviewSoldier` põe um soldado de lado na direção mais aberta e esconde os braços do jogador; `Marau6.PreviewRun [1]` faz o boneco de `Marau6.PreviewWeapon` correr em círculo à frente (1 também move o jogador). Use em `LocalSettings/viewmodel_shots.txt` com `Marau6.CaptureViewModel`.
