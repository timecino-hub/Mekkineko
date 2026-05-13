import openpyxl

LEVEL_ROWS = 21
LEVEL_COLS = 21

wb = openpyxl.load_workbook('reference/levels.xlsx')

header = []
header.append('#ifndef LEVELS_H')
header.append('#define LEVELS_H')
header.append('')
header.append('#include <stdint.h>')
header.append('')
header.append(f'#define LEVEL_COLS  {LEVEL_COLS}')
header.append(f'#define LEVEL_ROWS  {LEVEL_ROWS}')
header.append('#define CELL_SIZE   11')
header.append('')
header.append('// Cell types')
header.append('#define CELL_EMPTY          0')
header.append('#define CELL_PLATFORM       1')
header.append('#define CELL_SPIKE          2')
header.append('#define CELL_SPIKE_DOWN     3')
header.append('#define CELL_SPIKE_LEFT     4')
header.append('#define CELL_SPIKE_RIGHT    5')
header.append('#define CELL_SPRING         6')
header.append('#define CELL_MOVING_PLATFORM 7')
header.append('#define CELL_CHECKPOINT     8')
header.append('#define CELL_GOAL           9')
header.append('#define CELL_TRACK_START    10')
header.append('#define CELL_TRACK_END      11')
header.append('#define CELL_FALLING        12')
header.append('#define CELL_CRYSTAL        13')
header.append('#define CELL_STRAWBERRY     14')
header.append('// Legacy aliases')
header.append('#define CELL_MOVING_START  10')
header.append('#define CELL_MOVING_END    11')
header.append('#define CELL_PLAYER_SPAWN  8')
header.append('')

levels_body = []
for name in wb.sheetnames:
    ws = wb[name]
    levels_body.append(f'static const uint8_t {name}[LEVEL_ROWS][LEVEL_COLS] = {{')
    for r in range(1, LEVEL_ROWS + 1):
        vals = []
        for c in range(1, LEVEL_COLS + 1):
            v = ws.cell(row=r, column=c).value
            if v is None:
                vals.append('0')
            else:
                s = str(v)
                if '\\\\' in s: s = s.split('\\\\')[0]
                if '\\' in s: s = s.split('\\')[0]
                try:
                    vals.append(str(int(float(s))))
                except:
                    vals.append('0')
        comma = ',' if r < LEVEL_ROWS else ''
        levels_body.append('    {' + ','.join(vals) + '}' + comma)
    levels_body.append('};')
    levels_body.append('')

# Level names array
names = wb.sheetnames
levels_body.append(f'static const uint8_t (*level_list[{len(names)}])[LEVEL_COLS] = {{')
levels_body.append('    ' + ',\n    '.join(names))
levels_body.append('};')
levels_body.append(f'#define NUM_LEVELS {len(names)}')

header.append('')  # blank before endif

with open('game_2/Levels.h', 'w') as f:
    f.write('\n'.join(header))
    f.write('\n')
    f.write('\n'.join(levels_body))
    f.write('\n')
    f.write('\n#endif // LEVELS_H\n')

print(f'Generated {len(names)} levels ({LEVEL_COLS}x{LEVEL_ROWS}) in game_2/Levels.h')
