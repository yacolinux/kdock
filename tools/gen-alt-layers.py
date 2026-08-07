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

# ---- Los mismos apodos, en chino ------------------------------------------
# La asignación (qué apodo le toca a cada widget y a cada app) es una sola: esta
# tabla solo traduce el apodo, igual que cualquier otra capa traduce un texto.
# Los nombres con traducción establecida en chino la usan (Wintermute 冬寂,
# Enterprise 企业号); el resto se traduce por sentido, no por transliteración.
ZH_NAMES = {
 # 1) naves de Star Trek
 'Enterprise': '企业号',        'Discovery': '发现号',      'Voyager': '航海家号',
 'Defiant': '挑衅号',           'Reliant': '可靠号',        'Excelsior': '卓越号',
 'Stargazer': '观星者号',       'Cerritos': '塞里托斯号',    'Intrepid': '无惧号',
 'Prometheus': '普罗米修斯号',   'Titan': '泰坦号',          'Bozeman': '博兹曼号',
 'Phoenix': '凤凰号',           'Yorktown': '约克城号',      'Pegasus': '飞马号',
 'Shenzhou': '神舟号',          'Sutherland': '萨瑟兰号',    'Grissom': '格里索姆号',
 'Farragut': '法拉格特号',       'Equinox': '春分号',        'Constellation': '星座号',
 'Saratoga': '萨拉托加号',       'Hood': '胡德号',           'Yamato': '大和号',
 'Odyssey': '奥德赛号',         'Franklin': '富兰克林号',    'Kelvin': '开尔文号',
 'Lexington': '列克星敦号',      'Protostar': '原恒星号',     'Aventine': '阿文丁号',
 # 2) apodos de hacker
 'Zero Cool': '零度酷客',       'Acid Burn': '酸蚀',        'Crash Override': '崩溃覆写',
 'Cereal Killer': '麦片杀手',    'Phantom Phreak': '幽灵飞客','Phiber Optik': '光纤客',
 'Lord Nikon': '尼康大人',       'Razor': '剃刀',            'Blade': '利刃',
 'Neon': '霓虹',                'Voltage': '电压',          'Whistler': '吹哨人',
 'Chronos': '时序者',           'The Plague': '瘟疫',       'Mendax': '门达克斯',
 'Dark Dante': '黑暗但丁',       'Bishop': '主教',           'Reaper': '收割者',
 'Kaleido': '万花筒',           'Nightfall': '夜幕',        'Cosmo': '科斯莫',
 'Ghost': '幽灵',               'Sneakers': '潜行者',        'Condor': '秃鹰',
 'Data Thief': '数据窃贼',       'Script Kitty': '脚本小猫',  'Knight Lightning': '闪电骑士',
 'Root': '根权限',              'Slack Space': '残留空间',   'Null Byte': '空字节',
 # 3) hardware de Star Wars
 'Holocron': '全息档案',        'Datapad': '数据板',         'Hangar Bay': '机库',
 'Data Cache': '数据缓存',       'Astromech': '宇航技工机',   'Comlink': '通讯器',
 'Holoprojector': '全息投影仪',  'Kyber Crystal': '凯波水晶',  'Sonic Emitter': '声波发射器',
 'Glowrod': '光棒',             'Power Cell': '能量电池',    'Chrono': '计时器',
 'Ship Chrono': '舰载计时器',    'Sensor Dish': '传感天线',   'Hyperdrive': '超空间引擎',
 'Tractor Beam': '牵引光束',     'Repulsorlift': '斥力升降器','Ion Cannon': '离子炮',
 'Vaporator': '集水器',         'Carbonite': '碳素冷冻',     'Nav Computer': '导航电脑',
 'Cloaking Device': '隐形装置',  'Blast Door': '防爆门',      'Droid Socket': '机器人接口',
 'Gonk Droid': '冈克机器人',     'Power Converter': '能量转换器','Reactor Core': '反应堆核心',
 'Diagnostic Port': '诊断端口',  'Deflector Shield': '偏导护盾','Bulkhead': '隔舱壁',
 # 4) IA de novelas de ciencia ficción
 'Wintermute': '冬寂',          'Neuromancer': '神经漫游者',  'Mycroft Holmes': '迈克罗夫特',
 'Giskard': '吉斯卡特',         'Daneel': '丹尼尔',          'Multivac': '穆提瓦克',
 'Golem XIV': '高莱姆十四',      'Deep Thought': '深思',      'Marvin': '马文',
 'Jane': '简',                  'HAL 9000': '哈尔9000',      'AM': '联合主机 AM',  # Allied Mastercomputer
 'Colossus': '巨神',            'Erasmus': '伊拉斯谟',        'Omnius': '奥姆尼乌斯',
 'Hex': '赫克斯',               'Aineko': '艾内可',          'Ummon': '云门',
 'Solace': '慰藉',              'Sleeper Service': '沉睡者号', 'Grey Area': '灰色地带',
 'Prime Intelligence': '至高智能','The Library of Babel': '巴别图书馆',
 # 5) herramientas de hackeo ficticias
 'Kuang Mark Eleven': '匡氏十一型','Black ICE': '黑冰',        'Icebreaker': '破冰器',
 'The Gibson': '吉布森主机',     'Screamer': '尖啸者',        'Packet Ripper': '数据包撕裂者',
 'Process Wraith': '进程幽灵',   'Daemon Watch': '守护进程监视','Route Spoofer': '路由伪装器',
 'Sector Zero': '零号扇区',      'Deep Scan': '深度扫描',     'Payload Depot': '载荷仓库',
 'Patch Runner': '补丁信使',     'Exploit Market': '漏洞市集', 'Sandbox Rig': '沙箱机架',
 'Ghost Session': '幽灵会话',    'Puppet Strings': '提线木偶', 'Silicon Probe': '硅探针',
 'Registry Pick': '注册表撬锁',  'Dead Drop': '情报暗桩',     'Mail Relay Exploit': '邮件中继漏洞',
 'Chatterbox Relay': '闲话中继', 'Whisper Node': '低语节点',   'Signal Sniffer': '信号嗅探器',
 'Feedback Loop': '反馈回路',    'Zero Day Feed': '零日情报流','Compression Vault': '压缩保险库',
 'Document Sifter': '文档筛析器','Image Forensics': '图像取证','Screen Grabber': '屏幕抓取器',
 'Pixel Forge': '像素锻造',      'Blueprint Leak': '蓝图泄露', 'Voice Print': '声纹',
 'Signal Shaper': '信号整形器',  'Siren Feed': '塞壬信息流',   'Bureaucratic Engine': '官僚引擎',
 'Ghostwriter': '影子写手',      'Ledger Cracker': '账本破解器','Persuasion Deck': '说服幻灯',
 'Tethered Mind': '系连之心',    'Glyph Injector': '字形注入器','Dock Impostor': '冒牌 Dock',
 'Nullpointer': '空指针',        'Cipherbreak': '密文破译',    'PhantomShell': '幽灵终端',
}

HEADER_NOTE = {
 'startrek': 'Nombres de widgets: naves de Star Trek.',
 'hacker': 'Nombres de widgets: apodos de hacker.',
 'starwars': 'Nombres de widgets: hardware de Star Wars.',
}


def build(base, name, widgets, note, names=None):
    # names: traduce el apodo (mismo reparto, otro idioma).
    tr = (lambda v: names.get(v, v)) if names else (lambda v: v)
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
            out.append(key + SEP + tr(widgets.get(key, line.split(SEP, 1)[1])))
            continue
        if section == 'apps':
            continue  # rebuilt below
        out.append(line)
    while out and not out[-1].strip():
        out.pop()
    for key in sorted(APPS, key=str.lower):
        out.append(key + SEP + tr(APPS[key]))
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
    for base in ('english', 'spanish', 'zh-CN'):
        names = ZH_NAMES if base == 'zh-CN' else None
        for theme, widgets in (('startrek', STARTREK),
                               ('hacker', HACKER),
                               ('starwars', STARWARS)):
            name = f'{base}-ALT-{theme}'
            w, a = build(base, name, widgets, HEADER_NOTE[theme], names)
            print(f'{name}.md: {w} widgets, {a} apps')
