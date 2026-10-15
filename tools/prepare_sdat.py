"""Verify/decrypt disc SDAT into an atomic local cache using accelerated AES.

Only the uncompressed, version 3, self-keyed format present on this disc is
accepted. Every block's HMAC is verified before publication. Original files
are opened read-only. Requires pycryptodome, already used by the SDK tools.
"""
import argparse
import hashlib
import hmac
import os
from pathlib import Path
import struct
from Crypto.Cipher import AES

# Public SDAT/EDAT format constants (same as the MIT ps3recomp FS runtime).
SDAT_KEY = bytes.fromhex('0d655ef8e674a98ab8505cfa7d012933')
EDAT_KEY = bytes.fromhex('be959ca8308defa2e5e180c63712a9ae')

def prepare(path: Path):
    target = path.with_name(path.name + '.dec')
    receipt = target.with_name(target.name + '.verified.json')
    import json
    with path.open('rb') as src:
        hdr = src.read(256)
        if len(hdr) != 256 or hdr[:4] != b'NPD\0':
            raise ValueError(f'{path}: expected NPD SDAT')
        version = int.from_bytes(hdr[4:8], 'big')
        flags, block_size, file_size = struct.unpack_from('>IIQ', hdr, 128)
        if version != 3 or flags != 0x0100003c or not 0 < block_size <= 0x1000000:
            raise ValueError(f'{path}: unsupported SDAT format {version=} {flags=:x} {block_size=}')
        signature = hashlib.sha256(hdr).hexdigest()
        if target.exists() and receipt.exists() and target.stat().st_size == file_size:
            old = json.loads(receipt.read_text())
            if old.get('header_sha256') == signature and old.get('source_size') == path.stat().st_size:
                print(f'Verified cache exists: {target.name}', flush=True)
                return
        key = bytes(a ^ b for a, b in zip(hdr[96:112], SDAT_KEY))
        file_cipher = AES.new(key, AES.MODE_ECB)
        unwrap = AES.new(EDAT_KEY, AES.MODE_ECB)
        count = (file_size + block_size - 1) // block_size
        partial = target.with_name(target.name + '.partial')
        digest = hashlib.sha256()
        with partial.open('wb') as dst:
            for index in range(count):
                meta = src.read(32)
                if len(meta) != 32:
                    raise EOFError(f'{path}: metadata block {index}')
                size = min(block_size, file_size - index * block_size)
                enc = src.read((size + 15) & ~15)
                if len(enc) != ((size + 15) & ~15):
                    raise EOFError(f'{path}: payload block {index}')
                seed = file_cipher.encrypt(hdr[96:108] + index.to_bytes(4, 'big'))
                ckey = unwrap.decrypt(seed)
                hkey = unwrap.decrypt(file_cipher.encrypt(seed)) + bytes(4)
                expected = bytes(a ^ b for a, b in zip(meta[:16], meta[16:]))
                if not hmac.compare_digest(hmac.digest(hkey, enc, 'sha1')[:16], expected):
                    raise ValueError(f'{path}: authentication failed at block {index}')
                data = AES.new(ckey, AES.MODE_CBC, hdr[64:80]).decrypt(enc)[:size]
                dst.write(data)
                digest.update(data)
                if index and index % 16384 == 0:
                    print(f'{path.name}: {index}/{count} verified blocks', flush=True)
            dst.flush()
            os.fsync(dst.fileno())
        if partial.stat().st_size != file_size:
            raise ValueError('Decrypted size mismatch')
        with partial.open('rb') as check:
            if check.read(4) != b'PSAR':
                raise ValueError('Expected PSARC payload')
        os.replace(partial, target)
        receipt.write_text(json.dumps({'header_sha256': signature,
            'source_size': path.stat().st_size, 'plaintext_size': file_size,
            'plaintext_sha256': digest.hexdigest(), 'verified_blocks': count}, indent=2)+'\n')
        print(f'Verified {file_size} bytes: {target.name}', flush=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('usrdir', type=Path)
    args = parser.parse_args()
    for path in sorted(args.usrdir.glob('*.sdat')):
        prepare(path)
