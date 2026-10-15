"""Source-backed player Swap groups and one non-overlapping configuration."""
from ncaa_formats import sections, FormatError


def read_options(data):
    r, table = sections(data)
    if 'Swap' not in table:
        return {}
    count, groups, variants, indices = r.unpack('<4I', table['Swap']['offset'])
    r.check(groups, count * 20)
    result = {}
    for i in range(count):
        raw, start, number = r.unpack('<16sHH', groups + i*20)
        name = raw.split(b'\0')[0].decode('ascii')
        options = {}
        for j in range(number):
            raw, first, size = r.unpack('<16sHH', variants + (start+j)*20)
            option = raw.split(b'\0')[0].decode('ascii')
            options[option] = list(r.unpack('<'+str(size)+'H', indices + first*2))
        result[name] = options
    return result


def clean_selection(data, helmet='HelmetStd', facemask='2Bar'):
    groups = read_options(data)
    choices = {'Standard': 'Default', 'Head': helmet, 'LeftHand': 'Default',
               'RightHand': 'Default', 'LeftElbow': 'Default', 'RightElbow': 'Default',
               'FaceMask': facemask, 'FaceMaskTabs': 'Default', 'HelmetInner': 'HelmetInnerStd',
               'LeftShoe': 'Default', 'RightShoe': 'Default', 'ChinStrap': 'Default',
               'Cleats': 'Default', 'LeftWrist': 'Bare', 'RightWrist': 'Bare',
               'HelmetPadPie': helmet}
    nodes = set()
    used = {}
    for group, option in choices.items():
        if group not in groups:
            continue
        if option not in groups[group]:
            option = next(iter(groups[group]))
        nodes.update(groups[group][option])
        used[group] = option
    if not nodes:
        raise FormatError('No verified body option groups in this model')
    return nodes, used, groups
