"""Original adventurer material regions and bounded palette parameters.

--appearance skin=1,hair=2,coat=3 is for generation/preview; ship only the default
atlas, since the renderer applies the synchronized descriptor to that atlas.
Geometry remains shared: no proportions or clothing seams change with a palette.
"""
NAMES = ('skin', 'hair', 'eyes', 'coat', 'scarf', 'trousers', 'boots', 'gear')
GROUPS = (
    ('skin', 'skin_dark', 'nail', 'lips', 'mouth'),
    ('hair',), ('iris',), ('coat', 'coat_dark'), ('scarf', 'scarf_dark'),
    ('trousers',), ('boots', 'boots_cuff'), ('leather', 'strap', 'bedroll'),
)
TINTS = (
    ((.70,.55,.45),(.40,.26,.20),(1.16,1.27,1.35)),
    ((.35,.35,.35),(2.20,1.55,.85),(2.10,2.10,2.10)),
    ((.55,1.60,2.60),(.70,1.80,.85),(1.70,1.10,.55)),
    ((1.70,.55,.45),(.50,.75,1.65),(1.20,1.10,.65)),
    ((.30,.65,1.70),(.75,.20,.25),(.50,1.20,.65)),
    ((.45,.50,.55),(.55,.65,1.10),(.70,.90,.50)),
    ((.45,.50,.55),(1.50,1.20,.80),(.70,.95,.70)),
    ((.45,.50,.55),(1.40,1.30,1.10),(.55,.95,.65)),
)
def material_group(name):
    return next((i + 1 for i, names in enumerate(GROUPS) if name in names), 0)

def parse_appearance(text):
    choices = [0] * len(NAMES)
    if text:
        seen = set()
        for item in text.split(','):
            name, value = item.split('=')
            if name not in NAMES or value not in ('0', '1', '2', '3') or name in seen:
                raise ValueError('invalid appearance choice: ' + item)
            seen.add(name)
            choices[NAMES.index(name)] = int(value)
    return choices

def tint_colour(rgb, material, choices):
    group = material_group(material)
    if not group or not choices[group - 1]:
        return list(rgb)
    return [v * t for v, t in zip(rgb, TINTS[group - 1][choices[group - 1] - 1])]
