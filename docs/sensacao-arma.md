# Sensação da arma

Cadência, recuo, corrida, mira, movimento da arma em primeira pessoa e efeitos do disparo. Tudo o que é apresentação roda só nos clientes; regras de tiro e velocidade continuam no servidor.

## Regras

- Cadência de 450 disparos por minuto (0,133 s), validada pelo servidor.
- Recuo: cada disparo aceito localmente sobe a mira 1,3° e desvia até ±0,45° para os lados (30% menos mirando); 35% da subida volta sozinha logo depois do tiro (`RecoilSettle`). A arma em primeira pessoa recua 2,8 cm e levanta o cano 1,9° a cada tiro (metade mirando). O servidor dispara pela rotação de controle replicada, então o recuo move a mira real e o jogador compensa puxando para baixo. Antes de cada disparo o cliente envia os movimentos pendentes (`FlushServerMoves`), para o servidor usar a mira e o estado de corrida atuais.
- Andando: 5 m/s. Shift corre a 1,3× (6,5 m/s) apenas andando para frente e no chão. Não dispara nem mira correndo; o servidor recusa tiros durante a corrida. Correr cancela a mira; recarregar interrompe a corrida. Depois da corrida o disparo volta quando a arma já subiu.
- Botão direito segurado mira: 0,6× de velocidade (3 m/s), zoom de 20% e sensibilidade do mouse proporcional ao zoom. A mira fica indisponível durante a recarga e com o inventário aberto.
- `UMarau6MovementComponent` leva os desejos de correr e mirar em cada movimento salvo (flags `FLAG_Custom_0` e `FLAG_Custom_1`), de modo que o servidor aplica a mesma velocidade prevista pelo cliente.

## Arma em primeira pessoa

Os braços só se deslocam no espaço da câmera: `FirstPersonArmsOffset`, `FirstPersonAimOffset`, `SprintArmsOffset`, balanço ao andar e recuo para trás. A rotação passa pela câmera inativa `ViewModelAim`: o control rig do template aponta o rifle ao longo dela e mantém as duas mãos na arma, então girá-la inclina a arma sem brigar com o rig. A pose de corrida é `SprintAimRotation`; o atraso ao virar e a subida do cano no disparo também giram essa câmera. Os valores ficam nos defaults do personagem e foram calibrados com capturas. Mirando, a arma fica 2 cm abaixo da linha de visada (`FirstPersonAimOffset` Z=0,5), para mostrar um pouco do topo do lasgun; o tiro continua indo para a mira no centro. Outro modelo de arma exige recalibrar esse valor.

O corpo em primeira pessoa é o soldado inteiro. Para o dono, as seções de material do equipamento (capacete, máscara, ombreiras, mochila, pá, botas, calças) ficam ocultas e só mangas, luvas e correias aparecem; as seções são escolhidas pelo nome do slot, então um corpo sem esses slots não muda.

## Efeitos do disparo

`AMarau6ShotFX` complementa `AMarau6ShotEffect` em cada disparo aceito, de jogadores e de bots:

- Clarão texturizado na boca do cano: duas lâminas cruzadas, a estrela frontal e uma segunda estrela maior, girada e com outro quadro do atlas, mais uma luz breve. O clarão esférico antigo (`AMarau6ShotEffect`) foi removido.
- Camada de som do rifle (`S_RifleShot`) sobre o som atual: sem posição para o dono, espacial para os outros.
- Traçante a 500 m/s e rastro de fumaça que alarga, sobe e some em 1,4 s. Para o dono, o rastro começa na boca do cano desenhada (escala de primeira pessoa).
- No impacto em superfície: poeira, faíscas e marca de bala por 20 s. Em personagem: névoa e gotas de sangue e manchas na parede atrás e no chão abaixo, só em geometria estática, por 30 s.
- No impacto, a poeira (ou a névoa de sangue) é um sprite voltado para a câmera que cresce e some, suavizado onde encosta na superfície; as gotas de sangue em voo são tubos (`M_BloodDrop`).

Os materiais (`M_MuzzleFlash`, `M_MuzzleSmoke`, `M_Tracer`, `M_ShotSmoke`, `M_ImpactPuff`, `M_BloodDrop`, `M_BulletHole`, `M_BloodDecal`) são montados em C++ por `Marau6.SetupShotFX` (`Source/Marau6/Tests/Marau6ShotFXSetup.cpp`). As texturas são atlas 2x2 de variações; cada clarão, fumaça, poeira e decal sorteia um quadro (parâmetro `Frame`):

- `scripts/Generate-ShotTextures.py`: `T_MuzzleFlash_Side`, `T_MuzzleFlash_Front`, `T_MuzzleSmoke` (512 px).
- `scripts/Generate-ImpactTextures.py` (1024 px): `T_BloodSplatter` (mancha com gotas satélites e respingos alongados, centro mais escuro e borda úmida; brilho maior onde o sangue é espesso), `T_BloodMist` (névoa com respingos, trocada no `M_ImpactPuff` em acertos em personagem), `T_BulletHole` com o normal map `T_BulletHole_N` (furo, cratera esmagada, borda lascada, rachaduras e fuligem) e `T_ImpactDust` (nuvem de poeira com cascalho).

O comando também importa `Saved/SourceAudio/S_RifleFire.wav` (ver `docs/som-disparo.md`) e `S_Decompression.wav` (`scripts/Generate-Decompression.py`). Para regenerar:

1. `python -I scripts/Generate-ShotTextures.py Saved/SourceTextures` e `python -I scripts/Generate-ImpactTextures.py Saved/SourceTextures`
2. Com o editor fechado: `UnrealEditor-Cmd.exe Marau6.uproject -ExecCmds="Marau6.SetupShotFX,QUIT_EDITOR" -unattended -nullrhi`; o log mostra `MARAU6_SHOTFX <asset> saved=1` para cada textura e material.

Na primeira execução os shaders novos compilam em segundo plano; os primeiros disparos podem aparecer sem efeito.

## Morte

Jogadores e bots viram ragdoll (`Marau6Ragdoll`): a cápsula deixa de bloquear em todas as máquinas e só os clientes simulam o corpo. O jogador é empurrado na direção do último tiro (`LastHitDirection`, replicado). Uma nova rodada revive o mesmo pawn e devolve o corpo à cápsula.

## Inspeção

- `Marau6.CaptureViewModel` salva capturas em `LocalSettings/viewmodel_*.png`. Use um mapa fora da base persistente, onde o personagem já começa armado: `/Game/Variant_Shooter/Lvl_Shooter?game=/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C`. `LocalSettings/viewmodel_shots.txt` pode trocar a lista: uma linha `nome;pitch;comando|comando;espera`.
- `Marau6.ViewModelPose 1|2` força a pose de mira ou de corrida; `Marau6.ShotFXAge` congela os efeitos numa idade; `Marau6.PreviewShotFX` mostra um disparo do dono sem tiro no servidor.
- Testes: `Marau6.Combat.SprintMovement` e `Marau6.Combat.Ragdoll`.

## Decais de impacto (03/11/2026)

Os decais sempre renderizaram, mas eram pequenos demais para aparecer: a marca ocupa bem menos da metade do tile do atlas, e a caixa de projeção do furo tinha 2-3 cm de meia-extensão (furo visível de ~1 cm). Agora o furo usa 10-12 cm de meia-extensão (marca de ~7-9 cm) e o sangue usa caixa 1,6x maior com `Opacity` 2 para a mancha não ficar apagada. `Marau6.PreviewDecal` projeta um furo, um sangue e o decal padrão do motor (controle) sob a mira, para diagnóstico.
