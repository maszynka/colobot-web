# Assets analysis (haiku)
- ~110MB assets (excl .git). Installed: ai, fonts 2.5M, icons 2.4M, models 8.8M (324 binary .mod), sounds 7M (83 wav), textures 25M (149 png; textures/interface 11M incl intro*.png ~7M), mods 21M, help 5.3M, levels 9.7M (txt + .po translations generated at build via i18n-tools/scripts/process_translations.py), music downloaded at configure from colobot.info (ogg).
- Not shipped: textures-src, models-new, tools, i18n-tools.
- Recs: preload menu set (~15-18M, verify), per-chapter lazy bundles, PNG->WebP/KTX2, WAV->Opus, brotli for txt/mod, drop xcf/max/blend.
