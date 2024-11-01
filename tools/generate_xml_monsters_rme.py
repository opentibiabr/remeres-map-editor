import os
import re
from lxml import etree

# Função para extrair o nome do monstro
def extract_monster_name(content):
    match = re.search(r'local mType\s*=\s*Game\.createMonsterType\("(.+?)"\)', content)
    if match:
        return match.group(1)
    return None

# Função para extrair o outfit do monstro
def extract_monster_outfit(content):
    match = re.search(r'monster\.outfit\s*=\s*\{([^}]+)\}', content, re.DOTALL)
    if match:
        outfit_content = match.group(1)
        outfit = {}
        for line in outfit_content.splitlines():
            key_value = re.search(r'(\w+)\s*=\s*(\d+)', line)
            if key_value:
                key, value = key_value.groups()
                outfit[key] = value
        return outfit
    return None

# Função para gerar o XML a partir dos dados extraídos
def generate_xml(monsters_data, output_file, skipped_files):
    root = etree.Element("monsters")
    
    # Ordenar monstros por nome
    monsters_data.sort(key=lambda x: x['name'])
    
    for monster in monsters_data:
        outfit = monster['outfit']
        attributes = {
            'name': monster['name']
        }
        
        if 'lookTypeEx' in outfit and outfit['lookTypeEx'] != '0':
            # Se lookTypeEx está presente e não é '0', incluir lookitem e omitir looktype e outros atributos
            attributes['lookitem'] = outfit['lookTypeEx']
        else:
            # Verificar se algum dos atributos de roupa tem valor maior que zero
            has_non_zero_outfit = any(
                outfit.get(key, '0') != '0'
                for key in ['lookHead', 'lookBody', 'lookLegs', 'lookFeet', 'lookAddons']
            )
            
            if has_non_zero_outfit:
                # Se pelo menos um atributo de roupa é maior que zero, incluir todos os atributos de roupa e looktype
                attributes.update({
                    'looktype': outfit.get('lookType', '0'),
                    'lookhead': outfit.get('lookHead', '0'),
                    'lookbody': outfit.get('lookBody', '0'),
                    'looklegs': outfit.get('lookLegs', '0'),
                    'lookfeet': outfit.get('lookFeet', '0'),
                    'lookaddons': outfit.get('lookAddons', '0')
                })
                # Remover atributos que são '0' (exceto looktype)
                attributes = {k: v for k, v in attributes.items() if v != '0' or k == 'looktype'}
            else:
                # Caso contrário, incluir somente looktype se existir
                if 'lookType' in outfit:
                    attributes['looktype'] = outfit['lookType']
        
        # Criar o elemento XML
        monster_elem = etree.SubElement(root, "monster", **attributes)
    
    # Criar a árvore XML e adicionar a declaração XML
    tree = etree.ElementTree(root)
    with open(output_file, 'wb') as f:
        f.write(b'<?xml version="1.0" encoding="UTF-8"?>\n')
        tree.write(f, encoding='utf-8', xml_declaration=False, pretty_print=True)
    
    # Adicionar os arquivos não processados no final do arquivo XML
    if skipped_files:
        skipped_elem = etree.SubElement(root, "skipped_files")
        for file in skipped_files:
            etree.SubElement(skipped_elem, "file").text = file

# Função principal que percorre as pastas e processa os arquivos Lua
def process_lua_files(root_folder, output_xml):
    monsters_data = []
    skipped_files = []
    
    for subdir, _, files in os.walk(root_folder):
        for file in files:
            if file.endswith('.lua'):
                file_path = os.path.join(subdir, file)
                with open(file_path, 'r', encoding='utf-8') as f:
                    content = f.read()
                    name = extract_monster_name(content)
                    outfit = extract_monster_outfit(content)
                    if name and outfit:
                        # Verificar se o monstro atende às condições para ser incluído
                        has_non_zero_outfit = any(
                            outfit.get(key, '0') != '0'
                            for key in ['lookHead', 'lookBody', 'lookLegs', 'lookFeet', 'lookAddons']
                        )
                        
                        if ('lookTypeEx' in outfit and outfit['lookTypeEx'] != '0') or has_non_zero_outfit or 'lookType' in outfit:
                            monsters_data.append({'name': name, 'outfit': outfit})
                        else:
                            skipped_files.append(file_path)
                    else:
                        skipped_files.append(file_path)
    
    generate_xml(monsters_data, output_xml, skipped_files)

# Chamada da função principal com o diretório de entrada e arquivo XML de saída
root_folder = 'C:/Users/Usuario/Documents/GitHub/fork-canary/data-otservbr-global/monster'
output_xml = 'monstros.xml'
if __name__ == '__main__':
    process_lua_files(root_folder, output_xml)

    print(f"Arquivo XML gerado com sucesso: {output_xml}")
