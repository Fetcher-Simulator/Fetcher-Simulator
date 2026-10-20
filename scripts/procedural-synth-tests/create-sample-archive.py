"""Create an archive-only generated tone for isolated sampler QA. No game audio."""
import io
import math
from pathlib import Path
import struct
import sys
import wave

fixture = Path(sys.argv[1]).resolve()
assert fixture.name == 'procedural-synth' and fixture.parent.name == 'test-server'
stream = io.BytesIO()
with wave.open(stream, 'wb') as output:
    output.setnchannels(1)
    output.setsampwidth(2)
    output.setframerate(48000)
    output.writeframes(b''.join(struct.pack('<h', int(22000 * math.sin(n * 2 * math.pi * 220 / 48000))) for n in range(96000)))
name = b'sound\\fetcher\\synth\\qa-archive.wav'
low = high = 0
for n, byte in enumerate(name[:len(name) // 2]):
    low ^= (byte << ((n * 8) & 31)) & 0xffffffff
for n, byte in enumerate(name[len(name) // 2:]):
    temp = (byte << ((n * 8) & 31)) & 0xffffffff
    high ^= temp
    rotate = temp & 31
    if rotate:
        high = ((high << (32 - rotate)) | (high >> rotate)) & 0xffffffff
payload = stream.getvalue()
archive = fixture / 'synth-assets' / 'synth-sample-qa.bsa'
archive.parent.mkdir(exist_ok=True)
archive.write_bytes(struct.pack('<III', 0x100, 13 + len(name), 1)
                   + struct.pack('<III', len(payload), 0, 0) + name + b'\0'
                   + struct.pack('<II', low, high) + payload)
print('Generated archive-only sampler QA tone:', archive)
