# A file as a C array: python3 tools/bin2c.py FILE NAME > out.c
# gives NAME[] and NAME_size.
import sys

data = open(sys.argv[1], 'rb').read()
name = sys.argv[2]
print('/* made from %s by tools/bin2c.py */' % sys.argv[1])
print('const unsigned char %s[] = {' % name)
for i in range(0, len(data), 16):
    print('    ' + ','.join('0x%02x' % b for b in data[i:i + 16]) + ',')
print('};')
print('const unsigned long %s_size = sizeof %s;' % (name, name))
