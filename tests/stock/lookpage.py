# the test page for look.sh: every pen pair, styles, Latin-1, box lines,
# blocks and shades, a long line
import sys
E='\x1b['
o=[]
for bg in range(8):
    o.append(''.join(f'{E}{30+fg};{40+bg}m{fg}{bg}' for fg in range(8))+f'{E}0m '+''.join(f'{E}{90+fg};{40+bg}mX' for fg in range(8))+f'{E}0m')
o.append(f'{E}1mBold{E}0m {E}3mItalic{E}0m {E}4mUnder{E}0m {E}9mStrike{E}0m {E}7mInverse{E}0m {E}1;4;31mAll{E}0m plain')
o.append('Latin-1: '+''.join(chr(c) for c in range(0xC0,0x100)))
o.append('┌──┬──┐ ╔══╦══╗ ▀▄█▌▐ ░▒▓ ▁▂▃▄▅▆▇█')
o.append('│ab│cd│ ║ef║gh║ '+f'{E}32m░▒▓{E}0m {E}44;33m▀▄█{E}0m')
o.append('└──┴──┘ ╚══╩══╝')
o.append(''.join(chr(33+i%90) for i in range(150)))
for i in range(4): o.append(f'{E}3{i+1}mLine {i} {E}0m'+'x'*i*9+f'{E}4{i+1}m end{E}0m')
sys.stdout.buffer.write(('\n'.join(o)+'\n').encode('utf-8'))
