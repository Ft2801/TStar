import sys
import os
import xml.etree.ElementTree as ET

# Import unified translation data
try:
    from trans_data import TRANSLATIONS
except ImportError:
    print("Error: Could not import translation data. Make sure trans_data.py is in the same directory.")
    TRANSLATIONS = {}

def translate_file(input_file, lang_code, output_file):
    if not os.path.exists(input_file):
        print(f"Error: {input_file} not found.")
        return

    try:
        tree = ET.parse(input_file)
        root = tree.getroot()
        
        # Update language attribute
        root.set("language", lang_code)
        
        count = 0
        translated = 0
        
        for context in root.findall("context"):
            for message in context.findall("message"):
                source_elem = message.find("source")
                if source_elem is None or source_elem.text is None:
                    continue
                
                source = source_elem.text
                # Normalize source for matching: replace physical newlines/tabs with spaces, 
                # and also literal \n representations, then strip
                norm_source = source.replace('\\n', ' ')
                norm_source = ' '.join(norm_source.split())
                
                translation = message.find("translation")
                
                # Try exact match first
                match_val = None
                if source in TRANSLATIONS:
                    match_val = TRANSLATIONS[source]
                elif norm_source in TRANSLATIONS:
                    match_val = TRANSLATIONS[norm_source]
                
                if match_val and lang_code in match_val:
                    translation.text = match_val[lang_code]
                    translation.set("type", "finished")
                    translated += 1
                
                # Heuristics for patterns not in exact dict
                elif source.endswith(":") and source[:-1] in TRANSLATIONS and lang_code in TRANSLATIONS[source[:-1]]:
                     base = TRANSLATIONS[source[:-1]][lang_code]
                     translation.text = base + ":"
                     translation.set("type", "finished")
                     translated += 1
                
                # Specific "Applied to" heuristic
                # e.g. "Arcsinh Stretch Applied to %1" -> "Stiramento Arcsinh applicato a %1"
                elif " Applied to " in source:
                    parts = source.split(" Applied to ")
                    if len(parts) == 2 and parts[0] in TRANSLATIONS and lang_code in TRANSLATIONS[parts[0]]:
                        base = TRANSLATIONS[parts[0]][lang_code]
                        suffix = parts[1]
                        if lang_code == "it":
                            translation.text = f"{base} applicato a {suffix}"
                        elif lang_code == "es":
                            translation.text = f"{base} aplicado a {suffix}"
                        elif lang_code == "fr":
                            translation.text = f"{base} appliqué à {suffix}"
                        elif lang_code == "de":
                            translation.text = f"{base} angewendet auf {suffix}"
                        
                        if translation.text:
                            translation.set("type", "finished")
                            translated += 1

                count += 1
        
        tree.write(output_file, encoding="utf-8", xml_declaration=True)
        print(f"Generated {output_file}: {translated}/{count} strings translated.")
        
    except Exception as e:
        print(f"Failed to translate: {e}")

if __name__ == "__main__":
    input_ts = "tstar_template.ts"
    if len(sys.argv) > 1:
        input_ts = sys.argv[1]
    
    LANGUAGES = ["it", "es", "fr", "de"]
    
    for lang in LANGUAGES:
        output_ts = input_ts.replace("_template.ts", f"_{lang}.ts")
        if output_ts == input_ts:
             output_ts = input_ts.replace(".ts", f"_{lang}.ts")
             
        translate_file(input_ts, lang, output_ts)
