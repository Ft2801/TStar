
import xml.etree.ElementTree as ET
import os

ts_file = "translations/tstar_it.ts"
if not os.path.exists(ts_file):
    print(f"File {ts_file} not found")
    exit(1)

tree = ET.parse(ts_file)
root = tree.getroot()

missing = []
for context in root.findall("context"):
    for message in context.findall("message"):
        source = message.find("source").text
        translation = message.find("translation")
        if translation is None or translation.get("type") != "finished":
            missing.append(source)

print(f"Found {len(missing)} missing translations:")
for s in missing:
    print(f"- {s}")
