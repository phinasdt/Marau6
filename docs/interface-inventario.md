# Interface do inventário

Interface nativa UMG/Slate com arrastar e soltar, seleção por mouse e detalhes do item. A base mostra arma, mochila (8), safe pocket (2) e stash (32). Na partida, TAB abre/fecha; a stash não está disponível.

## Visual
A base usa o fundo do corredor metálico (/Game/Marau6/UI/T_Marau6_BaseCorridor, fonte em Content/Marau6/UI/Marau6_BaseCorridor.png; reimporte com scripts/import_ui_background.py, editor fechado). Cabeçalho com MARAU6 e abas; só EQUIPAMENTO existe neste protótipo; as demais aparecem com o selo EM BREVE. Abaixo: teclas 1/2/B/N clicáveis, colunas de equipamento, stash e inspeção, e rodapé com a mensagem atual, a luz de estado (verde conectado, âmbar aguardando, vermelho falha) e a legenda. Slots mostram código curto (MED, MAT, MUN, RIFLE) e categoria; o selecionado fica em latão. Na partida, o fundo é escuro e opaco, sem as abas e a stash.

## HUD da partida
Mesmo estilo da base (Marau6HUDWidget, Slate), mínima e sem rótulos. Não há barra rápida: itens são usados pelo inventário (TAB). Embaixo à direita: carregador / capacidade em destaque, total carregado abaixo (reserva do rifle mais caixas na mochila), a marca RECARREGANDO quando aplicável e, logo abaixo, a vida como uma barra vermelha larga com o número dentro (ex.: 85 / 100). Durante a cura aparece CURANDO com o tempo restante. Embaixo à esquerda: só a tecla TAB. No alto, EXTRAINDO com a contagem apenas dentro da área de extração. Perto da mira, E e o item sob a mira (ou MOCHILA CHEIA); no centro, o resultado (ELIMINADO/EXTRAÍDO) com itens mantidos/perdidos. O canvas antigo só desenha a mira quando a HUD existe; sem ela, desenha o mesmo conjunto enxuto. O layout foi feito para 1920x1080 e escala com a janela.

## Como usar
- Clique em um item para consultar seus dados. Rifle mostra munição e condição; suprimentos mostram cura, duração e restrições. Rótulos curtos nos slots evitam cortes; o painel de detalhes mostra o nome completo.
- Arraste para um slot vazio para mover, ou para um ocupado para trocar. A troca exige compatibilidade dos dois itens. Durante o arraste, a origem fica cinza; destinos válidos mantêm a aparência normal e os incompatíveis recebem um X diagonal sobre a caixa, sem verde ou vermelho. Soltar fora dos slots ou na origem cancela, sem descartar.
- TAB fecha na partida e cancela o arraste. Escape cancela um arraste; sem arraste, o comportamento padrão do PIE permanece.
- Setas e Enter continuam disponíveis. F equipa, G descarta, P transfere entre mochila/safe e H usa o suprimento selecionado nos contextos válidos da partida.
- O inventário aberto libera o cursor e bloqueia comandos de movimento, câmera, pulo e disparo. O mundo continua: dano, bots, extração e cura não pausam. Fechar devolve o controle do jogo.
- A movimentação só é concluída após confirmação e recebimento do estado atualizado. Enquanto isso, aguarde; fechar a janela não cancela uma operação já enviada ao servidor. Uma conexão indisponível não autoriza repetir ou criar itens.
- Em janelas menores, a interface reorganiza as áreas e permite rolagem vertical; fontes não encolhem junto com a janela.

## Teste manual
1. Inicie o Marau6 e abra dois clientes na Unreal, como nos blocos anteriores. Escolha um perfil por janela.
2. Na base, arraste suprimentos da stash para mochila e safe. Troque dois itens, consulte seus detalhes e tente colocar um rifle no safe: deve aparecer uma recusa sem mover os itens.
3. Arraste e solte fora dos slots: o item continua na origem. Repita cancelando com Escape. Confira também setas/Enter.
4. Entre na partida. Confira movimento/câmera, abra com TAB e mova itens por arraste. O personagem não anda enquanto a janela está aberta, mas continua sujeito ao mundo.
5. Selecione um suprimento com mouse e pressione H. Com vida cheia, deve explicar a recusa sem consumir; com dano, deve iniciar a cura e fechar o inventário. Confira F/P/G nos seus contextos válidos.
6. Feche com TAB e confira que câmera, movimento e disparo voltam normalmente, sem tiro pela liberação do mouse usado no inventário.
7. Extraia e confira os itens na base. Entre em outra rodada e verifique que a janela anterior não fica aberta por engano. Teste também uma janela menor e sua barra de rolagem.

## Preservação e autoridade
O arraste é apenas visual até a solicitação ao servidor. Pedidos contêm identidades, slots, contexto e revisão; o servidor verifica propriedade e estado atual. Repetição, origem substituída, destino alterado ou outra rodada não podem mover um item diferente. A interface não repara durabilidade, repõe munição ou concede equipamentos.

Sem alteração do esquema PostgreSQL ou do formato dos comandos HTTP existentes. Os dados reais do jogo são preservados; testes automatizados usam configurações, diários e banco isolados do Marau6.

## Verificação
Implementação local compilada e revisada. Suíte Unreal: 32 testes aprovados. Backend: 28 contratos aprovados, incluindo recibos e consumo após reinício. Integração com dois clientes reais e renderização aprovada: drag/drop com eventos de mouse Slate, foco de teclado, janela normal e pequena, mensagens de recusa, troca de modo, duas rodadas e reinício da API. Capturas de base, partida e janela pequena inspecionadas. Recuperação em novo processo também verificada. Teste manual confirmado pelo usuário, incluindo origem cinza e X nos destinos incompatíveis; PR e merge autorizados em 01/11/2026.
