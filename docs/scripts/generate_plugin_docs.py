import os
import re

def subfiles(path):
    return [name for name in os.listdir(path) if os.path.isfile(os.path.join(path, name)) and not name[0] == '.']

def subdirs(path):
    return [name for name in os.listdir(path) if os.path.isdir(os.path.join(path, name))]

def formatModule(module):
    if module == 'io':
        return 'i/o'
    else:
        return module.capitalize()

def parse(group):
    docs = re.compile('/\*\!(.*?)\*/', re.DOTALL)

    docsMatch = docs.match(group)
    clss = group[docsMatch.end():].strip()
    if len(clss) == 0 or 'class' not in clss:
        return None

    blocks = docsMatch.group().split('\\')[1:]
    if len(blocks) == 0:
        return None

    attributes = {}
    for block in blocks:
        key = block[:block.find(' ')]
        value = block[block.find(' '):].split('\n')[0].strip()
        if key in attributes:
            attributes[key].append(value)
        else:
            attributes[key] = [value]

    attributes['Name'] = clss[5:clss.find(':')].strip()
    attributes['Parent'] = clss[clss.find('public')+6:].strip().strip(',') # Handles the edge case of multiple inheritence
    return attributes

def parseInheritance(inheritance):
    abstractions = ['Transform', 'UntrainableTransform',
                    'MetaTransform', 'UntrainableMetaTransform',
                    'MetadataTransform', 'UntrainableMetadataTransform',
                    'TimeVaryingTransform',
                    'Distance', 'UntrainableDistance',
                    'Output', 'MatrixOutput',
                    'Format',
                    'Gallery', 'FileGallery',
                    'Representation',
                    'Classifier'
                   ]

    if inheritance in abstractions:
        return '../cpp_api/' + inheritance.lower() + '/' + inheritance.lower() + '.md'
    else: # Not an abstraction must inherit in the local file!
        return '#' + inheritance.lower()

def parseSees(sees):
    if not sees:
        return ""

    output = "* **see:**"
    if len(sees) > 1:
        output += "\n\n"
        for see in sees:
            output += "\t* [" + see + "](" + see + ")\n"
        output += "\n"
    else:
        link = sees[0]
        if not 'http' in link:
            link = '#' + link.lower()
        output += " [" + sees[0] + "](" + link + ")\n"

    return output

def parseAuthors(authors):
    if not authors:
        return "* **authors:** None\n"

    output = "* **author"
    if len(authors) > 1:
        output += "s:** " + ", ".join(authors) + "\n"
    else:
        output += ":** " + authors[0] + "\n"

    return output

def parseProperties(properties):
    if not properties:
        return "* **properties:** None\n\n"

    output = "* **properties:**\n\n"
    output += "Property | Type | Description\n"
    output += "--- | --- | ---\n"
    for prop in properties:
        split = prop.split(' ')
        ty = split[0]
        name = split[1]
        desc = ' '.join(split[2:])

        table_regex = re.compile('\[(.*?)\]')
        table_match = table_regex.search(desc)
        while table_match:
            before = desc[:table_match.start()]
            after = desc[table_match.end():]

            table_content = desc[table_match.start()+1:table_match.end()-1].split(',')

            table = "<ul>"
            for field in table_content:
                table += "<li>" + field.strip() + "</li>"
            table += "</ul>"

            desc = before.strip() + table + after.strip()
            table_match = table_regex.search(desc)

        output += name + " | " + ty + " | " + desc + "\n"

    return output

def main():
    plugins_dir = '../../openbr/plugins/'
    output_dir = '../docs/docs/plugins/'

    for module in subdirs(plugins_dir):
        if module == "cmake":
            continue

        output_file = open(os.path.join(output_dir, module + '.md'), 'w+')

        names = []
        docs = {} # Store the strings here first so they can be alphabetized

        for plugin in subfiles(os.path.join(plugins_dir, module)):
            f = open(os.path.join(os.path.join(plugins_dir, module), plugin), 'r')
            content = f.read()

            regex = re.compile('/\*\!(.*?)\*/\n(.*?)\n', re.DOTALL)
            it = regex.finditer(content)
            for match in it:
                attributes = parse(match.group())
                if not attributes or (attributes and attributes["Parent"] == "Initializer"):
                    continue

                plugin_string = "# " + attributes["Name"] + "\n\n"
                plugin_string += ' '.join([brief for brief in attributes["brief"]]) + "\n\n"
                plugin_string += "* **file:** " + os.path.join(module, plugin) + "\n"
                plugin_string += "* **inherits:** [" + attributes["Parent"] + "](" + parseInheritance(attributes["Parent"]) + ")\n"

                plugin_string += parseSees(attributes.get("see", None))
                plugin_string += parseAuthors(attributes.get("author", None))
                plugin_string += parseProperties(attributes.get("property", None))

                plugin_string += "\n---\n\n"

                names.append(attributes["Name"])
                docs[attributes["Name"]] = plugin_string

        for name in sorted(names):
            output_file.write(docs[name])
main()
