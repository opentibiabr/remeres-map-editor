# TODO - Materials Workbench Finalizacao

## Fechado
- [x] Materials Workbench como modulo principal
- [x] Leitura do catalogo via `materials.db`
- [x] Edicao visual de `palettes`
- [x] Edicao de propriedades de `brush`
- [x] Border Workspace visual inicial
- [x] Wall Workspace visual inicial
- [x] Refresh principal de palettes no runtime
- [x] Base de persistencia SQLite para continuar evoluindo sem gambiarra

## Pendente Antes De Chamar De Pronto
- [ ] Implementar edicao de `variations`
- [ ] Implementar `dirty state` por workspace
- [ ] Confirmar troca de selecao com alteracoes pendentes
- [ ] Melhorar validacoes antes de salvar
- [ ] Criar previews mais ricos para `borders` e `walls`
- [ ] Permitir criacao e remocao de entidades, nao so edicao das existentes
- [ ] Melhorar clareza semantica dos metadados de origem
- [ ] Lapidar visualmente a UI e compactar melhor os workspaces
- [ ] Implementar fluxo profissional de `deprecated` para XML

## Ajustes Finos De UI E UX
- [ ] Reduzir espacamentos e padding em todos os workspaces para ganhar densidade
- [ ] Unificar cabecalhos, status bars e botoes de acao para todas as telas da Workbench
- [ ] Destacar visualmente campos alterados desde o ultimo save
- [ ] Mostrar badge `modified` na arvore de navegacao
- [ ] Adicionar `Save`, `Revert` e `Reload from DB` com comportamento consistente
- [ ] Melhorar textos de status para mensagens mais uteis e menos genericas
- [ ] Preservar selecao e scroll ao recarregar listas e grids
- [ ] Adicionar tooltips ricos com `id`, tipo, source e relacionamentos
- [ ] Renomear labels ambiguos de proveniencia
- [ ] Melhorar contraste visual e alinhamento dos grids de preview
- [ ] Padronizar labels tecnicos como `lookId`, `serverLookId`, `zOrder`, `partType`

## Clareza Semantica E Proveniencia
- [ ] Trocar `Source` por `Imported From` na UI quando o campo representar origem legada
- [ ] Avaliar trocar `Source` por `Legacy XML Source`
- [ ] Avaliar trocar `Source` por `Origin File`
- [ ] Exibir separadamente `Storage: materials.db`
- [ ] Exibir separadamente `Imported from: ...`

## Robustez E Validacoes
- [ ] Validar `item id` inexistente antes de salvar
- [ ] Validar duplicidade indevida em slots de border quando fizer sentido
- [ ] Validar `door type` compativel com o `item id`
- [ ] Impedir save de brush com `type` invalido ou inesperado
- [ ] Proteger melhor transicoes de selecao em grids recriados dinamicamente
- [ ] Adicionar logs mais especificos para falhas de save/reload
- [ ] Revisar refresh de `walls` e `borders` no runtime, como ja foi feito com palettes
- [ ] Adicionar testes focados para serializacao SQLite de `wallParts`, `borderSetItems` e `tilesets`

## Features Novas
- [ ] Criar editor de `variations` para walls, doodads e alternates conforme o dominio
- [ ] Criar preview composto real de `wall brush`, nao so grid de partes
- [ ] Criar preview de border aplicada em mini-cena, nao so matriz de slots
- [ ] Permitir duplicar `border set`, `wall part set` ou `palette`
- [ ] Permitir criar novo `brush`, `palette` e `border set` direto pela Workbench
- [ ] Permitir remover entidades com confirmacao segura e validacao de referencias
- [ ] Adicionar busca e filtro em tempo real na arvore
- [ ] Criar inspector de referencias: onde este brush e usado
- [ ] Criar historico local de edicao por sessao
- [ ] Permitir export/import pontual de `brush`, `palette` ou `border set`
- [ ] Adicionar comando de `sync runtime` manual apos save
- [ ] Adicionar comparacao `DB vs XML` durante a fase hibrida

## Variations
- [ ] Priorizar `variations` como proxima feature funcional relevante
- [ ] Recuperar o valor funcional que existia no editor antigo
- [ ] Implementar `variations` com arquitetura limpa, sem repetir o espaguete antigo

## Roadmap Sugerido
- [ ] Etapa 8: Variations Workspace ou suporte de `variations` dentro dos workspaces existentes
- [ ] Etapa 9: Dirty state, validacoes, save workflow, protecao contra perda de alteracoes e revisao dos labels/metadados de origem na UI
- [ ] Etapa 10: Lapidacao visual pesada + previews mais ricos
- [ ] Etapa 11: Fluxo XML `deprecated` + utilitarios de migracao/sync

## Resumo Executivo
- [ ] Fechar gap funcional principal: `variations`
- [ ] Fechar polish fino de UX/UI
- [ ] Fechar validacoes e robustez
- [ ] Fechar UX de edicao profissional
- [ ] Fechar transicao profissional para XML `deprecated`