# Draft release-note lines: the game's own font in the co-op text, a Witness-looking launcher (not tagged, not published)

- **The co-op text uses the game's own face.** Names, notices, chat, the player panel, the wheels and the quick guide are drawn in Karmina Bold,
  the typeface of the game's menus. It is read when the game starts from your own copy of the game (`data-pc.zip`), kept in memory only:
  the mod ships no font and writes none to disk. If it cannot be read, the text uses Segoe UI as before (the log says why).
  `ui_font=segoe` in `witness_coop.cfg` keeps Segoe UI; `ui_font=notoserif` tries the game's Noto Serif.
- **The launcher's headings, tabs and buttons** use the same face (body text stays Segoe UI), the banner's puzzle panel now shows its grid
  under the traced line, and the Saves page shows the selected save's own picture (the one the game takes when it saves) beside its details.
  Without the game files the launcher looks as before.
