#! /usr/bin/env python --> #: sudo python vserialport.py

import pty
import os
import select

def mkpty():
    master1, slave = pty.openpty()
    slaveName1 = os.ttyname(slave)
    master2, slave = pty.openpty()
    slaveName2 = os.ttyname(slave)
    print '\nSlave Device Names: ', slaveName1, slaveName2
    return master1, master2

# convert string to hex
def toHex(s):
    lst = []
    for ch in s:
        hv = hex(ord(ch)).replace('0x', '')
        if len(hv) == 1:
            hv = '0'+hv
        lst.append(hv)
    return reduce(lambda x,y:x+y, lst)

if __name__ == "__main__":

    master1, master2 = mkpty()
    while True:
        rl, wl, el = select.select([master1,master2], [], [], 1)
        for master in rl:
            data = os.read(master, 512)
            print "Read %d bytes" % len(data)
            print "Data are: %s" % toHex(data)
            if master==master1:
                os.write(master2, data)
            else:
                os.write(master1, data)
