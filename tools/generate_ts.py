import os
import re
import sys
import xml.etree.ElementTree as ET
from datetime import datetime

# Configuration
script_dir = os.path.dirname(os.path.abspath(__file__))
SOURCE_DIR = os.path.join(script_dir, "../src")
OUTPUT_FILE = os.path.join(script_dir, "../translations/tstar_template.ts")

# Regex for matching tr("string") and QCoreApplication::translate("context", "string")
# Handles escaped quotes mostly
TR_REGEX = re.compile(r'tr\s*\(\s*"((?:[^"\\]|\\.)*)"\s*\)')
TRANSLATE_REGEX = re.compile(r'QCoreApplication::translate\s*\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)')

def scan_files(root_dir):
    strings = {} # context -> set(source_string)
    
    for dirpath, dirnames, filenames in os.walk(root_dir):
        for filename in filenames:
            if filename.endswith((".cpp", ".h", ".ui")):
                path = os.path.join(dirpath, filename)
                # Determine context from filename (classname usually)
                context = os.path.splitext(filename)[0]
                
                with open(path, 'r', encoding='utf-8') as f:
                    try:
                        content = f.read()
                    except UnicodeDecodeError:
                        print(f"Skipping binary file {path}")
                        continue
                        
                    # Find tr() calls
                    # Note: this is a simple regex parser, sophisticated C++ parsing is harder
                    # It assumes the file inheriting QObject uses the filename as context if context isn't explicit
                    # But tr() uses the class context. 
                    
                    matches = TR_REGEX.findall(content)
                    if matches:
                        if context not in strings: strings[context] = set()
                        for m in matches:
                            strings[context].add(m)
                            
                    # Find QCoreApplication::translate calls
                    matches_trans = TRANSLATE_REGEX.findall(content)
                    if matches_trans:
                        for ctx, src in matches_trans:
                            if ctx not in strings: strings[ctx] = set()
                            strings[ctx].add(src)
                            
    return strings

def generate_ts(strings, output_path):
    root = ET.Element("TS", version="2.1", language="en")
    
    for context in sorted(strings.keys()):
        ctx_elem = ET.SubElement(root, "context")
        name_elem = ET.SubElement(ctx_elem, "name")
        name_elem.text = context
        
        for source in sorted(strings[context]):
            msg_elem = ET.SubElement(ctx_elem, "message")
            source_elem = ET.SubElement(msg_elem, "source")
            source_elem.text = source
            trans_elem = ET.SubElement(msg_elem, "translation")
            trans_elem.set("type", "unfinished") # Mark as unfinished/new
            
    tree = ET.ElementTree(root)
    
    # Indent for pretty printing
    ET.indent(tree, space="    ", level=0)
    
    # Ensure directory exists
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    
    tree.write(output_path, encoding="utf-8", xml_declaration=True)
    print(f"Generated {output_path} with {sum(len(v) for v in strings.values())} strings.")

if __name__ == "__main__":
    print("Scanning for translatable strings...")
    found_strings = scan_files(SOURCE_DIR)
    generate_ts(found_strings, OUTPUT_FILE)
