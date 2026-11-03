# Bots básicos do protótipo

## Funcionamento
A arena possui três bots por rodada. Eles patrulham áreas navegáveis, enxergam jogadores ativos em um cone de120 graus até20 metros e não detectam através de paredes. Ao perder a visão, investigam a última posição vista por até5 segundos. Não enxergam jogadores na base nem continuam atacando mortos ou extraídos.

Cada bot tem100 de vida. O tiro causa10 de dano, com reação inicial de1 segundo, intervalo mínimo de0,8 segundo e dispersão de3 graus. Dano, cadência, escolha de alvo, movimento e morte são resolvidos no servidor. Os clientes exibem os mesmos personagens, movimento e efeitos. O rifle dos jogadores continua causando25 de dano.

Bots são identificados pelo texto vermelho BOT. Ao morrer, deixam uma Peça com GUID único. O rifle visível do bot não é coletável; munição/recarga e inventário completo de bots estão fora desta etapa. A morte usa uma pose simples, sem ragdoll. O corpo (ragdoll) fica 5 minutos e então é removido; uma nova rodada remove todos os bots antes disso. Queda/apoio das caixas continuam adiados conforme FUT-001.

O loot passa pelo mesmo ledger, validação de coleta e persistência dos outros itens. A identidade do prêmio é estável, inclusive se houver repetição de dano ou falha de gravação. O registro durável precede a exposição do item. Falha de gravação impede confirmar a morte/concessão; falha apenas na criação visual usa a recuperação de loot existente, preservando o GUID. O servidor não recria kits de jogadores.

Cada nova rodada confirmada remove a população anterior e cria três novos bots com novos prêmios, sem depender de matar os bots para encerrar a rodada. A disponibilidade do backend continua sendo requisito do fluxo normal.

## Preparação e teste manual
Não é necessário instalar ferramentas novas. Use Docker e Iniciar-Marau6.cmd como antes. Reabra o projeto para carregar o código e o mapa atualizados; dois jogadores PIE no modo Play As Client.

1. Prepare equipamentos na base e entre com os dois perfis.
2. Procure os bots na arena; observe patrulha e os mesmos movimentos nas duas janelas.
3. Aproxime-se e use os obstáculos: o bot deve reagir ao ver um jogador e parar de disparar quando a visão for bloqueada.
4. Elimine um bot com quatro acertos do rifle. Confira a morte na outra janela e colete a Peça com E.
5. Extraia com o item, confira o inventário na base e inicie outra rodada quando ambos os perfis terminarem. Devem aparecer três novos bots.

## Implementação e verificações
Marau6Bot contém comportamento e apresentação; Marau6Bots controla população por RaidId. Pontos ATargetPoint com tag Marau6BotSpawn e a NavMesh estão salvos em Lvl_FirstPerson. NavigationSystem é um módulo da Unreal já instalada.

A preparação do mapa é uma operação explícita EditorSetup.PrepareBotArena. A bateria normal Marau6 testa os caminhos sem reconstruir/salvar o mapa. Não repetir a preparação automaticamente no fluxo dos jogadores.

Test-Marau6Integration.ps1 -PIE -Bots usa PostgreSQL, API e diário de testes isolados. Verifica patrulha, replicação entre dois clientes, tiro de bot, morte por RPC do jogador, disputa pelo mesmo prêmio, extração, nova rodada e unicidade do item após reiniciar o backend de teste. As regressões anteriores continuam isoladas da população de inimigos; Marau6Integration só ativa bots se Marau6BotsTest também estiver presente.

Validação automática concluída: compilação Marau6Editor,21 testes Unreal,14 verificações de contrato do backend e replay após reinício, ciclo integrado anterior e recuperação em novo processo aprovados. Teste de bots com dois clientes PIE reais aprovado, incluindo movimento nos dois clientes, tiro/dano replicados, morte por quatro tiros do jogador, coleta disputada com um vencedor, extração, três novos bots e unicidade do item após reiniciar o backend de teste. Revisão independente sem bloqueios remanescentes. Validação manual do novo bloco confirmada pelo usuário. PR e merge autorizados para fechar este marco.