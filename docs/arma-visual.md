# Arma visual provisória

O personagem começa com o SKM_Rifle do template equipado. Dois componentes sem colisão usam o socket HandGrip_R: FirstPersonRifle é visível somente ao dono; ThirdPersonRifle é oculto ao dono e visível aos demais. O combate continua usando o hitscan do servidor, sem reutilizar AShooterWeapon.

As poses são carregadas em BeginPlay para evitar dependência circular durante a construção dos defaults. Os ossos continuam atualizados quando o corpo está oculto. O rifle não adiciona dano, munição ou RPCs.

## Poses do projeto

Content/Marau6/Anims contém ABP_Marau6_FP_Rifle e ABP_Marau6_TP_Rifle, cópias adaptadas das poses originais. As adaptações usam AMarau6Character, GetBaseAimRotation, validade do pawn remoto e PitchN ligado ao eixo Y do aim offset. Os assets originais permanecem intactos.

O comando exclusivo do editor Marau6.SetupWeaponVisuals permite reproduzir essas adaptações. Os assets prontos já fazem parte do projeto, portanto não é necessário executá-lo para jogar. Marau6.PreviewWeapon cria uma prévia local temporária para inspeção; não salva o mapa.

## Verificação em 28/10/2026

- Development Editor / Win64 compilado com sucesso.
- Marau6.Combat: Health, ServerFire, AmmoReload e WeaponVisuals aprovados.
- WeaponVisuals verifica componentes, socket, modelo, visibilidade, ausência de colisão e avaliação das poses sem referências nulas. Também confirma mudança da pose com mira vertical em um pawn sem controlador.
- Prévia gráfica local conferida: arma em primeira pessoa e personagem à frente segurando rifle com mira elevada.
- O teste automatizado usa um mundo local; arma e mira entre dois clientes também foram confirmadas pelo usuário.

## Teste com dois clientes

Abra Lvl_FirstPerson. Use Number of Players 2, Play As Client e New Editor Window (PIE). Confira o rifle na própria janela e nas mãos do outro personagem. Em uma janela, mire para cima e para baixo; observe a arma pela outra. Confira também movimento e salto. Repita disparo e recarga: dano 25, carregador 10, reserva 30 e recarga 2 segundos.

Ainda não há efeitos novos, animação de disparo ou animação de recarga. O visual é provisório.

Validação manual confirmada pelo usuário em 28/10/2026, com dois jogadores.

## Braços presos à câmera (02/11/2026)

A câmera continua na cápsula, na altura dos olhos, alinhada à origem do tiro calculada pelo servidor. Em BeginPlay, os braços em primeira pessoa passam a ser filhos da câmera, mantendo a posição neutra; olhar para cima ou para baixo não desloca mais o rifle na tela. Para o próprio dono, GetBaseAimRotation devolve pitch zero, pois o corpo copiado pelos braços já gira com a câmera; os demais jogadores continuam vendo o corpo mirar.

FirstPersonArmsOffset (padrão X=18, Z=-4, em centímetros no espaço da câmera) afasta os braços e o rifle da câmera, mostrando mais do corpo da arma e evitando corte pelo near clip plane. Pode ser ajustado nos defaults do Blueprint do personagem.

Marau6.CaptureViewModel salva capturas da visão em LocalSettings/viewmodel_*.png em vários ângulos de pitch. Use um mapa fora da base persistente, onde o personagem já começa armado:

`UnrealEditor.exe Marau6.uproject /Game/Variant_Shooter/Lvl_Shooter?game=/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C -game -ExecCmds="Marau6.CaptureViewModel"`

## Modelo lasgun (02/11/2026)

O rifle do template foi substituído pela lasgun Kantrael. SKM_Lasgun equipa as duas perspectivas do personagem e os bots; SM_Lasgun é o loot no chão. Ficam em Content/Weapons/Lasgun, com Materials, Meshes e Textures.

Fonte: "warhammer40k - modular kantrael pattern lasgun", de sanyaork (https://sketchfab.com/3d-models/warhammer40k-modular-kantrael-pattern-lasgun-e3c65702ed894a9e99d9b771f90d6d9b), licença CC BY-NC 4.0 (https://creativecommons.org/licenses/by-nc/4.0/). O crédito ao autor é obrigatório e o uso comercial é proibido. A arma também é propriedade intelectual da Games Workshop; trocar o modelo antes de qualquer distribuição comercial.

O modelo original é modular e animado. Foi usada a pose de repouso (coronha fixa, carregador padrão, cano curto, miras de ferro), sem os módulos guardados fora da arma. A pose foi aplicada aos vértices, nivelada e reduzida para 70%, de forma que punho, coronha e carregador coincidem com o SKM_Rifle. A origem continua no punho (HandGrip_R) e o cano aponta para +Y. Os soquetes Muzzle, GripPoint e GripPoint_002 seguem os nomes do rifle antigo; Muzzle fica em (0, 63,1, 10,5). Os materiais são instâncias de M_Lasgun (cor, ORM no padrão glTF e normal, com uso em malha esquelética), pois o M_GLTF do Interchange não tem esse uso e cairia no material padrão.

## Nitidez da lasgun em primeira pessoa (02/11/2026)

A arma parecia borrada de perto por três motivos, nenhum deles a resolução da arte:

- O Interchange copiou o sampler do .glb (LINEAR_MIPMAP_LINEAR) e gravou Filter=TF_Trilinear nas 27 texturas, o que desliga o filtro anisotrópico do grupo. Agora Filter=Default.
- r.ScreenPercentage.Default.Desktop.Mode=1 (baseado na resolução da tela) renderizava cerca de 73% em 1080p e 61% em 1440p, com upscale espacial, pois r.AntiAliasingMethod=0 não tem upscaler temporal. O PIE seguia a mesma regra pelo viewport do editor. Agora o modo é manual em 100%, no jogo e no viewport (r.Editor.Viewport.ScreenPercentageMode.RealTime=0).
- T_Lasgun_LasgunStock2_ORM tinha sido importada como normal map (BC5, grupo WorldNormalMap), incompatível com o sampler Linear Color do M_Lasgun.

scripts/fix_lasgun_textures.py aplica nas texturas: grupos Weapon, WeaponNormalMap e WeaponSpecular (aniso, sem limite de tamanho); NeverStream=True, para a arma colada na câmera sempre ter o mip 0 (cerca de 100 MB residentes); LODBias=0; MaxTextureSize=0; mips do grupo; BC sRGB, N em Normalmap (BC5) e ORM linear. DefaultEngine.ini ganhou r.MaxAnisotropy=16 em [SystemSettings], para todos os níveis de qualidade.

Limite da arte: as imagens originais são 2048×2048 (BC e N em JPEG) e ocupam todo o espaço UV do módulo. A densidade fica entre cerca de 38 px/cm (LasgunBase, ForegripWithRails) e 180 px/cm (Stock2). O corpo da arma, bem próximo da câmera, pode ficar maior na tela do que a própria textura; nesse caso, mais nitidez exige texturas 4K do autor. Não foi aplicado sharpen nos mips, porque realçaria os artefatos do JPEG.
