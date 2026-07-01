# City Learning Pipeline

Esta etapa fica limitada a aprendizado e exportacao de dados de leitura. A
geracao semantica de cidades e a escrita automatica no mapa estao desativadas.

## Artefatos

- `city-learning-model.json`: modelo v2 com papeis de predios, grafo de ruas
  por distrito e qualificacao de fonte.
- `building-templates.jsonl.gz`: footprints observados de construcoes.
- `semantic-layouts.jsonl.gz`: linhas semanticas compactadas do corpus.

## Extrair Modelo

```powershell
python tools\city_learning\extract_city_learning.py `
  D:\kl\canary\data-otservbr-global\world\all-towns-city-corpus.json `
  --output D:\kl\canary\data-otservbr-global\world\city-learning-v2 `
  --quiet
```

O modelo reconhece vias a partir do brush real do ground. Isso evita contar
`sandstone mountain` como rua apenas porque o item visual se chama
`sandstone`, e rejeita capturas em que uma ponte isolada seria confundida
com um centro urbano.

## Geracao Desativada

Os comandos `generate_city_blueprint.py` e `export_blueprint_lua.py` existem
apenas como stubs defensivos e retornam erro. O script antigo
`scripts/city_lot_generator.lua` tambem aborta imediatamente. Isso impede que
uma funcao experimental escreva ruas, lotes ou construcoes no mapa.

## Renderizacao OTBM

`otbm-render` le `.otbm`, `.dat`, `.spr` e `.otb`, mas a copia publica inclui
assets antigos do cliente 10.41. Os ids do mapa Canary requerem assets
compativeis; usar os sprites incluidos no renderer pode mostrar tiles errados.
Por isso a revisao SVG verifica topologia, enquanto a revisao visual final
deve ocorrer no editor ou em um renderer configurado com os assets do mesmo
cliente.

## Limitacao Atual

O pipeline nao cria cidades. Ele serve apenas para entender padroes de mapas
existentes e gerar arquivos de corpus/modelo para inspecao ou ferramentas
futuras aprovadas separadamente.
