"""Small launcher window (tkinter, ships with the bundled Python).
Palette: gunmetal base, olive panels, signal-amber primary action."""
from __future__ import annotations

import threading
import tkinter as tk
import webbrowser
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from . import __version__
from .feedback import collect, issue_url, write_bundle
from .games import load_profile
from .launch import run_session
from .settings import Settings
from .updater import Installer, UpdateError, list_releases

GUNMETAL, PANEL, OLIVE, AMBER, PAPER, MUTED = "#23272B", "#2E3438", "#5B6338", "#E0A526", "#E8E6DF", "#9AA0A6"


class LauncherWindow:
    def __init__(self, install_root: Path):
        self.root_dir = install_root
        self.settings = Settings.load(install_root / "settings.json")
        self.profile = load_profile(self.settings["game"])
        self.installer = Installer(install_root)

        self.tk = tk.Tk()
        self.tk.title(f"FrostbiteVR · {self.profile.title}")
        self.tk.configure(bg=GUNMETAL)
        self.tk.minsize(520, 0)
        style = ttk.Style(self.tk)
        style.theme_use("clam")
        style.configure("TFrame", background=GUNMETAL)
        style.configure("Panel.TFrame", background=PANEL)
        style.configure("TLabel", background=GUNMETAL, foreground=PAPER, font=("Segoe UI", 10))
        style.configure("Panel.TLabel", background=PANEL, foreground=PAPER)
        style.configure("Muted.TLabel", background=PANEL, foreground=MUTED, font=("Segoe UI", 9))
        style.configure("Title.TLabel", font=("Segoe UI Semibold", 16))
        style.configure("Primary.TButton", background=AMBER, foreground=GUNMETAL, font=("Segoe UI Semibold", 11), padding=10)
        style.map("Primary.TButton", background=[("active", "#F0B83A")])
        style.configure("TButton", background=OLIVE, foreground=PAPER, padding=6)
        style.configure("TRadiobutton", background=PANEL, foreground=PAPER)
        style.configure("TCheckbutton", background=PANEL, foreground=PAPER)

        outer = ttk.Frame(self.tk, padding=18)
        outer.pack(fill="both", expand=True)
        ttk.Label(outer, text=f"{self.profile.title} in VR", style="Title.TLabel").pack(anchor="w")
        cur = self.installer.state().get("current") or "not installed"
        ttk.Label(outer, text=f"Mod {cur} · launcher {__version__} · campaign only").pack(anchor="w", pady=(0, 12))

        # Game folder
        panel = ttk.Frame(outer, style="Panel.TFrame", padding=12)
        panel.pack(fill="x", pady=(0, 10))
        ttk.Label(panel, text="Game folder", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.game_dir = tk.StringVar(value=self.settings["game_dir"] or self.profile.default_dir)
        ttk.Label(panel, textvariable=self.game_dir, style="Muted.TLabel", wraplength=380).grid(row=1, column=0, sticky="w")
        ttk.Button(panel, text="Change…", command=self.pick_folder).grid(row=0, column=1, rowspan=2, padx=(12, 0))
        panel.columnconfigure(0, weight=1)

        # Reload option (manual is planned; the adapter falls back to button press until then)
        opts = ttk.Frame(outer, style="Panel.TFrame", padding=12)
        opts.pack(fill="x", pady=(0, 10))
        ttk.Label(opts, text="Reloading", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.reload = tk.StringVar(value=self.settings["reload_mode"])
        ttk.Radiobutton(opts, text="Button press", value="button", variable=self.reload,
                        command=self.save_options).grid(row=1, column=0, sticky="w")
        ttk.Radiobutton(opts, text="Manual (magazine by hand)", value="manual", variable=self.reload,
                        command=self.save_options).grid(row=2, column=0, sticky="w")
        ttk.Label(opts, text="Manual: B drops the magazine; take one from your belt (front left) and push it "
                             "into the gun. Holding B reloads at once. Change it in the headset menu too.",
                  style="Muted.TLabel", wraplength=440).grid(row=3, column=0, sticky="w", pady=(2, 8))
        self.cfg = tk.BooleanVar(value=bool(self.settings["apply_engine_cfg"]))
        ttk.Checkbutton(opts, text="Turn off motion blur and frame queuing during VR sessions", variable=self.cfg,
                        command=self.save_options).grid(row=4, column=0, sticky="w")

        # 3D through BF4's own stereoscopic renderer (emulated 3D Vision driver)
        s3d = ttk.Frame(outer, style="Panel.TFrame", padding=12)
        s3d.pack(fill="x", pady=(0, 10))
        ttk.Label(s3d, text="3D (experimental)", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.stereo = tk.StringVar(value=self.settings["stereo"])
        for row, (value, text) in enumerate((("off", "Off"),
                                             ("probe", "Test BF4's 3D mode and save a report"),
                                             ("on", "On")), start=1):
            ttk.Radiobutton(s3d, text=text, value=value, variable=self.stereo,
                            command=self.save_options).grid(row=row, column=0, sticky="w")
        self.nvidia = tk.BooleanVar(value=bool(self.settings["pretend_nvidia"]))
        ttk.Checkbutton(s3d, text="Report an NVIDIA GPU to BF4 (needed on AMD and Intel)", variable=self.nvidia,
                        command=self.save_options).grid(row=4, column=0, sticky="w", pady=(6, 0))
        ttk.Label(s3d, text="Needs the Steam launch option  -RenderDevice.StereoEnable 1  (Steam > Battlefield 4 > "
                  "Properties > Launch Options). The test leaves a report and a picture in the logs folder.",
                  style="Muted.TLabel", wraplength=440).grid(row=5, column=0, sticky="w")
        self.developer = tk.BooleanVar(value=bool(self.settings["developer"]))
        ttk.Checkbutton(s3d, text="Use experimental features (needed for the first headset tests)",
                        variable=self.developer, command=self.save_options).grid(row=6, column=0, sticky="w", pady=(6, 0))

        ttk.Button(outer, text="Launch Battlefield 4 in VR", style="Primary.TButton", command=self.launch).pack(fill="x")
        row = ttk.Frame(outer)
        row.pack(fill="x", pady=(8, 0))
        for text, cmd in (("Check for updates", self.update), ("Roll back", self.rollback), ("Send feedback", self.feedback)):
            ttk.Button(row, text=text, command=cmd).pack(side="left", expand=True, fill="x", padx=2)
        self.status = tk.StringVar(value="Ready.")
        ttk.Label(outer, textvariable=self.status, wraplength=480).pack(anchor="w", pady=(12, 0))
        if self.settings.recovered_from:
            self.say(f"Settings were unreadable and have been reset. A copy is at {self.settings.recovered_from.name}.")

    # ---- actions -------------------------------------------------------------------
    def say(self, text: str) -> None:
        self.tk.after(0, self.status.set, text)

    def pick_folder(self) -> None:
        chosen = filedialog.askdirectory(initialdir=self.game_dir.get(), title="Choose the Battlefield 4 folder")
        if chosen:
            self.game_dir.set(chosen.replace("/", "\\"))
            self.settings["game_dir"] = self.game_dir.get()
            self.settings.save()

    def save_options(self) -> None:
        self.settings["reload_mode"] = self.reload.get()
        self.settings["apply_engine_cfg"] = self.cfg.get()
        self.settings["stereo"] = self.stereo.get()
        self.settings["pretend_nvidia"] = self.nvidia.get()
        self.settings["developer"] = self.developer.get()
        self.settings.save()

    def _background(self, work) -> None:
        def run():
            try:
                work()
            except Exception as exc:  # noqa: BLE001 - surface every failure in the window
                self.say(str(exc))
        threading.Thread(target=run, daemon=True).start()

    def launch(self) -> None:
        current = self.installer.current_dir()
        if not current:
            self.say("Install the mod first: choose Check for updates.")
            return
        self._background(lambda: run_session(self.profile, self.settings, self.root_dir, current, self.say))

    def update(self) -> None:
        def work():
            releases = list_releases(self.profile.release_repo, self.installer.fetch, self.installer.tmp,
                                     self.profile.release_tag_prefix)
            pick = self.installer.pick_update(releases, self.settings["channel"])
            if not pick:
                self.say("You have the latest version.")
                return
            self.say(f"Downloading {pick.version}…")
            version = self.installer.install_from_urls(pick.manifest_url, pick.package_url)
            self.say(f"Updated to {version}. The previous version is kept for rollback.")
        self._background(work)

    def rollback(self) -> None:
        try:
            self.say(f"Rolled back to {self.installer.rollback()}.")
        except UpdateError as exc:
            self.say(str(exc))

    def feedback(self) -> None:
        win = tk.Toplevel(self.tk, bg=GUNMETAL, padx=14, pady=14)
        win.title("Send feedback")
        ttk.Label(win, text="What happened, in one line?").pack(anchor="w")
        summary = ttk.Entry(win, width=60)
        summary.pack(fill="x")
        ttk.Label(win, text="Details: what you did, what you expected, what you saw").pack(anchor="w", pady=(8, 0))
        details = tk.Text(win, width=60, height=8, bg=PANEL, fg=PAPER, insertbackground=PAPER)
        details.pack(fill="both")

        def send():
            logs = sorted((self.root_dir / "logs").glob("*.log"))
            report = collect(summary.get(), details.get("1.0", "end"), self.settings.values,
                             {"launcher": __version__, "mod": self.installer.state().get("current")}, logs)
            bundle = write_bundle(report, self.root_dir / "feedback")
            webbrowser.open(issue_url(self.profile.release_repo, report, bundle))
            messagebox.showinfo("Feedback", f"Your browser opened a pre-filled report. Review it, attach "
                                            f"{bundle.name} from the feedback folder if you like, then submit.")
            win.destroy()

        ttk.Button(win, text="Open report in browser", style="Primary.TButton", command=send).pack(fill="x", pady=(10, 0))

    def run(self) -> None:
        self.tk.mainloop()
