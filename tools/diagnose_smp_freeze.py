"""
SMP freeze diagnostic (COMPLETION_ACTION_PLAN P4, adendo 40).

Boots the image smp2 (real-time, -M toggle like the smp probes), detects a
TOTAL FREEZE as ">40s without a new timer: tick= line while no exception
fired", then dumps BOTH vCPUs over the QEMU monitor (single socket session
per cpu so the `cpu N` selection cannot desync): info registers + x/10i $pc +
x/24wx $esp into /tmp/vg/fz2_cpu{0,1}.txt. Stack words in kernel .text resolve
the spinning/waiting context per cpu. Use with -no-reboot (already set) so a
double-fault keeps final state visible.

Motivation: two distinct stop modes were measured — (a) tick-death freeze
(cpu runs off the rails at CPL0 with IF=0 holding a spinlock; other cpu
spins in spinlock_lock), (b) livelock stall (ticks continue, census shows a
READY-but-starved chain). This probe distinguishes them automatically.
"""
import socket, subprocess, time, os, re
W = '/tmp/vg'
os.system('cp build/boot.img /tmp/vg/smp.img')
log = f'{W}/smp.log'; mon = f'{W}/smpm.sock'
try: os.remove(log)
except FileNotFoundError: pass
p = subprocess.Popen(['qemu-system-i386', '-m', '3072', '-smp', '2',
                      '-drive', f'format=raw,file={W}/smp.img,snapshot=on',
                      '-boot', 'c', '-display', 'none',
                      '-serial', f'file:{log}',
                      '-monitor', f'unix:{mon},server,nowait', '-no-reboot'],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(200):
    if os.path.exists(mon): break
    time.sleep(0.05)

def session(cmds, t=2.0):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(t); s.connect(mon); time.sleep(0.05)
    try: s.recv(65536)
    except Exception: pass
    out = []
    for c in cmds:
        s.sendall(c.encode() + b'\n'); time.sleep(0.15)
        chunks = []
        try:
            while True:
                d = s.recv(65536)
                if not d: break
                chunks.append(d)
                if b'(qemu)' in d: break
        except socket.timeout: pass
        out.append(b''.join(chunks).decode('utf-8', 'replace'))
    s.close()
    return out

def rd():
    try: return open(log, errors='replace').read()
    except FileNotFoundError: return ''

time.sleep(2.5)
session(['sendkey m']); time.sleep(0.2); session(['sendkey ret'])
print('smp toggled', flush=True)

def last_tick(txt):
    ms = re.findall(r'timer: tick=(\d+)', txt)
    return int(ms[-1]) if ms else -1

t0 = time.time(); last = -1; last_at = t0; frozen = False
while time.time() - t0 < 360:
    txt = rd()
    if 'exception:' in txt:
        print('EXCEPTION (no freeze)', flush=True); break
    n = last_tick(txt)
    if n != last: last = n; last_at = time.time()
    elif n >= 0 and time.time() - last_at > 40:
        frozen = True; break
    time.sleep(2.0)
if frozen:
    print(f'FREEZE at tick {last} — dumping BOTH cpus (single sessions)', flush=True)
    for c in (0, 1):
        out = session([f'cpu {c}', 'info registers', 'x/10i $pc', 'x/24wx $esp'])
        with open(f'{W}/fz2_cpu{c}.txt', 'w') as f:
            f.write('\n@@@\n'.join(out))
        print(f'cpu{c}: ' + (re.search(r'EIP=([0-9a-f]+)', out[1]).group(1) if re.search(r'EIP=([0-9a-f]+)', out[1]) else '?'), flush=True)
p.terminate(); time.sleep(0.4)
try: p.kill()
except Exception: pass
print('done', flush=True)
