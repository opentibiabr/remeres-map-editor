# Semantic City Corpus And Learning - Design Specification

## Objetivo

Substituir a geracao de cidades baseada em copia aleatoria de tiles por uma
pipeline auditavel que aprende estruturas urbanas a partir de mapas OTBM reais.
A pipeline deve extrair um corpus semantico em JSON, calcular um modelo de
padroes espaciais e, em uma fase posterior, gerar cidades novas respeitando
terrenos, ruas, predios multiandar, telhados, casas e equipamentos civicos.

## Decisao Central

OTBM permanece como formato de armazenamento do mapa e sera lido/escrito
somente pelo `IOMapOTBM` existente no editor. Nao sera criado um escritor OTBM
paralelo e nao sera usado JSON bruto como representacao editavel de producao.

O JSON sera uma representacao de aprendizado: compacta, legivel e enriquecida
com informacoes que a arvore OTBM so fornece depois de carregada pelo editor,
como `GroundBrush`, `WallBrush`, houses, towns, portas, escadas e regioes
conectadas.

## Corpus Inicial

A primeira versao reconhece como fontes os maps
`D:/kl/canary/data-otservbr-global/world/otservbr.otbm` e
`D:/kl/canary/data-canary/world/canary.otbm`. A acao principal exporta
automaticamente todos os `Town` carregados em cada mapa, usando
`templePosition`, `townid` e houses como evidencias de delimitacao.

O editor tambem podera exportar futuramente regioes dos arquivos OTBM menores
de `world_changes`, mas eles nao entram automaticamente como cidades: muitos
sao patches de eventos, sem o contexto urbano completo necessario para treino.

Mapas inteiros nao serao despejados como JSON de tiles. O mapa global de
`otservbr.otbm` possui aproximadamente 185 MB e quase 18 milhoes de tiles; um
dump integral seria custoso, dificil de auditar e ruim como dado de treino.
Cada exemplo do corpus sera um distrito inferido de uma town. A exportacao
manual de uma selecao permanece util para curadoria ou para areas que nao
possuam `Town`.

## Escopo Da Primeira Entrega

Esta entrega cria os fundamentos de aprendizado, nao a geracao final.

- Acao `Export City Learning Sample...` para a selecao corrente do mapa.
- Atalho opcional somente depois de a acao estar funcional e validada.
- Exportacao JSON normalizada da selecao, incluindo todos os andares que
  possuam tiles dentro da projecao selecionada.
- Acao `Export All Town Learning Corpus...` para exportar todas as towns do
  mapa aberto em um unico JSON escrito incrementalmente por distrito.
- Agregador que carrega varios JSONs exportados e produz um
  `city-model.json` com estatisticas e catalogo de prototipos.
- Relatorio textual/JSON de diagnostico que permita verificar o que foi
  aprendido antes de habilitar qualquer nova geracao.
- Desativacao ou renomeacao explicita da geracao experimental atual enquanto
  ela ainda produzir cidades incoerentes.

## Delimitacao Automatica De Cities E Districts

Para cada `Town`, o exportador reune houses cujo `townid` corresponda ao id da
town. Cada house contribui com o envelope dos seus house tiles e com sua
entrada. Houses pertencem ao mesmo distrito quando a distancia de Chebyshev
entre seus envelopes e de no maximo 24 tiles; a transitividade dessa relacao
forma os clusters.

O distrito cuja distancia ao `templePosition` for menor e rotulado `main`; os
demais sao `satellite`. Cada envelope recebe margem de 16 tiles para capturar
ruas e contexto imediatamente conectado sem preencher artificialmente os
grandes vazios entre bairros distantes.

Se uma town nao possuir houses, o exportador gera um distrito `main` centrado
no templo, com raio de 96 tiles, `inferenceMethod` igual a
`temple_fallback` e confianca `low`. Distritos baseados em houses usam
`house_clusters_with_temple_anchor` e confianca `high` quando o cluster contem
mais de uma house, ou `medium` quando contem uma unica house.

O corpus completo usa formato `rme-city-corpus` versao 2:

```json
{
  "format": "rme-city-corpus",
  "version": 2,
  "sourceMap": "otservbr.otbm",
  "cities": [
    {
      "town": {},
      "districts": [
        {
          "role": "main",
          "inferenceMethod": "house_clusters_with_temple_anchor",
          "confidence": "high",
          "bounds": {},
          "houses": [],
          "tiles": [],
          "summary": {}
        }
      ]
    }
  ]
}
```

O arquivo e escrito incrementalmente, mantendo apenas um distrito em JSON na
memoria enquanto percorre o mapa. Isso evita acumular os dados de todas as
cidades de `otservbr.otbm` em memoria ao mesmo tempo.

## Nao Escopo Da Primeira Entrega

- Gerar imediatamente uma cidade nova em `Ctrl+K`.
- Treinar rede neural sobre imagens ou bytes OTBM.
- Salvar mapas por meio de `OTBM2JSON`.
- Inferir automaticamente que toda regiao do mapa e uma cidade.
- Editar ou substituir houses, towns ou XML auxiliares nesta etapa.

## Componentes

### `CityCorpusExporter`

Servico C++ interno do editor que recebe `Map`, retangulo selecionado, nome da
amostra e origem do mapa. Ele percorre as posicoes selecionadas em todos os
andares ocupados e emite JSON por meio de `nlohmann::json`, ja presente no
projeto.

Cada tile exportado contem:

- coordenada absoluta e `z`;
- ground item id e nome/brush, quando identificavel;
- itens adicionais com id, propriedades semanticas e brushes;
- house id e tile flags;
- marcadores de porta, janela, escada, depot, teleport e walkability;
- vizinhanca ortogonal/diagonal derivada somente no relatorio, evitando
  duplicar dados brutos no arquivo.

O exportador inclui metadados da amostra:

- arquivo fonte e versao de mapa;
- bounds selecionados;
- towns presentes e temple positions dentro/proximos da area;
- houses que possuam tiles na selecao, com entrada e quantidade de tiles
  observada;
- contagens por andar e por categoria.

### `CitySemanticClassifier`

Camada deterministica compartilhada pelo exportador e pelo futuro gerador.
Classifica itens e tiles usando os dados nativos do editor:

- `GroundBrush` para grass, road, floor, water e terrain;
- `WallBrush` para parede estrutural, fence, railing, porta e janela;
- propriedades de item para escada, depot, teleport e decoracao;
- `House` e `Town` para rotulos funcionais confiaveis.

A classificacao nao assume que um id isolado sempre significa a mesma funcao.
Quando nao houver evidencia semantica nativa suficiente, o item fica em
categoria `unclassified`, visivel no relatorio para curadoria posterior.

### `CityStructureExtractor`

Opera sobre uma amostra ja classificada e descobre estruturas:

- regioes conectadas de rua, pavimento, terreno, agua e praca;
- predios residenciais rotulados por `house id`;
- componentes construidos sem house id, inicialmente `unlabeled_building`;
- footprints por andar, envelopes multiandar e camadas de roof;
- ports de conexao: porta para rua e escada entre pisos;
- predios civicos comprovados por depot item ou temple position.

Um predio e uma entidade multi-tile e multiandar. Nenhuma fase posterior deve
recompor telhados, portas ou paredes sorteando tiles individuais.

### `CityModelLearner`

Agrega uma ou mais amostras JSON e produz `city-model.json`. O modelo e
estatistico e baseado em restricoes, nao neural, porque o dataset inicial e
pequeno e as regras de validade sao exatas.

O modelo contem:

- distribuicao de cobertura por terreno e andar;
- matriz de adjacencia entre categorias de ground;
- grafo resumido das ruas, largura e frequencia de intersecoes;
- distribuicao de lotes e distancia de entradas ate ruas;
- catalogo de predios completos com footprint, floors, roofs e ports;
- frequencia de casas, depot e temple por tamanho de amostra;
- lista de itens/estruturas nao classificados para curadoria.

Os prototipos preservam os tiles originais apenas como referencia de
reconstrucao e visualizacao. A futura sintese escolhera entidades completas e
usara brushes para adequar transicoes ao novo entorno.

### Relatorio E Validacao Visual

Cada exportacao gera um resumo JSON e, quando possivel, um relatorio legivel
na interface ou em arquivo. A validacao visual devera renderizar futuramente:

- bounds e pisos exportados;
- footprint de cada predio reconhecido;
- entradas, ruas e conectores de escada;
- comparacao do mapa original com a segmentacao aprendida.

`otbm-render` pode inspirar ou apoiar uma ferramenta externa de renderizacao,
mas a classificacao de producao permanece no editor, pois ele conhece houses,
brushes e itens modernos.

## Formato JSON Da Amostra Manual

Este formato continua disponivel para amostras curadas por selecao. O corpus
automatico por towns usa o formato `rme-city-corpus` v2 descrito acima.

O arquivo exportado tera esta forma conceitual:

```json
{
  "format": "rme-city-corpus",
  "version": 1,
  "sample": {
    "name": "Thais",
    "sourceMap": "otservbr.otbm",
    "bounds": { "minX": 32226, "maxX": 32461, "minY": 32134, "maxY": 32314 },
    "floors": [5, 6, 7, 8]
  },
  "towns": [],
  "houses": [],
  "tiles": [],
  "structures": [],
  "summary": {}
}
```

O formato sera versionado desde o inicio. A exportacao deve ser deterministica:
a mesma selecao no mesmo mapa produz tiles, estruturas e estatisticas na mesma
ordem.

## Fluxo Do Usuario

1. O mapper abre `otservbr.otbm` ou `canary.otbm`.
2. Aciona `Export All Town Learning Corpus...` e salva o JSON completo do
   mapa; opcionalmente exporta uma selecao manual para curadoria.
3. Repete a exportacao para o segundo mapa.
4. Aciona `Build City Learning Model...` e seleciona os JSONs do corpus.
5. O editor gera `city-model.json` e mostra contagens, estruturas
   classificadas e itens pendentes de curadoria.
6. Somente depois de revisar o modelo habilitamos a nova fase de geracao.

## Geracao Futura

O futuro sintetizador devera receber area vazia/gramada e tamanho desejado,
usar o modelo para planejar ruas, lotes e equipamentos civicos, e posicionar
predios completos. Depois aplicara brushes nativos:

- terrenos com `GroundBrush`, seguidos por `borderize`;
- paredes com `WallBrush`, seguidas por `wallize`;
- roofs, portas e escadas preservados como estruturas coordenadas;
- houses e towns criados com seus metadados e XML auxiliares pelo editor.

O sintetizador deve validar conectividade e integridade antes de alterar o
mapa selecionado.

## Tratamento Da Geracao Experimental Atual

O codigo existente em `city_generation.cpp` e util apenas como evidencia do
que nao deve ser repetido: ele aprende pools de tiles e materializa celulas de
forma independente. Durante a primeira entrega, a acao atual sera removida do
fluxo principal ou identificada como experimental e nao sera apresentada como
geracao aprendida.

Nenhuma tentativa sera feita de consertar a qualidade visual adicionando mais
amostras ao algoritmo atual, pois isso preservaria a falha estrutural.

## Testes E Criterios De Aceite

### Exportacao

- Exportar uma amostra minima sintetica produz JSON deterministico.
- Ground, wall, house tile, porta e escada conhecidos aparecem na categoria
  correta.
- A area de Thais produz contagens coerentes com o mapa carregado e inclui
  multiplos andares e house ids.

### Extracao De Estruturas

- Tiles de uma mesma house sao agrupados por `house id`.
- Entradas e conexoes verticais sao vinculadas ao predio correspondente.
- Componentes sem house id permanecem distintos e explicitamente nao
  rotulados, sem serem inventados como loja ou templo.

### Aprendizado

- Agregar duas ou mais amostras preserva a origem de cada prototipo.
- O modelo gera distribuicoes e catalogo de predios deterministicamente.
- Itens nao classificados aparecem em lista auditavel.

### Seguranca Do Mapa

- A fase de corpus e modelo nao altera tiles no mapa aberto.
- Nenhum arquivo OTBM e salvo pelo learner.
- Nenhuma acao de geracao experimental e executada implicitamente.

## Sequencia De Entrega

1. Introduzir tipos e classificacao semantica testavel.
2. Implementar exportacao JSON da selecao e preset Thais.
3. Extrair houses, footprints, floors, roofs e ports.
4. Implementar agregador e `city-model.json`.
5. Validar com amostras de `otservbr.otbm` e `canary.otbm`.
6. Projetar e implementar o sintetizador somente apos a revisao visual dos
   prototipos aprendidos.
