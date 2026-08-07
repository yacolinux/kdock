#!/usr/bin/env python3
"""Regenera las tres capas ALT (ver AGENTS.md -> Capa de traducciones).

Build the three ALT English layers: english.md with the widget names replaced
by a themed set, and an Apps section of sci-fi AI / fictional hacking-tool names."""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / 'translations'
SEP = ' = '

# ---- 1) Star Trek ships -----------------------------------------------------
STARTREK = {
 'menu': 'Enterprise',      'tilemenu': 'Discovery',   'apps': 'Voyager',
 'clipboard': 'Defiant',    'disks': 'Reliant',        'network': 'Excelsior',
 'iconthemes': 'Stargazer', 'colorschemes': 'Cerritos','volume': 'Intrepid',
 'brightness': 'Prometheus','battery': 'Titan',        'clock': 'Bozeman',
 'clock2': 'Phoenix',       'overview': 'Yorktown',    'movetodesktop': 'Pegasus',
 'movetoscreen': 'Shenzhou','maxmin': 'Sutherland',    'closewindow': 'Grissom',
 'nextwallpaper': 'Farragut','darkmode': 'Equinox',    'pager': 'Constellation',
 'autohide': 'Saratoga',    'showdesktop': 'Hood',     'systray': 'Yamato',
 'relanzadores': 'Odyssey', 'scriptrunners': 'Franklin','session': 'Kelvin',
 'settings': 'Lexington',   'spring': 'Protostar',     'sep': 'Aventine',
}

# ---- 2) Hacker handles ------------------------------------------------------
HACKER = {
 'menu': 'Zero Cool',       'tilemenu': 'Acid Burn',   'apps': 'Crash Override',
 'clipboard': 'Cereal Killer','disks': 'Phantom Phreak','network': 'Phiber Optik',
 'iconthemes': 'Lord Nikon','colorschemes': 'Razor',   'volume': 'Blade',
 'brightness': 'Neon',      'battery': 'Voltage',      'clock': 'Whistler',
 'clock2': 'Chronos',       'overview': 'The Plague',  'movetodesktop': 'Mendax',
 'movetoscreen': 'Dark Dante','maxmin': 'Bishop',      'closewindow': 'Reaper',
 'nextwallpaper': 'Kaleido','darkmode': 'Nightfall',   'pager': 'Cosmo',
 'autohide': 'Ghost',       'showdesktop': 'Sneakers', 'systray': 'Condor',
 'relanzadores': 'Data Thief','scriptrunners': 'Script Kitty','session': 'Knight Lightning',
 'settings': 'Root',        'spring': 'Slack Space',   'sep': 'Null Byte',
}

# ---- 3) Star Wars hardware --------------------------------------------------
STARWARS = {
 'menu': 'Holocron',        'tilemenu': 'Datapad',     'apps': 'Hangar Bay',
 'clipboard': 'Data Cache', 'disks': 'Astromech',      'network': 'Comlink',
 'iconthemes': 'Holoprojector','colorschemes': 'Kyber Crystal','volume': 'Sonic Emitter',
 'brightness': 'Glowrod',   'battery': 'Power Cell',   'clock': 'Chrono',
 'clock2': 'Ship Chrono',   'overview': 'Sensor Dish', 'movetodesktop': 'Hyperdrive',
 'movetoscreen': 'Tractor Beam','maxmin': 'Repulsorlift','closewindow': 'Ion Cannon',
 'nextwallpaper': 'Vaporator','darkmode': 'Carbonite',  'pager': 'Nav Computer',
 'autohide': 'Cloaking Device','showdesktop': 'Blast Door','systray': 'Droid Socket',
 'relanzadores': 'Gonk Droid','scriptrunners': 'Power Converter','session': 'Reactor Core',
 'settings': 'Diagnostic Port','spring': 'Deflector Shield','sep': 'Bulkhead',
}

# ---- 4) AI characters and systems from sci-fi novels, and
#      5) fictional hacking tools. Shared by the three files.
APPS = {
 # --- the launchers pinned on this machine ---------------------------------
 'microsoft-edge': 'Kuang Mark Eleven',            # 5, Neuromancer's icebreaker
 'org.kde.dolphin': 'Wintermute',                  # 4, Neuromancer
 'org.kde.konsole': 'Mycroft Holmes',              # 4, The Moon Is a Harsh Mistress
 'org.kde.krusader': 'Giskard',                    # 4, Asimov
 'sublime_text': 'Golem XIV',                      # 4, Lem
 'systemsettings': 'Multivac',                     # 4, Asimov
 'msedge-_cadlkienfkclaiaibeoongdcgmdikeeg-Default': 'Deep Thought',       # 4, Adams
 'msedge-_fmpnliohjhemenmnlpbfagaolkdacoja-Default': 'Jane',               # 4, Card
 'msedge-_ladkjkoheaidofpcfenmhedddgpdgoof-Default': 'Chatterbox Relay',   # 5
 'msedge-_hnpfjngllnobngcgfapefoaidbinmjnm-Default': 'Whisper Node',       # 5
 'msedge-_jgeocpdicgmkeemopbanhokmhcgcflmi-Default': 'Signal Sniffer',     # 5
 'msedge-agimnkijcaahngcdmfeangaknmldooml-Default': 'Feedback Loop',       # 5
 'brave-agimnkijcaahngcdmfeangaknmldooml-Default': 'Feedback Loop',
 # --- AI things get AI names (4) -------------------------------------------
 'com.anthropic.Claude': 'Jane',
 'brave-fmpnliohjhemenmnlpbfagaolkdacoja-Default': 'Jane',
 'brave-cadlkienfkclaiaibeoongdcgmdikeeg-Default': 'Deep Thought',
 'msedge-_gbpghmjlagojpokaelpobjahbmcjdcen-Default': 'Solace',             # 4, Hyperion-ish
 'msedge-gdnaalheikpndoceejecmeibmcgbgndk-Default': 'Daneel',              # 4, Asimov
 'brave-iabjalbfnepehopepnokgggcfhifhjmj-Default': 'Ummon',                # 4, Hyperion
 'brave-cfbmbdcmhmpondjkflgbghnelgpldahk-Default': 'Aineko',               # 4, Stross
 'cursor': 'HAL 9000',                             # 4, Clarke
 'aider-desk': 'Marvin',                           # 4, Adams
 'antigravity': 'Colossus',                        # 4, D. F. Jones
 # --- terminals, shells and system tools: hacking tools (5) ----------------
 'Alacritty': 'Black ICE',
 'ghostty_ghostty': 'PhantomShell',
 'org.gnome.Terminal': 'Nullpointer',
 'org.gnome.Ptyxis': 'Cipherbreak',
 'htop': 'Process Wraith',
 'org.gnome.SystemMonitor': 'Daemon Watch',
 'org.kde.plasma-systemmonitor': 'Daemon Watch',
 'org.wireshark.Wireshark': 'Packet Ripper',
 'nm-connection-editor': 'Route Spoofer',
 'org.gnome.DiskUtility': 'Sector Zero',
 'org.gnome.baobab': 'Deep Scan',
 'synaptic': 'Payload Depot',
 'update-manager': 'Patch Runner',
 'org.kde.discover': 'Exploit Market',
 'snap-store_snap-store': 'Exploit Market',
 'virtualbox': 'Sandbox Rig',
 'org.remmina.Remmina': 'Ghost Session',
 'org.deskflow.deskflow': 'Puppet Strings',
 'barrier': 'Puppet Strings',
 'io.github.thetumultuousunicornofdarkness.cpu-x': 'Silicon Probe',
 'ca.desrt.dconf-editor': 'Registry Pick',
 # --- browsers and comms: mixed 4 and 5 ------------------------------------
 'firefox_firefox': 'Neuromancer',                 # 4, Gibson
 'google-chrome': 'The Gibson',                    # 5, Hackers
 'brave-browser': 'Icebreaker',                    # 5
 'chromium_chromium': 'Screamer',                  # 5
 'chromium_daemon': 'Screamer',
 'telegram-desktop_telegram-desktop': 'Dead Drop', # 5
 'thunderbird_thunderbird': 'Mail Relay Exploit',  # 5
 'brave-fmgjjmmmlfnkbppncabfkddbjimcfncm-Default': 'Mail Relay Exploit',
 'brave-mfhpbolkhgobaabcbabdlnhidbjpoogc-Default': 'Chatterbox Relay',
 'brave-cocinacogklpjoldpckjijokfbpfbccm-Default': 'Zero Day Feed',
 'msedge-_fcbodnclggimceffplfhaeanglicgbmc-Default': 'Zero Day Feed',
 # --- files, editors, media: AI names for the "thinking" ones (4) ----------
 'org.gnome.Nautilus': 'Erasmus',                  # 4, Dune prequels
 'doublecmd': 'Omnius',                            # 4, Dune prequels
 'org.kde.kate': 'Sleeper Service',                # 4, Banks
 'code': 'Grey Area',                              # 4, Banks
 'vim': 'AM',                                      # 4, Ellison
 'org.kde.ark': 'Compression Vault',               # 5
 'org.kde.okular': 'Document Sifter',              # 5
 'org.kde.gwenview': 'Image Forensics',            # 5
 'org.kde.spectacle': 'Screen Grabber',            # 5
 'org.kde.kolourpaint': 'Pixel Forge',             # 5
 'drawio': 'Blueprint Leak',                       # 5
 'brave-ilmgmogedobmcfegdjcibiiaodmdenpf-Default': 'Blueprint Leak',
 'calibre-gui': 'The Library of Babel',            # 4-ish, Borges
 'vlc': 'Hex',                                     # 4, Discworld
 'audacity': 'Voice Print',                        # 5
 'com.github.wwmm.easyeffects': 'Signal Shaper',   # 5
 'spotify': 'Siren Feed',                          # 5
 'org.gnome.Calculator': 'Prime Intelligence',     # 4, Hughes
 'org.kde.kcalc': 'Prime Intelligence',
 'libreoffice-startcenter': 'Bureaucratic Engine', # 5
 'libreoffice-writer': 'Ghostwriter',              # 5
 'libreoffice-calc': 'Ledger Cracker',             # 5
 'libreoffice-impress': 'Persuasion Deck',         # 5
 'org.kde.kdeconnect.app': 'Tethered Mind',        # 5
 'org.kde.plasma.emojier': 'Glyph Injector',       # 5
 'cairo-dock': 'Dock Impostor',                    # 5
 'crystal-dock': 'Dock Impostor',
}

HEADER_NOTE = {
 'startrek': 'Nombres de widgets: naves de Star Trek.',
 'hacker': 'Nombres de widgets: apodos de hacker.',
 'starwars': 'Nombres de widgets: hardware de Star Wars.',
}


def build(base, name, widgets, note):
    # base es la traducción de la que se copia todo salvo Widgets y Apps: los
    # apodos son nombres propios, así que son los mismos en los seis archivos.
    lines = (ROOT / (base + '.md')).read_text(encoding='utf-8').split('\n')
    lines[0] = '# ' + name
    out, section = [], None
    for line in lines:
        t = line.strip()
        if t.startswith('<!-- Traducción'):
            out.append('<!-- kdock ALT layer: ' + base + '.md con los nombres de widgets y de apps')
            out.append('     cambiados por apodos. ' + note)
            out.append('     Apps: personajes y sistemas de IA de novelas de ciencia ficción,')
            out.append('     y herramientas de hackeo ficticias. Lo que no esté acá cae a')
            out.append('     capabase (o al Name= del .desktop, para las apps).')
            continue
        if t.startswith('de la capa nativa (capabase.md)'):
            continue  # segunda línea del encabezado de la base, ya reemplazada
        if t.startswith('##'):
            section = t[2:].strip().lower()
            out.append(line)
            continue
        if section == 'widgets' and SEP in line:
            key = line.split(SEP, 1)[0]
            out.append(key + SEP + widgets.get(key, line.split(SEP, 1)[1]))
            continue
        if section == 'apps':
            continue  # rebuilt below
        out.append(line)
    while out and not out[-1].strip():
        out.pop()
    for key in sorted(APPS, key=str.lower):
        out.append(key + SEP + APPS[key])
    (ROOT / (name + '.md')).write_text('\n'.join(out) + '\n', encoding='utf-8')
    return len(widgets), len(APPS)


if __name__ == '__main__':
    # Sanity: every app id has to exist on this machine, or the entry is dead.
    # APPS_CHECK: un volcado "<id>\t<Name>" de las apps instaladas (lo imprime una
    # sonda de tres líneas sobre DesktopEntryIndex). Sin él no se valida nada.
    check = os.environ.get('APPS_CHECK')
    if check and Path(check).exists():
        installed = {l.split('\t')[0] for l in Path(check).read_text(encoding='utf-8').split('\n') if l}
        for k in APPS:
            if k not in installed:
                print('  ! id sin .desktop instalado:', k)
    for base in ('english', 'spanish'):
        for theme, widgets in (('startrek', STARTREK),
                               ('hacker', HACKER),
                               ('starwars', STARWARS)):
            name = f'{base}-ALT-{theme}'
            w, a = build(base, name, widgets, HEADER_NOTE[theme])
            print(f'{name}.md: {w} widgets, {a} apps')
