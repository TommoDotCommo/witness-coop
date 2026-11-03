# Draft release-note lines: the game's own font in the co-op text, a Witness-looking launcher (not tagged, not published)

- **The co-op text uses the game's own face.** Names, notices, chat, the player panel, the wheels and the quick guide are drawn in Karmina Bold,
  the typeface of the game's menus. It is read when the game starts from your own copy of the game (`data-pc.zip`), kept in memory only:
  the mod ships no font and writes none to disk. If it cannot be read, the text uses Segoe UI as before (the log says why).
  `ui_font=segoe` in `witness_coop.cfg` keeps Segoe UI; `ui_font=notoserif` tries the game's Noto Serif.
- **The launcher's headings, tabs and buttons** use the same face (body text stays Segoe UI), the banner's puzzle panel now shows its grid
  under the traced line, and the Saves page shows the selected save's own picture (the one the game takes when it saves) beside its details.
  Without the game files the launcher looks as before.
- **The launcher looks like the game.** The banner shows the picture of your newest save (the one the game takes when it saves), cropped to fill and
  softly blurred, with THE WITNESS and CO-OP in the game's face over a shade; without a save picture (first start) it shows a Witness puzzle panel drawn by
  the launcher (a yellow panel, its grid, the start circle, the end cap and a traced white line). Pictures are read from your save folder when the
  launcher starts; nothing from the game or your saves is shipped or written anywhere.
- **Play's three choices are tiles** (name in the game's face, the description under it, a start circle that lights in the game's yellow when chosen);
  the primary button, the chosen tab and the chosen settings section use that yellow too. The Saves list shows each save's thumbnail.
  Every page keeps its wording, order, keys and pad navigation; long settings sections (Controls) set their rows a little closer to fit 1280 x 800.
- **Fixed: a talking player's name tag** drew the voice bars over the ping ("Adventurer 2  0 ms"); the bars now sit beside the ping, inside the tag.
