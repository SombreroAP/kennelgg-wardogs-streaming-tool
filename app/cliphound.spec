# PyInstaller spec for the installer build (run from app/ on Windows):
#   pyinstaller cliphound.spec
# Produces dist/ClipHound/ClipHound.exe (+ _internal/). Tesseract is copied in next to it by CI.
from PyInstaller.utils.hooks import collect_submodules, collect_all
# the speech models' runtimes carry DLLs and data files of their own (libvosk, ctranslate2,
# onnxruntime for the VAD, av): take everything they ship. Each is optional at runtime: voice.py
# copes when one is missing, so a package that fails to collect must not stop the build.
extra_datas, extra_bins, extra_hidden = [], [], []
for pkg in ("vosk", "faster_whisper", "ctranslate2", "onnxruntime", "av", "tokenizers", "huggingface_hub"):
    try:
        d, b, h = collect_all(pkg)
        extra_datas += d; extra_bins += b; extra_hidden += h
    except Exception as e:
        print("collect_all", pkg, "skipped:", e)
block_cipher = None
a = Analysis(
    ["main.py"],
    pathex=["."],
    binaries=extra_bins,
    datas=[("templates", "templates"), ("config.yaml", "."), ("icons/catalogue", "icons/catalogue")] + extra_datas,
    hiddenimports=collect_submodules("obsws_python") + ["bridge", "capture_obs", "capture", "obs", "twitch", "setup", "colors", "ocr", "detector", "nearby", "vehicle", "twitch_device", "websocket", "voice", "inventory", "weapons", "vosk", "faster_whisper", "ctranslate2", "tokenizers", "huggingface_hub", "onnxruntime", "av"] + extra_hidden,
    hookspath=[],
    runtime_hooks=[],
    excludes=["tkinter", "matplotlib", "PyQt5", "PySide6"],
    cipher=block_cipher,
    noarchive=False,
)
pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name="ClipHound", console=False, icon=None)
coll = COLLECT(exe, a.binaries, a.zipfiles, a.datas, strip=False, upx=False, name="ClipHound")
