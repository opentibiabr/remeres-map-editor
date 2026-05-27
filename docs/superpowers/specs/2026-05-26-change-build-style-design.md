# Change Build Style - Design Specification

## Objetivo

Adicionar ao Canary Map Editor uma operacao nativa para trocar o estilo de uma
construcao automagic a partir de uma parede, railing ou fence existente. A
operacao deve trabalhar com `WallBrush`, preservar portas e janelas, permitir
preview multiandar e aceitar novos estilos arquitetonicos somente quando forem
modelados e validados como brushes funcionais.

## Escopo Da Primeira Entrega

- Acao de menu de contexto `Change build style...` sobre `WallBrush` base.
- Janela modal de selecao de estilo com thumbnails, busca e filtros.
- Preview lateral da construcao com navegacao por andar.
- Conversao do componente do mesmo estilo clicado, sem invadir estilos
  diferentes que estejam conectados ao redor.
- Deteccao de pisos do mesmo predio e opcao para aplicar somente ao piso atual.
- Conversao compativel de portas, janelas e demais aberturas modeladas no
  `WallBrush`.
- Uma unica acao no historico de undo/redo.
- Levantamento e cadastro curado de novos estilos encontrados em Thais,
  priorizando `wooden railing`, `ship cabin wall`, `wooden wall` e extensoes
  consistentes de familias `limestone wall` / `stone wall`.

## Decisoes De Produto

- A lista de estilos inclui todos os `WallBrush` base utilizaveis, inclusive
  walls, railings e fences.
- Estilos serao organizados por filtro: `All`, `Walls`,
  `Railings/Fences` e `New validated styles`.
- Um estilo de destino incompativel permanece visivel, mas desabilitado, com
  a informacao de qual abertura obrigatoria nao existe nele.
- Uma construcao propositalmente composta por estilos diferentes deve ser
  alterada em operacoes separadas. O brush clicado define o que sera trocado.
- O comportamento padrao altera os pisos detectados do mesmo predio. A janela
  inclui a opcao `Only current floor`, que limita a aplicacao ao andar de
  origem.
- Decoracoes aplicadas sobre paredes nao sao trocadas automaticamente nesta
  entrega. Elas permanecem no preview e no mapa.

## Contexto Tecnico Existente

O editor representa paredes automagic, fences, barras e railings como
`WallBrush`. Um brush contem itens para os alinhamentos de parede e pode conter
itens de abertura por alinhamento. Os tipos de abertura existentes incluem:

- `normal`
- `locked`
- `quest`
- `magic`
- `window`
- `hatch window`
- `archway`, quando declarado pelo conjunto

`WallBrush::doWalls()` ja recalcula a forma adequada da parede conforme os
vizinhos. `MapPopupMenu` ja oferece `Select Wallbrush` para o tile clicado, e
o editor ja possui `BatchAction` e labels de historico para alteracoes
multi-tile.

`WallDecorationBrush` compartilha a classe base, mas nao e estrutura da
construcao. Ele nao deve aparecer como destino nem iniciar a operacao.

## Fluxo De Uso

1. O mapper seleciona ou clica em um tile com parede automagic base.
2. No menu de contexto, escolhe `Change build style...`.
3. O editor identifica o componente contiguo do `WallBrush` clicado no piso
   atual e detecta componentes correspondentes nos pisos vizinhos.
4. A janela mostra estilos de destino e o preview do resultado.
5. O mapper navega entre pisos com os botoes de seta, desmarca pisos detectados
   incorretamente ou marca `Only current floor`.
6. O mapper seleciona um estilo compativel e confirma.
7. O mapa e alterado em uma unica acao reversivel, com automagic recalculado.

## Janela `Change Build Style`

### Painel De Estilos

O painel esquerdo contem:

- campo de busca por nome do brush;
- filtros `All`, `Walls`, `Railings/Fences` e `New validated styles`;
- lista ou grade de estilos com sprite representativo e nome;
- indicador desabilitado e tooltip/mensagem para estilo incompativel.

Os estilos sao extraidos dos `WallBrush` carregados pelo editor, filtrando
brushes decorativos. Novos estilos cadastrados no materials backend entram
automaticamente na lista depois de validados.

### Painel De Preview

O painel direito renderiza a area da construcao selecionada usando o renderer
do editor sobre uma copia temporaria dos tiles afetados. Mudar a selecao de
estilo modifica somente essa copia; o mapa real nao muda ate `Apply`.

O painel contem:

- preview do piso atualmente inspecionado;
- botoes de icone `up` e `down` para alternar entre pisos detectados;
- indicacao compacta do piso (`z`) exibido;
- lista de pisos detectados, cada um com checkbox de inclusao;
- checkbox `Only current floor`, que desmarca/desabilita a aplicacao nos
  demais pisos, sem impedir que sejam visualizados.

## Identificacao Do Componente No Piso Atual

A semente da operacao e o item estrutural base no tile clicado. Se o tile
contiver apenas `WallDecorationBrush`, a acao nao aparece.

O editor executa flood fill ortogonal (`north`, `south`, `east`, `west`) no
mesmo `z`. Um tile entra no componente somente quando possui item estrutural
cujo `WallBrush` e exatamente o brush de origem, incluindo itens de abertura
desse brush. A regra deliberadamente nao atravessa brushes amigos ou outros
brushes visualmente conectados.

Consequencias:

- outra parede encostada, mas com estilo diferente, permanece intacta;
- railing ou fence conectado a uma parede nao e alterado junto, salvo quando
  ele proprio for o brush clicado;
- partes da mesma construcao feitas em brushes diferentes sao alteradas em
  operacoes separadas.

## Deteccao Multiandar

O componente inicial produz uma mascara de projecao composta por seus tiles
estruturais e seus quatro vizinhos ortogonais imediatos. Essa expansao de um
tile tolera pavimentos recuados ou ligeiramente deslocados.

Para cada piso imediatamente superior e inferior:

1. Encontrar componentes contiguos do mesmo `WallBrush` de origem que
   intersectem a mascara projetada do ultimo piso aceito.
2. Aceitar automaticamente o melhor candidato quando pelo menos 40% dos tiles
   estruturais do menor entre os dois componentes estiverem dentro da mascara
   projetada.
3. Ao aceitar, criar a mascara desse piso e continuar a busca no piso
   seguinte na mesma direcao.
4. Parar naquela direcao quando nenhum candidato cumprir o limiar.

O limiar inicial de 40% e conservador e deve ser verificado nas amostras de
Thais. Como controle final, todos os pisos detectados ficam explicitamente
visiveis e desmarcaveis na janela antes de aplicar.

Somente componentes do mesmo brush de origem podem ser incluidos pela deteccao
automatica; estilos diferentes em outro piso nao sao convertidos nessa
operacao.

## Compatibilidade De Portas E Janelas

Antes de habilitar um estilo de destino, a janela inspeciona todas as aberturas
dos componentes/pisos selecionados. A conversao exige equivalencia exata para:

- tipo semantico da abertura;
- alinhamento resultante da parede no componente convertido;
- estado aberto/fechado, quando esse estado existe para o item de origem.

Se faltar qualquer equivalente, `Apply` fica indisponivel para o estilo e a
lista informa a causa, por exemplo: `Missing locked door (closed), z=6` ou
`Missing hatch window (open), z=7`.

Essa regra evita substituir uma porta por parede comum, misturar item antigo
com parede nova ou fechar/abrir uma abertura involuntariamente.

## Aplicacao E Undo/Redo

A operacao cria um `BatchAction` dedicado, rotulado `Change Build Style`.

Para cada piso confirmado:

1. Copiar os tiles estruturais afetados para a acao.
2. Trocar paredes comuns pelo brush de destino.
3. Trocar aberturas pelos itens equivalentes validados.
4. Recalcular automagic nos tiles convertidos.
5. Recalcular tambem os quatro vizinhos imediatos, porque terminais, cantos e
   conexoes limitrofes podem mudar visualmente.

Todos os pisos selecionados pertencem ao mesmo batch; `undo` e `redo` atuam
sobre o predio convertido inteiro.

O preview reutiliza a mesma logica de conversao sobre tiles temporarios, para
que a imagem exibida e a alteracao final sigam a mesma regra.

## Novos Estilos Arquitetonicos

### Evidencia Inicial Em Thais

O mapa ativo e `data-otservbr-global/world/otservbr.otbm`. A cidade
`townid=8` e Thais, com templo em `[32369, 32241, 7]` e 132 houses listadas
no arquivo de houses.

Foi usado `Inconcessus/OTBM2JSON` somente para analise offline resumida. Ele
consegue ler o mapa OTBM version 4, mas consumiu aproximadamente 2.6 GB de
memoria e nao modela todos os atributos/nos modernos do Canary. Portanto nao
sera dependencia da feature nem fonte para editar o mapa.

O levantamento encontrou:

- `Alai Flats` em `z=5`, `z=6` e `z=7`, majoritariamente em
  `framework wall`; o piso `z=7` tambem contem `stone wall` e `fence`.
- `The City Wall 1-3` em dois pisos, combinando `framework wall` e
  `stone wall`.
- Entre os brushes existentes mais usados perto das houses de Thais:
  `framework wall`, `stone wall`, `brick wall`, `stone railing3`, `croft
  wall`, `marble wall` e `fence`.
- Itens arquitetonicos observados fora dos `WallBrush` existentes, com
  candidatos como `wooden railing`, `ship cabin wall`, `wooden wall` e
  pecas adicionais de `limestone wall` / `stone wall`.

### Processo De Inclusao

Um candidato novo so passa ao seletor quando o estudo manual identificar:

- segmentos e orientacoes necessarios para o automagic;
- cantos, terminacoes e/ou polos requeridos pela familia;
- equivalencias de porta e janela, quando o conjunto possuir aberturas;
- resultado coerente ao desenhar e re-wallizar um pequeno edificio de teste;
- comportamento coerente em uma area real de Thais onde a familia aparece.

Elementos isolados como wall lamps, trapdoors, ornamentos e gates sem familia
estrutural completa nao sao adicionados como estilo de construcao.

## Componentes Provaveis De Implementacao

- `map_display.*` e `gui_ids.h`: nova acao no menu de contexto e abertura da
  janela.
- Nova janela wxWidgets, seguindo os padroes de `replace_items_window.*`,
  para lista visual, filtros, estado de compatibilidade e preview.
- `editor.*`: servico de descoberta de componente, deteccao de pisos,
  construcao de preview e aplicacao em batch.
- `wall_brush.*`: API publica minima para consultar itens/aberturas
  equivalentes sem expor indevidamente os vetores internos.
- `action.*`: identificador e label `Change Build Style`.
- `data/materials/brushs/*.xml` e tilesets correspondentes: apenas para as
  familias novas comprovadas durante a validacao.

O detalhamento final de arquivos pode variar para respeitar a organizacao
existente sem criar dependencias circulares.

## Validacao

### Comportamento

- Menu aparece apenas ao acionar parede automagic estrutural.
- Flood fill nao cruza para brush diferente em construcao encostada.
- Pisos de `Alai Flats` sao detectados e podem ser removidos da aplicacao.
- `Only current floor` impede mudanca nos demais pisos.
- Preview reflete troca de paredes e aberturas sem modificar o mapa aberto.
- Destino sem variante exigida fica desabilitado e informa a abertura ausente.
- Porta/janela convertida preserva tipo e estado.
- Automagic recompoe cantos, finais e contatos de borda.
- `undo` e `redo` restauram/reaplicam todos os pisos selecionados.

### Catalogo Novo

- Cada novo brush e testado isoladamente com formas retas, cantos e
  contornos fechados.
- Brushes com portas/janelas sao testados em cada variante declarada.
- Cada familia aprovada e verificada em construcao real ou inspirada nos
  exemplos encontrados em Thais.

### Restricoes De Verificacao

Por politica do repositorio, compilacao nao sera executada sem solicitacao
explicita. Durante implementacao, as verificacoes padrao incluirao inspecao
estatica focalizada e `git diff --check`; uma execucao/build do editor sera
solicitada quando a feature estiver pronta para teste visual.

## Fora De Escopo

- Inferir automaticamente todos os estilos estruturais a partir de itens RAW.
- Alterar decoracoes sobrepostas junto com a parede.
- Usar `OTBM2JSON` ou `ot-otbm` como dependencia de runtime do editor.
- Trocar automaticamente brushes diferentes que componham a mesma fachada.
- Editor generico para o mapper criar brushes novos pela interface.
