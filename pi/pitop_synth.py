#!/usr/bin/python3
"""pi-top [4] OLED controller for FluidSynth: patches, favourites, setlist.

Concepts
  * Sound bank  - a SoundFont loaded by FluidSynth (read live from FluidSynth).
  * Preset      - one instrument inside a SoundFont (bank number + program).
  * Patch       - a named preset plus mixer settings (volume/reverb/chorus).
  * Favourites  - patches flagged "fav".
  * Setlist     - an ordered list of patches for playing live.

Files (DATA_DIR, default /var/lib/pitop-synth)
  patches.json  - the patch library and setlist; safe to hand-edit, it is
                  reloaded automatically when it changes.
  state.json    - last patch / list, restored at start-up.

Buttons
  PLAY mode   UP/DOWN  previous / next patch in the active list
              O        switch list: SETLIST <-> FAVOURITES
              hold O   open BROWSE
              X        all notes off + reset controllers
              hold X   open MENU (favourite, reorder, remove, delete)
  MENU mode   UP/DOWN  choose, O do it, X back
  BROWSE mode UP/DOWN  audition presets (hold to scroll faster)
              O        next sound bank
              hold O   save as a new patch (favourite + end of setlist)
              X        leave BROWSE, back to the current patch
"""

import json
import os
import re
import signal
import socket
import sys
import threading
import time

import mido
from PIL import Image, ImageDraw, ImageFont

TEST = os.environ.get("PITOP_SYNTH_TEST") == "1"
DATA_DIR = os.environ.get("PITOP_SYNTH_DATA", "/var/lib/pitop-synth")
PATCHES_FILE = os.path.join(DATA_DIR, "patches.json")
STATE_FILE = os.path.join(DATA_DIR, "state.json")
SHELL_ADDR = ("127.0.0.1", 9800)
LONG_PRESS = 1.0

DEFAULT_MIX = {"volume": 100, "reverb": 40, "chorus": 0}
DEFAULT_FONT = "FluidR3_GM.sf2"

# General MIDI names, used only if FluidSynth's own list can't be read.
GM_FALLBACK = {0: "Acoustic Grand Piano", 4: "Electric Piano 1", 16: "Drawbar Organ",
               19: "Church Organ", 24: "Nylon Guitar", 38: "Synth Bass 1",
               48: "String Ensemble 1", 52: "Choir Aahs", 56: "Trumpet",
               73: "Flute", 89: "Pad Warm", 11: "Vibraphone", 6: "Harpsichord"}

# First-run patches: (GM program, name, volume, reverb, chorus, favourite)
SEED = [
    (0, "Grand Piano", 100, 45, 0, True),
    (4, "Electric Piano", 100, 35, 25, True),
    (16, "Drawbar Organ", 95, 30, 0, True),
    (48, "Strings", 95, 55, 15, True),
    (89, "Warm Pad", 90, 60, 30, True),
    (38, "Synth Bass", 105, 10, 0, False),
    (24, "Nylon Guitar", 100, 40, 10, False),
    (11, "Vibraphone", 100, 50, 15, False),
    (6, "Harpsichord", 100, 35, 0, False),
    (19, "Church Organ", 90, 70, 0, False),
    (52, "Choir", 90, 65, 20, False),
    (56, "Trumpet", 100, 30, 0, False),
    (73, "Flute", 100, 40, 0, False),
]


# --------------------------------------------------------------------------
# FluidSynth shell / SoundFont catalogue
# --------------------------------------------------------------------------

def shell_query(command, quiet=0.25, total=4.0):
    """Run one FluidSynth shell command on a private connection."""
    with socket.create_connection(SHELL_ADDR, timeout=1.0) as sock:
        sock.sendall((command + "\n").encode("ascii"))
        sock.settimeout(quiet)
        data = b""
        end = time.monotonic() + total
        while time.monotonic() < end:
            try:
                chunk = sock.recv(65536)
            except socket.timeout:
                if data:
                    break
                continue
            if not chunk:
                break
            data += chunk
    return data.decode("utf-8", "replace")


class Catalog:
    """SoundFonts and their presets, as FluidSynth reports them."""

    def __init__(self):
        self.font_ids = {}   # basename -> FluidSynth soundfont id
        self.presets = {}    # basename -> [(bank, program, name), ...]

    @property
    def ready(self):
        return bool(self.font_ids)

    def load(self):
        font_ids, presets = {}, {}
        for line in shell_query("fonts").splitlines():
            m = re.match(r"\s*(\d+)\s+(\S.*)$", line)
            if m:
                font_ids[os.path.basename(m.group(2).strip())] = int(m.group(1))
        for name, sfid in font_ids.items():
            items = []
            for line in shell_query(f"inst {sfid}").splitlines():
                m = re.match(r"\s*(\d+)-(\d+)\s+(.*\S)\s*$", line)
                if m:
                    items.append((int(m.group(1)), int(m.group(2)), m.group(3)))
            items.sort()
            presets[name] = items
        self.font_ids, self.presets = font_ids, presets

    def fonts(self):
        return sorted(self.font_ids)

    def preset_name(self, font, bank, program):
        for b, p, name in self.presets.get(font, []):
            if (b, p) == (bank, program):
                return name
        return None


def short_font(name):
    base = os.path.splitext(name)[0]
    return base.replace("_GM", "").replace("_General", "").replace("-gs", "")[:12]


# --------------------------------------------------------------------------
# Patch library
# --------------------------------------------------------------------------

def clamp(value, low, high, default):
    try:
        return max(low, min(high, int(value)))
    except (TypeError, ValueError):
        return default


def clean_patch(raw):
    return {
        "id": str(raw["id"]),
        "name": str(raw.get("name", "Patch"))[:40],
        "font": str(raw.get("font", DEFAULT_FONT)),
        "bank": clamp(raw.get("bank", 0), 0, 16383, 0),
        "program": clamp(raw.get("program", 0), 0, 127, 0),
        "volume": clamp(raw.get("volume", DEFAULT_MIX["volume"]), 0, 127, 100),
        "reverb": clamp(raw.get("reverb", DEFAULT_MIX["reverb"]), 0, 127, 40),
        "chorus": clamp(raw.get("chorus", DEFAULT_MIX["chorus"]), 0, 127, 0),
        "fav": bool(raw.get("fav", False)),
    }


class Library:
    def __init__(self):
        self.patches = {}    # id -> patch (insertion ordered)
        self.setlist = []    # ordered patch ids
        self.mtime = 0.0

    def load(self):
        with open(PATCHES_FILE, encoding="utf-8") as handle:
            raw = json.load(handle)
        patches = {}
        for item in raw["patches"]:
            patch = clean_patch(item)
            patches[patch["id"]] = patch
        if not patches:
            raise ValueError("no patches")
        self.patches = patches
        self.setlist = [str(i) for i in raw.get("setlist", []) if str(i) in patches]
        self.mtime = os.path.getmtime(PATCHES_FILE)

    def save(self):
        os.makedirs(DATA_DIR, exist_ok=True)
        data = {"version": 1, "patches": list(self.patches.values()),
                "setlist": self.setlist}
        write_json(PATCHES_FILE, data)
        self.mtime = os.path.getmtime(PATCHES_FILE)

    def changed_on_disk(self):
        try:
            return os.path.getmtime(PATCHES_FILE) != self.mtime
        except OSError:
            return False

    def seed(self, catalog):
        font = DEFAULT_FONT
        if catalog.ready and font not in catalog.font_ids:
            font = catalog.fonts()[0]
        patches, setlist = {}, []
        for n, (program, name, vol, rev, cho, fav) in enumerate(SEED, 1):
            pid = f"p{n}"
            patches[pid] = clean_patch({
                "id": pid, "name": name, "font": font, "bank": 0,
                "program": program, "volume": vol, "reverb": rev,
                "chorus": cho, "fav": fav})
            setlist.append(pid)
        self.patches, self.setlist = patches, setlist
        self.save()

    def new_id(self):
        n = 1
        while f"p{n}" in self.patches:
            n += 1
        return f"p{n}"

    def favourites(self):
        return [pid for pid, p in self.patches.items() if p["fav"]]


def write_json(path, data):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(tmp, path)


# --------------------------------------------------------------------------
# Hardware stand-in for tests
# --------------------------------------------------------------------------

class FakeButton:
    is_pressed = False
    when_pressed = None
    when_released = None


class FakeScreen:
    def __init__(self):
        self.up_button, self.down_button = FakeButton(), FakeButton()
        self.select_button, self.cancel_button = FakeButton(), FakeButton()

    def set_control_to_pi(self): pass
    def reset(self): pass
    def show(self): pass
    def wake(self): pass

    def display_image(self, image):
        image.resize((512, 256), Image.NEAREST).save(
            os.path.join(DATA_DIR, "frame.png"))


# --------------------------------------------------------------------------
# Controller
# --------------------------------------------------------------------------

class PiTopSynth:
    def __init__(self):
        mido.set_backend("mido.backends.rtmidi")
        self.running = True
        self.lock = threading.RLock()
        self.midi_inputs = {}
        self.next_midi_refresh = 0.0
        self.synth_out = None
        self.synth_out_name = None
        self.synth_shell = None
        self.display_dirty = True
        self.last_render = 0.0
        self.status = ""
        self.status_until = 0.0
        self.battery_label = ""
        self.startup_until = time.monotonic() + 8.0
        self.pressed_at = {}      # button name -> press time
        self.next_repeat = {}
        self.state_dirty_at = None
        self.next_catalog_try = 0.0

        self.catalog = Catalog()
        self.library = Library()
        self.active_list = "setlist"
        self.current_id = None
        self.mode = "play"
        self.browse_font = None
        self.browse_index = 0
        self.menu_index = 0
        self.menu_confirm = False

        self.load_fonts()
        self.load_library()
        self.load_state()

        if TEST:
            self.screen = FakeScreen()
            self.battery = None
        else:
            from pitop import Pitop
            self.pitop = Pitop()
            self.screen = self.pitop.miniscreen
            self.screen.set_control_to_pi()
            self.screen.reset()
            self.screen.show()
            self.screen.wake()
            try:
                self.battery = self.pitop.battery
            except Exception:
                self.battery = None
            if self.battery is not None:
                threading.Thread(target=self.update_battery, daemon=True).start()

        buttons = {"up": self.screen.up_button, "down": self.screen.down_button,
                   "select": self.screen.select_button,
                   "cancel": self.screen.cancel_button}
        for name, button in buttons.items():
            button.when_pressed = lambda n=name: self.button_down(n)
            button.when_released = lambda n=name: self.button_up(n)
        self.buttons = buttons
        self.render()

    # ---- fonts, library, state -------------------------------------------

    def load_fonts(self):
        try:
            self.catalog.load()
            print(f"SoundFonts: {self.catalog.fonts()}", flush=True)
        except Exception as exc:
            print(f"SoundFont catalogue unavailable: {exc}", flush=True)

    def load_library(self):
        try:
            self.library.load()
        except FileNotFoundError:
            os.makedirs(DATA_DIR, exist_ok=True)
            self.library.seed(self.catalog)
            print("Created default patch library", flush=True)
        except Exception as exc:
            print(f"patches.json unreadable ({exc}); using defaults in memory",
                  flush=True)
            if not self.library.patches:
                self.library.seed(self.catalog)

    def load_state(self):
        try:
            with open(STATE_FILE, encoding="utf-8") as handle:
                state = json.load(handle)
        except Exception:
            state = {}
        if state.get("list") in ("setlist", "favs"):
            self.active_list = state["list"]
        wanted = state.get("patch")
        if wanted in self.library.patches:
            self.current_id = wanted
        else:
            ids = self.current_list() or list(self.library.patches)
            self.current_id = ids[0]

    def save_state(self):
        try:
            os.makedirs(DATA_DIR, exist_ok=True)
            write_json(STATE_FILE, {"patch": self.current_id,
                                    "list": self.active_list})
        except OSError as exc:
            print(f"Could not save state: {exc}", flush=True)

    def mark_state_dirty(self):
        self.state_dirty_at = time.monotonic()

    def current_list(self):
        if self.active_list == "favs":
            ids = self.library.favourites()
        else:
            ids = self.library.setlist
        return ids

    def current_patch(self):
        return self.library.patches[self.current_id]

    def reload_if_edited(self):
        if not self.library.changed_on_disk():
            return
        old = dict(self.current_patch())
        try:
            self.library.load()
        except Exception as exc:
            self.library.mtime = os.path.getmtime(PATCHES_FILE)
            self.flash("patches.json error")
            print(f"patches.json reload failed: {exc}", flush=True)
            return
        if self.current_id not in self.library.patches:
            ids = self.current_list() or list(self.library.patches)
            self.current_id = ids[0]
        if self.mode == "play" and self.current_patch() != old:
            self.apply_patch(self.current_patch())
        self.flash("Patches reloaded")

    # ---- MIDI ---------------------------------------------------------------

    @staticmethod
    def _find_port(names, required):
        for name in names:
            if all(f.lower() in name.lower() for f in required):
                return name
        return None

    def connect_midi(self):
        current = self._find_port(mido.get_output_names(),
                                  ["fluid synth", "synth input"])
        if self.synth_out is not None and current != self.synth_out_name:
            try:
                self.synth_out.close()
            except Exception:
                pass
            self.synth_out = None
            self.synth_out_name = None
            self.close_synth_shell()
            self.catalog = Catalog()   # soundfont ids may have changed

        if self.synth_out is None and current:
            self.synth_out = mido.open_output(current)
            self.synth_out_name = current
            self.next_catalog_try = 0.0

        now = time.monotonic()
        if self.synth_out is not None and not self.catalog.ready \
                and now >= self.next_catalog_try:
            self.next_catalog_try = now + 2.0
            self.load_fonts()
            if self.catalog.ready:
                self.apply_current()

        if now >= self.next_midi_refresh:
            self.next_midi_refresh = now + 1.0
            wanted = {n for n in mido.get_input_names()
                      if not any(x in n.lower()
                                 for x in ("through", "fluid", "rtmidi"))}
            for name in list(self.midi_inputs):
                if name not in wanted:
                    self.midi_inputs.pop(name).close()
                    self.panic(quiet=True)
            for name in sorted(wanted):
                if name not in self.midi_inputs:
                    try:
                        self.midi_inputs[name] = mido.open_input(name)
                        print(f"MIDI input connected: {name}", flush=True)
                    except Exception as exc:
                        print(f"MIDI input unavailable: {name}: {exc}",
                              flush=True)

    def send(self, message):
        try:
            if self.synth_out is not None:
                self.synth_out.send(message)
        except Exception:
            self.synth_out = None
            self.synth_out_name = None

    def connect_synth_shell(self):
        if self.synth_shell is not None:
            return
        try:
            self.synth_shell = socket.create_connection(SHELL_ADDR, timeout=0.4)
            self.synth_shell.settimeout(0.4)
        except OSError:
            self.synth_shell = None

    def close_synth_shell(self):
        if self.synth_shell is not None:
            try:
                self.synth_shell.close()
            except OSError:
                pass
        self.synth_shell = None

    def drain_shell(self):
        """Discard replies so the shell socket never fills up."""
        if self.synth_shell is None:
            return
        try:
            self.synth_shell.setblocking(False)
            while self.synth_shell.recv(65536):
                pass
        except (BlockingIOError, OSError):
            pass
        finally:
            if self.synth_shell is not None:
                try:
                    self.synth_shell.settimeout(0.4)
                except OSError:
                    pass

    def apply_patch(self, patch):
        """Select the preset and mixer settings on every channel."""
        self.connect_synth_shell()
        sfid = self.catalog.font_ids.get(patch["font"])
        selected = False
        if self.synth_shell is not None and sfid is not None:
            cmds = "".join(
                f"select {ch} {sfid} {patch['bank']} {patch['program']}\n"
                for ch in range(16))
            try:
                self.drain_shell()
                self.synth_shell.sendall(cmds.encode("ascii"))
                selected = True
            except OSError:
                self.close_synth_shell()
        for ch in range(16):
            if not selected:
                self.send(mido.Message("program_change",
                                       program=patch["program"], channel=ch))
            self.send(mido.Message("control_change", channel=ch,
                                   control=121, value=0))
            for cc, key in ((7, "volume"), (91, "reverb"), (93, "chorus")):
                self.send(mido.Message("control_change", channel=ch,
                                       control=cc, value=patch[key]))

    def apply_current(self):
        with self.lock:
            self.apply_patch(self.current_patch())

    def panic(self, quiet=False):
        with self.lock:
            for ch in range(16):
                self.send(mido.Message("control_change", channel=ch,
                                       control=123, value=0))
                self.send(mido.Message("control_change", channel=ch,
                                       control=64, value=0))
            if not quiet:
                self.flash("All notes off")

    def handle_midi(self, message):
        if message.type == "program_change":
            # Program N selects the patch with id "p<N+1>" (stable when the
            # setlist is reordered; see tools/gen_patch_names.py).
            pid = f"p{message.program + 1}"
            if self.mode == "play" and pid in self.library.patches:
                self.goto_patch(pid)
            return
        if message.type == "control_change" and message.control in (0, 32):
            return  # bank select is managed by patches
        if message.type in ("clock", "active_sensing"):
            return
        self.send(message)

    # ---- patch navigation ---------------------------------------------------

    def flash(self, text, seconds=1.8):
        self.status = text
        self.status_until = time.monotonic() + seconds
        self.display_dirty = True

    def goto_patch(self, patch_id):
        with self.lock:
            self.current_id = patch_id
            self.apply_patch(self.current_patch())
            self.mark_state_dirty()
            self.display_dirty = True

    def step_patch(self, delta):
        with self.lock:
            ids = self.current_list()
            if not ids:
                self.flash("List is empty")
                return
            if self.current_id in ids:
                index = (ids.index(self.current_id) + delta) % len(ids)
            else:
                index = 0 if delta > 0 else len(ids) - 1
            self.goto_patch(ids[index])

    def toggle_list(self):
        with self.lock:
            self.active_list = "favs" if self.active_list == "setlist" else "setlist"
            if not self.current_list():
                self.flash("No favourites yet" if self.active_list == "favs"
                           else "Setlist is empty")
            else:
                self.flash("FAVOURITES" if self.active_list == "favs" else "SETLIST")
            self.mark_state_dirty()

    def toggle_favourite(self):
        with self.lock:
            patch = self.current_patch()
            patch["fav"] = not patch["fav"]
            self.library.save()
            self.flash("Favourite added" if patch["fav"] else "Favourite removed")

    # ---- menu mode ----------------------------------------------------------

    def menu_items(self):
        patch = self.current_patch()
        in_set = self.current_id in self.library.setlist
        items = [("fav", "Unfavourite" if patch["fav"] else "Favourite")]
        if in_set:
            at = self.library.setlist.index(self.current_id)
            if at > 0:
                items.append(("up", "Move earlier"))
            if at < len(self.library.setlist) - 1:
                items.append(("down", "Move later"))
            items.append(("remove", "Remove from set"))
        else:
            items.append(("add", "Add to setlist"))
        if len(self.library.patches) > 1:
            items.append(("delete", "Delete patch"))
        items.append(("back", "Back"))
        return items

    def enter_menu(self):
        with self.lock:
            self.mode = "menu"
            self.menu_index = 0
            self.menu_confirm = False
            self.display_dirty = True

    def menu_step(self, delta):
        with self.lock:
            items = self.menu_items()
            self.menu_index = (self.menu_index + delta) % len(items)
            self.menu_confirm = False
            self.display_dirty = True

    def menu_do(self):
        with self.lock:
            items = self.menu_items()
            self.menu_index = min(self.menu_index, len(items) - 1)
            action = items[self.menu_index][0]
            lib, pid = self.library, self.current_id
            if action == "delete" and not self.menu_confirm:
                self.menu_confirm = True
                self.display_dirty = True
                return
            self.menu_confirm = False
            if action == "back":
                self.mode = "play"
            elif action == "fav":
                lib.patches[pid]["fav"] = not lib.patches[pid]["fav"]
                lib.save()
                self.flash("Favourite on" if lib.patches[pid]["fav"]
                           else "Favourite off")
            elif action in ("up", "down"):
                at = lib.setlist.index(pid)
                to = at + (-1 if action == "up" else 1)
                lib.setlist[at], lib.setlist[to] = lib.setlist[to], lib.setlist[at]
                lib.save()
                self.flash(f"Now position {to + 1}")
                self.menu_index = min(self.menu_index, len(self.menu_items()) - 1)
            elif action == "remove":
                lib.setlist.remove(pid)
                lib.save()
                self.flash("Removed from set")
                self.menu_index = 0
            elif action == "add":
                lib.setlist.append(pid)
                lib.save()
                self.flash(f"Added at {len(lib.setlist)}")
                self.menu_index = 0
            elif action == "delete":
                ids = self.current_list()
                at = ids.index(pid) if pid in ids else 0
                del lib.patches[pid]
                if pid in lib.setlist:
                    lib.setlist.remove(pid)
                lib.save()
                ids = self.current_list() or list(lib.patches)
                self.mode = "play"
                self.goto_patch(ids[min(at, len(ids) - 1)])
                self.flash("Patch deleted")
            self.display_dirty = True

    # ---- browse mode --------------------------------------------------------

    def enter_browse(self):
        with self.lock:
            fonts = self.catalog.fonts()
            if not fonts:
                self.flash("Sounds not ready")
                return
            patch = self.current_patch()
            font = patch["font"] if patch["font"] in fonts else fonts[0]
            items = self.catalog.presets[font]
            index = 0
            for i, (b, p, _) in enumerate(items):
                if (b, p) == (patch["bank"], patch["program"]):
                    index = i
            self.browse_font, self.browse_index = font, index
            self.mode = "browse"
            self.display_dirty = True

    def browse_items(self):
        return self.catalog.presets.get(self.browse_font, [])

    def browse_apply(self):
        items = self.browse_items()
        if not items:
            return
        bank, program, name = items[self.browse_index]
        trial = dict(DEFAULT_MIX, font=self.browse_font, bank=bank,
                     program=program, name=name)
        self.apply_patch(trial)
        self.display_dirty = True

    def browse_step(self, delta):
        with self.lock:
            items = self.browse_items()
            if not items:
                return
            self.browse_index = (self.browse_index + delta) % len(items)
            self.browse_apply()

    def browse_next_font(self):
        with self.lock:
            fonts = self.catalog.fonts()
            if not fonts:
                return
            at = fonts.index(self.browse_font) if self.browse_font in fonts else -1
            self.browse_font = fonts[(at + 1) % len(fonts)]
            self.browse_index = 0
            self.browse_apply()
            self.flash(short_font(self.browse_font))

    def browse_save(self):
        with self.lock:
            items = self.browse_items()
            if not items:
                return
            bank, program, name = items[self.browse_index]
            pid = self.library.new_id()
            self.library.patches[pid] = clean_patch({
                "id": pid, "name": name, "font": self.browse_font, "bank": bank,
                "program": program, "fav": True, **DEFAULT_MIX})
            self.library.setlist.append(pid)
            self.library.save()
            self.mode = "play"
            self.active_list = "setlist"
            self.goto_patch(pid)
            self.flash("Saved to setlist")

    def exit_browse(self):
        with self.lock:
            self.mode = "play"
            self.apply_patch(self.current_patch())
            self.display_dirty = True

    # ---- buttons --------------------------------------------------------------

    def button_down(self, name):
        now = time.monotonic()
        self.pressed_at[name] = now
        self.next_repeat[name] = now + 0.5
        if name in ("up", "down"):
            self.nudge(1 if name == "down" else -1)

    def button_up(self, name):
        started = self.pressed_at.pop(name, None)
        self.next_repeat.pop(name, None)
        if started is None or name in ("up", "down"):
            return
        long = time.monotonic() - started >= LONG_PRESS
        if name == "select":
            if self.mode == "play":
                self.enter_browse() if long else self.toggle_list()
            elif self.mode == "menu":
                self.menu_do()
            else:
                self.browse_save() if long else self.browse_next_font()
        elif name == "cancel":
            if self.mode == "browse":
                self.exit_browse()
            elif self.mode == "menu":
                self.mode = "play"
                self.menu_confirm = False
                self.display_dirty = True
            elif long:
                self.enter_menu()
            else:
                self.panic()
                self.apply_current()

    def nudge(self, delta, big=False):
        if self.mode == "browse":
            self.browse_step(delta * (5 if big else 1))
        elif self.mode == "menu":
            self.menu_step(delta)
        else:
            self.step_patch(delta)

    def handle_repeats(self, now):
        for name in ("up", "down"):
            started = self.pressed_at.get(name)
            if started is None:
                continue
            try:
                if not self.buttons[name].is_pressed:
                    self.pressed_at.pop(name, None)
                    continue
            except Exception:
                pass
            if now >= self.next_repeat.get(name, now + 1):
                self.next_repeat[name] = now + 0.12
                self.nudge(1 if name == "down" else -1,
                           big=now - started > 2.0)

    # ---- display --------------------------------------------------------------

    def update_battery(self):
        try:
            value = int(round(float(self.battery.capacity)))
            charging = "+" if self.battery.is_charging else ""
            self.battery_label = f"BATTERY {value}%{charging}"
            self.display_dirty = True
        except Exception:
            pass

    @staticmethod
    def fit_text(draw, text, font, width):
        if draw.textbbox((0, 0), text, font=font)[2] <= width:
            return text
        shortened = text
        while shortened and draw.textbbox(
                (0, 0), shortened + ".", font=font)[2] > width:
            shortened = shortened[:-1]
        return shortened + "."

    def load_fonts_pil(self):
        base = "/usr/share/fonts/truetype/dejavu/"
        try:
            self.f_head = ImageFont.truetype(base + "DejaVuSans-Bold.ttf", 9)
            self.f_name = ImageFont.truetype(base + "DejaVuSans-Bold.ttf", 12)
            self.f_sub = ImageFont.truetype(base + "DejaVuSans.ttf", 9)
            self.f_hint = ImageFont.truetype(base + "DejaVuSans.ttf", 8)
        except Exception:
            self.f_head = self.f_name = self.f_sub = self.f_hint = \
                ImageFont.load_default()

    def render(self):
        if not hasattr(self, "f_head"):
            self.load_fonts_pil()
        now = time.monotonic()
        image = Image.new("1", (128, 64), 0)
        d = ImageDraw.Draw(image)
        fit = self.fit_text

        if self.mode == "menu":
            patch = self.current_patch()
            items = self.menu_items()
            self.menu_index = min(self.menu_index, len(items) - 1)
            left = "MENU"
            right = f"{self.menu_index + 1}/{len(items)}"
            name = items[self.menu_index][1]
            sub = patch["name"]
            hints = ["UP/DN: choose  O: do", "X: back"]
            if items[self.menu_index][0] == "delete" and self.menu_confirm:
                name, hints = "Sure? Press O", ["O: delete forever", "UP/DN or X: cancel"]
            star = False
        elif self.mode == "browse":
            items = self.browse_items()
            bank, program, name = items[self.browse_index] if items else (0, 0, "-")
            left = short_font(self.browse_font or "").upper()
            right = f"{self.browse_index + 1}/{len(items)}"
            sub = f"bank {bank:03d}  program {program:03d}"
            hints = ["O: next sound bank", "hold O: save  X: back"]
            star = False
        else:
            patch = self.current_patch()
            ids = self.current_list()
            label = "FAVS" if self.active_list == "favs" else "SETLIST"
            pos = ids.index(self.current_id) + 1 if self.current_id in ids else 0
            left = label
            right = f"{pos or '-'}/{len(ids)}"
            name = patch["name"]
            sub = f"{short_font(patch['font'])} {patch['bank']:03d}:{patch['program']:03d}"
            if now < self.startup_until and self.battery_label:
                sub = self.battery_label
            hints = ["UP/DN: patch  O: list", "hold O: browse  hold X: menu"]
            star = patch["fav"]

        if self.status and now < self.status_until:
            sub = self.status

        d.rectangle((0, 0, 127, 12), fill=1)
        d.text((3, 1), left, font=self.f_head, fill=0)
        rw = d.textbbox((0, 0), right, font=self.f_head)[2]
        d.text((125 - rw, 1), right, font=self.f_head, fill=0)
        if star:
            sx = 125 - rw - 12
            d.text((sx, 1), "★", font=self.f_head, fill=0)
        name = fit(d, name, self.f_name, 124)
        d.text((2, 16), name, font=self.f_name, fill=1)
        d.text((2, 29), fit(d, sub, self.f_sub, 124), font=self.f_sub, fill=1)
        d.line((2, 41, 125, 41), fill=1)
        d.text((2, 43), hints[0], font=self.f_hint, fill=1)
        d.text((2, 52), hints[1], font=self.f_hint, fill=1)
        try:
            self.screen.display_image(image)
        except Exception as exc:
            print(f"OLED render error: {exc!r}", file=sys.stderr, flush=True)

    # ---- main loop ---------------------------------------------------------------

    def run(self):
        last_check = 0.0
        while self.running:
            try:
                self.connect_midi()
                for name, port in list(self.midi_inputs.items()):
                    try:
                        for message in port.iter_pending():
                            self.handle_midi(message)
                    except Exception:
                        self.midi_inputs.pop(name).close()
                        self.panic(quiet=True)
            except Exception as exc:
                print(f"MIDI refresh failed: {exc}", flush=True)

            now = time.monotonic()
            self.handle_repeats(now)
            if self.status and now >= self.status_until:
                self.status = ""
                self.display_dirty = True
            if now - last_check >= 1.0:
                last_check = now
                self.reload_if_edited()
            if self.state_dirty_at and now - self.state_dirty_at >= 1.5:
                self.state_dirty_at = None
                self.save_state()
            if self.display_dirty and now - self.last_render >= 0.1:
                self.render()
                self.display_dirty = False
                self.last_render = now
            time.sleep(0.01)

    def stop(self, *_args):
        self.running = False


def main():
    app = PiTopSynth()
    signal.signal(signal.SIGTERM, app.stop)
    signal.signal(signal.SIGINT, app.stop)
    # Apply the remembered patch as soon as FluidSynth is reachable.
    app.connect_midi()
    app.apply_current()
    try:
        app.run()
    finally:
        app.save_state()
        app.panic(quiet=True)
        for port in app.midi_inputs.values():
            port.close()
        if app.synth_out:
            app.synth_out.close()
        app.close_synth_shell()


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"pitop-synth: {exc}", file=sys.stderr)
        raise
