import csv

input_file = "EDON.txt"
output_file = "EDON.csv"

entries = []
current_lemma = None
current_definition_lines = []


def replace_ligatures(text):
  """Remplace les ligatures spécifiques par leurs équivalents."""
  replacements = {
      "Æ": "AE",
      "æ": "ae",
      "Œ": "OE",
      "œ": "oe",
      # Vous pouvez ajouter d'autres ligatures ici si nécessaire (ex: "ﬀ": "ff", "ﬁ": "fi")
  }
  for old, new in replacements.items():
    text = text.replace(old, new)
  return text


with open(input_file, "r", encoding="utf-8") as f:
  for line in f:
    line = line.rstrip("\n")
    line = replace_ligatures(line)  # Application du remplacement sur chaque ligne

    # Ligne vide → fin d'une entrée
    if line.strip() == "":
      if current_lemma is not None:
        definition = " ".join(current_definition_lines).strip()
        entries.append((current_lemma, definition))
      current_lemma = None
      current_definition_lines = []
      continue

    # Si aucun lemma n'est encore défini → première ligne de l'entrée
    if current_lemma is None:
      # Le premier mot = lemma
      # Le reste après la virgule = nature
      if "," in line:
        lemma, nature = line.split(",", 1)
        current_lemma = lemma.strip()
        current_definition_lines.append(nature.strip())
      else:
        # Cas rare : pas de virgule
        current_lemma = line.strip()
    else:
      # Lignes de définition
      current_definition_lines.append(line.strip())

# Dernière entrée si le fichier ne finit pas par une ligne vide
if current_lemma is not None:
  definition = " ".join(current_definition_lines).strip()
  entries.append((current_lemma, definition))

# Écriture du CSV
with open(output_file, "w", encoding="utf-8", newline="") as csvfile:
  writer = csv.writer(csvfile)
  writer.writerow(["mot", "definition"])
  for lemma, definition in entries:
    writer.writerow([lemma, definition])

print("Conversion terminée :", output_file)
