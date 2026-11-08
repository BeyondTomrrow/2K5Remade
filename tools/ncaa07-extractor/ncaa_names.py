"""Readable names for NCAA 07 models (stadium codes, player model files)."""
from __future__ import annotations

STADIUMS = {
    'ACCCHA': 'ACC Championship', 'AIRFOR': 'Air Force', 'AKRONX': 'Akron', 'ALAMOX': 'Alamo Bowl',
    'ALBAAM': 'Alabama A&M', 'ALBAMA': 'Alabama', 'ALCORN': 'Alcorn State', 'ALSTAT': 'Alabama State',
    'APPAST': 'Appalachian State', 'ARIZON': 'Arizona', 'ARKANS': 'Arkansas', 'ARKPBX': 'Arkansas-Pine Bluff',
    'ARKSTX': 'Arkansas State', 'ARMYXX': 'Army', 'AUBURN': 'Auburn', 'AZSTAT': 'Arizona State',
    'B12CHA': 'Big 12 Championship', 'BALLST': 'Ball State', 'BAYLOR': 'Baylor', 'BETHUN': 'Bethune-Cookman',
    'BOISES': 'Boise State', 'BOSTON': 'Boston College', 'BOWLGR': 'Bowling Green', 'BROWNX': 'Brown',
    'BUFFAL': 'Buffalo', 'BYUXXX': 'BYU', 'CALXXX': 'California', 'CHATTA': 'Chattanooga',
    'CINCIN': 'Cincinnati', 'CITADE': 'The Citadel', 'CITRUS': 'Citrus Bowl', 'CLEMSO': 'Clemson',
    'CMICHI': 'Central Michigan', 'COLORA': 'Colorado', 'COLOST': 'Colorado State', 'COLUMB': 'Columbia',
    'CORNEL': 'Cornell', 'COTTON': 'Cotton Bowl', 'CSBCMP': 'Created stadium: campus',
    'CSBDES': 'Created stadium: desert', 'CSBMTN': 'Created stadium: mountain',
    'CSBMTR': 'Created stadium: metro', 'CSBOWL': 'Created stadium: bowl', 'CSBRUR': 'Created stadium: rural',
    'CSBWUD': 'Created stadium: woods', 'CSDLSD': 'Created stadium: lakeside', 'CSDOME': 'Created stadium: dome',
    'CSHORS': 'Created stadium: horseshoe', 'CSSQRE': 'Created stadium: square', 'DARTMO': 'Dartmouth',
    'DELAWA': 'Delaware', 'DELWST': 'Delaware State', 'DUKEXX': 'Duke', 'EASPOR': 'EA Sports Stadium',
    'ECUXXX': 'East Carolina', 'EILLIN': 'Eastern Illinois', 'EKENTU': 'Eastern Kentucky', 'ELONXX': 'Elon',
    'EMICHI': 'Eastern Michigan', 'EWASHI': 'Eastern Washington', 'FAUXXX': 'Florida Atlantic',
    'FIESTA': 'Fiesta Bowl', 'FIUXXX': 'FIU', 'FLORAM': 'Florida A&M', 'FLORID': 'Florida',
    'FLORST': 'Florida State', 'FRESNO': 'Fresno State', 'FURMAN': 'Furman', 'GALLER': 'Gallery',
    'GATECH': 'Georgia Tech', 'GATORX': 'Gator Bowl', 'GEORGA': 'Georgia', 'GEOSOU': 'Georgia Southern',
    'GRAMBL': 'Grambling State', 'HAMPTO': 'Hampton', 'HARVAR': 'Harvard', 'HAWAII': 'Hawaii',
    'HAWIBL': 'Hawaii Bowl', 'HOFSTR': 'Hofstra', 'HOLIDA': 'Holiday Bowl', 'HOUSTO': 'Houston',
    'HOWARD': 'Howard', 'HUMANI': 'Humanitarian Bowl', 'IDAHOS': 'Idaho State', 'ILLINO': 'Illinois',
    'ILSTAT': 'Illinois State', 'INDEPE': 'Independence Bowl', 'INDIAN': 'Indiana', 'INSIGH': 'Insight Bowl',
    'INSTAT': 'Indiana State', 'IOWAST': 'Iowa State', 'IOWAXX': 'Iowa', 'JACKST': 'Jackson State',
    'JAXVST': 'Jacksonville State', 'JMADIS': 'James Madison', 'KANSAS': 'Kansas', 'KANSST': 'Kansas State',
    'KENTUC': 'Kentucky', 'KENTXX': 'Kent State', 'KIBBIE': 'Idaho (Kibbie Dome)', 'LASVEG': 'Las Vegas Bowl',
    'LATECH': 'Louisiana Tech', 'LIBERT': 'Liberty Bowl', 'LOUISV': 'Louisville', 'LSUXXX': 'LSU',
    'MACCHA': 'MAC Championship', 'MAINEX': 'Maine', 'MARSHA': 'Marshall', 'MARYLA': 'Maryland',
    'MASSAC': 'Massachusetts', 'MCNEST': 'McNeese State', 'MEMPHI': 'Memphis', 'MIAMFL': 'Miami (FL)',
    'MIAMOH': 'Miami (OH)', 'MICHIG': 'Michigan', 'MICHST': 'Michigan State', 'MICRON': 'MPC Computers Bowl',
    'MIDTEN': 'Middle Tennessee', 'MINNES': 'Minnesota', 'MISSOU': 'Missouri', 'MISSST': 'Mississippi State',
    'MISVAL': 'Mississippi Valley State', 'MOBILE': 'GMAC Bowl (Mobile)', 'MONTAN': 'Montana',
    'MORGAN': 'Morgan State', 'MOSTAT': 'Montana State', 'MOTORC': 'Motor City Bowl', 'MURRST': 'Murray State',
    'MUSICC': 'Music City Bowl', 'NARIZO': 'Northern Arizona', 'NATCH1': 'National Championship 1',
    'NATCH2': 'National Championship 2', 'NATCH3': 'National Championship 3', 'NATCH4': 'National Championship 4',
    'NAVYXX': 'Navy', 'NCAATX': 'NCAA (generic)', 'NCAROL': 'North Carolina', 'NCSTAT': 'NC State',
    'NEBRAS': 'Nebraska', 'NEUBRO': 'Neutral site', 'NEUCOT': 'Neutral site (Cotton)',
    'NEUGAT': 'Neutral site (Gator)', 'NEUGIA': 'Neutral site (Giants)', 'NEVADA': 'Nevada',
    'NEWHAM': 'New Hampshire', 'NEWMEX': 'New Mexico', 'NEWORL': 'New Orleans Bowl', 'NICHST': 'Nicholls State',
    'NILLIN': 'Northern Illinois', 'NIOWAX': 'Northern Iowa', 'NMEXST': 'New Mexico State',
    'NORFOL': 'Norfolk State', 'NORTHE': 'Northeastern', 'NORTHW': 'Northwestern',
    'NORWST': 'Northwestern State', 'NOTDAM': 'Notre Dame', 'NTEXAS': 'North Texas', 'OHIOST': 'Ohio State',
    'OHIOXX': 'Ohio', 'OKLAHO': 'Oklahoma', 'OKLAST': 'Oklahoma State', 'OLEMIS': 'Ole Miss',
    'ORANGE': 'Orange Bowl', 'OREGON': 'Oregon', 'OREGST': 'Oregon State', 'OUTBAC': 'Outback Bowl',
    'PEACHX': 'Peach Bowl', 'PENNST': 'Penn State', 'PENNSY': 'Penn', 'PITTSB': 'Pittsburgh',
    'PORTST': 'Portland State', 'PRACTA': 'Practice field A', 'PRACTI': 'Practice field',
    'PRAIRI': 'Prairie View A&M', 'PRINCE': 'Princeton', 'PURDUE': 'Purdue', 'QUEENX': 'Queens',
    'RHODEI': 'Rhode Island', 'RICEXX': 'Rice', 'RICHMO': 'Richmond', 'ROSEXX': 'Rose Bowl',
    'RUTGER': 'Rutgers', 'SACRAM': 'Sacramento State', 'SAMFOR': 'Samford', 'SAMHST': 'Sam Houston State',
    'SANFRN': 'Emerald Bowl (San Francisco)', 'SANJOS': 'San Jose State', 'SCAROL': 'South Carolina',
    'SCARST': 'South Carolina State', 'SDSTAT': 'San Diego State', 'SEATLE': 'Seattle',
    'SECCHA': 'SEC Championship', 'SELAXX': 'Southeastern Louisiana', 'SEMOST': 'Southeast Missouri State',
    'SFAUST': 'Stephen F. Austin', 'SILICO': 'Silicon Valley Bowl', 'SILLIN': 'Southern Illinois',
    'SMISSI': 'Southern Miss', 'SMUXXX': 'SMU', 'SOUTHE': 'Southern', 'STANFO': 'Stanford',
    'SUGARX': 'Sugar Bowl', 'SUNXXX': 'Sun Bowl', 'SWMISS': 'Southwest Missouri State', 'SYRACU': 'Syracuse',
    'TCUXXX': 'TCU', 'TEMPLE': 'Temple', 'TENMAR': 'Tennessee-Martin', 'TENNES': 'Tennessee',
    'TENNST': 'Tennessee State', 'TENTEC': 'Tennessee Tech', 'TEXAAM': 'Texas A&M', 'TEXAST': 'Texas State',
    'TEXASX': 'Texas', 'TEXSOU': 'Texas Southern', 'TIBURO': 'Tiburon (EA)', 'TOLEDO': 'Toledo',
    'TOWSON': 'Towson', 'TROYST': 'Troy', 'TULANE': 'Tulane', 'TULSAX': 'Tulsa', 'TXTECH': 'Texas Tech',
    'UABXXX': 'UAB', 'UCFXXX': 'UCF', 'UCLAXX': 'UCLA', 'UCONNX': 'UConn', 'ULLAFY': 'Louisiana-Lafayette',
    'ULMONR': 'Louisiana-Monroe', 'UNLVXX': 'UNLV', 'USCXXX': 'USC', 'USFXXX': 'South Florida',
    'UTAHST': 'Utah State', 'UTAHXX': 'Utah', 'UTEPXX': 'UTEP', 'VANDER': 'Vanderbilt', 'VATECH': 'Virginia Tech',
    'VILLAN': 'Villanova', 'VIRGIN': 'Virginia', 'WAKEFO': 'Wake Forest', 'WASHIN': 'Washington',
    'WASHST': 'Washington State', 'WCAROL': 'Western Carolina', 'WEBEST': 'Weber State',
    'WILLIN': 'Western Illinois', 'WISCON': 'Wisconsin', 'WKENTU': 'Western Kentucky',
    'WMICHI': 'Western Michigan', 'WMMARY': 'William & Mary', 'WOFFOR': 'Wofford', 'WVIRGI': 'West Virginia',
    'WYOMIN': 'Wyoming', 'YALEXX': 'Yale', 'YOUNST': 'Youngstown State',
}
WEATHER = {'FAIR': 'day', 'NIGHT': 'night', 'SNOW': 'snow'}

PLAYERS = {
    'fm2400': 'Face (high detail)',
    'pm2400': 'Player, modern (high detail, 2400)', 'pm1200': 'Player, modern (1200)',
    'pm600': 'Player, modern (600)', 'pm300': 'Player, modern (low, 300)',
    'h2400': 'Player, old era (high detail)', 'h1200': 'Player, old era (1200)',
    'h600': 'Player, old era (600)', 'h300': 'Player, old era (low)',
    '2400pm40s_50s': 'Player, 1940s-50s leather helmet (high)', '1200pm40s_50s': 'Player, 1940s-50s (1200)',
    '600pm40s_50s': 'Player, 1940s-50s (600)', '300pm40s_50s': 'Player, 1940s-50s (low)',
    '2400pm60s': 'Player, 1960s (high)', '1200pm60s': 'Player, 1960s (1200)',
    '600pm60s': 'Player, 1960s (600)', '300pm60s': 'Player, 1960s (low)',
    '2400pm60s_70s': 'Player, 1960s-70s (high)', '1200pm60s_70s': 'Player, 1960s-70s (1200)',
    '600pm60s_70s': 'Player, 1960s-70s (600)', '300pm60s_70s': 'Player, 1960s-70s (low)',
}


def friendly(name: str, materials: set | None = None, parts: int = 0) -> str:
    """'ALBAMA_FAIR.ea3' -> 'Alabama stadium (day)'; 'pm2400ps2.ea3' -> 'Player, modern ...'."""
    base = name.replace('.ea3', '')
    code, _, rest = base.partition('_')
    if code in STADIUMS:
        weather = WEATHER.get(rest.split('_')[0], rest.lower()) if rest else ''
        kind = 'stadium' if not STADIUMS[code].startswith(('Created', 'Practice', 'Neutral')) else ''
        return f"{STADIUMS[code]} {kind}".strip() + (f' ({weather})' if weather else '')
    stem = base[:-3] if base.endswith('ps2') else base
    if materials is not None:
        if materials <= {'hand_Left'}:
            return 'Hand, left (pose set)'
        if materials <= {'hand_Rt'}:
            return 'Hand, right (pose set)'
        if any(m.startswith('suit_') for m in materials):
            return 'Coach (suit)'
    if stem in PLAYERS:
        return PLAYERS[stem]
    return name
