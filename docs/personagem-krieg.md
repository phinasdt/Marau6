# Personagem Krieg

O jogador e os bots usam SKM_Krieg no lugar do SKM_Manny_Simple. O BP_FirstPersonCharacter aponta o corpo e a malha de primeira pessoa para ele; AMarau6Bot carrega o mesmo caminho no construtor.

## Crédito e licença

Modelo "Death-korps-of-krieg-20", de Kaiser-E, publicado no Sketchfab sob CC Attribution (https://sketchfab.com/3d-models/death-korps-of-krieg-20-ba2ffbe33b8c41a780e3154c90849dde). A licença exige crédito ao autor. Um comentário na página afirma que o upload é uma republicação, e o tema é propriedade intelectual de Warhammer 40k (Games Workshop): serve para protótipo, não para lançamento.

## Esqueleto

A malha usa o esqueleto SK_Mannequin e o PA_Mannequin, então animações, ABP_Marau6_FP_Rifle, ABP_Marau6_TP_Rifle e o socket HandGrip_R funcionam sem mudanças. No Blender, uma cópia do Manny foi posta em T-pose com as juntas ajustadas ao modelo; os pesos foram transferidos da superfície do Manny. Capacete, máscara e lentes seguem o osso head, a mochila segue spine_05 e a pá segue pelvis. Depois a malha foi deformada de volta para a A-pose e as proporções exatas do Manny, de modo que o bind pose coincide com o do esqueleto.

O sobretudo é pesado direto nas pernas e estica entre elas quando uma coxa se levanta.

## Materiais

Content/Marau6/Characters/Krieg/Materials/M_Krieg é o material mestre: mapas de cor, normal, rugosidade e metal, com escalares para desligar cada mapa. RoughnessBias deixa o capacete, as ombreiras e as peças de metal com aspecto de aço pintado. M_Krieg tem a flag Used with Skeletal Mesh; sem ela o jogo usa o material padrão.

As texturas são as 2048x2048 do FBX original. O GLB do Sketchfab traz versões de 1024 com UV invertido. As normais estão em convenção OpenGL e usam Flip Green Channel.
