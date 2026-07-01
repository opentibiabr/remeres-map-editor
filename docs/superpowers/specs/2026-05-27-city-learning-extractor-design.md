# City Learning Extractor - Design Specification

## Objetivo

Transformar o corpus bruto `rme-city-corpus` v2 exportado pelo editor em
artefatos compactos e auditaveis que preservem tanto estatisticas como a
geometria necessaria para uma futura geracao procedural de cidades.

O primeiro corpus real, `all-towns-city-corpus.json`, possui aproximadamente
7,8 GB, 30 towns, 58 distritos, 7 milhoes de tiles e 9,3 milhoes de itens.
Mesmo com 32 GB de RAM, carregar integralmente sua arvore JSON em Python cria
objetos com overhead elevado e impede escalar para mais mapas. O extrator sera
streaming desde a primeira versao.

## Entrada

O comando recebe um ou mais arquivos `rme-city-corpus` v2 gerados pelo editor.
O leitor usa `ijson` e processa uma tile de cada vez dentro de cada distrito.
Ele nao modifica o OTBM nem o JSON de origem.

## Saidas

O diretorio de saida contem tres artefatos:

- `city-learning-model.json`: resumo compacto, fontes processadas, perfis de
  towns/distritos, frequencias semanticas, palettes de itens por tag e
  referencias para prototipos.
- `building-templates.jsonl.gz`: um registro comprimido por house ancora,
  preservando metadados, bounds, pisos, footprint RLE por linha, contagens de
  parede/porta/janela/telhado/conector e palettes usadas na propria house.
- `semantic-layouts.jsonl.gz`: um registro comprimido por linha ocupada de
  distrito/piso, com runs horizontais `[xInicial, comprimento, assinatura]`.
  Essas linhas permitem reconstruir o contexto espacial de ruas, agua,
  terreno, construcoes e conectores sem repetir o JSON integral dos itens.

## Assinaturas Semanticas

Cada tile recebe uma assinatura deterministica formada pelas tags relevantes
presentes no ground e nos itens:

`house_tile`, `wall`, `door`, `window`, `roof`, `vertical_connector`, `depot`,
`teleport`, `road`, `water`, `grass`, `floor`, `terrain`, `border`,
`protection_zone` e `unclassified`.

Uma tile pode possuir mais de uma tag; a assinatura e ordenada pela lista
acima. Tiles consecutivas na mesma linha e andar com a mesma assinatura sao
unificadas em um run. A representacao conserva a forma sem armazenar nomes e
objetos repetidos em toda coordenada.

## Extracao De Prototipos

Um prototipo de construcao nasce apenas de uma house marcada como `anchor` no
distrito inferido. Houses observadas somente pela margem de outro distrito nao
sao emitidas novamente.

Para cada prototipo o extrator calcula:

- footprint por andar em RLE relativo ao menor `x/y` observado;
- largura, altura, quantidade de pisos e tiles;
- entrada e metadados originais da house;
- densidade e contagem de tags estruturais;
- IDs de itens mais usados por categoria semantica.

Esses prototipos sao material para selecionar construcoes inteiras na fase de
geracao, evitando reconstruir casas por sorteio de tiles independentes.

## Modelo Compacto

Para cada distrito, `city-learning-model.json` inclui dimensoes, papel
`main`/`satellite`, confianca, quantidade de houses, cobertura por piso,
contagens de assinatura e tag, densidades e top items por tag. O catalogo
global agrega todas as fontes sem perder a identificacao de origem dos
prototipos.

O modelo nao afirma ainda quais areas sao depot, templo ou loja sem evidencia
semantica. Itens `unclassified` permanecem quantificados para curadoria.

## Interface

O comando sera executavel a partir da raiz do repositorio:

```powershell
python tools/city_learning/extract_city_learning.py `
  D:\kl\canary\data-otservbr-global\world\all-towns-city-corpus.json `
  --output D:\kl\canary\data-otservbr-global\world\city-learning
```

Ele imprime progresso por distrito, totals finais e caminhos dos tres
arquivos. Se `ijson` nao estiver instalado, informa o comando para instalar a
dependencia declarada em `tools/city_learning/requirements.txt`.

## Erros E Determinismo

- Arquivo com formato ou versao diferente de `rme-city-corpus` v2 e rejeitado
  com mensagem clara.
- Escrita usa diretorio criado sob demanda e arquivos substituidos de forma
  deterministica.
- As listas e frequencias sao ordenadas por contagem decrescente e id/tag
  como desempate.
- O extrator nunca escreve sobre o corpus de entrada.

## Validacao

Testes unitarios com corpus pequeno verificam:

- assinatura e RLE de tiles consecutivas;
- deduplicacao de houses nao ancora;
- footprint relativo multiandar;
- agregacao deterministica de palettes e perfis;
- leitura streaming ate os tres artefatos.

Depois dos testes, o script sera executado no corpus real de `otservbr` e o
modelo produzido sera inspecionado quanto a city/district/template counts.
