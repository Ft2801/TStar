import xml.etree.ElementTree as ET
import sys
import os

try:
    from trans_data import TRANSLATIONS
except ImportError:
    print("Error: Could not import trans_data")
    sys.exit(1)

def check_missing(ts_file):
    tree = ET.parse(ts_file)
    root = tree.getroot()
    
    missing = []
    
    for context in root.findall("context"):
        ctx_name = context.find("name").text
        for message in context.findall("message"):
            source = message.find("source").text
            if not source: continue
            
            # Normalize
            norm = source.replace('\\n', ' ').strip()
            norm = ' '.join(norm.split())

            if source not in TRANSLATIONS and norm not in TRANSLATIONS:
               # Check heuristics too
               if " Applied to " in source: continue
               if source.endswith(":") and source[:-1] in TRANSLATIONS: continue
               
               missing.append(f"[{ctx_name}] {source}")

    for m in missing:
        print(m)

if __name__ == "__main__":
    path = "../translations/tstar_template.ts"
    if len(sys.argv) > 1:
        path = sys.argv[1]
    check_missing(path)
