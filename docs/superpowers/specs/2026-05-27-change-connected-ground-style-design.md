# Change Connected Ground Style - Design Specification

## Objetivo

Adicionar ao Remere's Map Editor uma operacao contextual para substituir o
estilo de um caminho urbano conectado no andar clicado, com preview antes da
aplicacao e undo/redo em uma unica acao. A operacao deve aproveitar a semantica
de `GroundBrush` e de suas bordas automaticas, em vez de manipular ids brutos.

A primeira entrega resolve alteracoes locais confiaveis, como trocar o
cobblestone de Thais ou o sandstone contornado de Ankrahmun. Ela tambem
permite trocar uma faixa isolada em um sistema composto, como Venore, mas nao
tenta substituir automaticamente todo o estilo urbano composto sem antes
existir um modelo de papeis e transicoes.

## Evidencias Verificadas

### Thais

O piso `870` pertence ao brush `cobblestone`. A via e um componente simples do
mesmo `GroundBrush`, interagindo com grounds vizinhos por bordas automagic.
Substituir o componente do brush e recalcular sua fronteira e suficiente.

### Ankrahmun

O piso `923` pertence ao brush `sandstone`. Os itens `924` a `935`, incluindo
`927`, `930` e `931`, sao definidos como `super border` com
`ground_equivalent="923"`. Embora aparecam visualmente como o contorno escuro
do caminho e possam ser serializados como ground, pertencem ao mesmo brush
logico.

No corpus exportado de Ankrahmun foram observados `1.447` tiles de `923`,
`703` tiles de `927`, `95` tiles de `931` e `91` tiles de `930`, alem das
demais formas de borda. A deteccao precisa considerar todos esses tiles como
um unico componente logico.

### Venore

Venore contem mais de um ground urbano adjacente:

- `venore cobblestone`: itens `17464` a `17468`;
- `venore plaster`: itens `17505` a `17510`;
- `terracotta`: itens `17544` a `17547`;
- `17470` e itens relacionados: borda do conjunto cobblestone, nao um terceiro
  ground.

Esses brushes formam ruas, faixas, pracas e patios compostos. Na primeira
entrega, clicar em uma faixa troca somente o componente logico daquele brush e
preserva os brushes vizinhos. A interface indica que ha outros grounds urbanos
adjacentes e que a alteracao nao representa uma conversao integral do estilo
de Venore.

## Decisao De Escopo

Esta entrega implementa `Change connected ground style...`, paralela a
`Change build style...`.

- A semente e o ground do tile clicado.
- A operacao percorre todo o componente conectado no mesmo `z`.
- A identidade do componente e o `GroundBrush`, incluindo ground equivalents
  e super borders do mesmo brush.
- O destino e selecionado entre estilos de caminho urbano curados.
- Grounds diferentes adjacentes nao entram automaticamente no componente.
- Itens nao-ground, como ralos, luminarias, vegetacao, escadas e decoracoes,
  permanecem no tile.
- Bordas automagic afetadas sao recalculadas no componente e na sua fronteira
  imediata.

O sistema de reconhecimento e conversao de caminhos urbanos compostos e uma
entrega posterior. O primeiro comando deve gerar dados que a suportem, mas
nao deve depender dela.

## Alternativas Consideradas

### Troca por ID bruto

Trocar apenas tiles cujo item id seja identico ao tile clicado e simples, mas
falha com brushes aleatorios, como `venore cobblestone`, e quebra o contorno
equivalente de Ankrahmun. Esta opcao foi descartada.

### Troca automatica de todos os grounds urbanos vizinhos

Trocar toda composicao conectada permitiria converter Venore em uma unica
operacao, mas nao ha ainda informacao suficiente para decidir qual faixa de um
estilo corresponde a qual faixa de outro. O resultado poderia substituir
pracas, patios ou calçadas pelo material errado. Esta opcao fica para a
entrega de sistemas urbanos compostos.

### Troca do brush logico clicado com aviso de composicao

Esta e a opcao escolhida. Ela resolve de forma correta Thais e Ankrahmun,
mantem a edicao de Venore previsivel e produz evidencias para a fase de
aprendizado posterior.

## Fluxo Do Usuario

1. O mapper clica com o botao direito em um ground urbano.
2. Escolhe `Change connected ground style...`.
3. O editor encontra todo o componente logico conectado daquele brush no
   andar clicado.
4. A janela abre com preview centralizado no tile clicado e informa a
   quantidade de tiles envolvidos.
5. O mapper escolhe um estilo de caminho urbano de destino.
6. Se houver grounds urbanos diferentes encostando no componente, a janela
   mostra um aviso: somente a faixa clicada sera alterada.
7. O preview mostra exatamente o resultado, inclusive bordas recalculadas.
8. Ao aplicar, o editor grava uma unica acao reversivel no historico.

O comando nao troca outros andares. Pisos superiores ou inferiores podem
conter vias, interiores ou telhados semanticamente distintos e devem ser
editados deliberadamente.

## Componente Logico

### Identidade

Um tile pertence ao componente de origem quando:

- esta no mesmo `z` do tile inicial;
- e alcancavel por vizinhanca ortogonal;
- seu ground retorna o mesmo `GroundBrush` da origem.

Essa regra abrange itens normais randomizados do brush e itens de
`ground_equivalent`, pois o carregamento de `AutoBorder` associa essas pecas
ao brush proprietario.

### Busca

O flood fill existente na pintura de ground e limitado ao bloco local de
`64x64`, portanto nao serve para uma rua inteira. O novo servico utiliza uma
busca ortogonal sem esse limite, com conjunto de posicoes visitadas, no mesmo
padrao usado pelo servico de `Change Build Style`.

Para componentes muito grandes, a janela mostra quantidade de tiles antes de
aplicar. A operacao continua permitida, pois o requisito e alterar toda a via
conectada daquele andar.

### Fronteira

O servico tambem coleta o anel de oito vizinhos dos tiles convertidos. Esse
anel nao tem seu ground substituido; ele e usado apenas para recalcular
bordas, pois as bordas representam a relacao entre o caminho alterado e seus
grounds vizinhos. Para calcular corretamente as bordas desse anel no overlay,
uma segunda faixa de vizinhos e copiada como contexto de leitura, mas nunca e
gravada como alteracao.

## Catalogo De Estilos Urbanos

A lista de destino nao pode expor todos os grounds do tileset. `Grounds -
Ornamented` inclui pisos internos, madeira, terrenos naturais e outros itens
que nao representam caminhos de cidade.

A primeira entrega introduz um catalogo curado de `GroundBrush` urbanos,
alimentado pelos brushes encontrados em cidades e pela secao de pavements do
tileset. O catalogo inicial inclui as familias verificadas:

- `cobblestone`, `dark cobblestone` e `ugly cobblestone`;
- `yellow pavement` e `dark pavement`;
- `venore cobblestone` e `venore plaster`;
- `terracotta`, quando usada como area urbana externa;
- `roshamuul pavement`;
- `oramond pavement` e `oramond other pavement`;
- `new grass pavement` e `new ornamented pavement`;
- `sandstone`, para o caso externo de Ankrahmun.

O catalogo exclui madeiras, roofs, carpetes, agua, swamp, grounds naturais e
pisos estritamente internos. Cada item tem um rotulo de familia para a
interface, por exemplo `Pavement`, `Venore`, `Desert stone` ou `Ornamented`.

O brush de origem pode iniciar a operacao quando fizer parte do catalogo. A
curadoria sera estendida conforme novas cidades do corpus forem classificadas.

## Janela E Preview

A janela segue a experiencia de `Change Build Style`, reutilizando a
abordagem de preview contextual:

- campo de busca e grade/lista de estilos urbanos permitidos;
- sprite e nome de cada `GroundBrush` candidato;
- contador de tiles do componente selecionado;
- identificacao do brush de origem;
- aviso de sistema composto quando houver brush urbano diferente adjacente;
- preview renderizado com o contexto do mapa, inicialmente centralizado
  exatamente no tile clicado;
- controles de pan e zoom equivalentes aos do preview de construcao;
- botoes `Apply` e `Cancel`.

O aviso de composicao lista os brushes urbanos adjacentes encontrados, por
exemplo `venore cobblestone`, `venore plaster` e `terracotta`. Ele e
informativo e nao impede preview nem aplicacao.

O preview deve ser deterministico: a aplicacao confirma os mesmos tiles
simulados no preview, ou usa a mesma escolha de variacoes randomizadas do
destino. Nao e aceitavel mostrar um padrao e gravar outro ao pressionar
`Apply`.

## Aplicacao Da Mudanca

O servico simula e aplica a operacao em duas fases:

1. Copiar os tiles do componente, do anel de fronteira e da segunda faixa
   necessaria como contexto de leitura para um mapa/overlay temporario.
2. Nos tiles do componente, remover somente o ground de origem e desenhar o
   ground do brush de destino, preservando os itens adicionais.
3. Limpar e recalcular bordas automagic no componente e no anel de fronteira.
4. Renderizar o overlay no preview.
5. Em `Apply`, transferir exatamente os tiles ja simulados do componente e do
   primeiro anel para um `BatchAction` do mapa real, sem executar nova
   randomizacao.

Objetos sobre a rua nao sao tratados como conflitos nesta entrega, pois trocar
o ground nao invalida ralos, postes ou decoracao solta. Itens que dependam de
uma semantica futura de cidade permanecem visiveis no preview para decisao do
mapper.

## Historico E Integracao

Novos componentes previstos:

- `ChangeConnectedGroundStyleService`: descoberta, simulacao e aplicacao do
  componente.
- `ChangeConnectedGroundStyleDialog`: selecao do destino, avisos e preview.
- `UrbanGroundStyleCatalog`: filtro curado e classificacao dos brushes
  permitidos.

Integracoes:

- novo item de menu de contexto em `MapCanvas` para grounds catalogados;
- novo identificador de comando em `gui_ids.h`;
- novo tipo/label de acao no historico, exibido como
  `Change connected ground style`;
- inclusao no sistema de build nos mesmos moldes dos arquivos de
  `Change Build Style`.

As alteracoes existentes e ainda nao commitadas de `Change Build Style` serao
preservadas. O novo codigo deve reutilizar somente interfaces estaveis ou
extrair componentes compartilhados de preview quando isso puder ser feito sem
alterar seu comportamento atual.

## Aprendizado Posterior

O learner atual agrega grounds urbanos genericamente como `road` ou
`terrain`, o que nao captura estruturas como Venore ou o uso de
`ground_equivalent` em Ankrahmun.

Depois de validar a edicao basica, uma entrega separada deve extrair:

- componentes logicos de `GroundBrush` por andar;
- equivalencias de ground e super borders;
- grafo de adjacencia entre brushes urbanos;
- largura local, intersecoes, pracas e faixas;
- frequencia de cada composicao por cidade;
- observacoes de edicoes confirmadas pelo usuario.

Esse modelo suportara `Change city path system...` e, depois, a geracao de
cidades com redes viarias coerentes. Ele nao bloqueia a primeira ferramenta.

## Validacao E Testes

### Servico

- detectar integralmente um componente simples de `cobblestone` (`870`);
- detectar `sandstone` incluindo tiles `923` e equivalentes `924` a `935`;
- nao atravessar de `venore cobblestone` para `venore plaster` ou
  `terracotta`;
- preservar itens nao-ground presentes sobre os tiles alterados;
- recalcular somente tiles do componente e de sua fronteira;
- gerar o mesmo resultado no preview e na aplicacao;
- produzir uma unica acao undo/redo.

### Interface

- exibir o comando somente para brushes urbanos catalogados;
- mostrar apenas destinos urbanos permitidos;
- abrir preview centrado no tile clicado;
- exibir aviso nao bloqueante em vias compostas de Venore;
- aplicar e desfazer alteracao em mapas reais de Thais, Ankrahmun e Venore.

### Regressao

- compilar o editor completo;
- abrir mapa real e verificar que `Change Build Style` continua funcional;
- verificar que bordas automaticas normais continuam sendo geradas pela
  pintura tradicional de grounds.

## Fora De Escopo

- substituir automaticamente todo o sistema composto de Venore;
- alterar varios andares na mesma operacao;
- alterar paredes, roofs, houses, portas ou objetos de rua;
- gerar cidades ou redes viarias novas;
- executar aprendizado online dentro do preview;
- transformar todos os grounds ornamented em opcoes de caminho.
