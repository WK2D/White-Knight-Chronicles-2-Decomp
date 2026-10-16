"""Original synthetic FNMS vectors; exact unbounded-integer oracle, no game data."""
from pathlib import Path
import random
import sys

def expected(a,b,c):
    def decode(word):
        exponent=(word>>23)&255
        if exponent==0:
            return 0,0
        significand=(word&0x7fffff)|0x800000
        return (-significand if word>>31 else significand),exponent-150
    aa,ae=decode(a)
    bb,be=decode(b)
    cc,ce=decode(c)
    product_exponent=ae+be
    common=min(product_exponent,ce)
    exact=(cc<<(ce-common))-(aa*bb<<(product_exponent-common))
    if not exact:
        return 0
    sign=0x80000000 if exact<0 else 0
    magnitude=abs(exact)
    top=magnitude.bit_length()-1
    exponent=top+common+127
    if exponent<=0:
        return 0
    if exponent>255:
        return sign|0x7fffffff
    significant=magnitude>>(top-23) if top>=23 else magnitude<<(23-top)
    return sign|(exponent<<23)|(significant&0x7fffff)

def main():
    edges=[0,0x80000000,1,0x7fffff,0x800000,0x3f800000,0xbf800000,
           0x7f7fffff,0x7f800000,0x7fffffff,0xff800000,0xffffffff]
    triples=[(a,b,c) for a in edges for b in edges for c in edges]
    rng=random.Random(1684)
    triples += [tuple(rng.getrandbits(32) for _ in range(3)) for _ in range(4096)]
    # Near cancellation, exponent boundaries and tiny opposite-signed addends.
    for exponent in range(1,256):
        a=exponent<<23
        triples.extend([(a,0x3f800000,a),(a,0xbf800000,a),
                        (a|0x7fffff,0x3f800001,a),(a,0x3f800000,1),
                        (a,0x3f800000,0x80800000)])
    lines=['#include <stdint.h>','static const uint32_t fp_vectors[][4] = {']
    for a,b,c in triples:
        lines.append(' {'+','.join(f'0x{x:08X}u' for x in (a,b,c,expected(a,b,c)))+'},')
    lines.append('};')
    target=Path(sys.argv[1]);target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
    print(f'Prepared {len(triples)} original SPU FNMS cases')

if __name__=='__main__':
    main()
