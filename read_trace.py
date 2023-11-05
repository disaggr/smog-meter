import struct
import sys

from collections import namedtuple
Page = namedtuple("Page", "is_present is_dirty")

def main(args):
    tracefile = args[1]

    with open(tracefile, "rb") as t:
        buffer = t.read()

    ptr = 0
    while ptr < len(buffer):
        sec, usec = struct.unpack_from("<LL", buffer, ptr)
        ptr += 8

        print(sec, usec)

        vmas = struct.unpack_from("<L", buffer, ptr)[0]
        ptr += 4

        print(vmas)

        for i in range(vmas):
            start, end = struct.unpack_from("<QQ", buffer, ptr)
            ptr += 16

            pages = struct.unpack_from("<L", buffer, ptr)[0]
            ptr += 4

            print(f"{i} {start:#x} ... {end:#x} {pages} Pages")

            # round to nearest long
            l = (pages * 2 + (32 - 1)) // 32

            a = struct.unpack_from(f"<{l}L", buffer, ptr)
            ptr += l * 4

            p = [ Page((e >> j) & 0x1, (e >> (j+1)) & 0x1) for e in a for j in range(0, 32, 2) ]
            p = p[:pages]

            #print(f"  {a}")
            #print(f"  {p}")

if __name__ == "__main__":
    main(sys.argv)
