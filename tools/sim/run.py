"""Roda o ambiente de testes (DLL compilada pelo build.bat) e devolve o resultado como código de saída.

Uso:
  python run.py <roadpanel_sim_*.dll> [pasta]                      testes
  python run.py <roadpanel_sim_*.dll> <pasta> frames AC [ms] [passo] quadros de uma missão a cada ms
"""
import ctypes
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
dll = ctypes.CDLL(sys.argv[1])
out = Path(sys.argv[2]) if len(sys.argv) > 2 else here / "out"
out.mkdir(parents=True, exist_ok=True)
if len(sys.argv) > 3 and sys.argv[3] == "frames":
    route = sys.argv[4] if len(sys.argv) > 4 else "AC"
    every = int(sys.argv[5]) if len(sys.argv) > 5 else 500
    step = int(sys.argv[6]) if len(sys.argv) > 6 else 33
    dll.roadpanel_frames.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
    sys.exit(dll.roadpanel_frames(str(out).encode("mbcs"), route.encode(), every, step))
dll.roadpanel_sim.argtypes = [ctypes.c_char_p]
sys.exit(dll.roadpanel_sim(str(out).encode("mbcs")))
