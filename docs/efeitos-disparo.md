> Atualização 03/11/2026: o clarão esférico e o brilho de impacto descritos abaixo foram removidos; o clarão texturizado e os impactos agora são de `AMarau6ShotFX` (ver docs/sensacao-arma.md). `AMarau6ShotEffect` só toca o som do tiro.

# Efeitos provisórios de disparo

O servidor mantém a decisão de disparar e acertar. Apenas após aceitar o disparo, envia um multicast cosmético não confiável com ponto, normal e existência de impacto. ServerFire continua sem parâmetros enviados pelo cliente.

Cada cliente cria um AMarau6ShotEffect local, sem replicação, colisão ou atualização por frame. O clarão dura 0,065s e acompanha o socket Muzzle do rifle em primeira pessoa para o dono e em terceira pessoa para outros. O impacto dura até 0,22s, deslocado da superfície pela normal. Amarelo indica superfície; alaranjado indica personagem vivo atingido. Não há impacto no limite do alcance quando o tiro não encontra nada.

O material emissivo M_ShotGlow pertence ao projeto em Content/Marau6/Effects. Usa geometria simples para esta primeira apresentação; nenhum asset original é modificado. As linhas de depuração foram substituídas pelos efeitos. Não há som ou nova animação nesta etapa.

## Validação

Development Editor / Win64 compilado. Cinco testes Marau6.Combat aprovados: Health, ServerFire, AmmoReload, WeaponVisuals e ShotEffects. O teste novo verifica disparo aceito, rejeição por cadência/recarga/morte, ausência de colisão/replicação, socket da boca, impacto apenas em acerto e destruição temporal. Esses testes usam mundo local; a observação entre clientes reais complementa a verificação.

Teste manual realizado com sucesso pelo usuário: em duas janelas Play As Client, observe o clarão da própria arma e da arma do outro jogador. Atire em parede, chão e personagem e confira o brilho no impacto. Atire para o céu: clarão sem impacto flutuante. Repita durante recarga e vazio: não deve haver efeito de tiro. Vida, dano e munição devem manter o comportamento validado.

Validação manual confirmada pelo usuário em 28/10/2026, com dois jogadores.

## Clarão texturizado e fumaça na boca (03/11/2026)

O clarão usa texturas pintadas por `scripts/Generate-ShotTextures.py` (atlas 2x2, uma variação sorteada por lâmina a cada tiro): `T_MuzzleFlash_Side` nas duas lâminas cruzadas ao longo do cano (chama com línguas de fogo, boca na borda esquerda) e `T_MuzzleFlash_Front` na lâmina de frente (estrela), 7cm à frente do soquete para não ficar escondida pela boca da lasgun. `M_MuzzleFlash` é aditivo, emissão = textura × cor × intensidade 14.

Quatro baforadas de fumaça (`UMaterialBillboardComponent`, sempre de frente para a câmera, textura `T_MuzzleSmoke`) saem da boca a cada tiro: empurradas pela explosão, freiam no ar, crescem, sobem e somem em 0,8–1,4s. Ficam no mundo, sem acompanhar a arma. Para o dono, saem da boca como ela é desenhada (escala de primeira pessoa) e são mais transparentes (0,32 contra 0,4) para não cobrir a mira. `M_MuzzleSmoke` é translúcido iluminado; o parâmetro Grow encolhe a fumaça dentro do quad, então crescer só altera um parâmetro.

Regenerar: `python -I scripts/Generate-ShotTextures.py Saved/SourceTextures`, `python -I scripts/Generate-RifleShot.py Saved/SourceAudio/S_RifleShot.wav`, `python -I scripts/Generate-Decompression.py Saved/SourceAudio/S_Decompression.wav` e então `Marau6.SetupShotFX` com o editor fechado (`UnrealEditor-Cmd.exe Marau6.uproject -ExecCmds="Marau6.SetupShotFX,QUIT_EDITOR" -unattended -nullrhi`).
