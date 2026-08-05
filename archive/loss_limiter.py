import json
import logging
import os
import subprocess
import sys
import threading
import time
from datetime import datetime
from enum import Enum, auto
from pathlib import Path
from tkinter import filedialog

import customtkinter as ctk
import mss
import psutil
import pytesseract
from PIL import Image

APP_DIR = Path(__file__).parent
CONFIG_FILE = APP_DIR / "config.json"

DEFAULT_CONFIG = {
    "losses": 3,
    "poll": 2.0,
    "process": "Overwatch.exe",
    "tesseract_path": "",
    "debug": False,
}

ROI_X_START = 0.30
ROI_X_END   = 0.70
ROI_Y_START = 0.20
ROI_Y_END   = 0.55

RESULT_HOLD_SECONDS = 4.0
RANK_HOLD_SECONDS   = 3.0
DEBOUNCE_COUNT      = 2


# ── Config ────────────────────────────────────────────────────────────────────

def load_config() -> dict:
    if CONFIG_FILE.exists():
        with open(CONFIG_FILE) as f:
            return {**DEFAULT_CONFIG, **json.load(f)}
    return DEFAULT_CONFIG.copy()


def save_config(cfg: dict) -> None:
    with open(CONFIG_FILE, "w") as f:
        json.dump(cfg, f, indent=2)


# ── Limiter logic ──────────────────────────────────────────────────────────────

class GameState(Enum):
    IN_GAME = auto()
    RESULT_SCREEN = auto()
    RANK_SCREEN = auto()


def find_process(name: str):
    for p in psutil.process_iter(["name"]):
        if p.info["name"] == name:
            return p
    return None


def kill_process(proc, log: logging.Logger) -> None:
    try:
        proc.terminate()
        proc.wait(timeout=5)
    except psutil.TimeoutExpired:
        proc.kill()
    except psutil.NoSuchProcess:
        pass
    log.info("Overwatch closed.")


def capture_roi(sct, monitor: dict) -> Image.Image:
    full = sct.grab(monitor)
    img  = Image.frombytes("RGB", full.size, full.bgra, "raw", "BGRX")
    w, h = img.size
    return img.crop((int(w * ROI_X_START), int(h * ROI_Y_START),
                     int(w * ROI_X_END),   int(h * ROI_Y_END)))


def preprocess(image: Image.Image) -> Image.Image:
    gray = image.convert("L")
    return gray.point(lambda p: 255 if p > 128 else 0, "1")


def ocr_detect(image: Image.Image) -> str | None:
    raw = pytesseract.image_to_string(preprocess(image), config="--psm 6").upper().strip()
    if "DEFEAT" in raw:
        return "DEFEAT"
    if "VICTORY" in raw:
        return "VICTORY"
    return None


class LimiterThread(threading.Thread):
    def __init__(self, cfg: dict):
        super().__init__(daemon=True)
        self._stop = threading.Event()
        self.cfg   = cfg
        self.log   = logging.getLogger("LossLimiter")

    def stop(self) -> None:
        self._stop.set()

    def run(self) -> None:
        cfg = self.cfg
        if cfg["tesseract_path"]:
            pytesseract.pytesseract.tesseract_cmd = cfg["tesseract_path"]

        losses       = 0
        state        = GameState.IN_GAME
        pending_loss = False
        debounce_buf: list[str | None] = []
        entered_at   = time.monotonic()

        self.log.info(f"Limiter started — limit={cfg['losses']} poll={cfg['poll']}s")

        with mss.mss() as sct:
            monitor = sct.monitors[1]
            while not self._stop.is_set():
                proc = find_process(cfg["process"])
                if proc is None:
                    self.log.info("Overwatch not running — waiting...")
                    self._stop.wait(5.0)
                    continue

                roi       = capture_roi(sct, monitor)
                detection = ocr_detect(roi)

                if cfg["debug"]:
                    out = APP_DIR / "debug_crops"
                    out.mkdir(exist_ok=True)
                    ts = datetime.now().strftime("%H%M%S_%f")
                    roi.save(out / f"{ts}_{detection or 'none'}.png")
                    self.log.debug(f"OCR={detection!r} state={state.name} losses={losses}")

                elapsed = time.monotonic() - entered_at

                if state == GameState.IN_GAME:
                    debounce_buf.append(detection)
                    if len(debounce_buf) > DEBOUNCE_COUNT:
                        debounce_buf.pop(0)
                    if (len(debounce_buf) == DEBOUNCE_COUNT
                            and len(set(debounce_buf)) == 1
                            and debounce_buf[0] in ("DEFEAT", "VICTORY")):
                        pending_loss = debounce_buf[0] == "DEFEAT"
                        state        = GameState.RESULT_SCREEN
                        entered_at   = time.monotonic()
                        debounce_buf.clear()
                        self.log.info(f"{'DEFEAT' if pending_loss else 'VICTORY'} detected")

                elif state == GameState.RESULT_SCREEN:
                    if elapsed >= RESULT_HOLD_SECONDS:
                        if pending_loss:
                            losses += 1
                            self.log.info(f"Loss streak: {losses}/{cfg['losses']}")
                            if losses >= cfg["losses"]:
                                self.log.info("Loss limit reached — closing Overwatch.")
                                kill_process(proc, self.log)
                                return
                        else:
                            self.log.info(f"Win — streak reset (was {losses})")
                            losses = 0
                        state      = GameState.RANK_SCREEN
                        entered_at = time.monotonic()

                elif state == GameState.RANK_SCREEN:
                    if elapsed >= RANK_HOLD_SECONDS:
                        state      = GameState.IN_GAME
                        entered_at = time.monotonic()
                        debounce_buf.clear()
                        self.log.info("Back in game.")

                self._stop.wait(cfg["poll"])


# ── GUI ───────────────────────────────────────────────────────────────────────

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")


class App(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("OW Loss Limiter")
        self.geometry("580x400")
        self.resizable(False, False)

        self._cfg     = load_config()
        self._limiter: LimiterThread | None = None

        self._build_sidebar()
        self._build_settings()

    # ── Sidebar ──────────────────────────────────────────────────────────────

    def _build_sidebar(self) -> None:
        sb = ctk.CTkFrame(self, width=160, corner_radius=0)
        sb.pack(side="left", fill="y")
        sb.pack_propagate(False)

        ctk.CTkLabel(
            sb, text="OW Loss\nLimiter",
            font=ctk.CTkFont(size=15, weight="bold")
        ).pack(pady=(24, 20))

        ctk.CTkButton(sb, text="Settings", anchor="w").pack(fill="x", padx=12, pady=4)

        # Start / Stop at the bottom
        self._start_btn = ctk.CTkButton(
            sb, text="Start",
            fg_color="#2d7a2d", hover_color="#235c23",
            command=self._toggle_limiter,
        )
        self._start_btn.pack(side="bottom", fill="x", padx=12, pady=16)

    # ── Settings panel ────────────────────────────────────────────────────────

    def _build_settings(self) -> None:
        panel = ctk.CTkFrame(self, fg_color="transparent")
        panel.pack(side="right", fill="both", expand=True, padx=24, pady=20)

        ctk.CTkLabel(
            panel, text="Settings",
            font=ctk.CTkFont(size=18, weight="bold")
        ).grid(row=0, column=0, columnspan=3, sticky="w", pady=(0, 14))

        fields = [
            ("Loss Limit",        "losses",        "int",   "Close Overwatch after N consecutive losses"),
            ("Poll Interval (s)", "poll",          "float", "Seconds between screen checks"),
            ("Process Name",      "process",       "str",   "Exact .exe name (e.g. Overwatch.exe)"),
            ("Tesseract Path",    "tesseract_path","path",  "Leave blank if Tesseract is on your PATH"),
        ]

        self._vars: dict[str, ctk.StringVar] = {}

        for i, (label, key, kind, hint) in enumerate(fields, start=1):
            ctk.CTkLabel(panel, text=label, anchor="w", width=140).grid(
                row=i, column=0, sticky="w", pady=6)

            var = ctk.StringVar(value=str(self._cfg[key]))
            self._vars[key] = var
            entry = ctk.CTkEntry(panel, textvariable=var, width=220)
            entry.grid(row=i, column=1, sticky="w", padx=8, pady=6)

            if kind == "path":
                ctk.CTkButton(
                    panel, text="Browse", width=70,
                    command=self._browse_tesseract
                ).grid(row=i, column=2, padx=4)
            else:
                ctk.CTkLabel(
                    panel, text=hint, text_color="gray55",
                    font=ctk.CTkFont(size=11)
                ).grid(row=i, column=2, sticky="w", padx=4)

        # Debug toggle
        dbg_row = len(fields) + 1
        ctk.CTkLabel(panel, text="Debug Mode", anchor="w", width=140).grid(
            row=dbg_row, column=0, sticky="w", pady=6)
        self._debug_var = ctk.BooleanVar(value=self._cfg["debug"])
        ctk.CTkSwitch(panel, text="Save ROI crops to ./debug_crops/",
                      variable=self._debug_var).grid(
            row=dbg_row, column=1, columnspan=2, sticky="w", padx=8)

        # Action buttons
        btn_row = dbg_row + 1
        ctk.CTkButton(panel, text="Save", width=100,
                      command=self._save).grid(
            row=btn_row, column=0, sticky="w", pady=(18, 0))

        ctk.CTkButton(
            panel, text="Open App Folder", width=130,
            fg_color="gray30", hover_color="gray20",
            command=self._open_folder,
        ).grid(row=btn_row, column=1, sticky="w", padx=8, pady=(18, 0))

        self._status_label = ctk.CTkLabel(panel, text="", text_color="gray55",
                                          font=ctk.CTkFont(size=11))
        self._status_label.grid(row=btn_row + 1, column=0, columnspan=3,
                                sticky="w", pady=(6, 0))

    # ── Actions ───────────────────────────────────────────────────────────────

    def _browse_tesseract(self) -> None:
        path = filedialog.askopenfilename(
            title="Select tesseract.exe",
            filetypes=[("Executable", "*.exe"), ("All files", "*.*")],
        )
        if path:
            self._vars["tesseract_path"].set(path)

    def _save(self) -> None:
        try:
            self._cfg.update({
                "losses":        int(self._vars["losses"].get()),
                "poll":          float(self._vars["poll"].get()),
                "process":       self._vars["process"].get().strip(),
                "tesseract_path":self._vars["tesseract_path"].get().strip(),
                "debug":         self._debug_var.get(),
            })
        except ValueError:
            self._flash("Invalid value — losses must be an integer, poll must be a number.")
            return
        save_config(self._cfg)
        self._flash("Settings saved.")

    def _open_folder(self) -> None:
        path = str(APP_DIR)
        if sys.platform == "win32":
            os.startfile(path)
        elif sys.platform == "darwin":
            subprocess.run(["open", path])
        else:
            subprocess.run(["xdg-open", path])

    def _toggle_limiter(self) -> None:
        if self._limiter and self._limiter.is_alive():
            self._limiter.stop()
            self._limiter = None
            self._start_btn.configure(text="Start",
                                      fg_color="#2d7a2d", hover_color="#235c23")
        else:
            self._save()
            self._limiter = LimiterThread(self._cfg)
            self._limiter.start()
            self._start_btn.configure(text="Stop",
                                      fg_color="#8b1a1a", hover_color="#6b1010")

    def _flash(self, msg: str) -> None:
        self._status_label.configure(text=msg)
        self.after(3000, lambda: self._status_label.configure(text=""))


# ── Entry point ───────────────────────────────────────────────────────────────

def _setup_logging() -> None:
    logging.basicConfig(
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
        level=logging.INFO,
    )


if __name__ == "__main__":
    _setup_logging()
    App().mainloop()
